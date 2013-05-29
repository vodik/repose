#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <openssl/md5.h>
#include <openssl/sha.h>

static char *hex_representation(unsigned char *bytes, size_t size)
{
	static const char *hex_digits = "0123456789abcdef";
	char *str;
	size_t i;

	str = malloc(2 * size + 1);

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

char *_compute_md5sum(int dirfd, char *filename)
{
    int fd = openat(dirfd, filename, O_RDONLY);
	unsigned char output[16];

	/* defined above for OpenSSL, otherwise defined in md5.h */
	if(md5_file(fd, output) > 0) {
        close(fd);
		return NULL;
	}

    close(fd);
	return hex_representation(output, 16);
}

/** Get the sha256 sum of file.
 * @param filename name of the file
 * @return the checksum on success, NULL on error
 * @addtogroup alpm_misc
 */
char *_compute_sha256sum(int dirfd, char *filename)
{
    int fd = openat(dirfd, filename, O_RDONLY);
	unsigned char output[32];

	/* defined above for OpenSSL, otherwise defined in sha2.h */
	if(sha2_file(fd, output, 0) > 0) {
        close(fd);
		return NULL;
	}

    close(fd);
	return hex_representation(output, 32);
}
