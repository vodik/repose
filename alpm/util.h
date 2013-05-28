#ifndef UTIL_H
#define UTIL_H

char *_compute_md5sum(int dirfd, char *filename);
char *_compute_sha256sum(int dirfd, char *filename);

#endif
