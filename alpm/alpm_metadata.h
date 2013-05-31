#ifndef ALPM_SIMPLE_H
#define ALPM_SIMPLE_H

#include <alpm_list.h>

typedef struct __alpm_pkghash_t alpm_pkghash_t;

typedef struct alpm_pkg_meta {
    unsigned long name_hash;
    char *filename;
    char *signame;
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
    alpm_list_t *files;
} alpm_pkg_meta_t;

int read_pkg_signature(int fd, alpm_pkg_meta_t *pkg);
int alpm_pkg_load_metadata(int fd, alpm_pkg_meta_t **_pkg);
void alpm_pkg_free_metadata(alpm_pkg_meta_t *pkg);

alpm_list_t *alpm_pkg_files(int fd, char *pkgname);

int alpm_db_populate(int fd, alpm_pkghash_t **pkgcache);

#endif
