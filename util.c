#include "util.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#define WHITESPACE " \t\n\r"

char *joinstring(const char *root, ...)
{
    size_t len;
    char *ret = NULL, *p;
    const char *temp;
    va_list ap;

    if (!root)
        return NULL;

    len = strlen(root);

    va_start(ap, root);
    while ((temp = va_arg(ap, const char *))) {
        size_t temp_len = strlen(temp);

        if (temp_len > ((size_t) -1) - len)
            return NULL;

        len += temp_len;
    }
    va_end(ap);

    ret = malloc(len + 1);
    if (ret) {
        p = stpcpy(ret, root);

        va_start(ap, root);
        while ((temp = va_arg(ap, const char *)))
            p = stpcpy(p, temp);
        va_end(ap);
    }

    return ret;
}

size_t memcspn(const void *s, size_t n, const char *reject)
{
    size_t i;
    char table[1 << CHAR_BIT] = { 0 };
    const unsigned char *buf = s;

    while (*reject)
        table[(int)*reject++] = 1;

    for (i = 0; i < n && !table[buf[i]]; i++);
    return i;
}

int xstrtol(const char *str, long *out)
{
    char *end = NULL;

    if (str == NULL || *str == '\0')
        return -1;

    errno = 0;
    *out = strtoul(str, &end, 10);
    if (errno || str == end || (end && *end))
        return -1;

    return 0;
}

int xstrtoul(const char *str, unsigned long *out)
{
    char *end = NULL;

    if (str == NULL || *str == '\0')
        return -1;

    errno = 0;
    *out = strtoul(str, &end, 10);
    if (errno || str == end || (end && *end))
        return -1;

    return 0;
}

int realize(const char *path, char **root, const char **name)
{
    char *real = realpath(path, NULL);
    if (!real)
        return -errno;

    char *slash = strrchr(real, '/');
    if (slash) {
        *root = real;
        *slash = '\0';
        *name = slash + 1;
    } else {
        *root = NULL;
        *name = path;
    }

    return 0;
}

static char *hex_representation(unsigned char *bytes, size_t size)
{
    static const char *hex_digits = "0123456789abcdef";
    char *str = malloc(2 * size + 1);
    size_t i;

    for(i = 0; i < size; i++) {
        str[2 * i] = hex_digits[bytes[i] >> 4];
        str[2 * i + 1] = hex_digits[bytes[i] & 0x0f];
    }

    str[2 * size] = '\0';
    return str;
}

/** Compute the MD5 message digest of a file.
 * @param path file path of file to compute  MD5 digest of
 * @param output string to hold computed MD5 digest
 * @return 0 on success, 1 on file open error, 2 on file read error
 */
static int md5_file(int fd, unsigned char output[16])
{
	MD5_CTX ctx;
	unsigned char *buf;
	ssize_t n;

	/* MALLOC(buf, (size_t)ALPM_BUFFER_SIZE, return 1); */
	buf = malloc(BUFSIZ);

	MD5_Init(&ctx);

	while((n = read(fd, buf, BUFSIZ)) > 0 || errno == EINTR) {
		if(n < 0) {
			continue;
		}
		MD5_Update(&ctx, buf, n);
	}

	free(buf);

	if(n < 0) {
		return 2;
	}

	MD5_Final(output, &ctx);
	return 0;
}

/* third param is so we match the PolarSSL definition */
/** Compute the SHA-224 or SHA-256 message digest of a file.
 * @param path file path of file to compute SHA2 digest of
 * @param output string to hold computed SHA2 digest
 * @param is224 use SHA-224 instead of SHA-256
 * @return 0 on success, 1 on file open error, 2 on file read error
 */
static int sha2_file(int fd, unsigned char output[32], int is224)
{
	SHA256_CTX ctx;
	unsigned char *buf;
	ssize_t n;

	/* MALLOC(buf, (size_t)ALPM_BUFFER_SIZE, return 1); */
	buf = malloc(BUFSIZ);

	if(is224) {
		SHA224_Init(&ctx);
	} else {
		SHA256_Init(&ctx);
	}

	while((n = read(fd, buf, BUFSIZ)) > 0 || errno == EINTR) {
		if(n < 0) {
			continue;
		}
		if(is224) {
			SHA224_Update(&ctx, buf, n);
		} else {
			SHA256_Update(&ctx, buf, n);
		}
	}

	free(buf);

	if(n < 0) {
		return 2;
	}

	if(is224) {
		SHA224_Final(output, &ctx);
	} else {
		SHA256_Final(output, &ctx);
	}
	return 0;
}

char *compute_md5sum(int dirfd, char *filename)
{
    unsigned char output[16];
    _cleanup_close_ int fd = openat(dirfd, filename, O_RDONLY);

    if (md5_file(fd, output) > 0)
        return NULL;
    return hex_representation(output, 16);
}

char *compute_sha256sum(int dirfd, char *filename)
{
    unsigned char output[32];
    _cleanup_close_ int fd = openat(dirfd, filename, O_RDONLY);

    if (sha2_file(fd, output, 0) > 0)
        return NULL;
    return hex_representation(output, 32);
}

char *strstrip(char *s)
{
    char *e;

    s += strspn(s, WHITESPACE);

    for (e = strchr(s, 0); e > s; --e) {
        if (!strchr(WHITESPACE, e[-1]))
            break;
    }

    *e = 0;
    return s;
}
