#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>
#include <alpm.h>
#include <alpm_list.h>

#include "alpm/alpm_metadata.h"
#include "alpm/pkghash.h"
#include "alpm/signing.h"
#include "buffer.h"

#define NOCOLOR     "\033[0m"
#define BOLD        "\033[1m"

#define BLACK       "\033[0;30m"
#define RED         "\033[0;31m"
#define GREEN       "\033[0;32m"
#define YELLOW      "\033[0;33m"
#define BLUE        "\033[0;34m"
#define MAGENTA     "\033[0;35m"
#define CYAN        "\033[0;36m"

#define BOLDBLACK   "\033[1;30m"
#define BOLDRED     "\033[1;31m"
#define BOLDGREEN   "\033[1;32m"
#define BOLDYELLOW  "\033[1;33m"
#define BOLDBLUE    "\033[1;34m"
#define BOLDMAGENTA "\033[1;35m"
#define BOLDCYAN    "\033[1;36m"

enum action {
    ACTION_VERIFY,
    ACTION_UPDATE,
    ACTION_REMOVE,
    ACTION_QUERY,
    INVALID_ACTION
};

enum compress {
    COMPRESS_NONE,
    COMPRESS_GZIP,
    COMPRESS_BZIP2,
    COMPRESS_XZ,
    COMPRESS_COMPRESS
};

typedef struct repo {
    alpm_db_meta_t *db;
    char root[PATH_MAX];
    char name[PATH_MAX];
    char file[PATH_MAX];
    bool db_signed;
    enum compress compression;
} repo_t;

typedef struct repo_writer {
    struct archive *archive;
    struct archive_entry *entry;
    struct buffer buf;
} repo_writer_t;

typedef struct colstr {
    const char *colon;
    const char *warn;
    const char *error;
    const char *nocolor;
} colstr_t;

static struct {
    const char *key;
    enum action action;

    short clean;
    bool info;
    bool sign;

    bool color;
    colstr_t colstr;
} cfg = {
    .action = INVALID_ACTION,
    .colstr = {
        .colon   = ":: ",
        .warn    = "",
        .error   = "",
        .nocolor = ""
    }
};

static int colon_printf(const char *fmt, ...)
{
    int ret;
    va_list args;

    va_start(args, fmt);
    fputs(cfg.colstr.colon, stdout);
    ret = vprintf(fmt, args);
    fputs(cfg.colstr.nocolor, stdout);
    va_end(args);

    fflush(stdout);
    return ret;
}

static inline void pkg_real_filename(repo_t *r, const char *pkgname, char *pkgpath, char *sigpath)
{
    if (pkgpath)
        snprintf(pkgpath, PATH_MAX, "%s/%s", r->root, pkgname);
    if (sigpath)
        snprintf(sigpath, PATH_MAX, "%s/%s.sig", r->root, pkgname);
}

/* {{{ WRITING REPOS */
static void write_list(struct buffer *buf, const char *header, const alpm_list_t *lst)
{
    buffer_printf(buf, "%%%s%%\n", header);
    for (; lst; lst = lst->next) {
        const char *str = lst->data;
        buffer_printf(buf, "%s\n", str);
    }
    buffer_putc(buf, '\n');
}

static void write_string(struct buffer *buf, const char *header, const char *str)
{
    buffer_printf(buf, "%%%s%%\n%s\n\n", header, str);
}

static void write_long(struct buffer *buf, const char *header, long val)
{
    buffer_printf(buf, "%%%s%%\n%ld\n\n", header, val);
}

static void write_depends_file(const alpm_pkg_meta_t *pkg, struct buffer *buf)
{
    write_list(buf, "DEPENDS",     pkg->depends);
    write_list(buf, "CONFLICTS",   pkg->conflicts);
    write_list(buf, "PROVIDES",    pkg->provides);
    write_list(buf, "OPTDEPENDS",  pkg->optdepends);
    write_list(buf, "MAKEDEPENDS", pkg->makedepends);
}

static void write_desc_file(repo_t *r, const alpm_pkg_meta_t *pkg, struct buffer *buf)
{
    char pkgpath[PATH_MAX];

    pkg_real_filename(r, pkg->filename, pkgpath, NULL);
    write_string(buf, "FILENAME",  pkg->filename);
    write_string(buf, "NAME",      pkg->name);
    write_string(buf, "VERSION",   pkg->version);
    write_string(buf, "DESC",      pkg->desc);
    write_long(buf,   "CSIZE",     (long)pkg->size);
    write_long(buf,   "ISIZE",     (long)pkg->isize);

    if (pkg->md5sum) {
        write_string(buf, "MD5SUM", pkg->md5sum);
    } else {
        char *md5sum = alpm_compute_md5sum(pkgpath);
        write_string(buf, "MD5SUM", md5sum);
        free(md5sum);
    }

    if (pkg->sha256sum) {
        write_string(buf, "SHA256SUM", pkg->sha256sum);
    } else {
        char *sha256sum = alpm_compute_sha256sum(pkgpath);
        write_string(buf, "SHA256SUM", sha256sum);
        free(sha256sum);
    }

    if (pkg->base64_sig)
        write_string(buf, "PGPSIG", pkg->base64_sig);

    write_string(buf, "URL",       pkg->url);
    write_list(buf,   "LICENSE",   pkg->license);
    write_string(buf, "ARCH",      pkg->arch);
    write_long(buf,   "BUILDDATE", pkg->builddate);
    write_string(buf, "PACKAGER",  pkg->packager);
}

static void archive_write_buffer(struct archive *a, struct archive_entry *ae,
                                 const char *path, struct buffer *buf)
{
    time_t now = time(NULL);

    archive_entry_set_pathname(ae, path);
    archive_entry_set_size(ae, buf->len);
    archive_entry_set_filetype(ae, AE_IFREG);
    archive_entry_set_perm(ae, 0644);
    archive_entry_set_ctime(ae, now, 0);
    archive_entry_set_mtime(ae, now, 0);
    archive_entry_set_atime(ae, now, 0);

    archive_write_header(a, ae);
    archive_write_data(a, buf->data, buf->len);
}

static repo_writer_t *repo_write_new(const char *filename, enum compress compression)
{
    repo_writer_t *repo = malloc(sizeof(repo_writer_t));
    repo->archive = archive_write_new();
    repo->entry = archive_entry_new();

    switch (compression) {
    case COMPRESS_NONE:
        archive_write_add_filter_none(repo->archive);
        break;
    case COMPRESS_GZIP:
        archive_write_add_filter_gzip(repo->archive);
        break;
    case COMPRESS_BZIP2:
        archive_write_add_filter_bzip2(repo->archive);
        break;
    case COMPRESS_XZ:
        archive_write_add_filter_xz(repo->archive);
        break;
    case COMPRESS_COMPRESS:
        archive_write_add_filter_compress(repo->archive);
        break;
    }
    archive_write_set_format_pax_restricted(repo->archive);
    archive_write_open_filename(repo->archive, filename);

    buffer_init(&repo->buf, 1024);

    return repo;
}

static void repo_write_pkg(repo_t *r, repo_writer_t *repo, alpm_pkg_meta_t *pkg)
{
    char entry[PATH_MAX];

    archive_entry_clear(repo->entry);
    buffer_clear(&repo->buf);
    write_desc_file(r, pkg, &repo->buf);

    /* generate the 'desc' file */
    snprintf(entry, PATH_MAX, "%s-%s/%s", pkg->name, pkg->version, "desc");
    archive_write_buffer(repo->archive, repo->entry, entry, &repo->buf);

    archive_entry_clear(repo->entry);
    buffer_clear(&repo->buf);
    write_depends_file(pkg, &repo->buf);

    /* generate the 'depends' file */
    snprintf(entry, PATH_MAX, "%s-%s/%s", pkg->name, pkg->version, "depends");
    archive_write_buffer(repo->archive, repo->entry, entry, &repo->buf);
}

static void repo_write_close(repo_writer_t *repo)
{
    archive_write_close(repo->archive);

    buffer_free(&repo->buf);
    archive_entry_free(repo->entry);
    archive_write_free(repo->archive);
    free(repo);
}

/* TODO: compy as much data as possible from the existing repo */
static void repo_compile(repo_t *r, alpm_pkghash_t *cache)
{
    char repopath[PATH_MAX];
    char linkpath[PATH_MAX];

    snprintf(repopath, PATH_MAX, "%s/%s", r->root, r->file);
    snprintf(linkpath, PATH_MAX, "%s/%s", r->root, r->name);

    repo_writer_t *repo = repo_write_new(repopath, r->compression);
    alpm_list_t *pkg, *pkgs = cache->list;

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        // FIXME: really pass r? or pass r->root?
        repo_write_pkg(r, repo, metadata);
    }

    repo_write_close(repo);

    if (symlink(r->file, linkpath) < 0 && errno != EEXIST)
        err(EXIT_FAILURE, "symlink to %s failed", linkpath);
}
/* }}} */

static repo_t *find_repo(char *path)
{
    char dbpath[PATH_MAX];
    char sigpath[PATH_MAX];

    if (realpath(path, dbpath) == NULL && errno != ENOENT)
        err(EXIT_FAILURE, "failed to find repo");

    size_t len = strlen(dbpath);
    char *dot = memchr(dbpath, '.', len);
    char *div = memrchr(dbpath, '/', len);

    repo_t *r = calloc(1, sizeof(repo_t));

    /* FIXME: figure this out on compression */
    if (!dot) {
        errx(EXIT_FAILURE, "no file extension");
    } else if (strcmp(dot, ".db") == 0) {
        r->compression = COMPRESS_GZIP;
    } else if (strcmp(dot, ".db.tar") == 0) {
        r->compression = COMPRESS_NONE;
    } else if (strcmp(dot, ".db.tar.gz") == 0) {
        r->compression = COMPRESS_GZIP;
    } else if (strcmp(dot, ".db.tar.bz2") == 0) {
        r->compression = COMPRESS_BZIP2;
    } else if (strcmp(dot, ".db.tar.xz") == 0) {
        r->compression = COMPRESS_XZ;
    } else if (strcmp(dot, ".db.tar.Z") == 0) {
        r->compression = COMPRESS_COMPRESS;
    } else {
        errx(EXIT_FAILURE, "%s invalid repo type", dot);
    }

    /* skip '.db' */
    dot += 3;

    memcpy(r->root, dbpath, div - dbpath);
    memcpy(r->name, div + 1, dot - div - 1);

    if (*dot == '\0') {
        snprintf(r->file, PATH_MAX, "%s.tar.gz", r->name);
    } else {
        strcpy(r->file, div + 1);
    }

    /* check if the repo actually exists */
    if (access(dbpath, F_OK) < 0) {
        warn("repo %s doesn't exist", r->name);
        return r;
    }

    /* check for a signature */
    snprintf(sigpath, PATH_MAX, "%s.sig", dbpath);
    if (access(sigpath, F_OK) == 0) {
        if (gpgme_verify(dbpath, sigpath) < 0)
            errx(EXIT_FAILURE, "repo signature is invalid or corrupt!");

        r->db_signed = true;
    }

    /* load the database into memory */
    r->db = malloc(sizeof(alpm_db_meta_t));
    alpm_db_populate(dbpath, r->db);
    return r;
}

static inline alpm_list_t *load_pkg(alpm_list_t *list, repo_t *r, const char *filepath)
{
    alpm_pkg_meta_t *metadata;
    char *basename = strrchr(filepath, '/');
    char realpath[PATH_MAX];

    if (basename) {
        if (memcmp(filepath, r->root, basename - filepath) != 0) {
            warnx("%s is not in the same path as the database", filepath);
            return list;
        }
    } else {
        pkg_real_filename(r, filepath, realpath, NULL);
        filepath = realpath;
    }

    alpm_pkg_load_metadata(filepath, &metadata);
    if (metadata)
        list = alpm_list_add(list, metadata);
    return list;
}

static alpm_list_t *find_all_packages(repo_t *r)
{
    struct dirent *dp;
    DIR *dir = opendir(r->root);
    alpm_list_t *pkgs = NULL;

    if (dir == NULL)
        err(EXIT_FAILURE, "failed to open directory");

    while ((dp = readdir(dir))) {
        if (!(dp->d_type & DT_REG))
            continue;

        if (fnmatch("*.pkg.tar*", dp->d_name, FNM_CASEFOLD) != 0 ||
            fnmatch("*.sig",      dp->d_name, FNM_CASEFOLD) == 0)
            continue;

        /* printf("LOADING: %s\n", dp->d_name); */
        pkgs = load_pkg(pkgs, r, dp->d_name);
    }

    closedir(dir);
    return pkgs;
}

static alpm_list_t *find_packages(repo_t *r, char *pkg_list[], int count)
{
    int i;
    alpm_list_t *pkgs = NULL;

    for (i = 0; i < count; ++i)
        pkgs = load_pkg(pkgs, r, pkg_list[i]);

    return pkgs;
}

static int unlink_pkg_files(repo_t *r, const alpm_pkg_meta_t *metadata)
{
    char pkgpath[PATH_MAX];
    char sigpath[PATH_MAX];

    pkg_real_filename(r, metadata->filename, pkgpath, sigpath);
    printf("DELETING: %s-%s\n", metadata->name, metadata->version);

    unlink(pkgpath);
    unlink(sigpath);
    return 0;
}

static void repo_sign(repo_t *r)
{
    char link[PATH_MAX];
    char signature[PATH_MAX];

    /* XXX: check return type */
    gpgme_sign(r->root, r->file, cfg.key);

    snprintf(signature, PATH_MAX, "%s.sig", r->file);
    snprintf(link, PATH_MAX, "%s/%s.sig", r->root, r->name);
    if (symlink(signature, link) < 0 && errno != EEXIST)
        err(EXIT_FAILURE, "symlink to %s failed", link);
}

/* {{{ VERIFY */
static int verify_pkg(repo_t *r, const alpm_pkg_meta_t *pkg, bool deep)
{
    char pkgpath[PATH_MAX];
    char sigpath[PATH_MAX];

    pkg_real_filename(r, pkg->filename, pkgpath, deep ? sigpath : NULL);
    if (access(pkgpath, F_OK) < 0) {
        warn("couldn't find pkg %s at %s", pkg->name, pkgpath);
        return 1;
    }

    if (!deep)
        return 0;

    /* if we have a signature, verify it */
    if (access(sigpath, F_OK) == 0 && gpgme_verify(pkgpath, sigpath) < 0) {
        warnx("package %s, signature is invalid or corrupt!", pkg->name);
        return 1;
    }

    /* if we have a md5sum, verify it */
    if (pkg->md5sum) {
        char *md5sum = alpm_compute_md5sum(pkgpath);
        if (strcmp(pkg->md5sum, md5sum) != 0) {
            warnx("md5 sum for pkg %s is different", pkg->name);
            return 1;
        }
        free(md5sum);
    }

    /* if we have a sha256sum, verify it */
    if (pkg->sha256sum) {
        char *sha256sum = alpm_compute_sha256sum(pkgpath);
        if (strcmp(pkg->sha256sum, sha256sum) != 0) {
            warnx("sha256 sum for pkg %s is different", pkg->name);
            return 1;
        }
        free(sha256sum);
    }

    return 0;
}

static int verify_db(repo_t *r)
{
    alpm_list_t *pkg, *pkgs = r->db->pkgcache->list;
    int rc = 0;

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        rc |= verify_pkg(r, metadata, true);
    }

    if (rc == 0)
        printf("repo okay!\n");

    return rc;
}
/* }}} */

/* {{{ UPDATE */
/* read the existing repo or construct a new package cache */
static int update_db(repo_t *r, int argc, char *argv[], int clean)
{
    bool dirty = false;
    alpm_pkghash_t *cache = NULL;

    if (r->db == NULL) {
        warnx("repo doesn't exist, creating...");
        cache = _alpm_pkghash_create(23);
    } else {
        colon_printf("Reading existing database...\n");

        cache = r->db->pkgcache;
        alpm_list_t *pkg, *db_pkgs = cache->list;

        for (pkg = db_pkgs; pkg; pkg = pkg->next) {
            alpm_pkg_meta_t *metadata = pkg->data;

            /* find packages that have been removed from the cache */
            if (verify_pkg(r, metadata, false) == 1) {
                printf("REMOVING: %s-%s\n", metadata->name, metadata->version);
                cache = _alpm_pkghash_remove(cache, metadata, NULL);
                alpm_pkg_free_metadata(metadata);
                dirty = true;
                continue;
            }

            /* check if a signature was added */
            // FIXME: cleanup!
            char sigpath[PATH_MAX];
            pkg_real_filename(r, metadata->filename, NULL, sigpath);
            if (metadata->base64_sig == NULL && read_pkg_signature(sigpath, metadata) == 0) {
                printf("ADD SIG: %s-%s\n", metadata->name, metadata->version);
                dirty = true;
            }
        }
    }

    /* if some file paths were specified, find all packages */
    colon_printf("Scanning for new packages...\n");

    alpm_list_t *pkg, *pkgs;
    if (argc > 0) {
        pkgs = find_packages(r, argv, argc);
    } else {
        pkgs = find_all_packages(r);
    }

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        alpm_pkg_meta_t *old = _alpm_pkghash_find(cache, metadata->name);

        int vercmp = old == NULL ? 0 : alpm_pkg_vercmp(metadata->version, old->version);

        if (old == NULL || vercmp == 1) {
            if (old) {
                printf("UPDATING: %s-%s\n", metadata->name, metadata->version);
                if (clean >= 1)
                    unlink_pkg_files(r, old);
                cache = _alpm_pkghash_remove(cache, old, NULL);
                alpm_pkg_free_metadata(old);
            } else  {
                printf("ADDING: %s-%s\n", metadata->name, metadata->version);
            }
            cache = _alpm_pkghash_add(cache, metadata);
            dirty = true;
        }

        if (vercmp == -1 && clean >= 2)
            unlink_pkg_files(r, metadata);
    }

    if (dirty) {
        colon_printf("Writing database to disk...\n");
        repo_compile(r, cache);
        printf("repo %s updated successfully\n", r->name);
    } else {
        printf("repo %s does not need updating\n", r->name);
    }

    /* FIXME: repo.db.sig symlink needs to be validated seperately it
     * appears */
    if ((cfg.sign && dirty) || (cfg.sign && !r->db_signed))
        repo_sign(r);

    return 0;
}
/* }}} */

/* {{{ REMOVE */
/* read the existing repo or construct a new package cache */
static int remove_db(repo_t *r, int argc, char *argv[], int clean)
{
    bool dirty = false;

    if (r->db == NULL) {
        warnx("repo doesn't exist...");
        return 1;
    } else if (argc > 0) {
        colon_printf("Reading existing database...\n");

        int i;
        for (i = 0; i < argc; ++i) {
            alpm_pkg_meta_t *pkg = _alpm_pkghash_find(r->db->pkgcache, argv[i]);
            if (pkg != NULL) {
                r->db->pkgcache = _alpm_pkghash_remove(r->db->pkgcache, pkg, NULL);
                printf("REMOVING: %s\n", pkg->name);
                if (clean >= 1)
                    unlink_pkg_files(r, pkg);
                alpm_pkg_free_metadata(pkg);
                dirty = true;
                continue;
            }
            warnx("didn't find entry: %s", argv[0]);
        }
    }

    if (dirty) {
        colon_printf("Writing database to disk...\n");
        repo_compile(r, r->db->pkgcache);
        printf("repo %s updated successfully\n", r->name);
    } else {
        printf("repo %s does not need updating\n", r->name);
    }

    if ((cfg.sign && dirty) || (cfg.sign && !r->db_signed))
        repo_sign(r);

    return 0;
}

/* }}} */

/* {{{ QUERY */
static void print_pkg_metadata(const alpm_pkg_meta_t *pkg)
{
    if (cfg.info) {
        printf("Filename     : %s\n", pkg->filename);
        printf("Name         : %s\n", pkg->name);
        printf("Version      : %s\n", pkg->version);
        printf("Description  : %s\n", pkg->desc);
        printf("Architecture : %s\n", pkg->arch);
        printf("URL          : %s\n", pkg->url);
        printf("Packager     : %s\n\n", pkg->packager);
    } else {
        printf("%s %s\n", pkg->name, pkg->version);
    }
}

/* read the existing repo or construct a new package cache */
static int query_db(repo_t *r, int argc, char *argv[])
{
    if (r->db == NULL) {
        warnx("repo doesn't exist");
        return 1;
    }

    if (argc > 0) {
        int i;
        for (i = 0; i < argc; ++i) {
            const alpm_pkg_meta_t *pkg = _alpm_pkghash_find(r->db->pkgcache, argv[i]);
            if (pkg == NULL) {
                warnx("pkg not found");
                return 1;
            }
            print_pkg_metadata(pkg);
        }
    } else {
        alpm_list_t *pkg, *pkgs = r->db->pkgcache->list;
        for (pkg = pkgs; pkg; pkg = pkg->next)
            print_pkg_metadata(pkg->data);
    }

    return 0;
}
/* }}} */

/* {{{ COMPAT! */
#include "repo-compat.c"
/* }}} */

static void __attribute__((__noreturn__)) usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <path-to-db> [pkgs|deltas ...]\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help            display this help and exit\n"
        " -v, --version         display version\n"
        " -U, --update          update the database\n"
        " -R, --remove          remove an entry\n"
        " -Q, --query           query the database\n"
        " -V, --verify          verify the contents of the database\n"
        " -i, --info            show package info\n"
        " -c, --clean           remove stuff\n"
        " -s, --sign            sign database with GnuPG after update\n"
        " -k, --key=KEY         use the specified key to sign the database\n"
        "     --color=MODE      enable colour support\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void enable_colors(bool on)
{
    if (!on)
        return;

    cfg.colstr = (colstr_t){
        .colon   = BOLDBLUE "::" NOCOLOR BOLD " ",
        .warn    = BOLDYELLOW,
        .error   = BOLDRED,
        .nocolor = NOCOLOR
    };
}

void parse_repoman_args(int *argc, char **argv[])
{
    static const struct option opts[] = {
        { "help",    no_argument,       0, 'h' },
        { "version", no_argument,       0, 'v' },
        { "verify",  no_argument,       0, 'V' },
        { "update",  no_argument,       0, 'U' },
        { "remove",  no_argument,       0, 'R' },
        { "query",   no_argument,       0, 'Q' },
        { "info",    no_argument,       0, 'i' },
        { "clean",   no_argument,       0, 'c' },
        { "sign",    no_argument,       0, 's' },
        { "key",     required_argument, 0, 'k' },
        { "color",   required_argument, 0, 0x100 },
        { 0, 0, 0, 0 }
    };

    cfg.color = isatty(fileno(stdout)) ? true : false;

    while (true) {
        int opt = getopt_long(*argc, *argv, "hvURQVicsk:", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'v':
            printf("%s %s\n", program_invocation_short_name, "devel");
            exit(EXIT_SUCCESS);
        case 'V':
            cfg.action = ACTION_VERIFY;
            break;
        case 'U':
            cfg.action = ACTION_UPDATE;
            break;
        case 'R':
            cfg.action = ACTION_REMOVE;
            break;
        case 'Q':
            cfg.action = ACTION_QUERY;
            break;
        case 'i':
            cfg.info = true;
            break;
        case 'c':
            ++cfg.clean;
            break;
        case 's':
            cfg.sign = true;
            break;
        case 'k':
            cfg.key = optarg;
            break;
        case 0x100:
            if (strcmp("never", optarg) == 0)
                cfg.color = false;
            else if (strcmp("always", optarg) == 0)
                cfg.color = true;
            else if (strcmp("auto", optarg) != 0)
                errx(EXIT_FAILURE, "invalid argument '%s' for --color", optarg);
            break;
        default:
            usage(stderr);
        }
    }

    *argc -= optind;
    *argv += optind;
}

int main(int argc, char *argv[])
{
    repo_t *repo;
    int rc = 0;

    if (strcmp(program_invocation_short_name, "repo-add") == 0) {
        parse_repo_add_args(&argc, &argv);
    } else if (strcmp(program_invocation_short_name, "repo-remove") == 0) {
        parse_repo_remove_args(&argc, &argv);
    } else {
        parse_repoman_args(&argc, &argv);
    }

    if (argc == 0)
        errx(EXIT_FAILURE, "not enough arguments");

    /* enable colors if necessary */
    enable_colors(cfg.color);

    // FIXME: should be a function
    repo = find_repo(argv[0]);
    if (!repo)
        return 1;

    switch (cfg.action) {
    case ACTION_VERIFY:
        rc = verify_db(repo);
        break;
    case ACTION_UPDATE:
        rc = update_db(repo, argc - 1, argv + 1, cfg.clean);
        break;
    case ACTION_REMOVE:
        rc = remove_db(repo, argc - 1, argv + 1, cfg.clean);
        break;
    case ACTION_QUERY:
        rc = query_db(repo, argc - 1, argv + 1);
        break;
    default:
        break;
    };

    return rc;
}
