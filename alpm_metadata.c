#include "alpm_metadata.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <alpm.h>
#include <archive.h>
#include <archive_entry.h>
#include "archive_reader.h"
#include "pkghash.h"
#include "base64.h"

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

static void read_pkg_metadata(struct archive *archive, struct archive_entry *entry, alpm_pkg_meta_t *pkg)
{
    struct archive_reader *reader = archive_reader_new(archive);
    size_t entry_size = archive_entry_size(entry);
    char *line = malloc(entry_size);

    while (archive_fgets(reader, line, entry_size) > 0) {
        read_pkg_metadata_line(line, pkg);
    }

    free(reader);
    free(line);
}

/* XXX: massive hack. might want to memmap too */
static void read_pkg_signature(alpm_pkg_meta_t *pkg)
{
    char sig[PATH_MAX];
    snprintf(sig, PATH_MAX, "%s.sig", pkg->filename);

    int fd = open(sig, O_RDONLY);
    if (fd < 0)
        return;

    struct stat st;

    fstat(fd, &st);
    char sigbuf[st.st_size + 1];
    read(fd, sigbuf, st.st_size + 1);
    size_t len = st.st_size * 3 + 1;

    pkg->base64_sig = malloc(st.st_size * 3);
    base64_encode((unsigned char *)pkg->base64_sig, &len,
                  (const unsigned char *)sigbuf, st.st_size);
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
    pkg->name_hash = _alpm_hash_sdbm(pkg->name);
    pkg->size = st.st_size;

    read_pkg_signature(pkg);

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
    free(pkg->md5sum);
    free(pkg->sha256sum);
    free(pkg->base64_sig);
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

static inline void read_desc_list(struct archive_reader *reader, char *buf, size_t entry_size, alpm_list_t **list)
{
    for (;;) {
        int bytes_r = archive_fgets(reader, buf, entry_size);
        if (_alpm_strip_newline(buf, bytes_r) == 0)
            return;
        *list = alpm_list_add(*list, strdup(buf));
    }
}

static inline void read_desc_entry(struct archive_reader *reader, char *buf, size_t entry_size, char **data)
{
    archive_fgets(reader, buf, entry_size);
    *data = strdup(buf);
}

static int xstrtol(const char *str, long *out)
{
    char *end = NULL;

    if (str == NULL || *str == '\0')
        return -1;

    errno = 0;
    *out = strtol(str, &end, 10);
    if (errno || str == end || (end && *end))
        return -1;

    return 0;
}

static inline void read_desc_long(struct archive_reader *reader, char *buf, size_t entry_size, long *data)
{
    archive_fgets(reader, buf, entry_size);
    xstrtol(buf, data);
}

static void read_desc(struct archive_reader *reader, struct archive_entry *entry, alpm_pkg_meta_t *pkg)
{
    size_t entry_size = archive_entry_size(entry);
    char *buf = malloc(entry_size);

    /* TODO: check -1 might not be the best here. need actual rc */
    while(archive_fgets(reader, buf, entry_size) != -1) {
        if (strcmp(buf, "%FILENAME%") == 0) {
            read_desc_entry(reader, buf, entry_size, &pkg->filename);
        } else if (strcmp(buf, "%NAME%") == 0) {
            /* XXX: name should already be set, rather, validate it */
            read_desc_entry(reader, buf, entry_size, &pkg->name);
        } else if (strcmp(buf, "%VERSION%") == 0) {
            /* XXX: version should already be set, rather, validate it */
            read_desc_entry(reader, buf, entry_size, &pkg->version);
        } else if (strcmp(buf, "%DESC%") == 0) {
            read_desc_entry(reader, buf, entry_size, &pkg->desc);
        } else if (strcmp(buf, "%CSIZE%") == 0) {
            read_desc_long(reader, buf, entry_size, (long *)&pkg->size);
        } else if (strcmp(buf, "%ISIZE%") == 0) {
            read_desc_long(reader, buf, entry_size, (long *)&pkg->isize);
        } else if (strcmp(buf, "%MD5SUM%") == 0) {
            read_desc_entry(reader, buf, entry_size, &pkg->md5sum);
        } else if (strcmp(buf, "%SHA256SUM%") == 0) {
            read_desc_entry(reader, buf, entry_size, &pkg->sha256sum);
        } else if(strcmp(buf, "%PGPSIG%") == 0) {
            read_desc_entry(reader, buf, entry_size, &pkg->base64_sig);
        } else if (strcmp(buf, "%URL%") == 0) {
            read_desc_entry(reader, buf, entry_size, &pkg->url);
        } else if (strcmp(buf, "%LICENSE%") == 0) {
            read_desc_list(reader, buf, entry_size, &pkg->license);
        } else if (strcmp(buf, "%ARCH%") == 0) {
            read_desc_entry(reader, buf, entry_size, &pkg->arch);
        } else if (strcmp(buf, "%BUILDDATE%") == 0) {
            read_desc_long(reader, buf, entry_size, &pkg->builddate);
        } else if (strcmp(buf, "%PACKAGER%") == 0) {
            read_desc_entry(reader, buf, entry_size, &pkg->packager);
        } else if (strcmp(buf, "%DEPENDS%") == 0) {
            read_desc_list(reader, buf, entry_size, &pkg->depends);
        } else if (strcmp(buf, "%CONFLICTS%") == 0) {
            read_desc_list(reader, buf, entry_size, &pkg->conflicts);
        } else if (strcmp(buf, "%PROVIDES%") == 0) {
            read_desc_list(reader, buf, entry_size, &pkg->provides);
        } else if (strcmp(buf, "%OPTDEPENDS%") == 0) {
            read_desc_list(reader, buf, entry_size, &pkg->optdepends);
        } else if (strcmp(buf, "%MAKEDEPENDS%") == 0) {
            read_desc_list(reader, buf, entry_size, &pkg->makedepends);
        }
    }

    free(buf);
}

static int _alpm_splitname(const char *target, char **name, char **version,
		unsigned long *name_hash)
{
	/* the format of a db entry is as follows:
	 *    package-version-rel/
	 *    package-version-rel/desc (we ignore the filename portion)
	 * package name can contain hyphens, so parse from the back- go bact
	 * two hyphens and we have split the version from the name.
	 */
	const char *pkgver, *end;

	if(target == NULL) {
		return -1;
	}

	/* remove anything trailing a '/' */
	end = strchr(target, '/');
	if(!end) {
		end = target + strlen(target);
	}

	/* do the magic parsing- find the beginning of the version string
	 * by doing two iterations of same loop to lop off two hyphens */
	for(pkgver = end - 1; *pkgver && *pkgver != '-'; pkgver--);
	for(pkgver = pkgver - 1; *pkgver && *pkgver != '-'; pkgver--);
	if(*pkgver != '-' || pkgver == target) {
		return -1;
	}

	/* copy into fields and return */
	if(version) {
		if(*version) {
			free(*version);
		}
		/* version actually points to the dash, so need to increment 1 and account
		 * for potential end character */
		*version = strndup(pkgver + 1, end - pkgver - 1);
        if(!*version)
            return -1;
	}

	if(name) {
		if(*name) {
			free(*name);
		}
		*name = strndup(target, pkgver - target);
        if(!*name)
            return -1;
		if(name_hash) {
			*name_hash = _alpm_hash_sdbm(*name);
		}
	}

	return 0;
}

static alpm_pkg_meta_t *load_pkg_for_entry(alpm_db_meta_t *db, const char *entryname,
		const char **entry_filename, alpm_pkg_meta_t *likely_pkg)
{
	char *pkgname = NULL, *pkgver = NULL;
	unsigned long pkgname_hash;
	alpm_pkg_meta_t *pkg;

	/* get package and db file names */
	if(entry_filename) {
		char *fname = strrchr(entryname, '/');
		if(fname) {
			*entry_filename = fname + 1;
		} else {
			*entry_filename = NULL;
		}
	}
	if(_alpm_splitname(entryname, &pkgname, &pkgver, &pkgname_hash) != 0) {
		/* _alpm_log(db->handle, ALPM_LOG_ERROR, */
		/* 		_("invalid name for database entry '%s'\n"), entryname); */
		return NULL;
	}

	if(likely_pkg && pkgname_hash == likely_pkg->name_hash
			&& strcmp(likely_pkg->name, pkgname) == 0) {
		pkg = likely_pkg;
	} else {
		pkg = _alpm_pkghash_find(db->pkgcache, pkgname);
	}
	if(pkg == NULL) {
		pkg = calloc(1, sizeof(alpm_pkg_meta_t));
		if(pkg == NULL) {
			/* RET_ERR(db->handle, ALPM_ERR_MEMORY, NULL); */
            return NULL;
		}

        pkg->filename = strdup(entryname);
		pkg->name = pkgname;
		pkg->version = pkgver;
		pkg->name_hash = pkgname_hash;

		/* pkg->origin = ALPM_PKG_FROM_SYNCDB; */
		/* pkg->origin_data.db = db; */
		/* pkg->ops = &default_pkg_ops; */
		/* pkg->ops->get_validation = _sync_get_validation; */
		/* pkg->handle = db->handle; */

		/* add to the collection */
		/* _alpm_log(db->handle, ALPM_LOG_FUNCTION, "adding '%s' to package cache for db '%s'\n", */
		/* 		pkg->name, db->treename); */
		db->pkgcache = _alpm_pkghash_add_sorted(db->pkgcache, pkg);
	} else {
		free(pkgname);
		free(pkgver);
	}

    return pkg;
}

static void db_read_pkg(alpm_db_meta_t *db, struct archive_reader *reader,
                        struct archive_entry *entry)
{
    const char *entryname = archive_entry_pathname(entry);
    const char *filename;  //= strsep(&entryname, "/");
    /* if (entryname == NULL) */
    /*     return; */

    alpm_pkg_meta_t *pkg = load_pkg_for_entry(db, entryname, (const char **)&filename, NULL);

    if (strcmp(filename, "desc") == 0 || strcmp(filename, "depends") == 0) {
        read_desc(reader, entry, pkg);
    }

    /* free(filename); */
}

int alpm_db_populate(const char *filename, alpm_db_meta_t *db)
{
    struct archive *archive = NULL;
    struct archive_reader* reader = NULL;
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
    reader = archive_reader_new(archive);

    archive_read_support_filter_all(archive);
    archive_read_support_format_all(archive);

    int r = archive_read_open_memory(archive, memblock, st.st_size);
    if (r != ARCHIVE_OK) {
        warnx("%s is not an archive", filename);
        rc = -1;
        goto cleanup;
    }

    db->pkgcache = _alpm_pkghash_create(23);
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
        reader->ret = ARCHIVE_OK;
        db_read_pkg(db, reader, entry);
    }

cleanup:
    if (fd >= 0)
        close(fd);

    if (memblock != MAP_FAILED)
        munmap(memblock, st.st_size);

    if (archive) {
        archive_read_close(archive);
        archive_read_free(archive);
        free(reader);
    }

    return rc;
}
