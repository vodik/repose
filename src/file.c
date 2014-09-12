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

#include "file.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>

int file_from_fd(struct file_t *file, int fd)
{
    *file = (struct file_t){.fd = fd};

    fstat(fd, &file->st);

    file->mmap = mmap(NULL, file->st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
    madvise(file->mmap, file->st.st_size, MADV_WILLNEED | MADV_SEQUENTIAL);

    return file->mmap == MAP_FAILED ? -errno : 0;
}

int file_open(struct file_t *file, const char *filename)
{
    int ret = 0, fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -errno;

    ret = file_from_fd(file, fd);
    return ret;
}

int file_close(struct file_t *file)
{
    close(file->fd);
    return file->mmap != MAP_FAILED ? munmap(file->mmap, file->st.st_size) : 0;
}
