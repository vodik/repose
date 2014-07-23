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

#pragma once

#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>

typedef struct buffer {
    char *data;
    size_t len;
    size_t buflen;
} buffer_t;

int buffer_init(buffer_t *buf, size_t reserve);
void buffer_clear(buffer_t *buf);
static inline void buffer_free(buffer_t *buf) { free(buf->data); }

int buffer_putc(buffer_t *buf, const char c);
ssize_t buffer_printf(buffer_t *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
