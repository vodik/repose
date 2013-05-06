#include "alpm_metadata.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <alpm.h>
#include <archive.h>
#include <archive_entry.h>
#include "archive_extra.h"

static void read_pkg_metadata_line(char *buf, alpm_pkg_meta_t *pkg)
{
    char *var;

    /* XXX: not really handling comments properly */
    if (buf[0] == '#')
        return;

    var = strsep(&buf, " = ");
    if (buf == NULL)
        return;
    buf += 2;

    if (strcmp(var, "pkgname") == 0)
        pkg->name = strdup(buf);
    else if (strcmp(var, "pkgver") == 0)
        pkg->version = strdup(buf);
    else if (strcmp(var, "pkgdesc") == 0)
        pkg->desc = strdup(buf);
    else if (strcmp(var, "url") == 0)
        pkg->url = strdup(buf);
    else if (strcmp(var, "builddate") == 0)
        pkg->builddate = atol(buf);
    else if (strcmp(var, "packager") == 0)
        pkg->packager = strdup(buf);
    else if (strcmp(var, "size") == 0)
        pkg->isize = atol(buf);
    else if (strcmp(var, "arch") == 0)
        pkg->arch = strdup(buf);

    else if (strcmp(var, "license") == 0)
        pkg->license = alpm_list_add(pkg->license, strdup(buf));
    else if (strcmp(var, "depend") == 0)
        pkg->depends = alpm_list_add(pkg->depends, strdup(buf));
    else if (strcmp(var, "conflict") == 0)
        pkg->conflicts = alpm_list_add(pkg->conflicts, strdup(buf));
    else if (strcmp(var, "provides") == 0)
        pkg->provides = alpm_list_add(pkg->provides, strdup(buf));
    else if (strcmp(var, "optdepend") == 0)
        pkg->optdepends = alpm_list_add(pkg->optdepends, strdup(buf));
    else if (strcmp(var, "makedepend") == 0)
        pkg->makedepends = alpm_list_add(pkg->makedepends, strdup(buf));
}

static void read_pkg_metadata(struct archive *a, struct archive_entry *ae, alpm_pkg_meta_t *pkg)
{
    off_t entry_size = archive_entry_size(ae);

    struct archive_read_buffer buf = {
        .line = malloc(entry_size)
    };

    while(archive_fgets(a, &buf, entry_size) == ARCHIVE_OK && buf.real_line_size > 0) {
        read_pkg_metadata_line(buf.line, pkg);
    }

    free(buf.line);
}

int alpm_pkg_load_metadata(const char *filename, alpm_pkg_meta_t **_pkg)
{
    struct archive *archive = NULL;
    alpm_pkg_meta_t *pkg;
    struct stat st;
    char *memblock = MAP_FAILED;
    int fd = 0, rc = 0;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        if(errno == ENOENT)
            err(EXIT_FAILURE, "failed to open %s", filename);
        rc = -errno;
        goto cleanup;
    }

    fstat(fd, &st);
    memblock = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
    if (memblock == MAP_FAILED)
        err(EXIT_FAILURE, "failed to mmap package %s", filename);


    archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);

    int r = archive_read_open_memory(archive, memblock, st.st_size);
    if (r != ARCHIVE_OK) {
        warnx("%s is not an archive", filename);
        rc = -1;
        goto cleanup;
    }

    pkg = calloc(1, sizeof(alpm_pkg_meta_t));
    for (;;) {
        struct archive_entry *entry;

        r = archive_read_next_header(archive, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        } else if (r != ARCHIVE_OK) {
            errx(EXIT_FAILURE, "failed to read header: %s", archive_error_string(archive));
        }

        const mode_t mode = archive_entry_mode(entry);
        const char *entry_name = archive_entry_pathname(entry);

        if (S_ISREG(mode) && strcmp(entry_name, ".PKGINFO") == 0) {
            read_pkg_metadata(archive, entry, pkg);
            break;
        }
    }

    pkg->filename = strdup(filename);
    pkg->size = st.st_size;

    *_pkg = pkg;

cleanup:
    if (fd >= 0)
        close(fd);

    if (memblock != MAP_FAILED)
        munmap(memblock, st.st_size);

    if (archive) {
        archive_read_close(archive);
        archive_read_free(archive);
    }

    return rc;
}

void alpm_pkg_free_metadata(alpm_pkg_meta_t *pkg)
{
    free(pkg->filename);
    free(pkg->name);
    free(pkg->version);
    free(pkg->desc);
    free(pkg->url);
    free(pkg->packager);
    free(pkg->arch);

    alpm_list_free_inner(pkg->license, free);
    alpm_list_free_inner(pkg->depends, free);
    alpm_list_free_inner(pkg->conflicts, free);
    alpm_list_free_inner(pkg->provides, free);
    alpm_list_free_inner(pkg->optdepends, free);
    alpm_list_free_inner(pkg->makedepends, free);

    free(pkg);
}

static size_t _alpm_strip_newline(char *str, size_t len)
{
	if(*str == '\0') {
		return 0;
	}
	if(len == 0) {
		len = strlen(str);
	}
	while(len > 0 && str[len - 1] == '\n') {
		len--;
	}
	str[len] = '\0';

	return len;
}

static inline void read_desc_list(struct archive *archive, struct archive_read_buffer *buf, off_t entry_size, alpm_list_t **list)
{
    for (;;) {
        archive_fgets(archive, buf, entry_size);

        if (_alpm_strip_newline(buf->line, buf->real_line_size) == 0)
            return;

        *list = alpm_list_add(*list, strdup(buf->line));
    }
}

static inline void read_desc_entry(struct archive *archive, struct archive_read_buffer *buf, off_t entry_size, char **data)
{
    archive_fgets(archive, buf, entry_size);
    *data = strdup(buf->line);
}

static void read_desc(struct archive *archive, struct archive_entry *entry, alpm_pkg_meta_t *pkg)
{
    off_t entry_size = archive_entry_size(entry);

    /* TODO: allocated too many times */
    struct archive_read_buffer buf = {
        .line = malloc(entry_size)
    };

    /* TODO: finish */
    while(archive_fgets(archive, &buf, entry_size) == ARCHIVE_OK) {
        if (strcmp(buf.line, "%FILENAME%") == 0) {
            read_desc_entry(archive, &buf, entry_size, &pkg->filename);
        } else if (strcmp(buf.line, "%NAME%") == 0) {
            /* XXX: name should already be set, rather, validate it */
            read_desc_entry(archive, &buf, entry_size, &pkg->name);
        } else if (strcmp(buf.line, "%VERSION%") == 0) {
            /* XXX: version should already be set, rather, validate it */
            read_desc_entry(archive, &buf, entry_size, &pkg->version);
        } else if (strcmp(buf.line, "%DESC%") == 0) {
            read_desc_entry(archive, &buf, entry_size, &pkg->desc);
        } else if (strcmp(buf.line, "%CSIZE%") == 0) {
            /* TODO */
        } else if (strcmp(buf.line, "%ISIZE%") == 0) {
            /* TODO */
        } else if (strcmp(buf.line, "%MD5SUM%") == 0) {
            read_desc_entry(archive, &buf, entry_size, &pkg->md5sum);
        } else if (strcmp(buf.line, "%SHA256SUM%") == 0) {
            read_desc_entry(archive, &buf, entry_size, &pkg->sha256sum);
        } else if (strcmp(buf.line, "%URL%") == 0) {
            read_desc_entry(archive, &buf, entry_size, &pkg->url);
        } else if (strcmp(buf.line, "%LICENSE%") == 0) {
            read_desc_list(archive, &buf, entry_size, &pkg->license);
        } else if (strcmp(buf.line, "%ARCH%") == 0) {
            read_desc_entry(archive, &buf, entry_size, &pkg->arch);
        } else if (strcmp(buf.line, "%BUILDDATE%") == 0) {
            /* TODO */
        } else if (strcmp(buf.line, "%PACKAGER%") == 0) {
            read_desc_entry(archive, &buf, entry_size, &pkg->packager);
        } else if (strcmp(buf.line, "%DEPENDS%") == 0) {
            read_desc_list(archive, &buf, entry_size, &pkg->depends);
        } else if (strcmp(buf.line, "%CONFLICTS%") == 0) {
            read_desc_list(archive, &buf, entry_size, &pkg->conflicts);
        } else if (strcmp(buf.line, "%PROVIDES%") == 0) {
            read_desc_list(archive, &buf, entry_size, &pkg->provides);
        } else if (strcmp(buf.line, "%OPTDEPENDS%") == 0) {
            read_desc_list(archive, &buf, entry_size, &pkg->optdepends);
        } else if (strcmp(buf.line, "%MAKEDEPENDS%") == 0) {
            read_desc_list(archive, &buf, entry_size, &pkg->makedepends);
        }
    }

    free(buf.line);
}

static alpm_pkg_meta_t *load_pkg(alpm_db_meta_t *db, char *entryname)
{
    char *p = entryname;
    p = strrchr(p, '-');
    p = strrchr(p, '-');
    *++p = '\0';

    alpm_pkg_meta_t *metadata = hashtable_get(db->pkgcache, entryname);

    if (metadata == NULL) {
        metadata = calloc(1, sizeof(alpm_pkg_meta_t));
        metadata->name = strdup(entryname);
        metadata->version = strdup(p);
    }

    hashtable_add(db->pkgcache, metadata->name, metadata);
    return metadata;
}

static void db_read_pkg(alpm_db_meta_t *db, struct archive *archive,
                        struct archive_entry *entry)
{
    char *entryname = strdup(archive_entry_pathname(entry));
    char *filename  = strsep(&entryname, "/");
    if (entryname == NULL)
        return;

    if (strcmp(entryname, "desc") == 0 || strcmp(entryname, "depends") == 0) {
        alpm_pkg_meta_t *pkg = load_pkg(db, filename);
        read_desc(archive, entry, pkg);
    }

    free(filename);
}

int alpm_db_populate(const char *filename, alpm_db_meta_t *db)
{
    struct archive *archive = NULL;
    struct stat st;
    char *memblock = MAP_FAILED;
    int fd = 0, rc = 0;

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        if(errno == ENOENT)
            err(EXIT_FAILURE, "failed to open %s", filename);
        rc = -errno;
        goto cleanup;
    }

    fstat(fd, &st);
    memblock = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED | MAP_POPULATE, fd, 0);
    if (memblock == MAP_FAILED)
        err(EXIT_FAILURE, "failed to mmap package %s", filename);


    archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);

    int r = archive_read_open_memory(archive, memblock, st.st_size);
    if (r != ARCHIVE_OK) {
        warnx("%s is not an archive", filename);
        rc = -1;
        goto cleanup;
    }

    db->pkgcache = hashtable_new(23, NULL); //_alpm_pkghash_create(23);
    if (db->pkgcache == NULL) {
        rc = -1;
        goto cleanup;
    }

    for (;;) {
        struct archive_entry *entry;

        r = archive_read_next_header(archive, &entry);
        if (r == ARCHIVE_EOF) {
            break;
        } else if (r != ARCHIVE_OK) {
            errx(EXIT_FAILURE, "failed to read header: %s", archive_error_string(archive));
        }

        const mode_t mode = archive_entry_mode(entry);
        if (S_ISDIR(mode))
            continue;

        /* we have desc, depends, or deltas - parse it */
        /* alpm_pkg_meta_t *pkg = NULL; */
        db_read_pkg(db, archive, entry);
    }

cleanup:
    if (fd >= 0)
        close(fd);

    if (memblock != MAP_FAILED)
        munmap(memblock, st.st_size);

    if (archive) {
        archive_read_close(archive);
        archive_read_free(archive);
    }

    return rc;
}
