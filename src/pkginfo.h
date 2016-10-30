#pragma once

#include <sys/types.h>
#include <limits.h>
#include "package.h"

struct archive;

struct pkginfo_parser {
    int cs;
    enum pkg_entry entry;
    size_t pos;
    char store[LINE_MAX];
};

void pkginfo_parser_init(struct pkginfo_parser *parser);
ssize_t pkginfo_parser_feed(struct pkginfo_parser *parser, struct pkg *pkg,
                            char *buf, size_t buf_len);
void read_pkginfo(struct archive *archive, struct pkg *pkg);
