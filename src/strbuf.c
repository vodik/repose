#include "strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "util.h"

static inline size_t next_power(size_t x)
{
    return 1UL << (64 - __builtin_clzl(x - 1));
}

static int buffer_extendby(struct buffer *buf, size_t extby)
{
    size_t newlen = _unlikely_(!buf->buflen && extby < 64)
        ? 64 : buf->len + extby;

    if (newlen > buf->buflen) {
        newlen = next_power(newlen);
        char *data = realloc(buf->data, newlen);
        if (!data)
            return -errno;

        buf->buflen = newlen;
        buf->data = data;
    }

    return 0;
}

int buffer_init(struct buffer *buf, size_t reserve)
{
    *buf = (struct buffer){0};

    if (buffer_extendby(buf, reserve) < 0)
        return -errno;

    buf->data[buf->len] = '\0';
    return 0;
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
