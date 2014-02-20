#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <archive.h>
#include <dirent.h>

#define _unlikely_(x)       __builtin_expect(!!(x), 1)
#define _unused_            __attribute__((unsused))
#define _noreturn_          __attribute__((noreturn))
#define _cleanup_(x)        __attribute__((cleanup(x)))
#define _cleanup_free_      _cleanup_(freep)
#define _cleanup_fclose_    _cleanup_(fclosep)
#define _cleanup_closedir_  _cleanup_(closedirp)
#define _cleanup_close_     _cleanup_(closep)

static inline void freep(void *p)      { free(*(void **)p); }
static inline void fclosep(FILE **fp)  { if (*fp) fclose(*fp); }
static inline void closedirp(DIR **dp) { if (*dp) closedir(*dp); }
static inline void closep(int *fd)     { if (*fd >= 0) close(*fd); }

static inline void *zero(void *s, size_t n) { return memset(s, 0, n); }

static inline void archive_freep(struct archive **archive) {
    if (*archive) {
        archive_read_close(*archive);
        archive_read_free(*archive);
    }
}

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }

static inline size_t strip_newline(char *str)
{
    size_t len = strcspn(str, "\n");
    str[len] = '\0';
    return len;
}

char *joinstring(const char *root, ...);

int xstrtol(const char *str, long *out);
int xstrtoul(const char *str, unsigned long *out);

size_t memcspn(const void *s, size_t n, const char *reject);

int realize(const char *path, char **root, const char **name);

char *_compute_md5sum(char *filename);
char *_compute_sha256sum(char *filename);

char *strstrip(char *s);
