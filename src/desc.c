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

static inline int read_desc_list(struct archive_reader *reader, alpm_list_t **list)
{
    char *buf;
    int nbytes_r;
    while ((nbytes_r = archive_getline(reader, &buf)) > 0)
        *list = alpm_list_add(*list, buf);
    return nbytes_r;
}

static inline int read_desc_entry(struct archive_reader *reader, char **data)
{
    return archive_getline(reader, data);
}

static inline int read_desc_size(struct archive_reader *reader, size_t *data)
{
    _cleanup_free_ char *buf;
    int nbytes_r = archive_getline(reader, &buf);
    if (nbytes_r > 0)
        fromstr(buf, data);
    return nbytes_r;
}

static inline int read_desc_time(struct archive_reader *reader, time_t *data)
{
    _cleanup_free_ char *buf;
    int nbytes_r = archive_getline(reader, &buf);
    if (nbytes_r > 0)
        fromstr(buf, data);
    return nbytes_r;
}

void read_desc(struct archive *archive, struct pkg *pkg)
{
    struct archive_reader *reader = archive_reader_new(archive);

    for (;;) {
        char buf[8192];
        int nbytes_r = archive_fgets(reader, buf, sizeof(buf));
        if (nbytes_r < 0)
            break;

        if (streq(buf, "%FILENAME%")) {
            nbytes_r = read_desc_entry(reader, &pkg->filename);
        } else if (streq(buf, "%NAME%")) {
            _cleanup_free_ char *temp = NULL;
            nbytes_r = read_desc_entry(reader, &temp);
            if (!streq(temp, pkg->name))
                errx(EXIT_FAILURE, "database entry %%NAME%% and desc record are mismatched!");
        } else if (streq(buf, "%BASE%")) {
            nbytes_r = read_desc_entry(reader, &pkg->base);
        } else if (streq(buf, "%VERSION%")) {
            _cleanup_free_ char *temp = NULL;
            nbytes_r = read_desc_entry(reader, &temp);
            if (!streq(temp, pkg->version))
                errx(EXIT_FAILURE, "database entry %%VERSION%% and desc record are mismatched!");
        } else if (streq(buf, "%DESC%")) {
            nbytes_r = read_desc_entry(reader, &pkg->desc);
        } else if (streq(buf, "%GROUPS%")) {
            nbytes_r = read_desc_list(reader, &pkg->groups);
        } else if (streq(buf, "%CSIZE%")) {
            nbytes_r = read_desc_size(reader, &pkg->size);
        } else if (streq(buf, "%ISIZE%")) {
            nbytes_r = read_desc_size(reader, &pkg->isize);
        } else if (streq(buf, "%MD5SUM%")) {
            nbytes_r = read_desc_entry(reader, &pkg->md5sum);
        } else if (streq(buf, "%SHA256SUM%")) {
            nbytes_r = read_desc_entry(reader, &pkg->sha256sum);
        } else if(streq(buf, "%PGPSIG%")) {
            nbytes_r = read_desc_entry(reader, &pkg->base64sig);
        } else if (streq(buf, "%URL%")) {
            nbytes_r = read_desc_entry(reader, &pkg->url);
        } else if (streq(buf, "%LICENSE%")) {
            nbytes_r = read_desc_list(reader, &pkg->licenses);
        } else if (streq(buf, "%ARCH%")) {
            nbytes_r = read_desc_entry(reader, &pkg->arch);
        } else if (streq(buf, "%BUILDDATE%")) {
            nbytes_r = read_desc_time(reader, &pkg->builddate);
        } else if (streq(buf, "%PACKAGER%")) {
            nbytes_r = read_desc_entry(reader, &pkg->packager);
        } else if (streq(buf, "%REPLACES%")) {
            nbytes_r = read_desc_list(reader, &pkg->replaces);
        } else if (streq(buf, "%DEPENDS%")) {
            nbytes_r = read_desc_list(reader, &pkg->depends);
        } else if (streq(buf, "%CONFLICTS%")) {
            nbytes_r = read_desc_list(reader, &pkg->conflicts);
        } else if (streq(buf, "%PROVIDES%")) {
            nbytes_r = read_desc_list(reader, &pkg->provides);
        } else if (streq(buf, "%OPTDEPENDS%")) {
            nbytes_r = read_desc_list(reader, &pkg->optdepends);
        } else if (streq(buf, "%MAKEDEPENDS%")) {
            nbytes_r = read_desc_list(reader, &pkg->makedepends);
        } else if (streq(buf, "%CHECKDEPENDS%")) {
            nbytes_r = read_desc_list(reader, &pkg->checkdepends);
        } else if (streq(buf, "%FILES%")) {
            nbytes_r = read_desc_list(reader, &pkg->files);
        }

        if (nbytes_r < 0)
            break;
    }

    free(reader);
}
