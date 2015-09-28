#include "package.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <err.h>
#include <archive.h>
#include <archive_entry.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "file.h"
#include "util.h"
#include "reader.h"
#include "pkghash.h"
#include "base64.h"

static void pkginfo_assignment(const char *key, const char *value, pkg_t *pkg)
{
    if (streq(key, "pkgname"))
        pkg->name = strdup(value);
    else if (streq(key, "pkgbase"))
        pkg->base = strdup(value);
    else if (streq(key, "pkgver"))
        pkg->version = strdup(value);
    else if (streq(key, "pkgdesc"))
        pkg->desc = strdup(value);
    else if (streq(key, "url"))
        pkg->url = strdup(value);
    else if (streq(key, "builddate"))
        fromstr(value, &pkg->builddate);
    else if (streq(key, "packager"))
        pkg->packager = strdup(value);
    else if (streq(key, "size"))
        fromstr(value, &pkg->isize);
    else if (streq(key, "arch"))
        pkg->arch = strdup(value);
    else if (streq(key, "group"))
        pkg->groups = alpm_list_add(pkg->groups, strdup(value));
    else if (streq(key, "license"))
        pkg->licenses = alpm_list_add(pkg->licenses, strdup(value));
    else if (streq(key, "replaces"))
        pkg->replaces = alpm_list_add(pkg->replaces, strdup(value));
    else if (streq(key, "depend"))
        pkg->depends = alpm_list_add(pkg->depends, strdup(value));
    else if (streq(key, "conflict"))
        pkg->conflicts = alpm_list_add(pkg->conflicts, strdup(value));
    else if (streq(key, "provides"))
        pkg->provides = alpm_list_add(pkg->provides, strdup(value));
    else if (streq(key, "optdepend"))
        pkg->optdepends = alpm_list_add(pkg->optdepends, strdup(value));
    else if (streq(key, "makedepend"))
        pkg->makedepends = alpm_list_add(pkg->makedepends, strdup(value));
    else if (streq(key, "checkdepend"))
        pkg->checkdepends = alpm_list_add(pkg->checkdepends, strdup(value));
}

static void read_pkginfo(struct archive *archive, pkg_t *pkg)
{
    _cleanup_free_ struct archive_reader *reader = archive_reader_new(archive);
    ssize_t nbytes_r = 0;
    char line[LINE_MAX];

    for (;;) {
        nbytes_r = archive_fgets(reader, line, sizeof(line));
        if (nbytes_r < 0)
            break;

        if (nbytes_r == 0)
            continue;
        nbytes_r = strcspn(line, "#");
        if (nbytes_r == 0)
            continue;

        line[nbytes_r] = '\0';
        char *e = memchr(line, '=', nbytes_r);
        if (!e)
            errx(EXIT_FAILURE, "failed to find '='");

        *e++ = 0;
        pkginfo_assignment(strstrip(line), strstrip(e), pkg);
    }
}

int load_package(pkg_t *pkg, int fd)
{
    struct archive *archive;
    struct file_t file;

    if (file_from_fd(&file, fd) < 0) {
        return -1;
    }

    archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);

    if (archive_read_open_memory(archive, file.mmap, file.st.st_size) != ARCHIVE_OK) {
        archive_read_free(archive);
        return -1;
    }

    bool found_pkginfo = false;
    struct archive_entry *entry;
    while (archive_read_next_header(archive, &entry) == ARCHIVE_OK && !found_pkginfo) {
        const char *entry_name = archive_entry_pathname(entry);
        const mode_t mode = archive_entry_mode(entry);

        if (S_ISREG(mode) && streq(entry_name, ".PKGINFO")) {
            read_pkginfo(archive, pkg);
            found_pkginfo = true;
        }
    }

    archive_read_close(archive);
    archive_read_free(archive);

    if (found_pkginfo) {
        pkg->size = file.st.st_size;
        pkg->mtime = file.st.st_mtime;
        pkg->name_hash = _alpm_hash_sdbm(pkg->name);
        return 0;
    }

    return -1;
}

int load_package_signature(struct pkg *pkg, int dirfd)
{
    struct file_t file;
    _cleanup_free_ char *signame = joinstring(pkg->filename, ".sig", NULL);
    _cleanup_close_ int fd = openat(dirfd, signame, O_RDONLY);
    if (fd < 0)
        return -1;

    if (file_from_fd(&file, fd) < 0)
        return -1;

    base64_encode((unsigned char **)&pkg->base64sig,
                  (const unsigned char *)file.mmap, file.st.st_size);

    // If the signature's timestamp is new than the packages, update
    // it to the newer value.
    if (file.st.st_mtime > pkg->mtime)
        pkg->mtime = file.st.st_mtime;

    file_close(&file);
    return 0;
}

static bool is_package_metadata(const char *entry_name) {
    static const char *metadata_names[] = {
        ".PKGINFO",
        ".CHANGELOG",
        ".MTREE",
        ".INSTALL",
        NULL
    };

    if (entry_name[0] != '.')
        return false;

    for (const char **n = metadata_names; *n; ++n) {
        if (streq(entry_name, *n))
            return true;
    }

    return false;
}

int load_package_files(struct pkg *pkg, int fd)
{
    struct archive *archive;
    struct file_t file;

    if (file_from_fd(&file, fd) < 0)
        return -1;

    archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);

    if (archive_read_open_memory(archive, file.mmap, file.st.st_size) != ARCHIVE_OK) {
        archive_read_free(archive);
        return -1;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
        const char *entry_name = archive_entry_pathname(entry);

        if (!is_package_metadata(entry_name))
            pkg->files = alpm_list_add(pkg->files, strdup(entry_name));
    }

    archive_read_close(archive);
    archive_read_free(archive);
    return 0;
}

void package_free(pkg_t *pkg)
{
    free(pkg->filename);
    free(pkg->name);
    free(pkg->version);
    free(pkg->desc);
    free(pkg->url);
    free(pkg->packager);
    free(pkg->md5sum);
    free(pkg->sha256sum);
    free(pkg->base64sig);
    free(pkg->arch);

    alpm_list_free_inner(pkg->groups, free);
    alpm_list_free(pkg->groups);
    alpm_list_free_inner(pkg->licenses, free);
    alpm_list_free(pkg->licenses);
    alpm_list_free_inner(pkg->depends, free);
    alpm_list_free(pkg->depends);
    alpm_list_free_inner(pkg->conflicts, free);
    alpm_list_free(pkg->conflicts);
    alpm_list_free_inner(pkg->provides, free);
    alpm_list_free(pkg->provides);
    alpm_list_free_inner(pkg->optdepends, free);
    alpm_list_free(pkg->optdepends);
    alpm_list_free_inner(pkg->makedepends, free);
    alpm_list_free(pkg->makedepends);
    alpm_list_free_inner(pkg->files, free);
    alpm_list_free(pkg->files);

    free(pkg);
}

