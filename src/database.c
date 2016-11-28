#include "database.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <archive.h>
#include <archive_entry.h>
#include <alpm_list.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <time.h>
#include <sys/stat.h>
#include <openssl/sha.h>

#include "repose.h"
#include "package.h"
#include "pkgcache.h"
#include "util.h"
#include "desc.h"
#include "buffer.h"
#include "signing.h"

struct db {
    int fd;
    struct archive *archive;
    time_t mtime;
    struct pkg *likely_pkg;
};

/* The name, version and type fields all share the same memory */
struct dbentry {
    char *name;
    const char *type;
    const char *version;
};

static char *sha256_fd(int fd)
{
    SHA256_CTX ctx;
    unsigned char output[32];

    SHA256_Init(&ctx);
    for (;;) {
        char buf[BUFSIZ];
        ssize_t nbytes_r = read(fd, buf, sizeof(buf));
        check_posix(nbytes_r, "failed to read file");
        if (nbytes_r == 0)
            break;
        SHA256_Update(&ctx, buf, nbytes_r);
    }
    SHA256_Final(output, &ctx);

    return hex_representation(output, sizeof(output));
}

static char *sha256_file(int dirfd, const char *filename)
{
    _cleanup_close_ int fd = openat(dirfd, filename, O_RDONLY);
    check_posix(fd, "failed to open %s for sha256 checksum", filename);
    return sha256_fd(fd);
}

static int open_database(struct db *db, int fd)
{
    struct stat st;
    check_posix(fstat(fd, &st), "failed to stat database");

    *db = (struct db){
        .archive = archive_read_new(),
        .fd = fd,
        .mtime = st.st_mtime
    };

    archive_read_support_filter_all(db->archive);
    archive_read_support_format_all(db->archive);

    if (archive_read_open_fd(db->archive, fd, 8192) != ARCHIVE_OK) {
        archive_read_close(db->archive);
        archive_read_free(db->archive);
    }

    return 0;
}

static int parse_database_pathname(const char *entryname, struct dbentry *entry)
{
    entry->name = strdup(entryname);
    const char *slash = strchrnul(entry->name, '/'), *dash = slash;

    dash = memrchr(entry->name, '-', dash - entry->name);
    dash = memrchr(entry->name, '-', dash - entry->name);

    if (*dash != '-')
        return -EINVAL;

    entry->name[dash - entry->name] = entry->name[slash - entry->name] = '\0';
    entry->type = slash ? slash + 1 : NULL;
    entry->version = &entry->name[dash - entry->name + 1];

    return 0;
}

static inline void dbentry_free(struct dbentry *dbentry)
{
    free(dbentry->name);
}

static struct pkg *get_package(struct db *db, struct dbentry *dbentry,
                               alpm_pkgcache_t **pkgcache, bool allocate)
{
    struct pkg *pkg;

    if (db->likely_pkg) {
        unsigned long pkgname_hash = _alpm_hash_sdbm(dbentry->name);
        if (pkgname_hash == db->likely_pkg->name_hash && streq(db->likely_pkg->name, dbentry->name))
            return db->likely_pkg;
    }

    pkg = pkgcache_find(*pkgcache, dbentry->name);
    if (allocate && !pkg) {
        pkg = malloc(sizeof(struct pkg));
        if (!pkg)
            return NULL;

        *pkg = (struct pkg){
            .name = strdup(dbentry->name),
            .name_hash = _alpm_hash_sdbm(dbentry->name),
            .version = strdup(dbentry->version),
            .mtime = db->mtime
        };

        *pkgcache = pkgcache_add_sorted(*pkgcache, pkg);
    }

    if (pkg)
        db->likely_pkg = pkg;

    return pkg;
}

static bool is_database_metadata(const char *entry_name)
{
    static const char *metadata_names[] = {
        "desc",
        "depends",
        "files",
        NULL
    };

    for (const char **n = metadata_names; *n; ++n) {
        if (streq(entry_name, *n))
            return true;
    }

    return false;
}

static int parse_database_entry(struct db *db, struct archive_entry *entry,
                                alpm_pkgcache_t **pkgcache)
{
    const char *pathname = archive_entry_pathname(entry);
    struct dbentry dbentry;
    int ret = 0;

    if (parse_database_pathname(pathname, &dbentry) < 0) {
        ret = -1;
        goto cleanup;
    }

    if (dbentry.type && is_database_metadata(dbentry.type)) {
        bool allocate = !streq(dbentry.type, "files");

        struct pkg *pkg = get_package(db, &dbentry, pkgcache, allocate);
        if (allocate && !pkg) {
            ret = -1;
            goto cleanup;
        }

        if (pkg) {
            read_desc(db->archive, pkg);
        }
    }

cleanup:
    dbentry_free(&dbentry);
    return ret;
}

int load_database(int fd, alpm_pkgcache_t **pkgcache)
{
    struct db db;
    struct archive_entry *entry;

    if (open_database(&db, fd) < 0)
        return -1;

    while (archive_read_next_header(db.archive, &entry) == ARCHIVE_OK) {
        const mode_t mode = archive_entry_mode(entry);

        if (S_ISREG(mode) && parse_database_entry(&db, entry, pkgcache) < 0) {
            archive_read_close(db.archive);
            archive_read_free(db.archive);
            return -1;
        }
    }

    return 0;
}

static void write_list(struct buffer *buf, const char *header, const alpm_list_t *lst)
{
    if (lst == NULL)
        return;

    buffer_printf(buf, "%%%s%%\n", header);
    for (; lst; lst = lst->next)
        buffer_printf(buf, "%s\n", (const char *)lst->data);
    buffer_putc(buf, '\n');
}

static void write_string(struct buffer *buf, const char *header, const char *str)
{
    if (str == NULL)
        return;

    buffer_printf(buf, "%%%s%%\n%s\n\n", header, str);
}

static void write_size(struct buffer *buf, const char *header, size_t val)
{
    buffer_printf(buf, "%%%s%%\n%zd\n\n", header, val);
}

static void write_time(struct buffer *buf, const char *header, time_t val)
{
    buffer_printf(buf, "%%%s%%\n%ld\n\n", header, val);
}

#define write_entry(buf, header, val) _Generic((val), \
    alpm_list_t *: write_list, \
    char *: write_string, \
    size_t: write_size, \
    time_t: write_time)(buf, header, val)

static void compile_depends_entry(struct pkg *pkg, struct buffer *buf)
{
    write_entry(buf, "DEPENDS",      pkg->depends);
    write_entry(buf, "CONFLICTS",    pkg->conflicts);
    write_entry(buf, "PROVIDES",     pkg->provides);
    write_entry(buf, "OPTDEPENDS",   pkg->optdepends);
    write_entry(buf, "MAKEDEPENDS",  pkg->makedepends);
    write_entry(buf, "CHECKDEPENDS", pkg->checkdepends);
}

static void compile_desc_entry(struct pkg *pkg, struct buffer *buf, int poolfd)
{
    write_entry(buf, "FILENAME",  pkg->filename);
    write_entry(buf, "NAME",      pkg->name);
    write_entry(buf, "BASE",      pkg->base);
    write_entry(buf, "VERSION",   pkg->version);
    write_entry(buf, "DESC",      pkg->desc);
    write_entry(buf, "GROUPS",    pkg->groups);
    write_entry(buf, "CSIZE",     pkg->size);
    write_entry(buf, "ISIZE",     pkg->isize);

    if (pkg->base64sig) {
        write_entry(buf, "PGPSIG", pkg->base64sig);
    } else {
        if (!pkg->sha256sum)
            pkg->sha256sum = sha256_file(poolfd, pkg->filename);
        write_entry(buf, "SHA256SUM", pkg->sha256sum);
    }

    write_entry(buf, "URL",       pkg->url);
    write_entry(buf, "LICENSE",   pkg->licenses);
    write_entry(buf, "ARCH",      pkg->arch);
    write_entry(buf, "BUILDDATE", pkg->builddate);
    write_entry(buf, "PACKAGER",  pkg->packager);
    write_entry(buf, "REPLACES",  pkg->replaces);
}

static void compile_files_entry(struct pkg *pkg, struct buffer *buf, int poolfd)
{
    if (!pkg->files) {
        _cleanup_close_ int pkgfd = openat(poolfd, pkg->filename, O_RDONLY);
        if (pkgfd < 0 && errno != ENOENT)
            err(EXIT_FAILURE, "failed to open %s", pkg->filename);

        load_package_files(pkg, pkgfd);
    }

    write_entry(buf, "FILES", pkg->files);
}

static void archive_entry_populate(struct archive_entry *e, unsigned int type,
                                   const char *path, mode_t mode)
{
    time_t now = time(NULL);

    archive_entry_set_pathname(e, path);
    archive_entry_set_filetype(e, type);
    archive_entry_set_perm(e, mode);
    archive_entry_set_uname(e, "repose");
    archive_entry_set_gname(e, "repose");
    archive_entry_set_ctime(e, now, 0);
    archive_entry_set_mtime(e, now, 0);
    archive_entry_set_atime(e, now, 0);
}

static void record_entry(struct archive *archive, struct archive_entry *e,
                         const char *root, const char *entry, struct buffer *buf)
{
    _cleanup_free_ char *entrypath = joinstring(root, "/", entry, NULL);

    archive_entry_populate(e, AE_IFREG, entrypath, 0644);
    archive_entry_set_size(e, buf->len);
    archive_write_header(archive, e);
    archive_write_data(archive, buf->data, buf->len);
    archive_entry_clear(e);
    buffer_clear(buf);
}

static void compile_database_entry(struct archive *archive, struct archive_entry *e, struct pkg *pkg,
                                   int contents, struct buffer *buf, int poolfd)
{
    _cleanup_free_ char *entrypath = joinstring(pkg->name, "-", pkg->version, NULL);

    archive_entry_populate(e, AE_IFDIR, entrypath, 0755);
    archive_write_header(archive, e);
    archive_entry_clear(e);

    if (contents & DB_DESC) {
        compile_desc_entry(pkg, buf, poolfd);
        record_entry(archive, e, entrypath, "desc", buf);
    }
    if (contents & DB_DEPENDS) {
        compile_depends_entry(pkg, buf);
        record_entry(archive, e, entrypath, "depends", buf);
    }
    if (contents & DB_FILES) {
        compile_files_entry(pkg, buf, poolfd);
        record_entry(archive, e, entrypath, "files", buf);
    }
}

static int compile_database(struct repo *repo, const char *repo_name,
                            enum contents what)
{
    _cleanup_close_ int dbfd = openat(repo->rootfd, repo_name,
                                      O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (dbfd < 0)
        return -1;

    int ret = 0;
    struct archive *archive = archive_write_new();
    struct archive_entry *entry = archive_entry_new();

    archive_write_add_filter(archive, config.compression);
    archive_write_set_format_pax_restricted(archive);

    if (archive_write_open_fd(archive, dbfd) < 0) {
        ret = -1;
        goto cleanup;
    }

    archive_entry_populate(entry, AE_IFDIR, "", 0755);
    archive_write_header(archive, entry);
    archive_entry_clear(entry);

    struct buffer buf = {0};

    /* The files database can get very, very large. Lets preallocate
     * a 2MiB buffer so we have plenty of room and avoid lots of
     * rallocations. */
    buffer_reserve(&buf, 0x200000);

    alpm_list_t *pkg, *pkgs = repo->cache->list;
    for (pkg = pkgs; pkg; pkg = pkg->next) {
        struct pkg *metadata = pkg->data;
        compile_database_entry(archive, entry, metadata, what, &buf, repo->poolfd);
    }

    archive_write_close(archive);
    buffer_release(&buf);

cleanup:
    archive_entry_free(entry);
    archive_write_free(archive);
    return ret;
}

int write_database(struct repo *repo, const char *repo_name, enum contents what)
{
    trace("writing %s...\n", repo_name);
    check_posix(compile_database(repo, repo_name, what),
                "failed to write %s database", repo_name);

    if (config.sign)
        gpgme_sign(repo->rootfd, repo_name, NULL);

    return 0;
}
