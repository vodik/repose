#ifndef ALPM_SIMPLE_H
#define ALPM_SIMPLE_H

#include <alpm_list.h>

typedef struct alpm_pkg_meta {
    unsigned long name_hash;
    char *filename;
    char *name;
    char *version;
    char *desc;
    char *url;
    char *packager;
    char *md5sum;
    char *sha256sum;
    char *base64_sig;
    char *arch;
    off_t size;
    off_t isize;
    time_t builddate;
    alpm_list_t *license;
    alpm_list_t *depends;
    alpm_list_t *conflicts;
    alpm_list_t *provides;
    alpm_list_t *optdepends;
    alpm_list_t *makedepends;
} alpm_pkg_meta_t;

typedef struct alpm_db_meta {
    char *treename;
    /* do not access directly, use _alpm_db_path(db) for lazy access */
    char *_path;
    struct __alpm_pkghash_t *pkgcache;
} alpm_db_meta_t;

int read_pkg_signature(alpm_pkg_meta_t *pkg);
int alpm_pkg_load_metadata(const char *filename, alpm_pkg_meta_t **pkg);
void alpm_pkg_free_metadata(alpm_pkg_meta_t *pkg);

int alpm_db_populate(const char *filename, alpm_db_meta_t *db);

#endif
