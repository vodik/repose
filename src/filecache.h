#pragma once

#include <alpm_list.h>
#include "pkghash.h"

alpm_pkghash_t *get_filecache(int dirfd, alpm_list_t *targets, const char *arch);
