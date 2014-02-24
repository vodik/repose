#pragma once

#include "package.h"

bool match_target_r(struct pkg *pkg, const char *target, const char *fullname);
bool match_targets(struct pkg *pkg, alpm_list_t *targets);
