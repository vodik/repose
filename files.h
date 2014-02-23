#pragma once

#include <stdbool.h>
#include "pkghash.h"

alpm_pkghash_t *get_filecache(int dirfd, bool loadfiles);
