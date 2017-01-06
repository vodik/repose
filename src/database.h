#pragma once

#include "pkgcache.h"

struct repo;

enum contents {
    DB_DESC    = 1,
    DB_DEPENDS = 1 << 2,
    DB_FILES   = 1 << 3,
    DB_DELTAS  = 1 << 4
};

int load_database(int fd, struct pkgcache **pkgcache);
int write_database(struct repo *repo, const char *repo_name, enum contents what);
