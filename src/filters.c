#include "filters.h"

#include <fnmatch.h>
#include "util.h"

static bool match_target(struct pkg *pkg, const char *target, const char *fullname)
{
    if (streq(target, pkg->filename) || streq(target, pkg->name))
        return true;
    return fnmatch(target, fullname, 0) == 0;
}

bool match_targets(struct pkg *pkg, alpm_list_t *targets)
{
    _cleanup_free_ char *fullname = joinstring(pkg->name, "-", pkg->version, NULL);
    const alpm_list_t *node;

    for (node = targets; node; node = node->next) {
        if (match_target(pkg, node->data, fullname))
            return true;
    }

    return false;
}
