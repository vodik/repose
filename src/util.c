#include "util.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include "repose.h"

#define WHITESPACE " \t\n\r"

void trace(const char *fmt, ...)
{
    if (config.verbose) {
        va_list ap;

        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
}

static int oflags(const char *mode)
{
    int m, o;

    switch (mode[0]) {
    case 'r':
        m = O_RDONLY;
        o = 0;
        break;
    case 'w':
        m = O_WRONLY;
        o = O_CREAT | O_TRUNC;
        break;
    case 'a':
        m = O_WRONLY;
        o = O_CREAT | O_APPEND;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    while (*++mode) {
        switch (*mode) {
        case '+':
            m = (m & ~O_ACCMODE) | O_RDWR;
            break;
        }
    }

    return m | o;
}

FILE *fopenat(int dirfd, const char *path, const char *mode)
{
    int flags = oflags(mode);
    if (flags < 0)
        return NULL;

    int fd = openat(dirfd, path, flags);
    if (_unlikely_(fd < 0))
        return NULL;
    return fdopen(fd, mode);
}

void check_posix(intmax_t rc, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (_unlikely_(rc == -1))
        verr(EXIT_FAILURE, fmt, args);
    va_end(args);
}

void check_null(const void *ptr, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (_unlikely_(!ptr))
        verr(EXIT_FAILURE, fmt, args);
    va_end(args);
}

char *joinstring(const char *root, ...)
{
    size_t len;
    char *ret = NULL, *p;
    const char *temp;
    va_list ap;

    if (!root)
        return NULL;

    len = strlen(root);

    va_start(ap, root);
    while ((temp = va_arg(ap, const char *))) {
        size_t temp_len = strlen(temp);

        if (temp_len > ((size_t) -1) - len)
            return NULL;

        len += temp_len;
    }
    va_end(ap);

    ret = malloc(len + 1);
    if (ret) {
        p = stpcpy(ret, root);

        va_start(ap, root);
        while ((temp = va_arg(ap, const char *)))
            p = stpcpy(p, temp);
        va_end(ap);
    }

    return ret;
}

int xstrtol(const char *str, long *out)
{
    char *end = NULL;

    if (str == NULL || *str == '\0')
        return -1;

    errno = 0;
    *out = strtoul(str, &end, 10);
    if (errno || str == end || (end && *end))
        return -1;

    return 0;
}

int xstrtoul(const char *str, unsigned long *out)
{
    char *end = NULL;

    if (str == NULL || *str == '\0')
        return -1;

    errno = 0;
    *out = strtoul(str, &end, 10);
    if (errno || str == end || (end && *end))
        return -1;

    return 0;
}

static char *hex_representation(unsigned char *bytes, size_t size)
{
    static const char *hex_digits = "0123456789abcdef";
    char *str = malloc(2 * size + 1);
    size_t i;

    for(i = 0; i < size; i++) {
        str[2 * i] = hex_digits[bytes[i] >> 4];
        str[2 * i + 1] = hex_digits[bytes[i] & 0x0f];
    }

    str[2 * size] = '\0';
    return str;
}

static char *md5_fd(int fd)
{
    MD5_CTX ctx;
    unsigned char buf[BUFSIZ], output[16];
    ssize_t n;

    MD5_Init(&ctx);

    while ((n = read(fd, buf, sizeof(buf))) > 0 || errno == EINTR) {
        if (n < 0)
            continue;
        MD5_Update(&ctx, buf, n);
    }

    if (n < 0)
        return NULL;

    MD5_Final(output, &ctx);
    return hex_representation(output, 16);
}

static char *sha2_fd(int fd)
{
    SHA256_CTX ctx;
    unsigned char buf[BUFSIZ], output[32];
    ssize_t n;

    SHA256_Init(&ctx);

    while ((n = read(fd, buf, sizeof(buf))) > 0 || errno == EINTR) {
        if (n < 0)
            continue;
        SHA256_Update(&ctx, buf, n);
    }

    if (n < 0)
        return NULL;

    SHA256_Final(output, &ctx);
    return hex_representation(output, 32);
}

char *md5_file(int dirfd, const char *filename)
{
    _cleanup_close_ int fd = openat(dirfd, filename, O_RDONLY);
    check_posix(fd, "failed to open %s for md5 checksum", filename);
    return md5_fd(fd);
}

char *sha256_file(int dirfd, const char *filename)
{
    _cleanup_close_ int fd = openat(dirfd, filename, O_RDONLY);
    check_posix(fd, "failed to open %s for sha256 checksum", filename);
    return sha2_fd(fd);
}

char *strstrip(char *s)
{
    char *e;
    s += strspn(s, WHITESPACE);

    for (e = strchr(s, 0); e > s; --e) {
        if (!strchr(WHITESPACE, e[-1]))
            break;
    }

    *e = 0;
    return s;
}
