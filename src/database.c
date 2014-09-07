/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Simon Gomizelj, 2014
 */

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

#include "memblock.h"
#include "pkghash.h"
#include "util.h"
#include "desc.h"
#include "strbuf.h"
#include <alpm.h>

struct db {
    int fd;
    struct memblock_t memblock;
    struct archive *archive;
    int filter;
    time_t mtime;
};

struct db_entry {
    char *name;
    const char *type;
    const char *version;
};

struct pkg *_likely_pkg = NULL;

static int open_db(struct db *db, int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0)
        return -1;

    *db = (struct db){
        .archive = archive_read_new(),
        .filter  = ARCHIVE_COMPRESSION_NONE,
        .mtime   = st.st_mtime
    };

    if (memblock_open_fd(&db->memblock, fd) < 0) {
        archive_read_free(db->archive);
        return -1;
    }

    archive_read_support_filter_all(db->archive);
    archive_read_support_format_all(db->archive);

    if (archive_read_open_memory(db->archive, db->memblock.mem, db->memblock.len) != ARCHIVE_OK) {
        archive_read_free(db->archive);
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

    /* ->name, ->version and ->type share the same memory */
    entry->name[dash - entry->name] = entry->name[slash - entry->name] = '\0';
    entry->type = slash ? slash + 1 : NULL;
    entry->version = &entry->name[dash - entry->name + 1];

    return 0;
}

static struct pkg *load_pkg_for_entry(alpm_pkghash_t **pkgcache, struct db_entry *e,
                                      struct pkg *likely_pkg, time_t mtime)
{
    struct pkg *pkg;

    if (likely_pkg) {
        unsigned long pkgname_hash = _alpm_hash_sdbm(e->name);

        if (pkgname_hash == likely_pkg->name_hash && streq(likely_pkg->name, e->name))
            return likely_pkg;
    }

    pkg = _alpm_pkghash_find(*pkgcache, e->name);

    if (!pkg) {
        pkg = malloc(sizeof(struct pkg));

        *pkg = (struct pkg){
            .name      = strdup(e->name),
            .version   = strdup(e->version),
            .name_hash = _alpm_hash_sdbm(e->name),
            .mtime     = mtime
        };

        *pkgcache = _alpm_pkghash_add_sorted(*pkgcache, pkg);
    }

    return pkg;
}

static void db_read_pkg(alpm_pkghash_t **pkgcache, struct archive *archive,
                        struct archive_entry *entry, time_t mtime)
{
    const char *pathname = archive_entry_pathname(entry);
    struct db_entry e;

    if (parse_db_entry(pathname, &e) < 0)
        goto cleanup;

    if (e.type) {
        struct pkg *pkg = load_pkg_for_entry(pkgcache, &e, _likely_pkg, mtime);
        if (pkg == NULL)
            goto cleanup;

        if (streq(e.type, "desc") || streq(e.type, "depends") || streq(e.type, "files"))
            read_desc(archive, pkg);

        _likely_pkg = pkg;
    }

cleanup:
    free(e.name);
}

int load_database(int fd, alpm_pkghash_t **pkgcache)
{
    struct db db;
    struct archive_entry *entry;

    if (open_db(&db, fd) < 0)
        return -1;

    while (archive_read_next_header(db.archive, &entry) == ARCHIVE_OK) {
        const mode_t mode = archive_entry_mode(entry);

        if (S_ISREG(mode))
            db_read_pkg(pkgcache, db.archive, entry, db.mtime);
    }

    archive_read_close(db.archive);
    archive_read_free(db.archive);

    return 0;
}

static void write_list(buffer_t *buf, const char *header, const alpm_list_t *lst)
{
    if (lst == NULL)
        return;

    buffer_printf(buf, "%%%s%%\n", header);
    for (; lst; lst = lst->next)
        buffer_printf(buf, "%s\n", (const char *)lst->data);
    buffer_putc(buf, '\n');
}

static void write_string(buffer_t *buf, const char *header, const char *str)
{
    if (str == NULL)
        return;

    buffer_printf(buf, "%%%s%%\n%s\n\n", header, str);
}

static void write_long(buffer_t *buf, const char *header, long val)
{
    buffer_printf(buf, "%%%s%%\n%ld\n\n", header, val);
}

static void compile_depends_entry(struct pkg *pkg, buffer_t *buf)
{
    write_list(buf, "DEPENDS",      pkg->depends);
    write_list(buf, "CONFLICTS",    pkg->conflicts);
    write_list(buf, "PROVIDES",     pkg->provides);
    write_list(buf, "OPTDEPENDS",   pkg->optdepends);
    write_list(buf, "MAKEDEPENDS",  pkg->makedepends);
    write_list(buf, "CHECKDEPENDS", pkg->checkdepends);
}

static void compile_desc_entry(struct pkg *pkg, buffer_t *buf, int poolfd)
{
    write_string(buf, "FILENAME",  pkg->filename);
    write_string(buf, "NAME",      pkg->name);
    write_string(buf, "BASE",      pkg->base);
    write_string(buf, "VERSION",   pkg->version);
    write_string(buf, "DESC",      pkg->desc);
    write_list(buf,   "GROUPS",    pkg->groups);
    write_long(buf,   "CSIZE",     (long)pkg->size);
    write_long(buf,   "ISIZE",     (long)pkg->isize);

    if (!pkg->md5sum)
        pkg->md5sum = compute_md5sum(poolfd, pkg->filename);
    if (!pkg->sha256sum)
        pkg->sha256sum = compute_sha256sum(poolfd, pkg->filename);
    if (!pkg->base64sig)
        load_package_signature(pkg, poolfd);

    write_string(buf, "MD5SUM", pkg->md5sum);
    write_string(buf, "SHA256SUM", pkg->sha256sum);

    if (pkg->base64sig)
        write_string(buf, "PGPSIG", pkg->base64sig);

    write_string(buf, "URL",       pkg->url);
    write_list(buf,   "LICENSE",   pkg->licenses);
    write_string(buf, "ARCH",      pkg->arch);
    write_long(buf,   "BUILDDATE", pkg->builddate);
    write_string(buf, "PACKAGER",  pkg->packager);
    write_list(buf,   "REPLACES",  pkg->replaces);
}

static void compile_files_entry(struct pkg *pkg, buffer_t *buf, int poolfd)
{
    if (!pkg->files) {
        _cleanup_close_ int pkgfd = openat(poolfd, pkg->filename, O_RDONLY);
        if (pkgfd < 0 && errno != ENOENT)
            err(EXIT_FAILURE, "failed to open %s", pkg->filename);

        load_package_files(pkg, pkgfd);
    }

    write_list(buf, "FILES", pkg->files);
}

static void record_entry(struct archive *archive, struct archive_entry *e,
                         const char *root, const char *entry, struct buffer *buf)
{
    _cleanup_free_ char *entry_path = joinstring(root, "/", entry, NULL);
    time_t now = time(NULL);

    archive_entry_set_pathname(e, entry_path);
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_size(e, buf->len);

    archive_entry_set_perm(e, 0644);
    archive_entry_set_ctime(e, now, 0);
    archive_entry_set_mtime(e, now, 0);
    archive_entry_set_atime(e, now, 0);

    archive_write_header(archive, e);
    archive_write_data(archive, buf->data, buf->len);

    archive_entry_clear(e);
    buffer_clear(buf);
}


static void compile_database_entry(struct archive *archive, struct archive_entry *e, struct pkg *pkg,
                                   int contents, struct buffer *buf, int poolfd)
{
    _cleanup_free_ char *entry = joinstring(pkg->name, "-", pkg->version, NULL);

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
