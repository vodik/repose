#pragma once

#include <alpm_list.h>
#include "pkgcache.h"

alpm_pkgcache_t *get_filecache(int dirfd, alpm_list_t *targets, const char *arch);
