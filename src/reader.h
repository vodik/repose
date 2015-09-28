#pragma once

#include <stddef.h>

struct archive;

struct archive_reader {
    struct archive *archive;

    char *block;
    char *block_offset;
    size_t block_size;

    int status;
};

struct archive_reader *archive_reader_new(struct archive *a);

int archive_getline(struct archive_reader *r, char **line);
int archive_fgets(struct archive_reader *r, char *line, size_t line_size);

