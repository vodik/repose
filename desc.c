#include "desc.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <err.h>

#include <archive.h>
#include <archive_entry.h>
#include <alpm_list.h>

#include "reader.h"
#include "package.h"
#include "util.h"

static inline void read_desc_list(struct archive_reader *reader, alpm_list_t **list)
{
    char *buf;
    while (archive_getline(reader, &buf) > 0)
        *list = alpm_list_add(*list, buf);
}

static inline void read_desc_entry(struct archive_reader *reader, char **data)
{
    archive_getline(reader, data);
}

static inline void read_desc_ulong(struct archive_reader *reader, unsigned long *data)
{
    _cleanup_free_ char *buf;
    if (archive_getline(reader, &buf) > 0)
        xstrtoul(buf, data);
}

static inline void read_desc_long(struct archive_reader *reader, long *data)
{
    _cleanup_free_ char *buf;
    if (archive_getline(reader, &buf) > 0)
        xstrtol(buf, data);
}

void read_desc(struct archive *archive, struct pkg *pkg)
{
    struct archive_reader *reader = archive_reader_new(archive);

    /* char buf[entry_size]; */
    char buf[8192];

    /* FIXME: check -1 might not be the best here. need actual rc */
    while(archive_fgets(reader, buf, sizeof(buf)) != -1) {
        if (strcmp(buf, "%FILENAME%") == 0) {
            read_desc_entry(reader, &pkg->filename);
        } else if (strcmp(buf, "%NAME%") == 0) {
            read_desc_entry(reader, &pkg->name);
            /* read_desc_entry(reader, buf, entry_size, &temp); */
            /* if (strcmp(temp, pkg->name) != 0) */
            /*     errx(EXIT_FAILURE, "database entry name and desc record are mismatched!"); */
            /* free(temp); */
        } else if (strcmp(buf, "%BASE%") == 0) {
            read_desc_entry(reader, &pkg->base);
        } else if (strcmp(buf, "%VERSION%") == 0) {
            read_desc_entry(reader, &pkg->version);
            /* read_desc_entry(reader, buf, entry_size, &temp); */
            /* if (strcmp(temp, pkg->version) != 0) */
            /*     errx(EXIT_FAILURE, "database entry version and desc record are mismatched!"); */
            /* free(temp); */
        } else if (strcmp(buf, "%DESC%") == 0) {
            read_desc_entry(reader, &pkg->desc);
        } else if (strcmp(buf, "%GROUPS%") == 0) {
            read_desc_list(reader, &pkg->groups);
        } else if (strcmp(buf, "%CSIZE%") == 0) {
            read_desc_ulong(reader, &pkg->size);
        } else if (strcmp(buf, "%ISIZE%") == 0) {
            read_desc_ulong(reader, &pkg->isize);
        } else if (strcmp(buf, "%MD5SUM%") == 0) {
            read_desc_entry(reader, &pkg->md5sum);
        } else if (strcmp(buf, "%SHA256SUM%") == 0) {
            read_desc_entry(reader, &pkg->sha256sum);
        } else if(strcmp(buf, "%PGPSIG%") == 0) {
            read_desc_entry(reader, &pkg->base64sig);
        } else if (strcmp(buf, "%URL%") == 0) {
            read_desc_entry(reader, &pkg->url);
        } else if (strcmp(buf, "%LICENSE%") == 0) {
            read_desc_list(reader, &pkg->licenses);
        } else if (strcmp(buf, "%ARCH%") == 0) {
            read_desc_entry(reader, &pkg->arch);
        } else if (strcmp(buf, "%BUILDDATE%") == 0) {
            read_desc_long(reader, &pkg->builddate);
        } else if (strcmp(buf, "%PACKAGER%") == 0) {
            read_desc_entry(reader, &pkg->packager);
        } else if (strcmp(buf, "%REPLACES%") == 0) {
            read_desc_list(reader, &pkg->replaces);
        } else if (strcmp(buf, "%DEPENDS%") == 0) {
            read_desc_list(reader, &pkg->depends);
        } else if (strcmp(buf, "%CONFLICTS%") == 0) {
            read_desc_list(reader, &pkg->conflicts);
        } else if (strcmp(buf, "%PROVIDES%") == 0) {
            read_desc_list(reader, &pkg->provides);
        } else if (strcmp(buf, "%OPTDEPENDS%") == 0) {
            read_desc_list(reader, &pkg->optdepends);
        } else if (strcmp(buf, "%MAKEDEPENDS%") == 0) {
            read_desc_list(reader, &pkg->makedepends);
        } else if (strcmp(buf, "%CHECKDEPENDS%") == 0) {
            read_desc_list(reader, &pkg->checkdepends);
        } else if (strcmp(buf, "%FILES%") == 0) {
            read_desc_list(reader, &pkg->files);
        }

        /* free(buf); */
    }

    free(reader);
}
