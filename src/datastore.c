#include "datastore.h"

#include <stdlib.h>
#include <errno.h>
#include "util.h"

static const double max_hash_load = 0.68;
static const double initial_hash_load = 0.58;

static const unsigned int prime_list[] =
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

static inline size_t next_power(size_t x)
{
    return 1UL << (64 - __builtin_clzl(x - 1));
}

static inline size_t next_prime(size_t x)
{
    size_t i, loopsize = sizeof(prime_list) / sizeof(prime_list[0]);
    for (i = 0; i < loopsize; i++) {
        if (prime_list[i] > x)
            break;
    }
    return prime_list[i];
}

static int vector_extendby(struct vector_t *buf, size_t nelem)
{
    size_t extby = nelem * buf->class;
    size_t newlen = _unlikely_(buf->length == 0 && extby < 64)
        ? 64 : buf->length + extby;

    if (newlen > buf->length) {
        char *data;

        newlen = next_power(newlen);
        data = realloc(buf->data, newlen);
        if (!data)
            return -errno;

        buf->length = newlen;
        buf->data = data;
    }

    return 0;
}

int vector_init(struct vector_t *v, size_t class, size_t reserve)
{
    *v = (struct vector_t){ .class = class };
    if (reserve && vector_extendby(v, reserve) < 0)
        return -errno;
    return 0;
}

void *vector_store(struct vector_t *v, size_t *idx)
{
    if (vector_extendby(v, 1) < 0)
        return NULL;

    *idx = v->entries++;
    return &v->data[*idx * v->class];
}

int hashtable_init(struct hashtable_t *t, size_t class, size_t size)
{
    size_t buckets = next_prime(size);
    size_t limit = buckets * max_hash_load;

    if (buckets < size) {
        errno = ERANGE;
        return -errno;
    }

    *t = (struct hashtable_t){
        .class   = class,
        .buckets = buckets,
        .limit   = limit,
        .length  = next_power(limit)
    };

    t->data = calloc(t->length, sizeof(keypair_t));
    return t->data ? 0 : -errno;
}

/* static unsigned int get_hash_position(struct hashtable_t *t, unsigned long hash) */
/* { */
/*     unsigned int position = hash % t->buckets; */

/*     /1* collision resolution using open addressing with linear probing *1/ */
/*     while (hash->hash_table[position] != NULL) { */
/*         position += stride; */
/*         while(position >= hash->buckets) { */
/*             position -= hash->buckets; */
/*         } */
/*     } */

/*     return position; */
/* } */

static unsigned long sdbm(const char *str)
{
    /* unsigned long hash = 0; */
    /* int c; */

    /* while ((c = *str++)) */
    /*     hash = c + (hash << 6) + (hash << 16) - hash; */

    /* return hash; */
    return 42;
}

int find_index(struct hashtable_t *t, const char *key)
{
    unsigned long hash = sdbm(key);
    unsigned int position = hash % t->buckets;

    keypair_t *kp = &t->data[position];
    /* if (strcmp(kp->key, key) == 0) */
    /*     return kp; */

    kp++;
    while (kp->hash % t->buckets == position) {
        if (strcmp(kp->key, key) == 0)
            return kp;
        kp++;
    }
}

keypair_t *hashtable_store(struct hashtable_t *t, const char *key)
{
    unsigned long hash = sdbm(key);
    unsigned int position = hash % t->buckets;
    keypair_t *kp = NULL;

    if (t->data[position].key == NULL)
        kp = &t->data[position];
    else {
        printf("FUCK!\n");
        exit(1);
    }

    kp->key  = key;
    kp->hash = hash;

    return kp;
}

keypair_t *hashtable_get(struct hashtable_t *t, const char *key)
{
    unsigned long hash = sdbm(key);
    unsigned int position = hash % t->buckets;

    keypair_t *kp = &t->data[position];
    if (strcmp(kp->key, key) == 0)
        return kp;

    kp++;
    while (kp->hash % t->buckets == position) {
        if (strcmp(kp->key, key) == 0)
            return kp;
        kp++;
    }

    return kp;
}
