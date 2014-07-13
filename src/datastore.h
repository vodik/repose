#pragma once

#include <sys/types.h>

typedef struct vector_t {
    size_t class;
    size_t length;
    size_t entries;
    char *data;
} vector_t;

typedef struct keypair_t {
    const char *key;
    unsigned long hash;
    int value;
} keypair_t;

typedef struct hashtable_t {
    size_t class;
    size_t length;
    size_t entries;
    size_t limit;
    size_t buckets;
    keypair_t *data;
} hashtable_t;

int vector_init(struct vector_t *v, size_t class, size_t reserve);
void *vector_store(struct vector_t *v, size_t *idx);

int hashtable_init(struct hashtable_t *t, size_t class, size_t size);
keypair_t *hashtable_store(struct hashtable_t *t, const char *key);
keypair_t *hashtable_get(struct hashtable_t *t, const char *key);
