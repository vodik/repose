#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
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

#include "alpm-simple.h"
#include "buffer.h"
#include "hashtable.h"

struct repo {
    struct archive *archive;
    struct archive_entry *entry;
    struct buffer buf;
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
    const char *md5sum = alpm_compute_md5sum(pkg->filename);
    const char *sha256sum = alpm_compute_sha256sum(pkg->filename);

    write_string(buf, "DESC",      pkg->desc);
    write_long(buf,   "CSIZE",     (long)pkg->size);
    write_long(buf,   "ISIZE",     (long)pkg->isize);
    write_string(buf, "MD5SUM",    md5sum);
    write_string(buf, "SHA256SUM", sha256sum);
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

static struct repo *repo_write_new(const char *filename)
{
    struct repo *repo = malloc(sizeof(struct repo));
    repo->archive = archive_write_new();
    repo->entry = archive_entry_new();

    archive_write_add_filter_gzip(repo->archive);
    archive_write_set_format_pax_restricted(repo->archive);
    archive_write_open_filename(repo->archive, filename);

    buffer_init(&repo->buf, 512);

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
}

static alpm_list_t *find_packages(char *const *paths)
{
    static const char *filter = "*.pkg.tar*";

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
            if (fnmatch(filter, p->fts_path, FNM_CASEFOLD) == 0)
                pkgs = alpm_list_add(pkgs, strdup(p->fts_path));
            break;
        default:
            break;
        }
    }

    fts_close(ftsp);
    return pkgs;
}

int main(int argc, const char *argv[])
{
    char *dot[] = { ".", NULL };
    char **paths = argc > 1 ? (char **)argv + 1 : dot;

    struct hashtable *table = hashtable_new(17, NULL);
    alpm_list_t *pkg, *pkgs = find_packages(paths);

    for (pkg = pkgs; pkg; pkg = pkg->next) {
        const char *path = pkg->data;
        alpm_pkg_meta_t *metadata;

        alpm_pkg_load_metadata(path, &metadata);
        alpm_pkg_meta_t *old = hashtable_get(table, metadata->name);

        if (old == NULL || alpm_pkg_vercmp(metadata->version, old->version) > 0)
            hashtable_add(table, metadata->name, metadata);
        /* pkg->data = metadata; */
    }

    /* TEMPORARY: HACKY */
    {
        struct repo *repo = repo_write_new("vodik.db.tar.gz");
        struct hashnode_t **nodes = table->nodes;
        size_t i;

        for (i = 0; i < table->size; ++i) {
            struct hashnode_t *node = nodes[i];
            while (node) {
                alpm_pkg_meta_t *pkg = node->val;
                printf("%s-%s [%s]\n", pkg->name, pkg->version, pkg->arch);

                repo_write_pkg(repo, pkg);
                node = node->next;
            }
        }

        repo_write_close(repo);
    }
}
