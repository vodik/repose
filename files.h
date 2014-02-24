#pragma once

#include <stdbool.h>
#include <alpm_list.h>
#include "pkghash.h"

alpm_pkghash_t *get_filecache(int dirfd, alpm_list_t *targets);
