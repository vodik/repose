#pragma once

#include <stdbool.h>
#include "pkghash.h"

enum state {
    REPO_NEW,
    REPO_CLEAN,
    REPO_DIRTY
};

struct repo {
    enum state state;
    const char *root;
    const char *pool;
    int rootfd;
    int poolfd;

    char *dbname;
    char *filesname;

    bool reflink;
    bool sign;
    alpm_pkghash_t *cache;
};

struct config {
    int verbose;
    int compression;
};
