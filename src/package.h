#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <alpm_list.h>

typedef uint64_t hash_t;

enum pkg_entry {
    PKG_FILENAME,
    PKG_PKGNAME,
    PKG_PKGBASE,
    PKG_VERSION,
    PKG_DESCRIPTION,
    PKG_GROUPS,
    PKG_CSIZE,
    PKG_ISIZE,
    PKG_SHA256SUM,
    PKG_PGPSIG,
    PKG_URL,
    PKG_LICENSE,
    PKG_ARCH,
    PKG_BUILDDATE,
    PKG_PACKAGER,
    PKG_REPLACES,
    PKG_DEPENDS,
    PKG_CONFLICTS,
    PKG_PROVIDES,
    PKG_OPTDEPENDS,
    PKG_MAKEDEPENDS,
    PKG_CHECKDEPENDS,
    PKG_FILES,
    PKG_BACKUP,
    PKG_DELTAS,
    PKG_MAKEPKGOPT
};

typedef struct pkg {
    hash_t hash;
    char *filename;
    char *name;
    char *base;
    char *version;
    char *desc;
    char *url;
    char *packager;
    char *sha256sum;
    char *base64sig;
    char *arch;
    size_t size;
    size_t isize;
    time_t builddate;
    time_t mtime;

    alpm_list_t *groups;
    alpm_list_t *licenses;
    alpm_list_t *replaces;
    alpm_list_t *depends;
    alpm_list_t *conflicts;
    alpm_list_t *provides;
    alpm_list_t *optdepends;
    alpm_list_t *makedepends;
    alpm_list_t *checkdepends;
    alpm_list_t *files;
    alpm_list_t *deltas;
} pkg_t;

int load_package(pkg_t *pkg, int fd);
int load_package_signature(struct pkg *pkg, int fd);
int load_package_files(pkg_t *pkg, int fd);
void package_free(pkg_t *pkg);
void package_set(pkg_t *pkg, enum pkg_entry type, const char *entry, size_t len);
