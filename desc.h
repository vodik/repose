#pragma once

#include "desc.h"
#include "package.h"
#include <archive.h>
#include <archive_entry.h>

/* struct desc_parser { */
/*     int cs, act; */
/*     char *ts, *te; */
/* }; */

void read_desc(struct archive *archive, struct pkg *pkg);
