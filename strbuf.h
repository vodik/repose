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
