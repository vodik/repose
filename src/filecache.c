#include "filecache.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <err.h>

#include "pkghash.h"
#include "filters.h"
#include "util.h"

static inline alpm_pkghash_t *pkgcache_add(alpm_pkghash_t *cache, struct pkg *pkg)
{
    struct pkg *old = _alpm_pkghash_find(cache, pkg->name);
    int vercmp = old == NULL ? 0 : alpm_pkg_vercmp(pkg->version, old->version);

    if (vercmp == 0 || vercmp == 1) {
        if (old) {
            cache = _alpm_pkghash_remove(cache, old, NULL);
            package_free(old);
        }
        return _alpm_pkghash_add(cache, pkg);
    }

    return cache;
}

static size_t get_filecache_size(DIR *dirp)
{
    struct dirent *entry;
    size_t size = 0;

    while ((entry = readdir(dirp)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN)
            ++size;
    }

    rewinddir(dirp);
    return size;
}

static struct pkg *load_from_file(int dirfd, const char *filename, const char *arch)
{
    _cleanup_close_ int pkgfd = openat(dirfd, filename, O_RDONLY);
    if (pkgfd < 0) {
        err(EXIT_FAILURE, "failed to open %s", filename);
    }

    struct pkg *pkg = malloc(sizeof(pkg_t));
    zero(pkg, sizeof(pkg_t));

    if (load_package(pkg, pkgfd) < 0)
        goto error;

    if (arch && pkg->arch && !match_arch(pkg, arch))
        goto error;

    pkg->filename = strdup(filename);
    return pkg;

error:
    package_free(pkg);
    return NULL;
}

static alpm_pkghash_t *scan_for_targets(alpm_pkghash_t *cache, int dirfd, DIR *dirp,
                                        alpm_list_t *targets, const char *arch)
{
    const struct dirent *dp;

    while ((dp = readdir(dirp))) {
        if (dp->d_type != DT_REG && dp->d_type != DT_UNKNOWN)
            continue;

        struct pkg *pkg = load_from_file(dirfd, dp->d_name, arch);
        if (!pkg)
            continue;
        else if (!targets || match_targets(pkg, targets))
            cache = pkgcache_add(cache, pkg);
        else
            package_free(pkg);
    }

    return cache;
}

alpm_pkghash_t *get_filecache(int dirfd, alpm_list_t *targets, const char *arch)
{
    int dupfd = dup(dirfd);
    if (dupfd < 0)
        err(EXIT_FAILURE, "failed to duplicate fd");

    if (lseek(dupfd, 0, SEEK_SET) < 0)
        err(EXIT_FAILURE, "failed to lseek");

    _cleanup_closedir_ DIR *dirp = fdopendir(dupfd);
    if (!dirp)
        err(EXIT_FAILURE, "fdopendir failed");

    size_t size = get_filecache_size(dirp);
    alpm_pkghash_t *cache = _alpm_pkghash_create(size);

    return scan_for_targets(cache, dirfd, dirp, targets, arch);
}
