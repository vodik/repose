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

struct database_reader {
    struct archive *archive;
    struct pkg *likely_pkg;
    time_t mtime;
};

struct database_writer {
    struct archive *archive;
    struct archive_entry *entry;
    struct buffer buf;
    enum contents contents;
    int poolfd;
};

/* The name, version and type fields all share the same memory */
struct entry_info {
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

static int parse_database_pathname(const char *entryname, struct entry_info *entry)
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

static inline void entry_info_free(struct entry_info *entry_info)
{
    free(entry_info->name);
}

static struct pkg *get_package(struct database_reader *db, struct entry_info *entry_info,
                               struct pkgcache **pkgcache, bool allocate)
{
    struct pkg *pkg;

    if (db->likely_pkg) {
        hash_t pkgname_hash = sdbm(entry_info->name);
        if (pkgname_hash == db->likely_pkg->hash && streq(db->likely_pkg->name, entry_info->name))
            return db->likely_pkg;
    }

    pkg = pkgcache_find(*pkgcache, entry_info->name);
    if (allocate && !pkg) {
        pkg = malloc(sizeof(struct pkg));
        if (!pkg)
            return NULL;

        *pkg = (struct pkg){
            .hash = sdbm(entry_info->name),
            .name = strdup(entry_info->name),
            .version = strdup(entry_info->version),
            .mtime = db->mtime
        };

        *pkgcache = pkgcache_add_sorted(*pkgcache, pkg);
    }

    if (pkg)
        db->likely_pkg = pkg;

    return pkg;
}

static bool is_database_metadata(const char *filename)
{
    static const char *metadata_names[] = {
        "desc",
        "depends",
        "files",
        NULL
    };

    for (const char **n = metadata_names; *n; ++n) {
        if (streq(filename, *n))
            return true;
    }

    return false;
}

static int parse_database_entry(struct database_reader *db, struct archive_entry *entry,
                                struct pkgcache **pkgcache)
{
    const char *pathname = archive_entry_pathname(entry);
    struct entry_info entry_info;
    int ret = 0;

    if (parse_database_pathname(pathname, &entry_info) < 0) {
        ret = -1;
        goto cleanup;
    }

    if (entry_info.type && is_database_metadata(entry_info.type)) {
        bool allocate = !streq(entry_info.type, "files");

        struct pkg *pkg = get_package(db, &entry_info, pkgcache, allocate);
        if (allocate && !pkg) {
            ret = -1;
            goto cleanup;
        }

        if (pkg && read_desc(db->archive, pkg) < 0) {
            errx(EXIT_FAILURE, "failed to parse %s for %s", entry_info.type, pathname);
        }
    }

cleanup:
    entry_info_free(&entry_info);
    return ret;
}

int load_database(int fd, struct pkgcache **pkgcache)
{
    int ret = 0;

    struct stat st;
    check_posix(fstat(fd, &st), "failed to stat database");

    struct archive_entry *entry;
    struct database_reader db = {
        .archive = archive_read_new(),
        .mtime = st.st_mtime
    };

    archive_read_support_filter_all(db.archive);
    archive_read_support_format_all(db.archive);

    if (archive_read_open_fd(db.archive, fd, 8192) != ARCHIVE_OK) {
        ret = -1;
        goto cleanup;
    }

    while (archive_read_next_header(db.archive, &entry) == ARCHIVE_OK) {
        const mode_t mode = archive_entry_mode(entry);

        if (S_ISREG(mode) && parse_database_entry(&db, entry, pkgcache) < 0) {
            ret = -1;
            goto cleanup;
        }
    }

cleanup:
    archive_read_close(db.archive);
    archive_read_free(db.archive);
    return ret;
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

static void commit_entry(struct database_writer *db, const char *name,
                         const char *folder)
{
    if (!db->buf.len)
        return;

    _cleanup_free_ char *entrypath = joinstring(folder, "/", name, NULL);

    archive_entry_populate(db->entry, AE_IFREG, entrypath, 0644);
    archive_entry_set_size(db->entry, db->buf.len);
    archive_write_header(db->archive, db->entry);
    archive_write_data(db->archive, db->buf.data, db->buf.len);
    archive_entry_clear(db->entry);
    buffer_clear(&db->buf);
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

static void compile_desc_entry(struct database_writer *db, struct pkg *pkg)
{
    write_entry(&db->buf, "FILENAME",  pkg->filename);
    write_entry(&db->buf, "NAME",      pkg->name);
    write_entry(&db->buf, "BASE",      pkg->base);
    write_entry(&db->buf, "VERSION",   pkg->version);
    write_entry(&db->buf, "DESC",      pkg->desc);
    write_entry(&db->buf, "GROUPS",    pkg->groups);
    write_entry(&db->buf, "CSIZE",     pkg->size);
    write_entry(&db->buf, "ISIZE",     pkg->isize);

    if (pkg->base64sig) {
        write_entry(&db->buf, "PGPSIG", pkg->base64sig);
    } else {
        if (!pkg->sha256sum)
            pkg->sha256sum = sha256_file(db->poolfd, pkg->filename);
        write_entry(&db->buf, "SHA256SUM", pkg->sha256sum);
    }

    write_entry(&db->buf, "URL",       pkg->url);
    write_entry(&db->buf, "LICENSE",   pkg->licenses);
    write_entry(&db->buf, "ARCH",      pkg->arch);
    write_entry(&db->buf, "BUILDDATE", pkg->builddate);
    write_entry(&db->buf, "PACKAGER",  pkg->packager);
    write_entry(&db->buf, "REPLACES",  pkg->replaces);

    write_entry(&db->buf, "DEPENDS",      pkg->depends);
    write_entry(&db->buf, "CONFLICTS",    pkg->conflicts);
    write_entry(&db->buf, "PROVIDES",     pkg->provides);
    write_entry(&db->buf, "OPTDEPENDS",   pkg->optdepends);
    write_entry(&db->buf, "MAKEDEPENDS",  pkg->makedepends);
    write_entry(&db->buf, "CHECKDEPENDS", pkg->checkdepends);
}

static void compile_files_entry(struct database_writer *db, struct pkg *pkg)
{
    if (!pkg->files) {
        _cleanup_close_ int pkgfd = openat(db->poolfd, pkg->filename, O_RDONLY);
        if (pkgfd < 0 && errno != ENOENT)
            err(EXIT_FAILURE, "failed to open %s", pkg->filename);

        load_package_files(pkg, pkgfd);
    }

    write_entry(&db->buf, "FILES", pkg->files);
}

static void compile_database_entry(struct database_writer *db, struct pkg *pkg)
{
    _cleanup_free_ char *folder = joinstring(pkg->name, "-", pkg->version, NULL);

    archive_entry_populate(db->entry, AE_IFDIR, folder, 0755);
    archive_write_header(db->archive, db->entry);
    archive_entry_clear(db->entry);

    if (db->contents & DB_DESC) {
        compile_desc_entry(db, pkg);
        commit_entry(db, "desc", folder);
    }
    if (db->contents & DB_FILES) {
        compile_files_entry(db, pkg);
        commit_entry(db, "files", folder);
    }
    if (db->contents & DB_DELTAS) {
        write_entry(&db->buf, "DELTAS", pkg->deltas);
        commit_entry(db, "deltas", folder);
    }
}

static int compile_database(struct repo *repo, const char *repo_name,
                            enum contents what)
{
    int ret = 0;
    _cleanup_close_ int dbfd = openat(repo->rootfd, repo_name,
                                      O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (dbfd < 0)
        return -1;

    struct database_writer db = {
        .archive = archive_write_new(),
        .entry = archive_entry_new(),
        .buf = {0},
        .contents = what,
        .poolfd = repo->poolfd,
    };

    archive_write_add_filter(db.archive, config.compression);
    archive_write_set_format_pax_restricted(db.archive);

    if (archive_write_open_fd(db.archive, dbfd) < 0) {
        ret = -1;
        goto cleanup;
    }

    archive_entry_populate(db.entry, AE_IFDIR, "", 0755);
    archive_write_header(db.archive, db.entry);
    archive_entry_clear(db.entry);

    /* The files database can get very, very large. Lets allocate a
     * 2MiB buffer so we have plenty of room and avoid reallocation. */
    buffer_reserve(&db.buf, 0x200000);

    const alpm_list_t *node;
    for (node = repo->cache->list; node; node = node->next) {
        struct pkg *pkg = node->data;
        compile_database_entry(&db, pkg);
    }

    archive_write_close(db.archive);
    buffer_release(&db.buf);

cleanup:
    archive_entry_free(db.entry);
    archive_write_free(db.archive);
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
