#include "package.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <err.h>
#include <archive.h>
#include <archive_entry.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "util.h"
#include "pkginfo.h"
#include "pkgcache.h"
#include "base64.h"

int load_package(pkg_t *pkg, int fd)
{
    struct archive *archive;
    struct stat st;

    check_posix(fstat(fd, &st), "failed to stat file");

    archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);

    if (archive_read_open_fd(archive, fd, 8192) != ARCHIVE_OK) {
        archive_read_free(archive);
        return -1;
    }

    bool found_pkginfo = false;
    struct archive_entry *entry;
    while (archive_read_next_header(archive, &entry) == ARCHIVE_OK && !found_pkginfo) {
        const char *entry_name = archive_entry_pathname(entry);
        const mode_t mode = archive_entry_mode(entry);

        if (S_ISREG(mode) && streq(entry_name, ".PKGINFO")) {
            if (read_pkginfo(archive, pkg) < 0) {
                errx(EXIT_FAILURE, "failed to parse PKGINFO");
            }
            found_pkginfo = true;
        }
    }

    archive_read_close(archive);
    archive_read_free(archive);

    if (found_pkginfo) {
        pkg->hash = sdbm(pkg->name);
        pkg->size = st.st_size;
        pkg->mtime = st.st_mtime;
        return 0;
    }

    return -1;
}

int load_package_signature(struct pkg *pkg, int dirfd)
{
    _cleanup_free_ char *signame = joinstring(pkg->filename, ".sig", NULL);
    _cleanup_close_ int fd = openat(dirfd, signame, O_RDONLY);
    if (fd < 0)
        return -1;

    struct stat st;
    check_posix(fstat(fd, &st), "failed to stat signature");

    _cleanup_free_ char *signature = malloc(st.st_size);
    check_posix(read(fd, signature, st.st_size), "failed to read signature");

    pkg->base64sig = base64_encode((const unsigned char *)signature,
                                   st.st_size, NULL);
    check_null(pkg->base64sig, "failed to find base64 signature");

    // If the signature's timestamp is new than the packages, update
    // it to the newer value.
    if (st.st_mtime > pkg->mtime)
        pkg->mtime = st.st_mtime;

    return 0;
}

int load_package_files(struct pkg *pkg, int fd)
{
    struct archive *archive;
    struct stat st;

    check_posix(fstat(fd, &st), "failed to stat file");

    archive = archive_read_new();
    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);

    if (archive_read_open_fd(archive, fd, 8192) != ARCHIVE_OK) {
        archive_read_free(archive);
        return -1;
    }

    struct archive_entry *entry;
    while (archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
        const char *entry_name = archive_entry_pathname(entry);

        if (entry_name[0] != '.')
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

static void pkg_append_list(const char *entry, size_t len, alpm_list_t **list)
{
    *list = alpm_list_add(*list, strndup(entry, len));
}

static void pkg_set_string(const char *entry, size_t len, char **data)
{
    free(*data);
    *data = strndup(entry, len);
}

static void pkg_set_size(const char *entry, size_t _unused_ len, size_t *data)
{
    parse_size(entry, data);
}

static void pkg_set_time(const char *entry, size_t _unused_ len, time_t *data)
{
    parse_time(entry, data);
}

#define pkg_set(entry, len, field) _Generic((field), \
    alpm_list_t **: pkg_append_list, \
    char **: pkg_set_string, \
    size_t *: pkg_set_size, \
    time_t *: pkg_set_time)(entry, len, field)

void package_set(pkg_t *pkg, enum pkg_entry type, const char *entry, size_t len)
{
    switch (type) {
    case PKG_FILENAME:
        pkg_set(entry, len, &pkg->filename);
        break;
    case PKG_PKGNAME:
        if (!pkg->name) {
            pkg_set(entry, len, &pkg->name);
        } else if (!strneq(entry, pkg->name, len)) {
            errx(EXIT_FAILURE, "database entry %%NAME%% and desc record are mismatched!");
        }
        break;
    case PKG_PKGBASE:
        pkg_set(entry, len, &pkg->base);
        break;
    case PKG_VERSION:
        if (!pkg->version) {
            pkg_set(entry, len, &pkg->version);
        } else if (!strneq(entry, pkg->version, len)) {
            errx(EXIT_FAILURE, "database entry %%VERSION%% and desc record are mismatched!");
        }
        break;
    case PKG_DESCRIPTION:
        pkg_set(entry, len, &pkg->desc);
        break;
    case PKG_GROUPS:
        pkg_set(entry, len, &pkg->groups);
        break;
    case PKG_CSIZE:
        pkg_set(entry, len, &pkg->size);
        break;
    case PKG_ISIZE:
        pkg_set(entry, len, &pkg->isize);
        break;
    case PKG_SHA256SUM:
        pkg_set(entry, len, &pkg->sha256sum);
        break;
    case PKG_PGPSIG:
        pkg_set(entry, len, &pkg->base64sig);
        break;
    case PKG_URL:
        pkg_set(entry, len, &pkg->url);
        break;
    case PKG_LICENSE:
        pkg_set(entry, len, &pkg->licenses);
        break;
    case PKG_ARCH:
        pkg_set(entry, len, &pkg->arch);
        break;
    case PKG_BUILDDATE:
        pkg_set(entry, len, &pkg->builddate);
        break;
    case PKG_PACKAGER:
        pkg_set(entry, len, &pkg->packager);
        break;
    case PKG_REPLACES:
        pkg_set(entry, len, &pkg->replaces);
        break;
    case PKG_DEPENDS:
        pkg_set(entry, len, &pkg->depends);
        break;
    case PKG_CONFLICTS:
        pkg_set(entry, len, &pkg->conflicts);
        break;
    case PKG_PROVIDES:
        pkg_set(entry, len, &pkg->provides);
        break;
    case PKG_OPTDEPENDS:
        pkg_set(entry, len, &pkg->optdepends);
        break;
    case PKG_MAKEDEPENDS:
        pkg_set(entry, len, &pkg->makedepends);
        break;
    case PKG_CHECKDEPENDS:
        pkg_set(entry, len, &pkg->checkdepends);
        break;
    case PKG_FILES:
        pkg_set(entry, len, &pkg->files);
        break;
    case PKG_DELTAS:
        pkg_set(entry, len, &pkg->deltas);
        break;
    default:
        break;
    }
}
