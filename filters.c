#include "filters.h"

#include <fnmatch.h>
#include "util.h"

bool match_target_r(struct pkg *pkg, const char *target, const char *fullname)
{
    if (streq(target, pkg->filename))
        return true;
    else if (streq(target, pkg->name))
        return true;
    return fnmatch(target, fullname, 0) == 0;
}

bool match_targets(struct pkg *pkg, alpm_list_t *targets)
{
    const alpm_list_t *node;
    bool ret = false;

    _cleanup_free_ char *fullname = joinstring(pkg->name, "-", pkg->version, NULL);

    for (node = targets; node && !ret; node = node->next)
        ret = match_target_r(pkg, node->data, fullname);

    return ret;
}
