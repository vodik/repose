#pragma once

#include <stdlib.h>

#include <alpm.h>
#include <alpm_list.h>
#include "package.h"

struct pkgcache {
    alpm_list_t **hash_table;
    alpm_list_t *list;
    size_t buckets;
    size_t entries;
    size_t limit;
};

hash_t sdbm(const char *str);

struct pkgcache *pkgcache_create(size_t size);
void pkgcache_free(struct pkgcache *cache);

struct pkgcache *pkgcache_add(struct pkgcache *cache, struct pkg *pkg);
struct pkgcache *pkgcache_replace(struct pkgcache *cache, struct pkg *new, struct pkg *old);
struct pkgcache *pkgcache_add_sorted(struct pkgcache *cache, struct pkg *pkg);
struct pkgcache *pkgcache_remove(struct pkgcache *cache, struct pkg *pkg, struct pkg **data);

struct pkg *pkgcache_find(struct pkgcache *cache, const char *name);
