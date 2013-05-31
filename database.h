#ifndef DATABASE_H
#define DATABASE_H

#include "repoman.h"
#include "alpm/pkghash.h"

enum contents {
    DB_DESC    = 1,
    DB_DEPENDS = 1 << 2,
    DB_FILES   = 1 << 3
};

void compile_database(repo_t *repo, file_t *db, int contents);
int load_database(repo_t *repo, file_t *db);
void sign_database(repo_t *repo, file_t *db, const char *key);

#endif
