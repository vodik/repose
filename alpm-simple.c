#include "alpm-simple.h"

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

static void read_metadata_line(char *buf, alpm_pkg_meta_t *pkg)
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

static void read_metadata(struct archive *a, struct archive_entry *ae, alpm_pkg_meta_t *pkg)
{
    off_t entry_size = archive_entry_size(ae);

    struct archive_read_buffer buf = {
        .line = malloc(entry_size)
    };

    while(archive_fgets(a, &buf, entry_size) == ARCHIVE_OK && buf.real_line_size > 0) {
        read_metadata_line(buf.line, pkg);
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
            read_metadata(archive, entry, pkg);
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
