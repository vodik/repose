#pragma once

#include <stddef.h>

typedef struct memblock {
    char *mem;
    size_t len;
} memblock_t;

int memblock_open_fd(memblock_t *memblock, int fd);
int memblock_open_file(memblock_t *memblock, const char *filename);
int memblock_close(memblock_t *memblock);
