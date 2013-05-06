#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <stdarg.h>

#include <stddef.h>

typedef struct buffer {
    char *data;
    size_t len;
    size_t buflen;
} buffer_t;

void buffer_init(buffer_t *buf, size_t reserve);
void buffer_free(buffer_t *buf);

void buffer_clear(buffer_t *buf);

void buffer_printf(buffer_t *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));
void buffer_putc(buffer_t *buf, const char c);

#endif /* end of include guard: BUFFER_H */
