#pragma once

#include <stdbool.h>
#include "pkghash.h"

struct repo {
    const char *root;
    const char *pool;
    int rootfd;
    int poolfd;

    char *dbname;
    char *filesname;

    bool dirty;
    bool reflink;
    bool sign;
    alpm_pkghash_t *cache;
};

struct config {
    int verbose;
    int compression;
};
