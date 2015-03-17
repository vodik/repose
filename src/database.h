#pragma once

#include "pkghash.h"

enum contents {
    DB_DESC    = 1,
    DB_DEPENDS = 1 << 2,
    DB_FILES   = 1 << 3
};

int load_database(int fd, alpm_pkghash_t **pkgcache);
int save_database(int fd, alpm_pkghash_t *pkgcache, enum contents what, int compression, int poolfd);
