#pragma once

#include <stddef.h>
#include <sys/types.h>
#include <limits.h>
#include "package.h"

struct archive;

struct desc_parser {
    int cs;
    enum pkg_entry entry;
    size_t pos;
    char store[LINE_MAX];
};

void desc_parser_init(struct desc_parser *parser);
ssize_t desc_parser_feed(struct desc_parser *parser, struct pkg *pkg,
                      char *buf, size_t buf_len);

void read_desc(struct archive *archive, struct pkg *pkg);
