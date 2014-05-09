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

#pragma once

#include "package.h"
#include "util.h"

bool match_target(struct pkg *pkg, const char *target, const char *fullname);
bool match_targets(struct pkg *pkg, alpm_list_t *targets);

static inline bool match_arch(struct pkg *pkg, const char *arch) {
    return streq(pkg->arch, arch) || streq(pkg->arch, "any");
}
