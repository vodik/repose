#include "memblock.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>

int memblock_open_fd(struct memblock_t *memblock, int fd)
{
    struct stat st;
    fstat(fd, &st);

    *memblock = (struct memblock_t){
        .len = st.st_size,
        .mem = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0)
    };

    return memblock->mem == MAP_FAILED ? -errno : 0;
}

int memblock_open_file(struct memblock_t *memblock, const char *filename)
{
    int ret = 0, fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -errno;

    ret = memblock_open_fd(memblock, fd);
    close(fd);
    return ret;
}

int memblock_close(struct memblock_t *memblock)
{
    if (memblock->mem != MAP_FAILED)
        return munmap(memblock->mem, memblock->len);
    return 0;
}
