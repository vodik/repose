#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <fts.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>
#include <alpm.h>
#include <alpm_list.h>

#include "alpm_metadata.h"
#include "buffer.h"
#include "pkghash.h"
#include "signing.h"

struct repo_writer {
    struct archive *archive;
    struct archive_entry *entry;
    struct buffer buf;
};

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

struct repo {
    char root[PATH_MAX];
    char db[PATH_MAX];
    char link[PATH_MAX];
    enum compress compression;
};

static struct {
    const char *key;
    enum action action;

    short clean;
    int color : 1;
    int sign : 1;
} cfg = { .action = INVALID_ACTION };

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

static void write_desc_file(const alpm_pkg_meta_t *pkg, struct buffer *buf)
{
    const char *filename = strrchr(pkg->filename, '/');

    write_string(buf, "FILENAME",  filename ? &filename[1] : pkg->filename);
    write_string(buf, "NAME",      pkg->name);
    write_string(buf, "VERSION",   pkg->version);
    write_string(buf, "DESC",      pkg->desc);
    write_long(buf,   "CSIZE",     (long)pkg->size);
    write_long(buf,   "ISIZE",     (long)pkg->isize);

    if (pkg->md5sum) {
        write_string(buf, "MD5SUM", pkg->md5sum);
    } else {
        char *md5sum = alpm_compute_md5sum(pkg->filename);
        write_string(buf, "MD5SUM", md5sum);
        free(md5sum);
    }

    if (pkg->sha256sum) {
        write_string(buf, "SHA256SUM", pkg->sha256sum);
    } else {
        char *sha256sum = alpm_compute_sha256sum(pkg->filename);
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

static struct repo_writer *repo_write_new(const char *filename, enum compress compression)
{
    struct repo_writer *repo = malloc(sizeof(struct repo_writer));
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

static void repo_write_pkg(struct repo_writer *repo, alpm_pkg_meta_t *pkg)
{
    char path[PATH_MAX];

    archive_entry_clear(repo->entry);
    buffer_clear(&repo->buf);
    write_desc_file(pkg, &repo->buf);

    /* generate the 'desc' file */
    snprintf(path, PATH_MAX, "%s-%s/%s", pkg->name, pkg->version, "desc");
    archive_write_buffer(repo->archive, repo->entry, path, &repo->buf);

    archive_entry_clear(repo->entry);
    buffer_clear(&repo->buf);
    write_depends_file(pkg, &repo->buf);

    /* generate the 'depends' file */
    snprintf(path, PATH_MAX, "%s-%s/%s", pkg->name, pkg->version, "depends");
    archive_write_buffer(repo->archive, repo->entry, path, &repo->buf);
}

static void repo_write_close(struct repo_writer *repo)
{
    archive_write_close(repo->archive);

    buffer_free(&repo->buf);
    archive_entry_free(repo->entry);
    archive_write_free(repo->archive);
    free(repo);
}

/* TODO: compy as much data as possible from the existing repo */
static void repo_compile(struct repo *r, alpm_pkghash_t *cache)
{
    struct repo_writer *repo = repo_write_new(r->db, r->compression);
    alpm_list_t *pkg, *pkgs = cache->list;

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        repo_write_pkg(repo, metadata);
    }

    repo_write_close(repo);
}
/* }}} */

static void find_repo(char *reponame, struct repo *r)
{
    int len = strlen(reponame);
    char *base, *dot = memchr(reponame, '.', len);

    if (!dot)
        errx(EXIT_FAILURE, "%s invalid repo", reponame);

    /* If the reponame ends in .db, we've been passed the symlink. Follow
     * it. If the file doesn't actually exist yet, assume a .db.tar.gz
     * extension */
    char target[PATH_MAX];
    if (strcmp(dot, ".db") == 0) {
        len = readlink(reponame, target, PATH_MAX);
        if (len < 0) {
            if (errno != ENOENT)
                err(EXIT_FAILURE, "failed to read %s link", reponame);
            snprintf(target, PATH_MAX, "%s.tar.gz", reponame);
        }

        /* recalculate positions */
        reponame = target;
        dot = memchr(reponame, '.', len);
        if (dot == NULL)
            errx(EXIT_FAILURE, "%s invalid repo", reponame);
    }

    *dot++ = '\0';
    base = memrchr(reponame, '/', len);

    if (strcmp(dot, "db.tar") == 0) {
        r->compression = COMPRESS_NONE;
    } else if (strcmp(dot, "db.tar.gz") == 0) {
        r->compression = COMPRESS_GZIP;
    } else if (strcmp(dot, "db.tar.bz2") == 0) {
        r->compression = COMPRESS_BZIP2;
    } else if (strcmp(dot, "db.tar.xz") == 0) {
        r->compression = COMPRESS_XZ;
    } else if (strcmp(dot, "db.tar.Z") == 0) {
        r->compression = COMPRESS_COMPRESS;
    } else {
        errx(EXIT_FAILURE, "%s invalid repo", dot);
    }

    snprintf(r->db,   PATH_MAX, "%s.%s", reponame, dot);
    snprintf(r->link, PATH_MAX, "%s.db", reponame);

    if (base) {
        *base = '\0';
        realpath(reponame, r->root);
    } else {
        realpath(".", r->root);
    }
}

static alpm_list_t *find_packages(struct repo *r, char **paths)
{
    FTS *tree;
    FTSENT *entry;
    int fts_options = FTS_COMFOLLOW | FTS_LOGICAL | FTS_NOCHDIR;
    alpm_list_t *pkgs = NULL;

    tree = fts_open(paths, fts_options, NULL);
    if (tree == NULL)
        err(EXIT_FAILURE, "fts_open");

    while ((entry = fts_read(tree)) != NULL) {
        char root[PATH_MAX], *pkgpath = entry->fts_path;

        /* don't search recursively */
        if (entry->fts_level > 1) {
            fts_set(tree, entry, FTS_SKIP);
            continue;
        }

        switch (entry->fts_info) {
        case FTS_F:
            if (fnmatch("*.pkg.tar*", pkgpath, FNM_CASEFOLD) != 0 ||
                fnmatch("*.sig",      pkgpath, FNM_CASEFOLD) == 0)
                continue;

            char *base = strrchr(pkgpath, '/');
            if (base) {
                *base = '\0';
                realpath(pkgpath, root);
                *base = '/';
            } else {
                realpath(".", root);
            }

            if (strcmp(root, r->root) != 0) {
                warnx("pkg and repo aren't in the same directory");
                continue;
            }

            alpm_pkg_meta_t *metadata;
            alpm_pkg_load_metadata(entry->fts_path, &metadata);
            if (metadata)
                pkgs = alpm_list_add(pkgs, metadata);
            break;
        default:
            break;
        }
    }

    fts_close(tree);
    return pkgs;
}

static int unlink_pkg_files(const char *pkgpath)
{
    char sigpath[PATH_MAX];
    snprintf(sigpath, PATH_MAX, "%s.sig", pkgpath);

    unlink(pkgpath);
    return unlink(sigpath);
}

/* {{{ VERIFY */
static int verify_pkg(const alpm_pkg_meta_t *pkg, bool deep)
{
    struct stat st;

    if (stat(pkg->filename, &st) < 0) {
        warn("couldn't find pkg %s", pkg->filename);
        return 1;
    }

    if (!deep)
        return 0;

    char *md5sum = alpm_compute_md5sum(pkg->filename);
    if (strcmp(pkg->md5sum, md5sum) != 0) {
        warnx("md5 sum for pkg %s is different", pkg->filename);
        return 1;
    }
    free(md5sum);

    char *sha256sum = alpm_compute_sha256sum(pkg->filename);
    if (strcmp(pkg->sha256sum, sha256sum) != 0) {
        warnx("sha256 sum for pkg %s is different", pkg->filename);
        return 1;
    }
    free(sha256sum);

    /* XXX: check the signature */

    return 0;
}

static int verify_db(const char *path)
{
    alpm_db_meta_t db;
    alpm_db_populate(path, &db);

    alpm_list_t *pkg, *pkgs = db.pkgcache->list;
    int rc = 0;

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        rc |= verify_pkg(metadata, true);
    }

    if (rc == 0)
        printf("repo okay!\n");

    return rc;
}
/* }}} */

/* {{{ UPDATE */
static int update_db(struct repo *r, int argc, char *argv[], int clean)
{
    struct stat st;
    bool dirty = false;
    alpm_pkghash_t *cache = NULL;

    /* read the existing repo or construct a new package cache */
    if (stat(r->db, &st) < 0) {
        warnx("warning: repo doesn't exist, creating...");
        dirty = true;
        cache = _alpm_pkghash_create(23);
    } else {
        printf(":: Reading existing database...\n");

        alpm_db_meta_t db;
        alpm_db_populate(r->db, &db);
        cache = db.pkgcache;

        /* XXX: verify entries still exist here */
        alpm_list_t *pkg, *db_pkgs = cache->list;

        for (pkg = db_pkgs; pkg; pkg = pkg->next) {
            alpm_pkg_meta_t *metadata = pkg->data;

            /* find packages that have been removed from the cache */
            if (verify_pkg(metadata, false) == 1) {
                printf("REMOVING: %s-%s\n", metadata->name, metadata->version);
                cache = _alpm_pkghash_remove(cache, metadata, NULL);
                alpm_pkg_free_metadata(metadata);
                dirty = true;
            }

            /* check if a signature was added */
            else if (metadata->base64_sig == NULL && read_pkg_signature(metadata) == 0) {
                printf("ADD SIG: %s-%s\n", metadata->name, metadata->version);
                dirty = true;
            }
        }
    }

    /* if some file paths were specified, find all packages */
    if (argc > 0) {
        printf(":: Scanning for new packages...\n");

        alpm_list_t *pkg, *pkgs = find_packages(r, argv);

        for (pkg = pkgs; pkg; pkg = pkg->next) {
            alpm_pkg_meta_t *metadata = pkg->data;
            alpm_pkg_meta_t *old = _alpm_pkghash_find(cache, metadata->name);

            int vercmp = old == NULL ? 0 : alpm_pkg_vercmp(metadata->version, old->version);

            if (old == NULL || vercmp == 1) {
                if (old) {
                    printf("UPDATING: %s-%s\n", metadata->name, metadata->version);
                    if (clean >= 1)
                        unlink_pkg_files(old->filename);
                    cache = _alpm_pkghash_remove(cache, old, NULL);
                    alpm_pkg_free_metadata(old);
                } else  {
                    printf("ADDING: %s-%s\n", metadata->name, metadata->version);
                }
                cache = _alpm_pkghash_add(cache, metadata);
                dirty = true;
            }

            if (vercmp == -1 && clean >= 2) {
                printf("REMOVING: %s-%s\n", metadata->name, metadata->version);
                unlink_pkg_files(metadata->filename);
            }
        }
    }

    if (dirty)
    {
        printf(":: Writing database to disk...\n");
        repo_compile(r, cache);
        printf("repo %s updated successfully\n", r->db);

        if (cfg.sign)
            gpgme_sign(r->db, cfg.key);
    } else {
        printf("repo %s does not need updating\n", r->db);
    }

    return 0;
}
/* }}} */

/* {{{ REMOVE */
static int remove_db(struct repo *r, int argc, char *argv[], int clean)
{
    alpm_db_meta_t db;
    bool dirty = false;
    struct stat st;

    /* read the existing repo or construct a new package cache */
    if (stat(r->db, &st) < 0) {
        warnx("warning: repo doesn't exist...");
        return 1;
    } else if (argc > 0) {
        printf(":: Reading existing database...\n");
        alpm_db_populate(r->db, &db);

        int i;
        for (i = 0; i < argc; ++i) {
            alpm_pkg_meta_t *pkg = _alpm_pkghash_find(db.pkgcache, argv[i]);
            if (pkg != NULL) {
                db.pkgcache = _alpm_pkghash_remove(db.pkgcache, pkg, NULL);
                printf("REMOVING: %s\n", pkg->name);
                if (clean >= 1)
                    unlink_pkg_files(pkg->filename);
                alpm_pkg_free_metadata(pkg);
                dirty = true;
                continue;
            }
            warnx("didn't find entry: %s\n", argv[0]);
        }
    }

    if (dirty)
    {
        printf(":: Writing database to disk...\n");
        repo_compile(r, db.pkgcache);
        printf("repo %s updated successfully\n", r->db);

        if (cfg.sign)
            gpgme_sign(r->db, cfg.key);
    } else {
        printf("repo %s does not need updating\n", r->db);
    }

    return 0;
}

/* }}} */

/* {{{ QUERY */
static void print_pkg_metadata(const alpm_pkg_meta_t *pkg)
{
    printf("Filename     : %s\n", pkg->filename);
    printf("Name         : %s\n", pkg->name);
    printf("Version      : %s\n", pkg->version);
    printf("Description  : %s\n", pkg->desc);
    printf("Architecture : %s\n", pkg->arch);
    printf("URL          : %s\n", pkg->url);
    printf("Packager     : %s\n\n", pkg->packager);
}

static int query_db(const char *path, int argc, char *argv[])
{
    struct stat st;

    /* read the existing repo or construct a new package cache */
    if (stat(path, &st) < 0) {
        warnx("repo doesn't exist");
        return 1;
    }

    alpm_db_meta_t db;
    alpm_db_populate(path, &db);

    if (argc > 0) {
        int i;
        for (i = 0; i < argc; ++i) {
            const alpm_pkg_meta_t *pkg = _alpm_pkghash_find(db.pkgcache, argv[i]);
            if (pkg == NULL) {
                warnx("pkg not found");
                return 1;
            }
            print_pkg_metadata(pkg);
        }
    } else {
        alpm_list_t *pkg, *pkgs = db.pkgcache->list;
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
        " -c, --clean           remove stuff\n"
        " -s, --sign            sign database with GnuPG after update\n"
        " -k, --key=KEY         use the specified key to sign the database\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
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
        { "sign",    no_argument,       0, 's' },
        { "key",     required_argument, 0, 'k' },
        { 0, 0, 0, 0 }
    };

    while (true) {
        int opt = getopt_long(*argc, *argv, "hvURQVcsk:", opts, NULL);
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
        case 'c':
            ++cfg.clean;
            break;
        case 's':
            cfg.sign = true;
            break;
        case 'k':
            cfg.key = optarg;
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

    struct repo repo;
    find_repo(argv[0], &repo);

    switch (cfg.action) {
    case ACTION_VERIFY:
        rc = verify_db(repo.db);
        break;
    case ACTION_UPDATE:
        rc = update_db(&repo, argc - 1, argv + 1, cfg.clean);
        break;
    case ACTION_REMOVE:
        rc = remove_db(&repo, argc - 1, argv + 1, cfg.clean);
        break;
    case ACTION_QUERY:
        rc = query_db(repo.db, argc - 1, argv + 1);
        break;
    default:
        break;
    };

    /* symlink repo.db -> repo.db.tar.gz */
    if (rc == 0)
        symlink(repo.db, repo.link);

    return rc;
}
