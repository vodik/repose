#ifndef SIGNING_H
#define SIGNING_H

void gpgme_sign(int rootfd, const char *file, const char *key);
int gpgme_verify(int rootfd, const char *file);

#endif
