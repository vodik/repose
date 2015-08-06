typedef struct __dirstream DIR;
typedef struct __alpm_list_t alpm_list_t;
typedef int (*alpm_list_fn_cmp)(const void *, const void *);

alpm_list_t *alpm_list_add(alpm_list_t *list, void *data) {
    __coverity_escape__(data);
}

alpm_list_t *alpm_list_add_sorted(alpm_list_t *list, void *data, alpm_list_fn_cmp fn) {
    __coverity_escape__(data);
}

DIR *fdopendir(int fd) {
    __coverity_escape__(fd);
}
