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

static int buffer_extendby(buffer_t *buf, size_t extby)
{
    char *data;
    size_t newlen = _unlikely_(!buf->buflen && extby < 64)
        ? 64 : buf->len + extby;

    if (newlen > buf->buflen) {
        newlen = next_power(newlen);
        data = realloc(buf->data, newlen);
        if (!data)
            return -errno;

        buf->buflen = newlen;
        buf->data = data;
    }

    return 0;
}

int buffer_init(buffer_t *buf, size_t reserve)
{
    zero(buf, sizeof(buffer_t));

    if (reserve && buffer_extendby(buf, reserve) < 0)
        return -errno;

    buf->data[buf->len] = '\0';
    return 0;
}

void buffer_clear(buffer_t *buf)
{
    buf->len = 0;
    if (buf->data)
        buf->data[buf->len] = '\0';
}

int buffer_putc(buffer_t *buf, const char c)
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
