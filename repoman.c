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

struct repo {
    struct archive *archive;
    struct archive_entry *entry;
    struct buffer buf;
};

enum repoman_action {
    ACTION_VERIFY,
    ACTION_UPDATE,
    ACTION_QUERY,
    INVALID_ACTION
};

enum repoman_compression {
    COMPRESS_NONE,
    COMPRESS_GZ,
    COMPRESS_BZ2,
    COMPRESS_XZ,
    COMPRESS_Z
};

struct repo_name {
    char repopath[PATH_MAX];
    char linkpath[PATH_MAX];
    enum repoman_compression compression;
};

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
    char *md5sum = alpm_compute_md5sum(pkg->filename);
    char *sha256sum = alpm_compute_sha256sum(pkg->filename);

    write_string(buf, "FILENAME",  filename ? &filename[1] : pkg->filename);
    write_string(buf, "NAME",      pkg->name);
    write_string(buf, "VERSION",   pkg->version);
    write_string(buf, "DESC",      pkg->desc);
    write_long(buf,   "CSIZE",     (long)pkg->size);
    write_long(buf,   "ISIZE",     (long)pkg->isize);
    write_string(buf, "MD5SUM",    md5sum);
    write_string(buf, "SHA256SUM", sha256sum);

    if (pkg->base64_sig)
        write_string(buf, "PGPSIG", pkg->base64_sig);

    write_string(buf, "URL",       pkg->url);
    write_list(buf,   "LICENSE",   pkg->license);
    write_string(buf, "ARCH",      pkg->arch);
    write_long(buf,   "BUILDDATE", pkg->builddate);
    write_string(buf, "PACKAGER",  pkg->packager);

    free(md5sum);
    free(sha256sum);
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

static struct repo *repo_write_new(const char *filename, enum repoman_compression compression)
{
    struct repo *repo = malloc(sizeof(struct repo));
    repo->archive = archive_write_new();
    repo->entry = archive_entry_new();

    switch (compression) {
    case COMPRESS_NONE:
        archive_write_add_filter_none(repo->archive);
        break;
    case COMPRESS_GZ:
        archive_write_add_filter_gzip(repo->archive);
        break;
    case COMPRESS_BZ2:
        archive_write_add_filter_bzip2(repo->archive);
        break;
    case COMPRESS_XZ:
        archive_write_add_filter_xz(repo->archive);
        break;
    case COMPRESS_Z:
        archive_write_add_filter_compress(repo->archive);
        break;
    }
    archive_write_set_format_pax_restricted(repo->archive);
    archive_write_open_filename(repo->archive, filename);

    /* XXX: apparently isn't resizing correctly */
    buffer_init(&repo->buf, 1024);

    return repo;
}

static void repo_write_pkg(struct repo *repo, alpm_pkg_meta_t *pkg)
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

static void repo_write_close(struct repo *repo)
{
    archive_write_close(repo->archive);

    buffer_free(&repo->buf);
    archive_entry_free(repo->entry);
    archive_write_free(repo->archive);
    free(repo);
}

static alpm_list_t *find_packages(char **paths)
{
    FTS *ftsp;
    FTSENT *p, *chp;
    int fts_options = FTS_COMFOLLOW | FTS_LOGICAL | FTS_NOCHDIR;
    alpm_list_t *pkgs = NULL;

    ftsp = fts_open(paths, fts_options, NULL);
    if (ftsp == NULL)
        err(EXIT_FAILURE, "fts_open");

    chp = fts_children(ftsp, 0);
    if (chp == NULL)
        return 0;

    while ((p = fts_read(ftsp)) != NULL) {
        switch (p->fts_info) {
        case FTS_F:
            if (fnmatch("*.pkg.tar*", p->fts_path, FNM_CASEFOLD) == 0 &&
                fnmatch("*.sig",      p->fts_path, FNM_CASEFOLD) != 0)
                pkgs = alpm_list_add(pkgs, strdup(p->fts_path));
            break;
        default:
            break;
        }
    }

    fts_close(ftsp);
    return pkgs;
}

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

    return 0;
}

static int verify_db(const char *repopath)
{
    alpm_db_meta_t db;
    alpm_db_populate(repopath, &db);

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

static int update_db(struct repo_name *repopath, int argc, char *argv[], int clean)
{
    struct stat st;
    bool dirty = false;
    alpm_pkghash_t *cache = NULL;

    /* read the existing repo or construct a new package cache */
    if (stat(repopath->repopath, &st) < 0) {
        warnx("warning: repo doesn't exist, creating...");
        dirty = true;
        cache = _alpm_pkghash_create(23);
    } else {
        printf(":: Reading existing database...\n");

        alpm_db_meta_t db;
        alpm_db_populate(repopath->repopath, &db);
        cache = db.pkgcache;

        /* XXX: verify entries still exist here */
        alpm_list_t *pkg, *db_pkgs = cache->list;

        for (pkg = db_pkgs; pkg; pkg = pkg->next) {
            alpm_pkg_meta_t *metadata = pkg->data;
            if (verify_pkg(metadata, false) == 1) {
                printf("REMOVING: %s-%s\n", metadata->name, metadata->version);
                cache = _alpm_pkghash_remove(cache, metadata, NULL);
                alpm_pkg_free_metadata(metadata);
                dirty = true;
            }
        }
    }

    /* if some file paths were specified, find all packages */
    if (argc > 0) {
        printf(":: Scanning for new packages...\n");

        alpm_list_t *pkg, *pkgs = find_packages(argv);

        for (pkg = pkgs; pkg; pkg = pkg->next) {
            const char *path = pkg->data;
            alpm_pkg_meta_t *metadata;

            alpm_pkg_load_metadata(path, &metadata);
            alpm_pkg_meta_t *old = _alpm_pkghash_find(cache, metadata->name);

            int vercmp = old == NULL ? 0 : alpm_pkg_vercmp(metadata->version, old->version);

            if (old == NULL || vercmp == 1) {
                if (old) {
                    printf("UPDATING: %s-%s\n", metadata->name, metadata->version);
                    if (clean)
                        unlink(old->filename);
                    cache = _alpm_pkghash_remove(cache, metadata, NULL);
                    alpm_pkg_free_metadata(old);
                } else  {
                    printf("ADDING: %s-%s\n", metadata->name, metadata->version);
                }
                cache = _alpm_pkghash_add(cache, metadata);
                dirty = true;
            }

            if (vercmp == -1 && clean)
                unlink(metadata->filename);
        }
    }

    /* TEMPORARY: HACKY, write a new repo out */
    if (dirty)
    {
        printf(":: Writing database to disk...\n");
        struct repo *repo = repo_write_new(repopath->repopath, repopath->compression);
        alpm_list_t *pkg, *pkgs = cache->list;

        for (pkg = pkgs; pkg; pkg = pkg->next) {
            alpm_pkg_meta_t *metadata = pkg->data;
            repo_write_pkg(repo, metadata);
        }

        repo_write_close(repo);
        printf("repo %s updated successfully\n", repopath->repopath);
    } else {
        printf("repo %s does not need updating\n", repopath->repopath);
    }

    return 0;
}

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

static int query_db(const char *repopath, int argc, char *argv[])
{
    struct stat st;

    /* read the existing repo or construct a new package cache */
    if (stat(repopath, &st) < 0) {
        warnx("repo doesn't exist");
        return 1;
    }

    alpm_db_meta_t db;
    alpm_db_populate(repopath, &db);

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

static void find_repo(char *reponame, struct repo_name *r)
{
    const char *dot = strrchr(reponame, '.');
    int found_link = false;

    if (!dot)
        errx(EXIT_FAILURE, "%s invalid repo", reponame);

    /* If the reponame ends in .db, we've been passed the symlink. Follow
     * it. If the file doesn't actually exist yet, assume a .db.tar.gz
     * extension */
    char target[PATH_MAX];
    if (strcmp(dot, ".db") == 0) {
        found_link = true;

        if (readlink(reponame, target, PATH_MAX) < 0)
            snprintf(target, PATH_MAX, "%s.tar.gz", reponame);

        reponame = target;
    }

    /* otherwise, figure it out */
    char *var = strsep(&reponame, ".db.");
    if (reponame == NULL)
        errx(EXIT_FAILURE, "%s invalid repo", reponame);

    reponame += 3;
    if (strcmp(reponame, "tar") == 0) {
        r->compression = COMPRESS_NONE;
    } else if (strcmp(reponame, "tar.gz") == 0) {
        r->compression = COMPRESS_GZ;
    } else if (strcmp(reponame, "tar.bz2") == 0) {
        r->compression = COMPRESS_BZ2;
    } else if (strcmp(reponame, "tar.xz") == 0) {
        r->compression = COMPRESS_XZ;
    } else if (strcmp(reponame, "tar.Z") == 0) {
        r->compression = COMPRESS_Z;
    } else {
        errx(EXIT_FAILURE, "%s invalid repo", reponame);
    }

    snprintf(r->repopath, PATH_MAX, "%s.db.%s", var, reponame);
    snprintf(r->linkpath, PATH_MAX, "%s.db", var);
}

/* {{{ repo-add compat */
static void __attribute__((__noreturn__)) repo_add_usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <path-to-db> [pkgs|deltas ...]\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help            display this help and exit\n"
        " -d, --delta           generate and add deltas for package updates\n"
        " -f, --files           update database's file list\n"
        " --nocolor             turn off color in output\n"
        " -q, --quiet           minimize output\n"
        " -s, --sign            sign database with GnuPG after update\n"
        " -k, --key=KEY         use the specified key to sign the database\n"
        " -v, --verify          verify the contents of the database\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int repo_add(int argc, char *argv[])
{
    const char *key = NULL;
    bool sign = false;

    static const struct option opts[] = {
        { "help",    no_argument,       0, 'h' },
        { "delta",   no_argument,       0, 'd' },
        { "files",   no_argument,       0, 'f' },
        { "nocolor", no_argument,       0, 0x100 },
        { "quiet",   no_argument,       0, 'q' },
        { "sign",    no_argument,       0, 's' },
        { "key",     required_argument, 0, 'k' },
        { "verify",  no_argument,       0, 'v' },
        { 0, 0, 0, 0 }
    };

    while (true) {
        int opt = getopt_long(argc, argv, "hdfqsk:v", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            repo_add_usage(stdout);
            break;
        case 's':
            sign = true;
            break;
        case 'k':
            key = optarg;
            break;
        default:
            repo_add_usage(stderr);
        }
    }

    argc -= optind;
    argv += optind;

    if (argc == 0)
        errx(EXIT_FAILURE, "not enough arguments");

    struct repo_name reponame;

    find_repo(argv[0], &reponame);
    printf("REPOPATH: %s\n", reponame.repopath);
    printf("LINKPATH: %s\n", reponame.linkpath);

    int rc = update_db(&reponame, argc - 1, argv + 1, 0);
    if (rc != 0)
        return rc;
    if (sign)
        gpgme_sign(reponame.repopath, key);

    /* symlink repo.db -> repo.db.tar.gz */
    symlink(reponame.repopath, reponame.linkpath);
    return 0;
}
/* }}} */

static void __attribute__((__noreturn__)) usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <path-to-db> [pkgs|deltas ...]\n", program_invocation_short_name);
    fputs("Options:\n"
        " -h, --help            display this help and exit\n"
        " -v, --version         display version\n"
        " -U, --update          update the database\n"
        " -Q, --query           query the database\n"
        " -V, --verify          verify the contents of the database\n"
        " -c, --clean           remove stuff\n"
        " -s, --sign            sign database with GnuPG after update\n"
        " -k, --key=KEY         use the specified key to sign the database\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    const char *key = NULL;
    enum repoman_action action = INVALID_ACTION;
    int clean = 0;
    bool sign = false;

    static const struct option opts[] = {
        { "help",    no_argument,       0, 'h' },
        { "version", no_argument,       0, 'v' },
        { "verify",  no_argument,       0, 'V' },
        { "update",  no_argument,       0, 'U' },
        { "query",   no_argument,       0, 'Q' },
        { "sign",    no_argument,       0, 's' },
        { "key",     required_argument, 0, 'k' },
        { 0, 0, 0, 0 }
    };

    if (strcmp(program_invocation_short_name, "repo-add") == 0) {
        return repo_add(argc, argv);
    }

    while (true) {
        int opt = getopt_long(argc, argv, "hvUQVcsk:", opts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'v':
            printf("%s %s\n", program_invocation_short_name, "devel");
            return 0;
        case 'V':
            action = ACTION_VERIFY;
            break;
        case 'U':
            action = ACTION_UPDATE;
            break;
        case 'Q':
            action = ACTION_QUERY;
            break;
        case 'c':
            ++clean;
            break;
        case 's':
            sign = true;
            break;
        case 'k':
            key = optarg;
            break;
        default:
            usage(stderr);
        }
    }

    argc -= optind;
    argv += optind;

    if (argc == 0)
        errx(EXIT_FAILURE, "not enough arguments");

    struct repo_name reponame;

    find_repo(argv[0], &reponame);
    printf("REPOPATH: %s\n", reponame.repopath);
    printf("LINKPATH: %s\n", reponame.linkpath);

    int rc = 1;
    switch (action) {
    case ACTION_VERIFY:
        rc = verify_db(reponame.repopath);
        break;
    case ACTION_UPDATE:
        rc = update_db(&reponame, argc - 1, argv + 1, 0);
        if (rc != 0)
            return rc;
        if (sign)
            gpgme_sign(reponame.repopath, key);

        /* symlink repo.db -> repo.db.tar.gz */
        symlink(reponame.repopath, reponame.linkpath);
        break;
    case ACTION_QUERY:
        rc = query_db(reponame.repopath, argc - 1, argv + 1);
        break;
    default:
        break;
    };

    return rc;
}
