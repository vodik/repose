#ifndef SIGNING_H
#define SIGNING_H

void gpgme_sign(int dirfd, const char *filepath, const char *sigpath, const char *key);
int gpgme_verify(int dirfd, const char *filepath, const char *sigpath);

#endif
