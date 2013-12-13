#ifndef BUFFER_H
#define BUFFER_H

#include <stdarg.h>
#include <sys/types.h>

typedef struct buffer {
    char *data;
    size_t len;
    size_t buflen;
} buffer_t;

int buffer_init(buffer_t *buf, size_t reserve);
void buffer_free(buffer_t *buf);

void buffer_clear(buffer_t *buf);

ssize_t buffer_putc(buffer_t *buf, const char c);
ssize_t buffer_printf(buffer_t *buf, const char *fmt, ...) __attribute__((format (printf, 2, 3)));

#endif /* end of include guard: BUFFER_H */
