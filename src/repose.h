#pragma once

#include <stdbool.h>
#include "pkghash.h"
#include "util.h"

struct repo {
    const char *root;
    const char *pool;
    int rootfd;
    int poolfd;

    char *dbname;
    char *filesname;

    bool dirty;
    alpm_pkghash_t *cache;
};

struct config {
    int verbose;
    int compression;
    bool reflink;
    bool sign;
    char *arch;
};

extern struct config config;
void trace(const char *fmt, ...) _printf_(1, 2);
