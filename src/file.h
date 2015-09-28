#pragma once

#include <sys/stat.h>

struct file_t {
    int fd;
    struct stat st;
    char *mmap;
};

int file_from_fd(struct file_t *file, int fd);
int file_close(struct file_t *file);
