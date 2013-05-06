#ifndef LIBROE_UTIL_HASHTABLE
#define LIBROE_UTIL_HASHTABLE

//#include "common.h"
#include <stddef.h>

typedef void (*cleanup_func)(void *ptr);
typedef unsigned int (*hashfunc)(const char *);

struct keyval_pair_t {
	int key;
	void *val;
};

struct hashnode_t {
	char *key;
	void *val;
	struct hashnode_t *next;
};

struct hashtable {
	size_t size;
	struct hashnode_t **nodes;
	hashfunc hasher;
};

struct hashtable *hashtable_new(size_t size, hashfunc hasher);
void hashtable_free(struct hashtable *table, cleanup_func clean);

void hashtable_add(struct hashtable *table, const char *key, void *data);
void *hashtable_get(const struct hashtable *table, const char *key);
void *hashtable_remove(struct hashtable *table, const char *key);

#endif
