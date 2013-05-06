#ifndef ARCHIVE_EXTRA_H
#define ARCHIVE_EXTRA_H

#include <stddef.h>
#include <archive.h>

struct archive_read_buffer {
    char *line;
    char *line_offset;
    size_t line_size;
    size_t real_line_size;

    char *block;
    char *block_offset;
    size_t block_size;

    long ret;
};

int archive_fgets(struct archive *a, struct archive_read_buffer *b, size_t entry_size);

#endif
