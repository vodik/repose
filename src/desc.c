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
    while (archive_fgets(reader, buf, sizeof(buf)) != -1) {
        if (streq(buf, "%FILENAME%")) {
            read_desc_entry(reader, &pkg->filename);
        } else if (streq(buf, "%NAME%")) {
            _cleanup_free_ char *temp = NULL;
            read_desc_entry(reader, &temp);
            if (!streq(temp, pkg->name))
                errx(EXIT_FAILURE, "database entry %%NAME%% and desc record are mismatched!");
        } else if (streq(buf, "%BASE%")) {
            read_desc_entry(reader, &pkg->base);
        } else if (streq(buf, "%VERSION%")) {
            _cleanup_free_ char *temp = NULL;
            read_desc_entry(reader, &temp);
            if (!streq(temp, pkg->version))
                errx(EXIT_FAILURE, "database entry %%VERSION%% and desc record are mismatched!");
        } else if (streq(buf, "%DESC%")) {
            read_desc_entry(reader, &pkg->desc);
        } else if (streq(buf, "%GROUPS%")) {
            read_desc_list(reader, &pkg->groups);
        } else if (streq(buf, "%CSIZE%")) {
            read_desc_ulong(reader, &pkg->size);
        } else if (streq(buf, "%ISIZE%")) {
            read_desc_ulong(reader, &pkg->isize);
        } else if (streq(buf, "%MD5SUM%")) {
            read_desc_entry(reader, &pkg->md5sum);
        } else if (streq(buf, "%SHA256SUM%")) {
            read_desc_entry(reader, &pkg->sha256sum);
        } else if(streq(buf, "%PGPSIG%")) {
            read_desc_entry(reader, &pkg->base64sig);
        } else if (streq(buf, "%URL%")) {
            read_desc_entry(reader, &pkg->url);
        } else if (streq(buf, "%LICENSE%")) {
            read_desc_list(reader, &pkg->licenses);
        } else if (streq(buf, "%ARCH%")) {
            read_desc_entry(reader, &pkg->arch);
        } else if (streq(buf, "%BUILDDATE%")) {
            read_desc_long(reader, &pkg->builddate);
        } else if (streq(buf, "%PACKAGER%")) {
            read_desc_entry(reader, &pkg->packager);
        } else if (streq(buf, "%REPLACES%")) {
            read_desc_list(reader, &pkg->replaces);
        } else if (streq(buf, "%DEPENDS%")) {
            read_desc_list(reader, &pkg->depends);
        } else if (streq(buf, "%CONFLICTS%")) {
            read_desc_list(reader, &pkg->conflicts);
        } else if (streq(buf, "%PROVIDES%")) {
            read_desc_list(reader, &pkg->provides);
        } else if (streq(buf, "%OPTDEPENDS%")) {
            read_desc_list(reader, &pkg->optdepends);
        } else if (streq(buf, "%MAKEDEPENDS%")) {
            read_desc_list(reader, &pkg->makedepends);
        } else if (streq(buf, "%CHECKDEPENDS%")) {
            read_desc_list(reader, &pkg->checkdepends);
        } else if (streq(buf, "%FILES%")) {
            read_desc_list(reader, &pkg->files);
        }

        /* free(buf); */
    }

    free(reader);
}
