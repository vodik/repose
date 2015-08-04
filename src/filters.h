#pragma once

#include "package.h"
#include "util.h"

bool match_target(struct pkg *pkg, const char *target, const char *fullname);
bool match_targets(struct pkg *pkg, alpm_list_t *targets);

static inline bool match_arch(struct pkg *pkg, const char *arch) {
    if (!pkg->arch)
        return arch != NULL;
    return streq(pkg->arch, arch) || streq(pkg->arch, "any");
}
