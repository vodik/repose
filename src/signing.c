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
#include <locale.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <gpgme.h>

static int init_gpgme(void)
{
    static int init = 0;
    const char *version; // *sigdir;
    gpgme_error_t err;
    gpgme_engine_info_t enginfo;

    /* we already successfully initialized the library */
    if (init)
        return 0;

    /* calling gpgme_check_version() returns the current version and runs
     * some internal library setup code */
    version = gpgme_check_version(NULL);
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

    init = 1;
    return 0;
}

int gpgme_verify(const char *filepath, const char *sigpath)
{
    gpgme_error_t err;
    gpgme_ctx_t ctx;
    gpgme_data_t in, sig;
    gpgme_verify_result_t result;
    gpgme_signature_t sigs;
    int rc = 0;

    if (init_gpgme() < 0)
        return -1;

    err = gpgme_new(&ctx);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to call gpgme_new()");

    err = gpgme_data_new_from_file(&in, filepath, 1);
    if (err)
        errx(EXIT_FAILURE, "error reading `%s': %s", filepath, gpgme_strerror(err));

    err = gpgme_data_new_from_file(&sig, sigpath, 1);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "error reading `%s': %s", filepath, gpgme_strerror(err));

    err = gpgme_op_verify(ctx, sig, in, NULL);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to verify: %s", gpgme_strerror(err));

    result = gpgme_op_verify_result(ctx);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to get results: %s", gpgme_strerror(err));

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

void gpgme_sign(const char *root, const char *file, const char *key)
{
    gpgme_error_t err;
    gpgme_ctx_t ctx;
    gpgme_data_t in, out;
    gpgme_sign_result_t result;
    char filepath[PATH_MAX];

    if (init_gpgme() < 0)
        return;

    err = gpgme_new(&ctx);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to call gpgme_new()");

    if (key) {
        gpgme_key_t akey;

        err = gpgme_get_key(ctx, key, &akey, 1);
        if (err)
            errx(EXIT_FAILURE, "failed to set key %s", key);

        err = gpgme_signers_add(ctx, akey);
        if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
            errx(EXIT_FAILURE, "failed to call gpgme_signers_add()");

        gpgme_key_unref(akey);
    }

    snprintf(filepath, PATH_MAX, "%s/%s", root, file);
    err = gpgme_data_new_from_file(&in, filepath, 1);
    if (err)
        errx(EXIT_FAILURE, "error reading `%s': %s", file, gpgme_strerror(err));

    err = gpgme_data_new(&out);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to call gpgme_data_new()");

    err = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_DETACH);
    if (err)
        errx(EXIT_FAILURE, "signing failed: %s", gpgme_strerror(err));

    result = gpgme_op_sign_result(ctx);
    if (!result)
        errx(EXIT_FAILURE, "signaure failed?");

    snprintf(filepath, PATH_MAX, "%s/%s.sig", root, file);
    FILE *fp = fopen(filepath, "w");

    char buf[BUFSIZ];
    int ret;

    ret = gpgme_data_seek(out, 0, SEEK_SET);
    if (ret)
        return;

    while ((ret = gpgme_data_read(out, buf, BUFSIZ)) > 0)
        fwrite(buf, 1, ret, fp);

    gpgme_data_release(out);
    gpgme_data_release(in);
    gpgme_release(ctx);
}
