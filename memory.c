#include "memory.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "util.h"

int memblock_open_fd(memblock_t *memblock, int fd)
{
    struct stat st;

    fstat(fd, &st);
    *memblock = (memblock_t) {
        .len = st.st_size,
        .mem = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0)
    };

    return memblock->mem == MAP_FAILED ? -errno : 0;
}

int memblock_open_file(memblock_t *memblock, const char *filename)
{
    _cleanup_close_ int fd = open(filename, O_RDONLY);
    if (fd < 0)
        return -errno;

    return memblock_open_fd(memblock, fd);
}

int memblock_close(memblock_t *memblock)
{
    if (memblock->mem != MAP_FAILED)
        return munmap(memblock->mem, memblock->len);
    return 0;
}
