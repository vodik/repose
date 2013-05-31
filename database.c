#include "database.h"

#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <archive.h>
#include <archive_entry.h>

#include "alpm/signing.h"
#include "alpm/util.h"
#include "repoman.h"
#include "buffer.h"

typedef struct db_writer {
    struct archive *archive;
    struct archive_entry *entry;
    struct buffer buf;
    int fd;
} db_writer_t;

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

static db_writer_t *db_writer_new(repo_t *repo, file_t *db)
{
    db_writer_t *writer = malloc(sizeof(db_writer_t));
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

    writer->fd = openat(repo->dirfd, db->file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (writer->fd < 0)
        err(EXIT_FAILURE, "failed to open %s for writing", db->file);
    archive_write_open_fd(writer->archive, writer->fd);

    buffer_init(&writer->buf, 1024);

    return writer;
}

static void db_writer_close(db_writer_t *writer)
{
    archive_write_close(writer->archive);

    buffer_free(&writer->buf);
    archive_entry_free(writer->entry);
    archive_write_free(writer->archive);
    close(writer->fd);
    free(writer);
}

static void compile_depends_entry(const alpm_pkg_meta_t *pkg, struct buffer *buf)
{
    write_list(buf, "DEPENDS",     pkg->depends);
    write_list(buf, "CONFLICTS",   pkg->conflicts);
    write_list(buf, "PROVIDES",    pkg->provides);
    write_list(buf, "OPTDEPENDS",  pkg->optdepends);
    write_list(buf, "MAKEDEPENDS", pkg->makedepends);
}

static void compile_desc_entry(repo_t *repo, const alpm_pkg_meta_t *pkg, struct buffer *buf)
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

static void compile_files_entry(repo_t *repo, const alpm_pkg_meta_t *pkg, struct buffer *buf)
{
    if (pkg->files) {
        write_list(buf, "FILES", pkg->files);
    } else {
        int pkgfd = openat(repo->dirfd, pkg->filename, O_RDONLY);
        if (pkgfd < 0 && errno != ENOENT) {
            err(EXIT_FAILURE, "failed to open %s", pkg->filename);
        }

        alpm_list_t *files = alpm_pkg_files(pkgfd);
        close(pkgfd);

        write_list(buf, "FILES", files);
        alpm_list_free_inner(files, free);
        alpm_list_free(files);
    }
}

static void record_entry(db_writer_t *writer, const char *root, const char *entry)
{
    time_t now = time(NULL);
    char entry_path[PATH_MAX];

    snprintf(entry_path, PATH_MAX, "%s/%s", root, entry);
    archive_entry_set_pathname(writer->entry, entry_path);
    archive_entry_set_filetype(writer->entry, AE_IFREG);
    archive_entry_set_size(writer->entry, writer->buf.len);

    archive_entry_set_perm(writer->entry, 0644);
    archive_entry_set_ctime(writer->entry, now, 0);
    archive_entry_set_mtime(writer->entry, now, 0);
    archive_entry_set_atime(writer->entry, now, 0);

    archive_write_header(writer->archive, writer->entry);
    archive_write_data(writer->archive, writer->buf.data, writer->buf.len);

    archive_entry_clear(writer->entry);
    buffer_clear(&writer->buf);
}

static void compile_database_entry(repo_t *repo, db_writer_t *writer, alpm_pkg_meta_t *pkg, int contents)
{
    char entry[PATH_MAX];
    snprintf(entry, PATH_MAX, "%s-%s", pkg->name, pkg->version);

    if (contents & DB_DESC) {
        compile_desc_entry(repo, pkg, &writer->buf);
        record_entry(writer, entry, "desc");
    }
    if (contents & DB_DEPENDS) {
        compile_depends_entry(pkg, &writer->buf);
        record_entry(writer, entry, "depends");
    }
    if (contents & DB_FILES) {
        compile_files_entry(repo, pkg, &writer->buf);
        record_entry(writer, entry, "files");
    }
}

void sign_database(repo_t *repo, file_t *db, const char *key)
{
    int dbfd = openat(repo->dirfd, db->file, O_RDONLY);
    if (dbfd < 0)
        err(EXIT_FAILURE, "failed to open %s", db->file);

    int sigfd = openat(repo->dirfd, db->sig, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (sigfd < 0)
        err(EXIT_FAILURE, "failed to open %s for writing", db->sig);

    /* XXX: check return type */
    gpgme_sign(dbfd, sigfd, key);

    close(sigfd);
    close(dbfd);

    if (symlinkat(db->sig, repo->dirfd, db->link_sig) < 0 && errno != EEXIST)
        err(EXIT_FAILURE, "symlink for %s failed", db->link_sig);
}

void compile_database(repo_t *repo, file_t *db, int contents)
{
    db_writer_t *writer = db_writer_new(repo, db);
    alpm_list_t *pkg, *pkgs = repo->pkgcache->list;

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        alpm_pkg_meta_t *metadata = pkg->data;
        compile_database_entry(repo, writer, metadata, contents);
    }

    db_writer_close(writer);

    /* make the appropriate symlink for the database */
    if (symlinkat(db->file, repo->dirfd, db->link_file) < 0 && errno != EEXIST)
        err(EXIT_FAILURE, "symlink for %s failed", db->link_file);
}

int load_database(repo_t *repo, file_t *db)
{
    int sigfd, dbfd = openat(repo->dirfd, db->file, O_RDONLY);
    if (dbfd < 0) {
        if(errno != ENOENT)
            err(EXIT_FAILURE, "failed to open %s", db->file);
        return -errno;
    }

    /* FIXME: error reporting should be here, not inside this funciton */
    if (alpm_db_populate(dbfd, &repo->pkgcache) < 0)
        return -1; /* FIXME: fix (but atm this shouldn't ever run) */

    sigfd = openat(repo->dirfd, db->sig, O_RDONLY);
    if (sigfd < 0) {
       if (errno != ENOENT)
            err(EXIT_FAILURE, "failed to open %s", db->file);
        close(dbfd);
        return 0;
    }

    if (gpgme_verify(dbfd, sigfd) < 0) {
        errx(EXIT_FAILURE, "database signature is invalid or corrupt!");
    }

    close(dbfd);
    close(sigfd);
    return 0;
}

