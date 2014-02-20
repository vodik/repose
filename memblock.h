#pragma once

#include <stddef.h>

struct memblock_t {
    char *mem;
    size_t len;
};

int memblock_open_fd(struct memblock_t *memblock, int fd);
int memblock_open_file(struct memblock_t *memblock, const char *filename);
int memblock_close(struct memblock_t *memblock);
