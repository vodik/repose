#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include "util.h"

static inline size_t next_power(size_t x)
{
    const size_t zeros = __builtin_clzl(x - 1);
    if (!zeros)
        return SIZE_MAX;
    return 1UL << (sizeof(x) * 8 - zeros);
}

static inline int addsz(size_t a, size_t b, size_t *r)
{
    errno = 0;
    if (_unlikely_(__builtin_add_overflow(a, b, r)))
        errno = ERANGE;
    return -errno;
}

static int buffer_extendby(struct buffer *buf, size_t extby)
{
    size_t newlen = 64;
    if (buf->buflen || extby > newlen) {
        if (addsz(buf->len, extby, &newlen) < 0)
            return -errno;
    }

    if (_unlikely_(!buf->data) || newlen > buf->buflen) {
        newlen = next_power(newlen);

        char *data = realloc(buf->data, newlen);
        if (!data)
            return -errno;

        buf->buflen = newlen;
        buf->data = data;
    }

    buf->data[buf->len] = 0;
    return 0;
}

int buffer_reserve(struct buffer *buf, size_t reserve)
{
    if (buffer_extendby(buf, reserve) < 0)
        return -errno;
    return 0;
}

void buffer_release(struct buffer *buf)
{
    free(buf->data);
    *buf = (struct buffer){0};
}

void buffer_clear(struct buffer *buf)
{
    buf->len = 0;
    if (buf->data)
        buf->data[buf->len] = '\0';
}

int buffer_putc(struct buffer *buf, const char c)
{
    if (buffer_extendby(buf, 2) < 0)
        return -errno;

    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
    return 0;
}

ssize_t buffer_printf(struct buffer *buf, const char *fmt, ...)
{
    size_t len = buf->buflen - buf->len;

    if (!buf->data && buffer_extendby(buf, 0) < 0)
        return -errno;

    va_list ap;
    va_start(ap, fmt);
    size_t nbytes_w = vsnprintf(&buf->data[buf->len], len, fmt, ap);
    va_end(ap);

    if (nbytes_w >= len) {
        if (buffer_extendby(buf, nbytes_w + 1) < 0)
            return -errno;

        va_start(ap, fmt);
        nbytes_w = vsnprintf(&buf->data[buf->len], nbytes_w + 1, fmt, ap);
        va_end(ap);
    }

    buf->len += nbytes_w;
    return nbytes_w;
}
