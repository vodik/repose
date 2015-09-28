#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <alpm_list.h>
#include "package.h"
#include "util.h"

bool match_targets(struct pkg *pkg, alpm_list_t *targets);

static inline bool match_arch(struct pkg *pkg, const char *arch)
{
    if (!pkg->arch)
        return arch != NULL;
    return streq(pkg->arch, arch) || streq(pkg->arch, "any");
}
