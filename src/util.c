#include "util.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>

#define WHITESPACE " \t\n\r"

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

void check_posix(intmax_t rc, const char *fmt, ...)
{
    if (_unlikely_(rc == -1)) {
        va_list args;
        va_start(args, fmt);
        verr(EXIT_FAILURE, fmt, args);
        va_end(args);
    }
}

void check_null(const void *ptr, const char *fmt, ...)
{
    if (_unlikely_(!ptr)) {
        va_list args;
        va_start(args, fmt);
        verr(EXIT_FAILURE, fmt, args);
        va_end(args);
    }
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

        if (temp_len > ((size_t) -1) - len) {
            va_end(ap);
            return NULL;
        }

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

static int xstrtoul(const char *str, unsigned long *out)
{
    char *end = NULL;
    errno = 0;

    if (!str || !str[0]) {
        errno = EINVAL;
        return -1;
    }

    *out = strtoul(str, &end, 10);
    if (errno) {
        return -1;
    } else if (str == end || (end && *end)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int str_to_size(const char *str, size_t *out)
{
    unsigned long value = 0;
    if (xstrtoul(str, &value) < 0)
        return -1;

    if (value > SIZE_MAX) {
        errno = ERANGE;
        return -1;
    }

    *out = (size_t)value;
    return 0;
}

int str_to_time(const char *str, time_t *out)
{
    struct tm tm;
    errno = 0;

    const char *end = strptime(str, "%s", &tm);
    if (str == end || (end && *end)) {
        errno = EINVAL;
        return -1;
    }

    *out = mktime(&tm);
    return 0;
}

char *hex_representation(unsigned char *bytes, size_t size)
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
