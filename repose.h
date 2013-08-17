#ifndef REPOMAN_H
#define REPOMAN_H

#include <stdbool.h>
#include "alpm/pkghash.h"

enum state {
    REPO_NEW,
    REPO_CLEAN,
    REPO_DIRTY
};

enum compress {
    COMPRESS_NONE,
    COMPRESS_GZIP,
    COMPRESS_BZIP2,
    COMPRESS_XZ,
    COMPRESS_COMPRESS
};

typedef struct file {
    char *file, *link_file;
    char *sig,  *link_sig;
} file_t;

typedef struct repo {
    size_t cachesize;
    alpm_pkghash_t *pkgcache;
    char *pool;
    char *root;
    file_t db;
    file_t files;
    enum state state;
    enum compress compression;
    int dirfd;
} repo_t;

#endif
