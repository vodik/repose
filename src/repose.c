#include "repose.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <archive.h>
#include <archive_entry.h>
#include <alpm_list.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/btrfs.h>

#include "database.h"
#include "filecache.h"
#include "pkghash.h"
#include "filters.h"
#include "signing.h"
#include "base64.h"
#include "util.h"

struct config config = {0};

static _noreturn_ void usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <database> [pkgs|deltas ...]\n", program_invocation_short_name);
    fputs("Options\n"
          " -h, --help            display this help and exit\n"
          " -V, --version         display version\n"
          " -v, --verbose         verbose output\n"
          " -f, --files           also build the .files database\n"
          " -l, --list            list packages in the repository\n"
          " -d, --drop            drop the specified package from the db\n"
          " -r, --root=PATH       set the root for the repository\n"
          " -p, --pool=PATH       set the pool to find packages in\n"
          " -m, --arch=ARCH       the architecture of the database\n"
          " -j, --bzip2           filter the archive through bzip2\n"
          " -J, --xz              filter the archive through xz\n"
          " -z, --gzip            filter the archive through gzip\n"
          " -Z, --compress        filter the archive through compress\n"
          "     --reflink         make repose make reflinks instead of symlinks\n"
          "     --rebuild         force rebuild the repo\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static _noreturn_ void elephant(void)
{
    static const char *big_elephant =
        "ICAgICBfXwogICAgJy4gXAogICAgICctIFwKICAgICAgLyAvXyAgICAgICAgIC4tLS0uCiAgICAg"
        "LyB8IFxcLC5cLy0tLi8vICAgICkKICAgICB8ICBcLy8gICAgICAgICkvICAvCiAgICAgIFwgICcg"
        "XiBeICAgIC8gICAgKV9fX18uLS0tLS4uICA2CiAgICAgICAnLl9fX18uICAgIC5fX18vICAgICAg"
        "ICAgICAgXC5fKQogICAgICAgICAgLlwvLiAgICAgICAgICAgICAgICAgICAgICApCiAgICAgICAg"
        "ICAgJ1wgICAgICAgICAgICAgICAgICAgICAgIC8KICAgICAgICAgICBfLyBcLyAgICApLiAgICAg"
        "ICAgKSAgICAoCiAgICAgICAgICAvIyAgLiEgICAgfCAgICAgICAgL1wgICAgLwogICAgICAgICAg"
        "XCAgQy8vICMgIC8nLS0tLS0nJy8gIyAgLwogICAgICAgLiAgICdDLyB8ICAgIHwgICAgfCAgIHwg"
        "ICAgfG1yZiAgLAogICAgICAgXCksIC4uIC4nT09PLScuIC4uJ09PTydPT08tJy4gLi5cKCw=";

    static const char *small_elephant =
        "ICAgIF8gICAgXwogICAvIFxfXy8gXF9fX19fCiAgLyAgLyAgXCAgXCAgICBgXAogICkgIFwnJy8g"
        "ICggICAgIHxcCiAgYFxfXykvX18vJ19cICAvIGAKICAgICAvL198X3x+fF98X3wKICAgICBeIiIn"
        "IicgIiInIic=";

    unsigned char *data = NULL;

    switch (srand(time(NULL)), rand() % 2) {
    case 0:
        base64_decode(&data, (const unsigned char *)big_elephant, strlen(big_elephant));
        break;
    case 1:
        base64_decode(&data, (const unsigned char *)small_elephant, strlen(small_elephant));
        break;
    default:
        errx(EXIT_FAILURE, "failed to find elephant");
        break;
    }

    puts((char *)data);
    exit(EXIT_SUCCESS);
}

static inline int clone_pkg(const struct repo *repo, const struct pkg *pkg)
{
    _cleanup_close_ int src = openat(repo->poolfd, pkg->filename, O_RDONLY);
    if (src < 0)
        err(1, "failed to open pool package %s\n", pkg->filename);

    _cleanup_close_ int dest = openat(repo->rootfd, pkg->filename, O_WRONLY | O_TRUNC, 0664);
    if (dest < 0 && errno == ENOENT)
        dest = openat(repo->rootfd, pkg->filename, O_WRONLY | O_CREAT, 0664);
    if (dest < 0)
        err(1, "failed to open repo package %s", pkg->filename);

    return ioctl(dest, BTRFS_IOC_CLONE, src);
}

static inline int symlink_pkg(const struct repo *repo, const struct pkg *pkg)
{
    _cleanup_free_ char *link = joinstring(repo->pool, "/", pkg->filename, NULL);

    int ret = symlinkat(link, repo->rootfd, pkg->filename);
    if (ret < 0 && errno == EEXIST)
        return 0;
    return ret;
}

static inline void link_pkg(const struct repo *repo, const struct pkg *pkg)
{
    if (config.reflink) {
        check_posix(clone_pkg(repo, pkg),
                    "failed to make reflink for %s", pkg->filename);
    } else {
        check_posix(symlink_pkg(repo, pkg),
                    "failed to make symlink for %s", pkg->filename);
    }
}

static inline int unlink_pkg(const struct repo *repo, const struct pkg *pkg)
{
    struct stat st;
    if (fstatat(repo->rootfd, pkg->filename, &st, AT_SYMLINK_NOFOLLOW) < 0)
        return errno != ENOENT ? -1 : 0;
    if (S_ISLNK(st.st_mode))
        return unlinkat(repo->rootfd, pkg->filename, 0);
    return 0;
}

static void link_db(struct repo *repo)
{
    if (!repo->pool)
        return;

    alpm_list_t *node;
    for (node = repo->cache->list; node; node = node->next)
        link_pkg(repo, node->data);
}

static void drop_from_repo(struct repo *repo, alpm_list_t *targets)
{
    if (!targets || !repo->cache)
        return;

    alpm_list_t *node;
    for (node = repo->cache->list; node; node = node->next) {
        struct pkg *pkg = node->data;

        if (match_targets(pkg, targets)) {
            trace("dropping %s\n", pkg->name);

            repo->cache = _alpm_pkghash_remove(repo->cache, pkg, NULL);
            unlink_pkg(repo, pkg);
            package_free(pkg);
            repo->dirty = true;
        }
    }
}

static void list_repo(struct repo *repo)
{
    alpm_list_t *node;
    for (node = repo->cache->list; node; node = node->next) {
        struct pkg *pkg = node->data;

        printf("%s %s\n", pkg->name, pkg->version);
    }
}

static void reduce_repo(struct repo *repo)
{
    if (!repo->cache)
        return;

    alpm_list_t *node;
    for (node = repo->cache->list; node; node = node->next) {
        struct pkg *pkg = node->data;

        if (faccessat(repo->poolfd, pkg->filename, F_OK, 0) < 0) {
            if (errno != ENOENT)
                err(EXIT_FAILURE, "couldn't access package %s", pkg->filename);

            trace("dropping %s\n", pkg->name);
            repo->cache = _alpm_pkghash_remove(repo->cache, pkg, NULL);
            unlink_pkg(repo, pkg);
            package_free(pkg);
            repo->dirty = true;
        }
    }
}

static void update_repo(struct repo *repo, alpm_pkghash_t *src)
{
    if (!repo->cache)
        repo->cache = _alpm_pkghash_create(src->entries);

    alpm_list_t *node;
    for (node = src->list; node; node = node->next) {
        struct pkg *pkg = node->data;
        struct pkg *old = _alpm_pkghash_find(repo->cache, pkg->name);

        if (!old) {
            /* The package isn't already in the database. Just add it */
            trace("adding %s %s\n", pkg->name, pkg->version);
            repo->cache = _alpm_pkghash_add(repo->cache, pkg);
            repo->dirty = true;
            continue;
        }

        switch(alpm_pkg_vercmp(pkg->version, old->version)) {
        case 1:
            /* The filecache package has a newer version than the
               package in the database. */
            trace("updating %s %s => %s\n", pkg->name, old->version, pkg->version);
            break;
        case 0:
            /* The filecache package has the same version as the
               package in the database. Only update the package if the
               file is newer than the database */
            if (pkg->mtime > old->mtime) {
                trace("updating %s %s [newer timestamp]\n", pkg->name, pkg->version);
            } else if (pkg->builddate > old->builddate) {
                trace("updating %s %s [newer build]\n", pkg->name, pkg->version);
            } else if (old->base64sig == NULL && pkg->base64sig) {
                trace("adding signature for %s\n", pkg->name);
            } else {
                continue;
            }
            break;
        default:
            continue;
        }

        repo->cache = _alpm_pkghash_replace(repo->cache, pkg, old);
        unlink_pkg(repo, pkg);
        package_free(old);
        repo->dirty = true;
    }
}

static alpm_list_t *parse_targets(char *targets[], int count)
{
    int i;
    alpm_list_t *list = NULL;
    for (i = 0; i < count; ++i)
        list = alpm_list_add(list, targets[i]);
    return list;
}

static int load_db(struct repo *repo, const char *filename)
{
    _cleanup_close_ int dbfd = openat(repo->rootfd, filename, O_RDONLY);
    if (dbfd < 0) {
        if (errno != ENOENT)
            err(EXIT_FAILURE, "failed to open database %s", filename);
        return -1;
    }

    if (load_database(dbfd, &repo->cache) < 0) {
        warn("failed to open %s database", filename);
        return -1;
    }

    return 0;
}

static void check_signature(struct repo *repo, const char *name)
{
    _cleanup_free_ char *sig = joinstring(name, ".sig", NULL);

    if (faccessat(repo->rootfd, sig, F_OK, 0) == 0) {
        if (gpgme_verify(repo->rootfd, name) < 0) {
            errx(EXIT_FAILURE, "repo signature is invalid or corrupt!");
        } else {
            trace("found a valid signature, will resign...\n");
            config.sign = true;
        }
    } else if (errno != ENOENT) {
        err(EXIT_FAILURE, "countn't access %s", name);
    }
}

static int init_repo(struct repo *repo, const char *reponame, bool files,
                     bool load_cache)
{
    repo->rootfd = open(repo->root, O_RDONLY | O_DIRECTORY);
    check_posix(repo->rootfd, "failed to open root directory %s", repo->root);

    if (repo->pool) {
        repo->poolfd = open(repo->pool, O_RDONLY | O_DIRECTORY);
        check_posix(repo->poolfd, "failed to open pool directory %s", repo->pool);
    } else {
        repo->poolfd = repo->rootfd;
    }

    repo->dbname = joinstring(reponame, ".db", NULL);
    repo->filesname = joinstring(reponame, ".files", NULL);

    if (!files && faccessat(repo->rootfd, repo->filesname, F_OK, 0) < 0) {
        if (errno == ENOENT) {
            free(repo->filesname);
            repo->filesname = NULL;
        } else {
            err(EXIT_FAILURE, "couldn't access %s", repo->filesname);
        }
    }

    if (config.sign) {
        check_signature(repo, repo->dbname);
        check_signature(repo, repo->filesname);
    }

    if (load_cache) {
        repo->cache = _alpm_pkghash_create(100);

        if (load_db(repo, repo->dbname) < 0)
            return -1;
        if (repo->filesname)
            return load_db(repo, repo->filesname);
    }

    return 0;
}

static alpm_list_t *load_manifest(struct repo *repo, const char *reponame)
{
    _cleanup_free_ char *manifest = joinstring(reponame, ".manifest", NULL);
    _cleanup_fclose_ FILE *fp = fopenat(repo->rootfd, manifest, "r");
    if (fp == NULL)
        return NULL;

    alpm_list_t *list = NULL;
    for (;;) {
        errno = 0;
        char *line = NULL;
        ssize_t nbytes_r = getline(&line, &(size_t){ 0 }, fp);
        if (nbytes_r < 0) {
            if (errno != 0)
                err(EXIT_FAILURE, "Failed to read manifest file");
            break;
        }

        line[nbytes_r - 1] = 0;
        if (line && line[0])
            list = alpm_list_add(list, line);
    }
    return list;
}

static char *get_rootname(char *name)
{
    char *sep = strrchr(name, '.');
    if (sep && streq(sep, ".db"))
        *sep = 0;
    return name;
}

int main(int argc, char *argv[])
{
    const char *rootname;
    const char *arch = NULL;
    bool files = false, rebuild = false, drop = false, list = false;

    static const struct option opts[] = {
        { "help",     no_argument,       0, 'h' },
        { "version",  no_argument,       0, 'V' },
        { "drop",     no_argument,       0, 'd' },
        { "list",     no_argument,       0, 'l' },
        { "verbose",  no_argument,       0, 'v' },
        { "files",    no_argument,       0, 'f' },
        { "sign",     no_argument,       0, 's' },
        { "root",     required_argument, 0, 'r' },
        { "pool",     required_argument, 0, 'p' },
        { "arch",     required_argument, 0, 'm' },
        { "bzip2",    no_argument,       0, 'j' },
        { "xz",       no_argument,       0, 'J' },
        { "gzip",     no_argument,       0, 'z' },
        { "compress", no_argument,       0, 'Z' },
        { "reflink",  no_argument,       0, 0x100 },
        { "rebuild",  no_argument,       0, 0x101 },
        { "elephant", no_argument,       0, 0x102 },
        { 0, 0, 0, 0 }
    };

    struct repo repo = { .root = "." };

    for (;;) {
        int opt = getopt_long(argc, argv, "hVvdlfsr:p:m:jJzZ", opts, NULL);
        if (opt < 0)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'V':
            printf("%s %s\n",  program_invocation_short_name, REPOSE_VERSION);
            exit(EXIT_SUCCESS);
        case 'v':
            config.verbose += 1;
            break;
        case 'd':
            drop = true;
            break;
        case 'l':
            list = true;
            break;
        case 'f':
            files = true;
            break;
        case 's':
            config.sign = true;
            break;
        case 'r':
            repo.root = optarg;
            break;
        case 'p':
            repo.pool = optarg;
            break;
        case 'm':
            arch = optarg;
            break;
        case 'j':
            config.compression = ARCHIVE_FILTER_BZIP2;
            break;
        case 'J':
            config.compression = ARCHIVE_FILTER_XZ;
            break;
        case 'z':
            config.compression = ARCHIVE_FILTER_GZIP;
            break;
        case 'Z':
            config.compression = ARCHIVE_FILTER_COMPRESS;
            break;
        case 0x100:
            config.reflink = true;
            break;
        case 0x101:
            rebuild = true;
            break;
        case 0x102:
            elephant();
            break;
        }
    }

    argv += optind;
    argc -= optind;

    if (argc == 0)
        errx(1, "incorrect number of arguments provided");

    if (!arch) {
        struct utsname uts;
        uname(&uts);
        arch = strdup(uts.machine);
    }

    if (list && drop)
        errx(EXIT_FAILURE, "List and drop operations are mutually exclusive");

    if (rebuild && (list || drop)) {
        fprintf(stderr, "Can't rebuild while performing a list or drop operation.\n"
                        "Ignoring the --rebuild flag.\n");
        rebuild = false;
    }

    rootname = get_rootname(*argv++), --argc;
    int ret = init_repo(&repo, rootname, files, !rebuild);
    if (list) {
        check_posix(ret, "failed to open database %s.db", rootname);
        list_repo(&repo);
        return 0;
    }

    alpm_list_t *targets = parse_targets(argv, argc);

    if (drop) {
        drop_from_repo(&repo, targets);
    } else {
        if (argc == 0) {
            targets = load_manifest(&repo, rootname);
        }

        alpm_pkghash_t *filecache = get_filecache(repo.poolfd, targets, arch);
        check_null(filecache, "failed to get filecache");

        reduce_repo(&repo);
        update_repo(&repo, filecache);
    }

    if (!repo.dirty) {
        trace("repo does not need updating\n");
    } else {
        write_database(&repo, repo.dbname, DB_DESC | DB_DEPENDS);

        if (repo.filesname) {
            write_database(&repo, repo.filesname, DB_FILES);
        }

        link_db(&repo);
    }
}
