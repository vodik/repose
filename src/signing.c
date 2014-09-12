/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Simon Gomizelj, 2014
 */

#include "signing.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <fcntl.h>
#include <locale.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <gpgme.h>

#include "util.h"

static void _noreturn_ _printf_(3,4) gpgme_err(int eval, gpgme_error_t err, const char *fmt, ...)
{
    fprintf(stderr, "%s: ", program_invocation_short_name);

    if (fmt) {
        va_list ap;

        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fprintf(stderr, ": ");
    }

    fprintf(stderr, "%s\n", gpgme_strerror(err));
    exit(eval);
}

static inline char *sig_for(const char *file)
{
    return joinstring(file, ".sig", NULL);
}

static int init_gpgme(void)
{
    static int inited = false;
    gpgme_error_t err;
    gpgme_engine_info_t enginfo;

    /* we already successfully initialized the library */
    if (inited)
        return 0;

    /* calling gpgme_check_version() returns the current version and runs
     * some internal library setup code */
    gpgme_check_version(NULL);
    gpgme_set_locale(NULL, LC_CTYPE, setlocale(LC_CTYPE, NULL));
#ifdef LC_MESSAGES
    gpgme_set_locale(NULL, LC_MESSAGES, setlocale(LC_MESSAGES, NULL));
#endif

    /* check for OpenPGP support (should be a no-brainer, but be safe) */
    err = gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        return -1;

    err = gpgme_get_engine_info(&enginfo);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        return -1;

    inited = true;
    return 0;
}

int gpgme_verify(int rootfd, const char *file)
{
    gpgme_error_t err;
    gpgme_ctx_t ctx;
    gpgme_data_t in, sig;
    gpgme_verify_result_t result;
    gpgme_signature_t sigs;
    int rc = 0;

    if (init_gpgme() < 0)
        return -1;

    _cleanup_free_ char *sigfile = sig_for(file);
    _cleanup_close_ int sigfd = openat(rootfd, sigfile, O_RDONLY);
    _cleanup_close_ int fd = openat(rootfd, file, O_RDONLY);

    err = gpgme_new(&ctx);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        gpgme_err(EXIT_FAILURE, err, "failed to call gpgme_new()");

    err = gpgme_data_new_from_fd(&in, fd);
    if (err)
        gpgme_err(EXIT_FAILURE, err, "error reading %s", file);

    err = gpgme_data_new_from_fd(&sig, sigfd);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        gpgme_err(EXIT_FAILURE, err, "error reading %s", sigfile);

    err = gpgme_op_verify(ctx, sig, in, NULL);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        gpgme_err(EXIT_FAILURE, err, "failed to verify");

    result = gpgme_op_verify_result(ctx);
    sigs = result->signatures;
    if (gpgme_err_code(sigs->status) != GPG_ERR_NO_ERROR) {
        warnx("unexpected signature status: %s", gpgme_strerror(sigs->status));
        rc = -1;
    } else if (sigs->next) {
        warnx("unexpected number of signatures");
        rc = -1;
    } else if (sigs->summary == GPGME_SIGSUM_RED) {
        warnx("unexpected signature summary 0x%x", sigs->summary);
        rc = -1;
    } else if (sigs->wrong_key_usage) {
        warnx("unexpected wrong key usage");
        rc = -1;
    } else if (sigs->validity != GPGME_VALIDITY_FULL) {
        warnx("unexpected validity 0x%x", sigs->validity);
        rc = -1;
    } else if (gpgme_err_code(sigs->validity_reason) != GPG_ERR_NO_ERROR) {
        warnx("unexpected validity reason: %s", gpgme_strerror(sigs->validity_reason));
        rc = -1;
    }

    gpgme_data_release(in);
    gpgme_data_release(sig);
    gpgme_release(ctx);
    return rc;
}

void gpgme_sign(int rootfd, const char *file, const char *key)
{
    gpgme_error_t err;
    gpgme_ctx_t ctx;
    gpgme_data_t in, out;
    gpgme_sign_result_t result;

    if (init_gpgme() < 0)
        return;

    err = gpgme_new(&ctx);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        gpgme_err(EXIT_FAILURE, err, "failed to call gpgme_new()");

    if (key) {
        gpgme_key_t akey;

        err = gpgme_get_key(ctx, key, &akey, 1);
        if (err)
            gpgme_err(EXIT_FAILURE, err, "failed to set key %s", key);

        err = gpgme_signers_add(ctx, akey);
        if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
            gpgme_err(EXIT_FAILURE, err, "failed to call gpgme_signers_add()");

        gpgme_key_unref(akey);
    }

    _cleanup_free_ char *sigfile = sig_for(file);
    _cleanup_close_ int sigfd = openat(rootfd, sigfile, O_CREAT | O_WRONLY, 00644);
    _cleanup_close_ int fd = openat(rootfd, file, O_RDONLY);

    err = gpgme_data_new_from_fd(&in, fd);
    if (err)
        gpgme_err(EXIT_FAILURE, err, "error reading %s", file);

    err = gpgme_data_new(&out);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        gpgme_err(EXIT_FAILURE, err, "failed to call gpgme_data_new()");

    err = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_DETACH);
    if (err)
        gpgme_err(EXIT_FAILURE, err, "signing failed");

    result = gpgme_op_sign_result(ctx);
    if (!result)
        gpgme_err(EXIT_FAILURE, err, "signaure failed?");

    char buf[BUFSIZ];
    int ret;

    ret = gpgme_data_seek(out, 0, SEEK_SET);
    if (ret)
        return;

    while ((ret = gpgme_data_read(out, buf, BUFSIZ)) > 0)
        write(sigfd, buf, ret);

    gpgme_data_release(out);
    gpgme_data_release(in);
    gpgme_release(ctx);
}
