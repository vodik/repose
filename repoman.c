#include "repoman.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>
#include <fnmatch.h>

#include "alpm/signing.h"
#include "alpm/util.h"
#include "database.h"

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
    bool files;

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

static repo_t *repo_new(char *path)
{
    char *dot, *name, *dbpath = NULL;

    repo_t *repo = calloc(1, sizeof(repo_t));
    repo->dirty = false;

    dbpath = realpath(path, NULL);
    if (dbpath) {
        size_t len = strlen(dbpath);
        char *div = memrchr(dbpath, '/', len);

        dot = memchr(dbpath, '.', len);
        name = strndup(div + 1, dot - div - 1);
        repo->root = strndup(dbpath, div - dbpath);
    } else {
        if (errno != ENOENT)
            err(EXIT_FAILURE, "failed to find repo");

        dot = strchr(path, '.');
        name = strndup(path, dot - path);
        repo->root = get_current_dir_name();
    }

    /* FIXME: figure this out on compression */
    if (!dot) {
        errx(EXIT_FAILURE, "no file extension");
    } else if (strcmp(dot, ".db") == 0) {
        repo->compression = COMPRESS_GZIP;
    } else if (strcmp(dot, ".db.tar") == 0) {
        repo->compression = COMPRESS_NONE;
    } else if (strcmp(dot, ".db.tar.gz") == 0) {
        repo->compression = COMPRESS_GZIP;
    } else if (strcmp(dot, ".db.tar.bz2") == 0) {
        repo->compression = COMPRESS_BZIP2;
    } else if (strcmp(dot, ".db.tar.xz") == 0) {
        repo->compression = COMPRESS_XZ;
    } else if (strcmp(dot, ".db.tar.Z") == 0) {
        repo->compression = COMPRESS_COMPRESS;
    } else {
        errx(EXIT_FAILURE, "%s invalid repo type", dot);
    }

    /* open the directory so we can use openat later */
    repo->dirfd = open(repo->root, O_RDONLY);

    /* skip '.db' */
    dot += 3;
    if (*dot == '\0') {
        dot = ".tar.gz";
    }

    /* populate the package database paths */
    asprintf(&repo->db.file,      "%s.db%s",     name, dot);
    asprintf(&repo->db.sig,       "%s.db%s.sig", name, dot);
    asprintf(&repo->db.link_file, "%s.db",       name);
    asprintf(&repo->db.link_sig,  "%s.db.sig",   name);

    /* populate the files database paths */
    asprintf(&repo->files.file,      "%s.files%s",     name, dot);
    asprintf(&repo->files.sig,       "%s.files%s.sig", name, dot);
    asprintf(&repo->files.link_file, "%s.files",       name);
    asprintf(&repo->files.link_sig,  "%s.files.sig",   name);

    /* load the databases if possible */
    load_database(repo, &repo->db);
    load_database(repo, &repo->files);

    free(dbpath);
    free(name);
    return repo;
}

static void repo_write(repo_t *repo)
{
    colon_printf("Writing database to disk...\n");
    compile_database(repo, &repo->db, DB_DESC | DB_DEPENDS);
    if (cfg.sign) {
        sign_database(repo, &repo->db, cfg.key);
    }

    if (cfg.files) {
        colon_printf("Writing file database to disk...\n");
        compile_database(repo, &repo->files, DB_FILES);
        if (cfg.sign) {
            sign_database(repo, &repo->db, cfg.key);
        }
    }
}

static int unlink_package(repo_t *repo, const alpm_pkg_meta_t *pkg)
{
    printf("DELETING: %s-%s\n", pkg->name, pkg->version);

    unlinkat(repo->dirfd, pkg->filename, 0);
    unlinkat(repo->dirfd, pkg->signame, 0);
    return 0;
}

static alpm_pkg_meta_t *load_package(repo_t *repo, const char *filename)
{
    alpm_pkg_meta_t *pkg = NULL;
    int sigfd, pkgfd = openat(repo->dirfd, filename, O_RDONLY);
    if (pkgfd < 0) {
        err(EXIT_FAILURE, "failed to open %s", filename);
    }

    alpm_pkg_load_metadata(pkgfd, &pkg);
    pkg->filename = strdup(filename);
    asprintf(&pkg->signame, "%s.sig", filename);

    sigfd = openat(repo->dirfd, pkg->signame, O_RDONLY);
    if (sigfd < 0) {
        if (errno != ENOENT)
            err(EXIT_FAILURE, "failed to open %s", pkg->signame);
    } else {
        read_pkg_signature(sigfd, pkg);
    }

    return pkg;
}

static inline alpm_pkghash_t *filecache_add(alpm_pkghash_t *cache, repo_t *repo, const char *filepath)
{
    alpm_pkg_meta_t *pkg, *old;
    char *basename = strrchr(filepath, '/');

    if (basename) {
        if (memcmp(filepath, repo->root, basename - filepath) != 0) {
            warnx("%s is not in the same path as the database", filepath);
            return cache;
        }
    }

    pkg = load_package(repo, filepath);
    if (!pkg)
        return cache;

    old = _alpm_pkghash_find(cache, pkg->name);
    int vercmp = old == NULL ? 0 : alpm_pkg_vercmp(pkg->version, old->version);

    if (vercmp == 0 || vercmp == 1) {
        if (old) {
            cache = _alpm_pkghash_remove(cache, old, NULL);
            if (cfg.clean >= 2)
                unlink_package(repo, old);
            alpm_pkg_free_metadata(old);
        }
        return _alpm_pkghash_add(cache, pkg);
    } else {
        if (cfg.clean >= 2)
            unlink_package(repo, pkg);
        alpm_pkg_free_metadata(pkg);
        return cache;
    }
}

static alpm_pkghash_t *get_filecache(repo_t *repo, char *pkg_list[], int count)
{
    alpm_pkghash_t *cache = _alpm_pkghash_create(23);

    if (count == 0) {
        struct dirent *dp;
        DIR *dir = opendir(repo->root);
        if (dir == NULL)
            err(EXIT_FAILURE, "failed to open directory");

        while ((dp = readdir(dir))) {
            if (!(dp->d_type & DT_REG))
                continue;

            if (fnmatch("*.pkg.tar*", dp->d_name, FNM_CASEFOLD) != 0 ||
                fnmatch("*.sig",      dp->d_name, FNM_CASEFOLD) == 0)
                continue;

            cache = filecache_add(cache, repo, dp->d_name);
        }

        closedir(dir);
    } else {
        int i;

        for (i = 0; i < count; ++i)
            cache = filecache_add(cache, repo, pkg_list[i]);
    }
    return cache;
}

/* {{{ VERIFY */
static int verify_pkg_sig(repo_t *repo, const alpm_pkg_meta_t *pkg)
{
    int pkgfd, sigfd = openat(repo->dirfd, pkg->signame, O_RDONLY);
    if (sigfd < 0) {
        if (errno == ENOENT)
            return -1;
        err(EXIT_FAILURE, "failed to open %s", pkg->signame);
    }

    pkgfd = openat(repo->dirfd, pkg->filename, O_RDONLY);
    if (pkgfd < 0) {
        err(EXIT_FAILURE, "failed to open %s", pkg->filename);
    }

    int rc = gpgme_verify(pkgfd, sigfd);
    close(pkgfd);
    close(sigfd);
    return rc;
}

static int verify_pkg(repo_t *repo, const alpm_pkg_meta_t *pkg, bool deep)
{
    if (faccessat(repo->dirfd, pkg->filename, F_OK, 0) < 0) {
        warn("couldn't find pkg %s at %s", pkg->name, pkg->filename);
        return 1;
    }

    if (!deep)
        return 0;

    /* if we have a signature, verify it */
    if (verify_pkg_sig(repo, pkg) < 0) {
        warnx("package %s, signature is invalid or corrupt!", pkg->name);
        return 1;
    }

    /* if we have a md5sum, verify it */
    if (pkg->md5sum) {
        char *md5sum = _compute_md5sum(repo->dirfd, pkg->filename);
        if (strcmp(pkg->md5sum, md5sum) != 0) {
            warnx("md5 sum for pkg %s is different", pkg->name);
            return 1;
        }
        free(md5sum);
    }

    /* if we have a sha256sum, verify it */
    if (pkg->sha256sum) {
        char *sha256sum = _compute_sha256sum(repo->dirfd, pkg->filename);
        if (strcmp(pkg->sha256sum, sha256sum) != 0) {
            warnx("sha256 sum for pkg %s is different", pkg->name);
            return 1;
        }
        free(sha256sum);
    }

    return 0;
}

static int repo_database_verify(repo_t *repo)
{
    alpm_list_t *node;
    int rc = 0;

    if (!repo->pkgcache)
        return 0;

    for (node = repo->pkgcache->list; node; node = node->next) {
        alpm_pkg_meta_t *pkg = node->data;
        rc |= verify_pkg(repo, pkg, true);
    }

    if (rc == 0)
        printf("repo okay!\n");

    return rc;
}
/* }}} */

static void repo_database_reduce(repo_t *repo)
{
    if (repo->pkgcache) {
        colon_printf("Reading existing database...\n");

        alpm_pkghash_t *cache = repo->pkgcache;
        alpm_list_t *node, *pkgs = cache->list;

        for (node = pkgs; node; node = node->next) {
            alpm_pkg_meta_t *pkg = node->data;

            /* find packages that have been removed from the cache */
            if (verify_pkg(repo, pkg, false) == 1) {
                printf("REMOVING: %s-%s\n", pkg->name, pkg->version);
                cache = _alpm_pkghash_remove(cache, pkg, NULL);
                alpm_pkg_free_metadata(pkg);
                repo->dirty = true;
            }
        }

        repo->pkgcache = cache;
    }
}

/* {{{ UPDATE */
static inline alpm_pkghash_t *_alpm_pkghash_replace(alpm_pkghash_t *cache, alpm_pkg_meta_t *new,
                                                    alpm_pkg_meta_t *old)
{
    cache = _alpm_pkghash_remove(cache, old, NULL);
    return _alpm_pkghash_add(cache, new);
}

/* read the existing repo or construct a new package cache */
static int repo_database_update(repo_t *repo, int argc, char *argv[])
{
    alpm_pkghash_t *cache = NULL;

    /* if some file paths were specified, find all packages */
    colon_printf("Scanning for new packages...\n");

    if (!repo->pkgcache) {
        warnx("repo doesn't exist, creating...");
        cache = _alpm_pkghash_create(23);
    } else {
        repo_database_reduce(repo);
        cache = repo->pkgcache;
    }

    alpm_list_t *node, *pkgs;
    bool force = argc > 0 ? true : false;
    alpm_pkghash_t *filecache = get_filecache(repo, argv, argc);
    pkgs = filecache->list;

    for (node = pkgs; node; node = node->next) {
        alpm_pkg_meta_t *pkg = node->data;
        alpm_pkg_meta_t *old = _alpm_pkghash_find(cache, pkg->name);

        /* if the package isn't in the cache, add it */
        if (!old) {
            printf("ADDING: %s-%s\n", pkg->name, pkg->version);
            cache = _alpm_pkghash_add(cache, pkg);
            repo->dirty = true;
            continue;
        }

        /* if the package is in the cache, but we're doing a forced
         * update, replace it anyways */
        if (force) {
            printf("REPLACING: %s %s => %s\n", pkg->name, old->version, pkg->version);
            cache = _alpm_pkghash_replace(cache, pkg, old);
            if (cfg.clean >= 2)
                unlink_package(repo, old);
            alpm_pkg_free_metadata(old);
            repo->dirty = true;
            continue;
        }

        /* if the package is in the cache and we have a newer version,
         * replace it */
        switch(alpm_pkg_vercmp(pkg->version, old->version)) {
        case 1:
            printf("UPDATING: %s %s => %s\n", pkg->name, old->version, pkg->version);
            cache = _alpm_pkghash_replace(cache, pkg, old);
            if (cfg.clean >= 1)
                unlink_package(repo, old);
            alpm_pkg_free_metadata(old);
            repo->dirty = true;
            break;
        case 0:
            /* check to see if the package now has a signature */
            if (old->base64_sig == NULL && pkg->base64_sig) {
                printf("ADD SIG: %s-%s\n", pkg->name, pkg->version);
                old->base64_sig = strdup(pkg->base64_sig);
                repo->dirty = true;
            }
            break;
        case -1:
            if (cfg.clean >= 2)
                unlink_package(repo, pkg);
        }

    }

    repo->pkgcache = cache;
    return 0;
}
/* }}} */

/* {{{ REMOVE */
/* read the existing repo or construct a new package cache */
static int repo_database_remove(repo_t *repo, int argc, char *argv[])
{
    if (!repo->pkgcache) {
        warnx("repo doesn't exist...");
        return 1;
    } else {
        repo_database_reduce(repo);
    }

    int i;
    for (i = 0; i < argc; ++i) {
        alpm_pkg_meta_t *pkg = _alpm_pkghash_find(repo->pkgcache, argv[i]);
        if (pkg != NULL) {
            repo->pkgcache = _alpm_pkghash_remove(repo->pkgcache, pkg, NULL);
            printf("REMOVING: %s-%s\n", pkg->name, pkg->version);
            if (cfg.clean >= 1)
                unlink_package(repo, pkg);
            alpm_pkg_free_metadata(pkg);
            repo->dirty = true;
            continue;
        }
        warnx("didn't find entry: %s", argv[0]);
    }

    return 0;
}

/* }}} */

/* {{{ QUERY */
static void print_metadata(const alpm_pkg_meta_t *pkg)
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
static int repo_database_query(repo_t *repo, int argc, char *argv[])
{
    if (!repo->pkgcache) {
        warnx("repo doesn't exist");
        return 1;
    }

    if (argc > 0) {
        int i;
        for (i = 0; i < argc; ++i) {
            const alpm_pkg_meta_t *pkg = _alpm_pkghash_find(repo->pkgcache, argv[i]);
            if (pkg == NULL) {
                warnx("pkg not found");
                return 1;
            }
            print_metadata(pkg);
        }
    } else {
        alpm_list_t *node, *pkgs = repo->pkgcache->list;
        for (node = pkgs; node; node = node->next)
            print_metadata(node->data);
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
        { "files",   no_argument,       0, 'f' },
        { "sign",    no_argument,       0, 's' },
        { "key",     required_argument, 0, 'k' },
        { "color",   required_argument, 0, 0x100 },
        { 0, 0, 0, 0 }
    };

    cfg.color = isatty(fileno(stdout)) ? true : false;

    while (true) {
        int opt = getopt_long(*argc, *argv, "hvURQVicfsk:", opts, NULL);
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
        case 'f':
            cfg.files = true;
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

    repo = repo_new(argv[0]);
    if (!repo)
        return 1;

    switch (cfg.action) {
    case ACTION_VERIFY:
        rc = repo_database_verify(repo);
        break;
    case ACTION_UPDATE:
        rc = repo_database_update(repo, argc - 1, argv + 1);
        break;
    case ACTION_REMOVE:
        rc = repo_database_remove(repo, argc - 1, argv + 1);
        break;
    case ACTION_QUERY:
        rc = repo_database_query(repo, argc - 1, argv + 1);
        break;
    default:
        break;
    };

    /* if the database is dirty, rewrite it */
    if (repo->dirty) {
        repo_write(repo);
        printf("repo updated successfully\n");
    } else {
        printf("repo does not need updating\n");
    }

    return rc;
}
