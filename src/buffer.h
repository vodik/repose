#pragma once

#include <stdlib.h>
#include <stdarg.h>

struct buffer {
    char *data;
    size_t len;
    size_t buflen;
};

int buffer_init(struct buffer *buf, size_t reserve);
static inline void buffer_free(struct buffer *buf) { free(buf->data); }
void buffer_clear(struct buffer *buf);

int buffer_putc(struct buffer *buf, const char c);
ssize_t buffer_printf(struct buffer *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
