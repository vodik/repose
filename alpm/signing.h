#ifndef SIGNING_H
#define SIGNING_H

void gpgme_sign(const char *root, const char *file, const char *key);
int gpgme_verify(const char *filepath, const char *sigpath);

#endif
