/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Simon Gomizelj, 2014
 */

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
