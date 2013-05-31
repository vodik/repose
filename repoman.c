#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
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
#include "alpm/util.h"
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

enum contents {
    DB_DESC    = 1,
    DB_DEPENDS = 1 << 2,
    DB_FILES   = 1 << 3
};

typedef struct file {
    char *file, *link_file;
    char *sig,  *link_sig;
} file_t;

typedef struct repo {
    alpm_pkghash_t *pkgcache;
    char *root;
    file_t db;
    file_t files;
    bool dirty;
    enum compress compression;
    int dirfd;
} repo_t;

typedef struct repo_writer {
    struct archive *archive;
    struct archive_entry *entry;
    struct buffer buf;
    int fd;
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

static void write_desc_file(repo_t *repo, const alpm_pkg_meta_t *pkg, struct buffer *buf)
{
    write_string(buf, "FILENAME",  pkg->filename);
    write_string(buf, "NAME",      pkg->name);
    write_string(buf, "VERSION",   pkg->version);
    write_string(buf, "DESC",      pkg->desc);
    write_long(buf,   "CSIZE",     (long)pkg->size);
    write_long(buf,   "ISIZE",     (long)pkg->isize);

    if (pkg->md5sum) {
        write_string(buf, "MD5SUM", pkg->md5sum);
    } else {
        char *md5sum = _compute_md5sum(repo->dirfd, pkg->filename);
        write_string(buf, "MD5SUM", md5sum);
        free(md5sum);
    }

    if (pkg->sha256sum) {
        write_string(buf, "SHA256SUM", pkg->sha256sum);
    } else {
        char *sha256sum = _compute_sha256sum(repo->dirfd, pkg->filename);
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

static void write_files_file(repo_t *repo, const alpm_pkg_meta_t *pkg, struct buffer *buf)
{
    /* TODO: try to preserve as much data as possible. this will always
     * be NULL at the moment */
    if (pkg->files) {
        write_list(buf, "FILES", pkg->files);
    } else {
        alpm_list_t *files = alpm_pkg_files(repo->dirfd, pkg->filename);
        write_list(buf, "FILES", files);
        alpm_list_free_inner(files, free);
        alpm_list_free(files);
    }
}

static void repo_write_entry(repo_writer_t *writer,
                                 const char *root, const char *entry)
{
    time_t now = time(NULL);
    char entry_path[PATH_MAX];

    snprintf(entry_path, PATH_MAX, "%s/%s", root, entry);
    archive_entry_set_pathname(writer->entry, entry_path);
    archive_entry_set_size(writer->entry, writer->buf.len);
    archive_entry_set_filetype(writer->entry, AE_IFREG);
    archive_entry_set_perm(writer->entry, 0644);
    archive_entry_set_ctime(writer->entry, now, 0);
    archive_entry_set_mtime(writer->entry, now, 0);
    archive_entry_set_atime(writer->entry, now, 0);

    archive_write_header(writer->archive, writer->entry);
    archive_write_data(writer->archive, writer->buf.data, writer->buf.len);

    archive_entry_clear(writer->entry);
    buffer_clear(&writer->buf);
}

static repo_writer_t *repo_write_new(repo_t *repo, file_t *db)
{
    repo_writer_t *writer = malloc(sizeof(repo_writer_t));
    writer->archive = archive_write_new();
    writer->entry = archive_entry_new();

    switch (repo->compression) {
    case COMPRESS_NONE:
        archive_write_add_filter_none(writer->archive);
        break;
    case COMPRESS_GZIP:
        archive_write_add_filter_gzip(writer->archive);
        break;
    case COMPRESS_BZIP2:
        archive_write_add_filter_bzip2(writer->archive);
        break;
    case COMPRESS_XZ:
        archive_write_add_filter_xz(writer->archive);
        break;
    case COMPRESS_COMPRESS:
        archive_write_add_filter_compress(writer->archive);
        break;
    }

    archive_write_set_format_pax_restricted(writer->archive);

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    writer->fd = openat(repo->dirfd, db->file, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (writer->fd < 0)
        err(EXIT_FAILURE, "failed to open %s for writing", db->file);
    archive_write_open_fd(writer->archive, writer->fd);

    buffer_init(&writer->buf, 1024);

    return writer;
}

static void repo_write_pkg(repo_t *repo, repo_writer_t *writer, alpm_pkg_meta_t *pkg, int contents)
{
    char entry[PATH_MAX];
    snprintf(entry, PATH_MAX, "%s-%s", pkg->name, pkg->version);

    /* generate the 'desc' file */
    if (contents & DB_DESC) {
        write_desc_file(repo, pkg, &writer->buf);
        repo_write_entry(writer, entry, "desc");
    }

    /* generate the 'depends' file */
    if (contents & DB_DEPENDS) {
        write_depends_file(pkg, &writer->buf);
        repo_write_entry(writer, entry, "depends");
    }

    /* generate the 'files' file */
    if (contents & DB_FILES) {
        write_files_file(repo, pkg, &writer->buf);
        repo_write_entry(writer, entry, "files");
    }
}

static void repo_write_close(repo_writer_t *writer)
{
    archive_write_close(writer->archive);

    buffer_free(&writer->buf);
    archive_entry_free(writer->entry);
    archive_write_free(writer->archive);
    close(writer->fd);
    free(writer);
}
/* }}} */

static void sign_database(repo_t *repo, file_t *db)
{
    /* XXX: check return type */
    gpgme_sign(repo->dirfd, db->file, db->sig, cfg.key);

    if (symlinkat(db->sig, repo->dirfd, db->link_sig) < 0 && errno != EEXIST)
        err(EXIT_FAILURE, "symlink for %s failed", db->link_sig);
}

/* TODO: compy as much data as possible from the existing repo */
static void compile_database(repo_t *repo, file_t *db, alpm_pkghash_t *cache, int contents)
{
    repo_writer_t *writer = repo_write_new(repo, db);
    alpm_list_t *pkg, *pkgs = cache->list;

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        repo_write_pkg(repo, writer, metadata, contents);
    }

    repo_write_close(writer);

    /* make the appropriate symlink for the database */
    if (symlinkat(db->file, repo->dirfd, db->link_file) < 0 && errno != EEXIST)
        err(EXIT_FAILURE, "symlink for %s failed", db->link_file);

    sign_database(repo, db);
}

static void load_database(repo_t *repo, file_t *db, alpm_pkghash_t **cache)
{
    /* FIXME: error reporting should be here, not inside this funciton */
    if (alpm_db_populate(repo->dirfd, db->file, cache) < 0)
        return;

    if (faccessat(repo->dirfd, db->sig, F_OK, 0) == 0) {
        /* check for a signature */
        if (gpgme_verify(repo->dirfd, db->file, db->sig) < 0)
            errx(EXIT_FAILURE, "database signature is invalid or corrupt!");
    }
}

static repo_t *find_repo(char *path)
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
    load_database(repo, &repo->db, &repo->pkgcache);
    load_database(repo, &repo->files, &repo->pkgcache);

    free(dbpath);
    free(name);
    return repo;
}

static int unlink_pkg_files(repo_t *repo, const alpm_pkg_meta_t *metadata)
{
    printf("DELETING: %s-%s\n", metadata->name, metadata->version);

    /* TODO: store pkg signature filepath somewhere */
    unlinkat(repo->dirfd, metadata->filename, 0);
    unlinkat(repo->dirfd, metadata->signame, 0);
    return 0;
}

static inline alpm_pkghash_t *load_pkg(alpm_pkghash_t *cache, repo_t *repo, const char *filepath)
{
    alpm_pkg_meta_t *metadata, *old;
    char *basename = strrchr(filepath, '/');

    if (basename) {
        if (memcmp(filepath, repo->root, basename - filepath) != 0) {
            warnx("%s is not in the same path as the database", filepath);
            return cache;
        }
    }

    alpm_pkg_load_metadata(repo->dirfd, filepath, &metadata);
    if (!metadata)
        return cache;

    old = _alpm_pkghash_find(cache, metadata->name);
    int vercmp = old == NULL ? 0 : alpm_pkg_vercmp(metadata->version, old->version);

    if (vercmp == 0 || vercmp == 1) {
        if (old) {
            cache = _alpm_pkghash_remove(cache, old, NULL);
            if (cfg.clean >= 2)
                unlink_pkg_files(repo, old);
            alpm_pkg_free_metadata(old);
        }
        return _alpm_pkghash_add(cache, metadata);
    } else {
        if (cfg.clean >= 2)
            unlink_pkg_files(repo, metadata);
        alpm_pkg_free_metadata(metadata);
        return cache;
    }
}

static alpm_pkghash_t *find_all_packages(repo_t *repo)
{
    struct dirent *dp;
    DIR *dir = opendir(repo->root);
    alpm_pkghash_t *cache = _alpm_pkghash_create(23);

    if (dir == NULL)
        err(EXIT_FAILURE, "failed to open directory");

    while ((dp = readdir(dir))) {
        if (!(dp->d_type & DT_REG))
            continue;

        if (fnmatch("*.pkg.tar*", dp->d_name, FNM_CASEFOLD) != 0 ||
            fnmatch("*.sig",      dp->d_name, FNM_CASEFOLD) == 0)
            continue;

        cache = load_pkg(cache, repo, dp->d_name);
    }

    closedir(dir);
    return cache;
}

static alpm_pkghash_t *find_packages(repo_t *repo, char *pkg_list[], int count)
{
    int i;
    alpm_pkghash_t *cache = _alpm_pkghash_create(23);

    for (i = 0; i < count; ++i)
        cache = load_pkg(cache, repo, pkg_list[i]);

    return cache;
}

/* {{{ VERIFY */
static int verify_pkg(repo_t *repo, const alpm_pkg_meta_t *pkg, bool deep)
{
    if (faccessat(repo->dirfd, pkg->filename, F_OK, 0) < 0) {
        warn("couldn't find pkg %s at %s", pkg->name, pkg->filename);
        return 1;
    }

    if (!deep)
        return 0;

    /* if we have a signature, verify it */
    if (faccessat(repo->dirfd, pkg->signame, F_OK, 0) == 0 &&
        gpgme_verify(repo->dirfd, pkg->filename, pkg->signame) < 0) {
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

static int verify_db(repo_t *repo)
{
    alpm_list_t *pkg;
    int rc = 0;

    if (!repo->pkgcache)
        return 0;

    for (pkg = repo->pkgcache->list; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        rc |= verify_pkg(repo, metadata, true);
    }

    if (rc == 0)
        printf("repo okay!\n");

    return rc;
}
/* }}} */

static void reduce_db(repo_t *repo)
{
    if (repo->pkgcache) {
        colon_printf("Reading existing database...\n");

        alpm_pkghash_t *cache = repo->pkgcache;
        alpm_list_t *pkg, *pkgs = cache->list;

        for (pkg = pkgs; pkg; pkg = pkg->next) {
            alpm_pkg_meta_t *metadata = pkg->data;

            /* find packages that have been removed from the cache */
            if (verify_pkg(repo, metadata, false) == 1) {
                printf("REMOVING: %s-%s\n", metadata->name, metadata->version);
                cache = _alpm_pkghash_remove(cache, metadata, NULL);
                alpm_pkg_free_metadata(metadata);
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
static int update_db(repo_t *repo, int argc, char *argv[])
{
    alpm_pkghash_t *cache = NULL;

    /* if some file paths were specified, find all packages */
    colon_printf("Scanning for new packages...\n");

    if (!repo->pkgcache) {
        warnx("repo doesn't exist, creating...");
        cache = _alpm_pkghash_create(23);
    } else {
        reduce_db(repo);
        cache = repo->pkgcache;
    }

    alpm_list_t *pkg, *pkgs;
    bool force = false;
    if (argc > 0) {
        alpm_pkghash_t *filecache = find_packages(repo, argv, argc);
        pkgs = filecache->list;
        force = true;
    } else {
        alpm_pkghash_t *filecache = find_all_packages(repo);
        pkgs = filecache->list;
    }

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        alpm_pkg_meta_t *old = _alpm_pkghash_find(cache, metadata->name);

        /* if the package isn't in the cache, add it */
        if (!old) {
            printf("ADDING: %s-%s\n", metadata->name, metadata->version);
            cache = _alpm_pkghash_add(cache, metadata);
            repo->dirty = true;
            continue;
        }

        /* if the package is in the cache, but we're doing a forced
         * update, replace it anyways */
        if (force) {
            printf("REPLACING: %s %s => %s\n", metadata->name, old->version, metadata->version);
            cache = _alpm_pkghash_replace(cache, metadata, old);
            if (cfg.clean >= 2)
                unlink_pkg_files(repo, old);
            alpm_pkg_free_metadata(old);
            repo->dirty = true;
            continue;
        }

        /* if the package is in the cache and we have a newer version,
         * replace it */
        int vercmp = old == NULL ? 0 : alpm_pkg_vercmp(metadata->version, old->version);

        switch (vercmp) {
        case 1:
            printf("UPDATING: %s %s => %s\n", metadata->name, old->version, metadata->version);
            cache = _alpm_pkghash_replace(cache, metadata, old);
            if (cfg.clean >= 1)
                unlink_pkg_files(repo, old);
            alpm_pkg_free_metadata(old);
            repo->dirty = true;
            break;
        case 0:
            /* check to see if the package now has a signature */
            if (old->base64_sig == NULL && metadata->base64_sig) {
                printf("ADD SIG: %s-%s\n", metadata->name, metadata->version);
                old->base64_sig = strdup(metadata->base64_sig);
                repo->dirty = true;
            }
            break;
        case -1:
            if (cfg.clean >= 2)
                unlink_pkg_files(repo, metadata);
        }

    }

    repo->pkgcache = cache;
    return 0;
}
/* }}} */

/* {{{ REMOVE */
/* read the existing repo or construct a new package cache */
static int remove_db(repo_t *repo, int argc, char *argv[])
{
    if (!repo->pkgcache) {
        warnx("repo doesn't exist...");
        return 1;
    } else {
        reduce_db(repo);
    }

    int i;
    for (i = 0; i < argc; ++i) {
        alpm_pkg_meta_t *pkg = _alpm_pkghash_find(repo->pkgcache, argv[i]);
        if (pkg != NULL) {
            repo->pkgcache = _alpm_pkghash_remove(repo->pkgcache, pkg, NULL);
            printf("REMOVING: %s-%s\n", pkg->name, pkg->version);
            if (cfg.clean >= 1)
                unlink_pkg_files(repo, pkg);
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
static int query_db(repo_t *repo, int argc, char *argv[])
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
            print_pkg_metadata(pkg);
        }
    } else {
        alpm_list_t *pkg, *pkgs = repo->pkgcache->list;
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

    repo = find_repo(argv[0]);
    if (!repo)
        return 1;

    switch (cfg.action) {
    case ACTION_VERIFY:
        rc = verify_db(repo);
        break;
    case ACTION_UPDATE:
        rc = update_db(repo, argc - 1, argv + 1);
        break;
    case ACTION_REMOVE:
        rc = remove_db(repo, argc - 1, argv + 1);
        break;
    case ACTION_QUERY:
        rc = query_db(repo, argc - 1, argv + 1);
        break;
    default:
        break;
    };

    /* if the database is dirty, rewrite it */
    if (repo->dirty) {
        colon_printf("Writing database to disk...\n");
        compile_database(repo, &repo->db, repo->pkgcache, DB_DESC | DB_DEPENDS);
        if (cfg.files) {
            colon_printf("Writing file database to disk...\n");
            compile_database(repo, &repo->files, repo->pkgcache, DB_FILES);
        }
        printf("repo updated successfully\n");
    } else {
        printf("repo does not need updating\n");
    }

    return rc;
}
