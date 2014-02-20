#include "files.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <err.h>
#include <fnmatch.h>

#include "pkghash.h"
#include "util.h"

static inline alpm_pkghash_t *pkgcache_add(alpm_pkghash_t *cache, struct pkg *pkg)
{
    struct pkg *old = _alpm_pkghash_find(cache, pkg->name);
    int vercmp = old == NULL ? 0 : alpm_pkg_vercmp(pkg->version, old->version);

    printf(" -- vercmp = %d\n", vercmp);

    if (vercmp == 0 || vercmp == 1) {
        if (old) {
            cache = _alpm_pkghash_remove(cache, old, NULL);
            package_free(old);
        }
        return _alpm_pkghash_add(cache, pkg);
    } else {
        /* package_free(pkg); */
        return cache;
    }
}

static size_t filecache_size(DIR *dirp)
{
    struct dirent *entry;
    size_t size = 0;

    while ((entry = readdir(dirp)) != NULL) {
        if (entry->d_type == DT_REG)
            ++size;
    }

    printf("cache size: %zd\n", size);
    rewinddir(dirp);
    return size;
}

static bool match_target_r(struct pkg *pkg, const char *target, char **buf)
{
    if (streq(target, pkg->filename))
        return true;
    else if (streq(target, pkg->name))
        return true;

    /* since this may be called multiple times, buf is external to avoid
     * recalculating it each time. */
    if (*buf == NULL)
        asprintf(buf, "%s-%s", pkg->name, pkg->version);

    if (fnmatch(target, *buf, 0) == 0)
        return true;
    return false;
}

static bool match_targets(struct pkg *pkg, alpm_list_t *targets)
{
    bool ret = false;
    _cleanup_free_ char *buf = NULL;
    const alpm_list_t *node;

    for (node = targets; node && !ret; node = node->next)
        ret = match_target_r(pkg, node->data, &buf);

    return ret;
}

static struct pkg *load_pkg(int dirfd, const char *filename)
{
    int pkgfd = openat(dirfd, filename, O_RDONLY);
    if (pkgfd < 0) {
        err(EXIT_FAILURE, "failed to open %s", filename);
    }

    struct pkg *pkg = malloc(sizeof(pkg_t));
    zero(pkg, sizeof(pkg_t));

    load_package(pkg, pkgfd);

    /* if (strcmp(pkg->arch, cfg.arch) != 0 && strcmp(pkg->arch, "any") != 0) { */
    /*     /1* alpm_pkg_free_metadata(pkg); *1/ */
    /*     return NULL; */
    /* } */
    pkg->filename = strdup(filename);

    /* safe_asprintf(&pkg->signame, "%s.sig", filename); */

    /* sigfd = openat(repo->poolfd, pkg->signame, O_RDONLY); */
    /* if (sigfd < 0) { */
    /*     if (errno != ENOENT) */
    /*         err(EXIT_FAILURE, "failed to open %s", pkg->signame); */
    /* } else { */
    /*     read_pkg_signature(sigfd, pkg); */
    /* } */

    return pkg;
}

 
static alpm_pkghash_t *scan_for_targets(alpm_pkghash_t *cache, int dirfd, DIR *dirp, alpm_list_t *targets)
{
    /* DIR *dir = fdopendir(dirfd); */
    const struct dirent *dp;

    /* if (!dir) */
    /*     err(1, "failed to open dir"); */

    while ((dp = readdir(dirp))) {
        if (!(dp->d_type & DT_REG))
            continue;

        if (fnmatch("*.pkg.tar*", dp->d_name, FNM_CASEFOLD) != 0 ||
            fnmatch("*.sig",      dp->d_name, FNM_CASEFOLD) == 0)
            continue;

        printf("considering %s", dp->d_name);

        struct pkg *pkg = load_pkg(dirfd, dp->d_name);
        if (!pkg) {
            /* warnx("failed to open %s", dp->d_name); */
            continue;
        }

        if (targets == NULL || match_targets(pkg, targets))
            cache = pkgcache_add(cache, pkg);
    }

    return cache;
}

alpm_pkghash_t *get_filecache(const char *path)
{
    int dirfd = open(path, O_RDONLY | O_DIRECTORY);
    if (dirfd < 0)
        return NULL;

    _cleanup_closedir_ DIR *dirp = fdopendir(dirfd);

    size_t size = filecache_size(dirp);
    alpm_pkghash_t *cache = _alpm_pkghash_create(size);

    return scan_for_targets(cache, dirfd, dirp, NULL);
}
