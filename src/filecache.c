#include "filecache.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <alpm.h>

#include "package.h"
#include "pkgcache.h"
#include "filters.h"
#include "util.h"

static inline bool is_file(int d_type)
{
    return d_type == DT_REG || d_type == DT_UNKNOWN;
}

static inline struct pkgcache *filecache_add(struct pkgcache *cache, struct pkg *pkg)
{
    struct pkg *old = pkgcache_find(cache, pkg->name);
    if (!old) {
        return pkgcache_add(cache, pkg);
    }

    int vercmp = alpm_pkg_vercmp(pkg->version, old->version);
    if (vercmp == 0 || vercmp == 1) {
        return pkgcache_replace(cache, pkg, old);
    }

    return cache;
}

static size_t get_filecache_size(DIR *dirp)
{
    struct dirent *dp;
    size_t size = 0;

    for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
        if (is_file(dp->d_type))
            ++size;
    }

    rewinddir(dirp);
    return size;
}

static struct pkg *load_from_file(int dirfd, const char *filename)
{
    _cleanup_close_ int pkgfd = openat(dirfd, filename, O_RDONLY);
    check_posix(pkgfd, "failed to open %s", filename);

    struct pkg *pkg = malloc(sizeof(pkg_t));
    *pkg = (struct pkg){ .filename = strdup(filename) };

    if (load_package(pkg, pkgfd) < 0) {
        package_free(pkg);
        return NULL;
    }

    if (load_package_signature(pkg, dirfd) < 0 && errno != ENOENT) {
        package_free(pkg);
        return NULL;
    }

    return pkg;
}

static struct pkgcache *scan_for_targets(struct pkgcache *cache, int dirfd, DIR *dirp,
                                        alpm_list_t *targets, const char *arch)
{
    const struct dirent *dp;

    for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
        if (!is_file(dp->d_type))
            continue;

        struct pkg *pkg = load_from_file(dirfd, dp->d_name);
        if (!pkg)
            continue;

        if (targets && !match_targets(pkg, targets)) {
            package_free(pkg);
            continue;
        }

        if (arch && !match_arch(pkg, arch)) {
            package_free(pkg);
            continue;
        }

        cache = filecache_add(cache, pkg);
    }

    return cache;
}

struct pkgcache *get_filecache(int dirfd, alpm_list_t *targets, const char *arch)
{
    int dupfd = dup(dirfd);
    check_posix(dupfd, "failed to duplicate fd");
    check_posix(lseek(dupfd, 0, SEEK_SET), "failed to lseek");

    _cleanup_closedir_ DIR *dirp = fdopendir(dupfd);
    check_null(dirp, "fdopendir failed");

    size_t size = get_filecache_size(dirp);
    struct pkgcache *cache = pkgcache_create(size);

    return scan_for_targets(cache, dirfd, dirp, targets, arch);
}
