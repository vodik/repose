#include <stdlib.h>
#include <stdio.h>
#include <locale.h>
#include <errno.h>
#include <err.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <gpgme.h>

static int init_gpgme(void)
{
    static int init = 0;
    const char *version; // *sigdir;
    gpgme_error_t err;
    gpgme_engine_info_t enginfo;

    if(init) {
        /* we already successfully initialized the library */
        return 0;
    }

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

    /* set and check engine information */
    /* err = gpgme_set_engine_info(GPGME_PROTOCOL_OpenPGP, NULL, sigdir); */
    /* if (gpg_err_code(err) != GPG_ERR_NO_ERROR) */
    /*     return -1; */

    err = gpgme_get_engine_info(&enginfo);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        return -1;

    init = 1;
    return 0;
}

int gpgme_verify(int fd, int sigfd)
{
    gpgme_error_t rc;
    gpgme_ctx_t ctx;
    gpgme_data_t in, sig;
    gpgme_verify_result_t result;
    gpgme_signature_t sigs;
    int ret = 0;

    if (init_gpgme() < 0)
        return -1;

    rc = gpgme_new(&ctx);
    if (gpg_err_code(rc) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to call gpgme_new()");

    rc = gpgme_data_new_from_fd(&in, fd);
    if (gpg_err_code(rc) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "error reading `%s': %s", "file", gpgme_strerror(rc));

    rc = gpgme_data_new_from_fd(&sig, sigfd);
    if (gpg_err_code(rc) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "error reading `%s': %s", "signature", gpgme_strerror(rc));

    rc = gpgme_op_verify(ctx, sig, in, NULL);
    if (gpg_err_code(rc) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to verify: %s", gpgme_strerror(rc));

    result = gpgme_op_verify_result(ctx);
    if (gpg_err_code(rc) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to get results: %s", gpgme_strerror(rc));

    sigs = result->signatures;
    if (gpgme_err_code(sigs->status) != GPG_ERR_NO_ERROR) {
        warnx("unexpected signature status: %s", gpgme_strerror(sigs->status));
        ret = -1;
    } else if (sigs->next) {
        warnx("unexpected number of signatures");
        ret = -1;
    } else if (sigs->summary == GPGME_SIGSUM_RED) {
        warnx("unexpected signature summary 0x%x", sigs->summary);
        ret = -1;
    } else if (sigs->wrong_key_usage) {
        warnx("unexpected wrong key usage");
        ret = -1;
    } else if (sigs->validity != GPGME_VALIDITY_FULL) {
        warnx("unexpected validity 0x%x", sigs->validity);
        ret = -1;
    } else if (gpgme_err_code(sigs->validity_reason) != GPG_ERR_NO_ERROR) {
        warnx("unexpected validity reason: %s", gpgme_strerror(sigs->validity_reason));
        ret = -1;
    }

    gpgme_data_release(in);
    gpgme_data_release(sig);
    gpgme_release(ctx);
    return ret;
}

void gpgme_sign(int fd, int sigfd, const char *key)
{
    gpgme_error_t rc;
    gpgme_ctx_t ctx;
    gpgme_data_t in, out;
    gpgme_sign_result_t result;

    if (init_gpgme() < 0)
        return;

    rc = gpgme_new(&ctx);
    if (gpg_err_code(rc) != GPG_ERR_NO_ERROR)
        err(EXIT_FAILURE, "failed to call gpgme_new()");

    /* use armor for now... we're testing! */
    /* gpgme_set_armor(ctx, 1); */

    if (key) {
        gpgme_key_t akey;

        rc = gpgme_get_key(ctx, key, &akey, 1);
        if (rc)
            errx(EXIT_FAILURE, "failed to set key %s", key);

        rc = gpgme_signers_add(ctx, akey);
        if (gpg_err_code(rc) != GPG_ERR_NO_ERROR)
            errx(EXIT_FAILURE, "failed to call gpgme_signers_add()");

        gpgme_key_unref(akey);
    }

    rc = gpgme_data_new_from_fd(&in, fd);
    if (rc)
        errx(EXIT_FAILURE, "rcor reading `%s': %s", "file", gpgme_strerror(rc));

    rc = gpgme_data_new(&out);
    if (gpg_err_code(rc) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to call gpgme_data_new()");

    rc = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_DETACH);
    if (rc)
        errx(EXIT_FAILURE, "signing failed: %s", gpgme_strerror(rc));

    result = gpgme_op_sign_result(ctx);
    /* if (result) */
    /*     print_result(result); */
    if (!result)
        errx(EXIT_FAILURE, "signaure failed?");

    /* fputs("Begin Output:\n", stdout); */
    /* print_data(out); */
    /* fputs("End Output.\n", stdout); */

    char buf[BUFSIZ];
    int ret;

    ret = gpgme_data_seek(out, 0, SEEK_SET);
    if (ret)
        return;

    while ((ret = gpgme_data_read(out, buf, BUFSIZ)) > 0) {
        if (write(sigfd, buf, ret) < 0)
            err(EXIT_FAILURE, "failed to write out signtaure");
    }

    /* if (ret < 0) */
    /*     return; */
    gpgme_data_release(out);
    gpgme_data_release(in);
    gpgme_release(ctx);
}
