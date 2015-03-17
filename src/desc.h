#pragma once

#include "desc.h"
#include "package.h"
#include <archive.h>
#include <archive_entry.h>

void read_desc(struct archive *archive, struct pkg *pkg);
