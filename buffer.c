#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline size_t next_power(size_t x)
{
    --x;
    x |= x >> 0x01;
    x |= x >> 0x02;
    x |= x >> 0x04;
    x |= x >> 0x08;
    x |= x >> 0x10;
    return ++x;
}

/* Extend the buffer in buf by at least len bytes.  Note len should
 * include the space required for the NUL terminator */
static inline void buffer_extendby(buffer_t *buf, size_t len)
{
    char* data;

    len += buf->len;
    if (len <= buf->buflen)
        return;
    if (!buf->buflen)
        buf->buflen = 32;

    buf->buflen = next_power(len);
    printf("--- expand: %zd\n", buf->buflen);
    data = realloc(buf->data, buf->buflen);
    buf->data = data;
}

void buffer_init(buffer_t *buf, size_t reserve)
{
    buf->data = NULL;
    buf->len = 0;
    buf->buflen = 0;

    if (reserve)
        buffer_extendby(buf, reserve);
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

void buffer_putc(buffer_t *buf, const char c)
{
    buffer_extendby(buf, 2);
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

void buffer_header(buffer_t *buf, const char *header)
{
    size_t len = strlen(header);
    char *p = &buf->data[buf->len];

    buffer_extendby(buf, len + 4);

    *p++ = '%';
     p   = mempcpy(p, header, len);
    *p++ = '%';
    *p++ = '\n';
    *p   = '\0';

    buf->len += len + 3;
}

void buffer_printf(buffer_t *buf, const char *fmt, ...)
{
    size_t len = buf->buflen - buf->len;
    char *p = &buf->data[buf->len];

    va_list ap;
    va_start(ap, fmt);

    size_t rc = vsnprintf(p, len, fmt, ap);
    while (rc >= len) {
        buffer_extendby(buf, len + 1);
        rc = vsnprintf(p, len, fmt, ap);
    }
    buf->len += rc;

    va_end(ap);
}
