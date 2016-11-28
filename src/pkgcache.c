#include "pkgcache.h"

#include <string.h>
#include <errno.h>

hash_t sdbm(const char *str)
{
    hash_t c;
    hash_t hash = 0;

    if (!str) {
        return hash;
    }
    while ((c = *str++)) {
        hash = c + hash * 65599;
    }

    return hash;
}

static int pkg_cmp(const void *p1, const void *p2)
{
    const struct pkg *pkg1 = p1;
    const struct pkg *pkg2 = p2;
    return strcmp(pkg1->name, pkg2->name);
}

/* List of primes for possible sizes of hash tables.
 *
 * The maximum table size is the last prime under 1,000,000.  That is
 * more than an order of magnitude greater than the number of packages
 * in any Linux distribution, and well under UINT_MAX.
 */
static const size_t prime_list[] =
{
    11u, 13u, 17u, 19u, 23u, 29u, 31u, 37u, 41u, 43u, 47u,
    53u, 59u, 61u, 67u, 71u, 73u, 79u, 83u, 89u, 97u, 103u,
    109u, 113u, 127u, 137u, 139u, 149u, 157u, 167u, 179u, 193u,
    199u, 211u, 227u, 241u, 257u, 277u, 293u, 313u, 337u, 359u,
    383u, 409u, 439u, 467u, 503u, 541u, 577u, 619u, 661u, 709u,
    761u, 823u, 887u, 953u, 1031u, 1109u, 1193u, 1289u, 1381u,
    1493u, 1613u, 1741u, 1879u, 2029u, 2179u, 2357u, 2549u,
    2753u, 2971u, 3209u, 3469u, 3739u, 4027u, 4349u, 4703u,
    5087u, 5503u, 5953u, 6427u, 6949u, 7517u, 8123u, 8783u,
    9497u, 10273u, 11113u, 12011u, 12983u, 14033u, 15173u,
    16411u, 17749u, 19183u, 20753u, 22447u, 24281u, 26267u,
    28411u, 30727u, 33223u, 35933u, 38873u, 42043u, 45481u,
    49201u, 53201u, 57557u, 62233u, 67307u, 72817u, 78779u,
    85229u, 92203u, 99733u, 107897u, 116731u, 126271u, 136607u,
    147793u, 159871u, 172933u, 187091u, 202409u, 218971u, 236897u,
    256279u, 277261u, 299951u, 324503u, 351061u, 379787u, 410857u,
    444487u, 480881u, 520241u, 562841u, 608903u, 658753u, 712697u,
    771049u, 834181u, 902483u, 976369u
};

/* How far forward do we look when linear probing for a spot? */
static const size_t stride = 1;
/* What is the maximum load percentage of our hash table? */
static const double max_hash_load = 0.68;
/* Initial load percentage given a certain size */
static const double initial_hash_load = 0.58;

/* Allocate a hash table with space for at least "size" elements */
struct pkgcache *pkgcache_create(size_t size)
{
    struct pkgcache *cache = NULL;
    size_t i, loopsize;

    cache = calloc(1, sizeof(struct pkgcache));
    if (!cache)
        return NULL;

    size = size / initial_hash_load + 1;

    loopsize = sizeof(prime_list) / sizeof(*prime_list);
    for (i = 0; i < loopsize; i++) {
        if (prime_list[i] > size) {
            cache->buckets = prime_list[i];
            cache->limit = cache->buckets * max_hash_load;
            break;
        }
    }

    if (cache->buckets < size) {
        errno = ERANGE;
        free(cache);
        return NULL;
    }

    cache->hash_table = calloc(cache->buckets, sizeof(alpm_list_t *));
    if (!cache->hash_table) {
        free(cache);
        return NULL;
    }

    return cache;
}

static size_t get_hash_position(hash_t hash, struct pkgcache *cache)
{
    size_t position;

    position = hash % cache->buckets;

    /* collision resolution using open addressing with linear probing */
    while (cache->hash_table[position] != NULL) {
        position += stride;
        while (position >= cache->buckets) {
            position -= cache->buckets;
        }
    }

    return position;
}

/* Expand the hash table size to the next increment and rebin the entries */
static struct pkgcache *rehash(struct pkgcache *oldcache)
{
    struct pkgcache *newcache;
    size_t newsize, i;

    /* Hash tables will need resized in two cases:
     *  - adding packages to the local database
     *  - poor estimation of the number of packages in sync database
     *
     * For small hash tables sizes (<500) the increase in size is by a
     * minimum of a factor of 2 for optimal rehash efficiency.  For
     * larger database sizes, this increase is reduced to avoid excess
     * memory allocation as both scenarios requiring a rehash should not
     * require a table size increase that large. */
    if (oldcache->buckets < 500) {
        newsize = oldcache->buckets * 2;
    } else if (oldcache->buckets < 2000) {
        newsize = oldcache->buckets * 3 / 2;
    } else if (oldcache->buckets < 5000) {
        newsize = oldcache->buckets * 4 / 3;
    } else {
        newsize = oldcache->buckets + 1;
    }

    newcache = pkgcache_create(newsize);
    if (newcache == NULL) {
        /* creation of newcache failed, stick with old one... */
        return oldcache;
    }

    newcache->list = oldcache->list;
    oldcache->list = NULL;

    for (i = 0; i < oldcache->buckets; i++) {
        if (oldcache->hash_table[i] != NULL) {
            struct pkg *package = oldcache->hash_table[i]->data;
            size_t position = get_hash_position(package->hash, newcache);

            newcache->hash_table[position] = oldcache->hash_table[i];
            oldcache->hash_table[i] = NULL;
        }
    }

    newcache->entries = oldcache->entries;

    pkgcache_free(oldcache);

    return newcache;
}

static struct pkgcache *pkgcache_add_pkg(struct pkgcache *cache, struct pkg *pkg,
                                         int sorted)
{
    alpm_list_t *ptr;
    size_t position;

    if (pkg == NULL || cache == NULL) {
        return cache;
    }

    if (cache->entries >= cache->limit) {
        cache = rehash(cache);
    }

    position = get_hash_position(pkg->hash, cache);

    ptr = malloc(sizeof(alpm_list_t));
    if (ptr == NULL) {
        return cache;
    }

    ptr->data = pkg;
    ptr->prev = ptr;
    ptr->next = NULL;

    cache->hash_table[position] = ptr;
    if (!sorted) {
        cache->list = alpm_list_join(cache->list, ptr);
    } else {
        cache->list = alpm_list_mmerge(cache->list, ptr, pkg_cmp);
    }

    cache->entries += 1;
    return cache;
}


struct pkgcache *pkgcache_add(struct pkgcache *cache, struct pkg *pkg)
{
    return pkgcache_add_pkg(cache, pkg, 0);
}

struct pkgcache *pkgcache_replace(struct pkgcache *cache, struct pkg *new, struct pkg *old)
{
    cache = pkgcache_remove(cache, old, NULL);
    return pkgcache_add(cache, new);

}

struct pkgcache *pkgcache_add_sorted(struct pkgcache *cache, struct pkg *pkg)
{
    return pkgcache_add_pkg(cache, pkg, 1);
}

static size_t move_one_entry(struct pkgcache *cache,
                             size_t start, size_t end)
{
    /* Iterate backwards from 'end' to 'start', seeing if any of the items
     * would hash to 'start'. If we find one, we move it there and break.  If
     * we get all the way back to position and find none that hash to it, we
     * also end iteration. Iterating backwards helps prevent needless shuffles;
     * we will never need to move more than one item per function call.  The
     * return value is our current iteration location; if this is equal to
     * 'start' we can stop this madness. */
    while (end != start) {
        alpm_list_t *i = cache->hash_table[end];
        struct pkg *info = i->data;
        size_t new_position = get_hash_position(info->hash, cache);

        if (new_position == start) {
            cache->hash_table[start] = i;
            cache->hash_table[end] = NULL;
            break;
        }

        /* the odd math ensures we are always positive, e.g.
         * e.g. (0 - 1) % 47      == -1
         * e.g. (47 + 0 - 1) % 47 == 46 */
        end = (cache->buckets + end - stride) % cache->buckets;
    }
    return end;
}

struct pkgcache *pkgcache_remove(struct pkgcache *cache, struct pkg *pkg,
                                 struct pkg **data)
{
    alpm_list_t *i;
    size_t position;

    if (data) {
        *data = NULL;
    }

    if (pkg == NULL || cache == NULL) {
        return cache;
    }

    position = pkg->hash % cache->buckets;
    while ((i = cache->hash_table[position]) != NULL) {
        struct pkg *info = i->data;

        if (info->hash == pkg->hash &&
           strcmp(info->name, pkg->name) == 0) {
            size_t stop, prev;

            /* remove from list and hash */
            cache->list = alpm_list_remove_item(cache->list, i);
            if (data) {
                *data = info;
            }
            cache->hash_table[position] = NULL;
            free(i);
            cache->entries -= 1;

            /* Potentially move entries following removed entry to keep open
             * addressing collision resolution working. We start by finding the
             * next null bucket to know how far we have to look. */
            stop = position + stride;
            while (stop >= cache->buckets) {
                stop -= cache->buckets;
            }
            while (cache->hash_table[stop] != NULL && stop != position) {
                stop += stride;
                while (stop >= cache->buckets) {
                    stop -= cache->buckets;
                }
            }
            stop = (cache->buckets + stop - stride) % cache->buckets;

            /* We now search backwards from stop to position. If we find an
             * item that now hashes to position, we will move it, and then try
             * to plug the new hole we just opened up, until we finally don't
             * move anything. */
            while ((prev = move_one_entry(cache, position, stop)) != position) {
                position = prev;
            }

            return cache;
        }

        position += stride;
        while (position >= cache->buckets) {
            position -= cache->buckets;
        }
    }

    return cache;
}

void pkgcache_free(struct pkgcache *cache)
{
    if (cache != NULL) {
        size_t i;
        for (i = 0; i < cache->buckets; i++) {
            free(cache->hash_table[i]);
        }
        free(cache->hash_table);
    }
    free(cache);
}

struct pkg *pkgcache_find(struct pkgcache *cache, const char *name)
{
    alpm_list_t *lp;
    size_t position;

    if (name == NULL || cache == NULL) {
        return NULL;
    }

    hash_t hash = sdbm(name);
    position = hash % cache->buckets;

    while ((lp = cache->hash_table[position]) != NULL) {
        struct pkg *info = lp->data;

        if (info->hash == hash && strcmp(info->name, name) == 0) {
            return info;
        }

        position += stride;
        while (position >= cache->buckets) {
            position -= cache->buckets;
		}
	}

	return NULL;
}
