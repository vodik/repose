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

/* static void print_data(gpgme_data_t dh) */
/* { */
/* #define BUF_SIZE 512 */
/*     char buf[BUF_SIZE + 1]; */
/*     int ret; */

/*     ret = gpgme_data_seek(dh, 0, SEEK_SET); */
/*     if (ret) */
/*         return; */

/*     while ((ret = gpgme_data_read(dh, buf, BUF_SIZE)) > 0) */
/*         fwrite (buf, ret, 1, stdout); */

/*     if (ret < 0) */
/*         return; */
/* } */

/* static void print_result(gpgme_sign_result_t result) */
/* { */
/*     gpgme_invalid_key_t invkey; */
/*     gpgme_new_signature_t sig; */

/*     for (invkey = result->invalid_signers; invkey; invkey = invkey->next) { */
/*         printf("Signing key `%s' not used: %s <%s>\n", */
/*                invkey->fpr ? invkey->fpr : "[none]", */
/*                gpgme_strerror (invkey->reason), gpgme_strsource (invkey->reason)); */
/*     } */

/*     for (sig = result->signatures; sig; sig = sig->next) { */
/*         printf("Key fingerprint: %s\n", sig->fpr ? sig->fpr : "[none]"); */
/*         printf("Public key algo: %d\n", sig->pubkey_algo); */
/*         printf("Hash algo .....: %d\n", sig->hash_algo); */
/*         printf("Creation time .: %ld\n", sig->timestamp); */
/*         printf("Sig class .....: 0x%u\n", sig->sig_class); */
/*     } */
/* } */

void gpgme_sign(const char *file, const char *key)
{
    gpgme_error_t err;
    gpgme_ctx_t ctx;
    gpgme_data_t in, out;
    gpgme_sign_result_t result;

    if (init_gpgme() < 0)
        return;

    err = gpgme_new(&ctx);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to call gpgme_new()");

    /* use armor for now... we're testing! */
    /* gpgme_set_armor(ctx, 1); */

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

    err = gpgme_data_new_from_file(&in, file, 1);
    if (err)
        errx(EXIT_FAILURE, "error reading `%s': %s\n", file, gpgme_strerror(err));

    err = gpgme_data_new(&out);
    if (gpg_err_code(err) != GPG_ERR_NO_ERROR)
        errx(EXIT_FAILURE, "failed to call gpgme_data_new()");

    err = gpgme_op_sign(ctx, in, out, GPGME_SIG_MODE_DETACH);
    if (err)
        errx(EXIT_FAILURE, "signing failed: %s\n", gpgme_strerror(err));

    result = gpgme_op_sign_result(ctx);
    /* if (result) */
    /*     print_result(result); */
    if (!result)
        errx(EXIT_FAILURE, "signaure failed?\n");

    /* fputs("Begin Output:\n", stdout); */
    /* print_data(out); */
    /* fputs("End Output.\n", stdout); */

    char filepath[PATH_MAX];
    snprintf(filepath, PATH_MAX, "%s.sig", file);
    FILE *fp = fopen(filepath, "w");

    char buf[BUFSIZ];
    int ret;

    ret = gpgme_data_seek(out, 0, SEEK_SET);
    if (ret)
        return;

    while ((ret = gpgme_data_read(out, buf, BUFSIZ)) > 0)
        fwrite(buf, 1, ret, fp);

    /* if (ret < 0) */
    /*     return; */

    gpgme_data_release(out);
    gpgme_data_release(in);
}
