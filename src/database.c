#include "database.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <archive.h>
#include <archive_entry.h>

#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <time.h>

#include "file.h"
#include "pkghash.h"
#include "util.h"
#include "desc.h"
#include "strbuf.h"
#include <alpm.h>

struct db {
    int fd;
    struct archive *archive;
    int filter;
    time_t mtime;
    struct pkg *likely_pkg;
};

struct db_entry {
    char *name;
    const char *type;
    const char *version;
};

static int open_database(struct db *db, int fd)
{
    struct file_t file;

    if (file_from_fd(&file, fd) < 0)
        return -1;

    *db = (struct db){
        .archive = archive_read_new(),
        .filter = ARCHIVE_COMPRESSION_NONE,
        .fd = file.fd,
        .mtime = file.st.st_mtime
    };

    archive_read_support_filter_all(db->archive);
    archive_read_support_format_all(db->archive);

    if (archive_read_open_memory(db->archive, file.mmap, file.st.st_size) != ARCHIVE_OK) {
        file_close(&file);
        return -1;
    }

    db->filter = archive_filter_code(db->archive, 0);
    return 0;
}

static int parse_db_entry(const char *entryname, struct db_entry *entry)
{
    entry->name = strdup(entryname);
    const char *slash = strchrnul(entry->name, '/'), *dash = slash;

    dash = memrchr(entry->name, '-', dash - entry->name);
    dash = memrchr(entry->name, '-', dash - entry->name);

    if (*dash != '-')
        return -EINVAL;

    /* name, version and type share the same memory */
    entry->name[dash - entry->name] = entry->name[slash - entry->name] = '\0';
    entry->type = slash ? slash + 1 : NULL;
    entry->version = &entry->name[dash - entry->name + 1];

    return 0;
}

static inline void free_db_entry(struct db_entry *e)
{
    free(e->name);
}

static struct pkg *load_pkg_for_entry(struct db *db, alpm_pkghash_t **pkgcache,
                                      struct db_entry *e, bool allocate)
{
    struct pkg *pkg;

    if (db->likely_pkg) {
        unsigned long pkgname_hash = _alpm_hash_sdbm(e->name);

        if (pkgname_hash == db->likely_pkg->name_hash && streq(db->likely_pkg->name, e->name))
            return db->likely_pkg;
    }

    pkg = _alpm_pkghash_find(*pkgcache, e->name);

    if (allocate && !pkg) {
        pkg = malloc(sizeof(struct pkg));
        if (!pkg)
            return NULL;

        *pkg = (struct pkg){
            .name      = strdup(e->name),
            .name_hash = _alpm_hash_sdbm(e->name),
            .version   = strdup(e->version),
            .mtime     = db->mtime
        };

        *pkgcache = _alpm_pkghash_add_sorted(*pkgcache, pkg);
    }

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

static int db_read_pkg(struct db *db, alpm_pkghash_t **pkgcache,
                        struct archive_entry *entry)
{
    const char *pathname = archive_entry_pathname(entry);
    struct db_entry e;

    if (parse_db_entry(pathname, &e) < 0) {
        free_db_entry(&e);
        return -1;
    }

    if (e.type && is_database_metadata(e.type)) {
        bool is_files = streq(e.type, "files");

        struct pkg *pkg = load_pkg_for_entry(db, pkgcache, &e, !is_files);
        if (!is_files && pkg == NULL) {
            free_db_entry(&e);
            return -1;
        }

        if (pkg)
            read_desc(db->archive, pkg);
    }

    free_db_entry(&e);
    return 0;
}

int load_database(int fd, alpm_pkghash_t **pkgcache)
{
    struct db db;
    struct archive_entry *entry;

    if (open_database(&db, fd) < 0) {
        archive_read_close(db.archive);
        archive_read_free(db.archive);
        return -1;
    }

    while (archive_read_next_header(db.archive, &entry) == ARCHIVE_OK) {
        const mode_t mode = archive_entry_mode(entry);

        if (S_ISREG(mode) && db_read_pkg(&db, pkgcache, entry) < 0)
            return -1;
    }

    archive_read_close(db.archive);
    archive_read_free(db.archive);
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

static void write_long(struct buffer *buf, const char *header, long val)
{
    buffer_printf(buf, "%%%s%%\n%ld\n\n", header, val);
}

#define write_entry(buf, header, val) _Generic((val), \
    alpm_list_t *: write_list, \
    char *: write_string, \
    size_t: write_size, \
    long: write_long)(buf, header, val)

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

    if (!pkg->md5sum)
        pkg->md5sum = md5_file(poolfd, pkg->filename);
    if (!pkg->sha256sum)
        pkg->sha256sum = sha256_file(poolfd, pkg->filename);

    write_entry(buf, "MD5SUM",    pkg->md5sum);
    write_entry(buf, "SHA256SUM", pkg->sha256sum);
    write_entry(buf, "PGPSIG",    pkg->base64sig);

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

static void archive_entry_populate(struct archive_entry *e, mode_t mode)
{
    time_t now = time(NULL);

    archive_entry_set_perm(e, mode);
    archive_entry_set_uname(e, getlogin());
    archive_entry_set_gname(e, getlogin());
    archive_entry_set_ctime(e, now, 0);
    archive_entry_set_mtime(e, now, 0);
    archive_entry_set_atime(e, now, 0);
}

static void record_entry(struct archive *archive, struct archive_entry *e,
                         const char *root, const char *entry, struct buffer *buf)
{
    _cleanup_free_ char *entry_path = joinstring(root, "/", entry, NULL);

    archive_entry_set_pathname(e, entry_path);
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_populate(e, 0644);
    archive_entry_set_size(e, buf->len);
    archive_write_header(archive, e);
    archive_write_data(archive, buf->data, buf->len);

    archive_entry_clear(e);
    buffer_clear(buf);
}

static void compile_database_entry(struct archive *archive, struct archive_entry *e, struct pkg *pkg,
                                   int contents, struct buffer *buf, int poolfd)
{
    _cleanup_free_ char *entry = joinstring(pkg->name, "-", pkg->version, NULL);

    archive_entry_set_pathname(e, entry);
    archive_entry_set_filetype(e, AE_IFDIR);
    archive_entry_populate(e, 0755);
    archive_write_header(archive, e);
    archive_entry_clear(e);

    if (contents & DB_DESC) {
        compile_desc_entry(pkg, buf, poolfd);
        record_entry(archive, e, entry, "desc", buf);
    }
    if (contents & DB_DEPENDS) {
        compile_depends_entry(pkg, buf);
        record_entry(archive, e, entry, "depends", buf);
    }
    if (contents & DB_FILES) {
        compile_files_entry(pkg, buf, poolfd);
        record_entry(archive, e, entry, "files", buf);
    }
}

int save_database(int fd, alpm_pkghash_t *pkgcache, enum contents what, int compression, int poolfd)
{
    struct archive *archive = archive_write_new();
    struct archive_entry *entry = archive_entry_new();
    alpm_list_t *pkg, *pkgs = pkgcache->list;
    struct buffer buf;

    archive_write_add_filter(archive, compression);
    archive_write_set_format_pax_restricted(archive);

    if (archive_write_open_fd(archive, fd) < 0)
        return -1;

    buffer_init(&buf, 1024);

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        struct pkg *metadata = pkg->data;
        compile_database_entry(archive, entry, metadata, what, &buf, poolfd);
    }

    buffer_free(&buf);

    archive_write_close(archive);
    archive_entry_free(entry);
    archive_write_free(archive);

    return 0;
}
