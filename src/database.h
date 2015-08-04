#pragma once

#include "repose.h"
#include "pkghash.h"

enum contents {
    DB_DESC    = 1,
    DB_DEPENDS = 1 << 2,
    DB_FILES   = 1 << 3
};

int load_database(int fd, alpm_pkghash_t **pkgcache);
int write_database(struct repo *repo, const char *repo_name, enum contents what);
