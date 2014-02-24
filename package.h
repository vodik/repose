#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <alpm_list.h>

typedef struct pkg {
    unsigned long name_hash;
    char *filename;
    char *name;
    char *base;
    char *version;
    char *desc;
    char *url;
    char *packager;
    char *md5sum;
    char *sha256sum;
    char *base64sig;
    char *arch;
    size_t size;
    size_t isize;
    time_t builddate;

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
} pkg_t;

int load_package(pkg_t *pkg, int fd);
int load_package_signature(struct pkg *pkg, int fd);
int load_package_files(pkg_t *pkg, int fd);
void package_free(pkg_t *pkg);
