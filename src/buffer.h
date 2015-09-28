#pragma once

#include <stdlib.h>
#include <sys/types.h>

struct buffer {
    char *data;
    size_t len;
    size_t buflen;
};

int buffer_reserve(struct buffer *buf, size_t reserve);
void buffer_release(struct buffer *buf);
void buffer_clear(struct buffer *buf);

int buffer_putc(struct buffer *buf, const char c);
ssize_t buffer_printf(struct buffer *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
