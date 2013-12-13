#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define unlikely(x)  __builtin_expect (!!(x), 0)

static inline size_t next_power(size_t x)
{
    return 1UL << (64 - __builtin_clzl(x - 1));
}

/* Extend the buffer in buf by at least len bytes.  Note len should
 * include the space required for the NUL terminator */
static inline int buffer_extendby(buffer_t *buf, size_t len)
{
    char* data;

    if (unlikely(!buf->buflen && len < 64))
        len = 64;
    else
        len += buf->len;

    if (len <= buf->buflen)
        return 0;

    buf->buflen = next_power(len);
    data = realloc(buf->data, buf->buflen);
    if (!data)
        return -errno;

    buf->data = data;
    return 0;
}

int buffer_init(buffer_t *buf, size_t reserve)
{
    buf->data = NULL;
    buf->len = 0;
    buf->buflen = 0;

    if (reserve)
        return buffer_extendby(buf, reserve);
    return 0;
}

void buffer_free(buffer_t *buf)
{
    if (buf->data)
        free(buf->data);
}

void buffer_clear(buffer_t *buf)
{
    buf->len = 0;
    if (buf->data)
        buf->data[buf->len] = '\0';
}

ssize_t buffer_putc(buffer_t *buf, const char c)
{
    if (buffer_extendby(buf, 2) < 0)
        return -errno;

    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
    return 0;
}

ssize_t buffer_printf(buffer_t *buf, const char *fmt, ...)
{
    size_t len = buf->buflen - buf->len;
    char *p = &buf->data[buf->len];

    va_list ap;
    va_start(ap, fmt);
    size_t rc = vsnprintf(p, len, fmt, ap);
    va_end(ap);

    if (rc >= len) {
        if (buffer_extendby(buf, rc + 1) < 0)
            return -errno;

        p = &buf->data[buf->len];

        va_start(ap, fmt);
        rc = vsnprintf(p, rc + 1, fmt, ap);
        va_end(ap);
    }
    buf->len += rc;
    return rc;
}
