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

static struct config config = {0};

static inline _printf_(1,2) void trace(const char *fmt, ...)
{
    if (config.verbose) {
        va_list ap;

        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
}

static _noreturn_ void usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <database> [pkgs|deltas ...]\n", program_invocation_short_name);
    fputs("Options\n"
          " -h, --help            display this help and exit\n"
          " -V, --version         display version\n"
          " -v, --verbose         verbose output\n"
          " -f, --files           also build the .files database\n"
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
    static const unsigned char big_elephant[] =
        "ICAgICBfXwogICAgJy4gXAogICAgICctIFwKICAgICAgLyAvXyAgICAgICAgIC4tLS0uCiAgICAg"
        "LyB8IFxcLC5cLy0tLi8vICAgICkKICAgICB8ICBcLy8gICAgICAgICkvICAvCiAgICAgIFwgICcg"
        "XiBeICAgIC8gICAgKV9fX18uLS0tLS4uICA2CiAgICAgICAnLl9fX18uICAgIC5fX18vICAgICAg"
        "ICAgICAgXC5fKQogICAgICAgICAgLlwvLiAgICAgICAgICAgICAgICAgICAgICApCiAgICAgICAg"
        "ICAgJ1wgICAgICAgICAgICAgICAgICAgICAgIC8KICAgICAgICAgICBfLyBcLyAgICApLiAgICAg"
        "ICAgKSAgICAoCiAgICAgICAgICAvIyAgLiEgICAgfCAgICAgICAgL1wgICAgLwogICAgICAgICAg"
        "XCAgQy8vICMgIC8nLS0tLS0nJy8gIyAgLwogICAgICAgLiAgICdDLyB8ICAgIHwgICAgfCAgIHwg"
        "ICAgfG1yZiAgLAogICAgICAgXCksIC4uIC4nT09PLScuIC4uJ09PTydPT08tJy4gLi5cKCw=";

    static const unsigned char small_elephant[] =
        "ICAgIF8gICAgXwogICAvIFxfXy8gXF9fX19fCiAgLyAgLyAgXCAgXCAgICBgXAogICkgIFwnJy8g"
        "ICggICAgIHxcCiAgYFxfXykvX18vJ19cICAvIGAKICAgICAvL198X3x+fF98X3wKICAgICBeIiIn"
        "IicgIiInIic=";

    int ret = 0;
    unsigned char *data = NULL;

    switch (srand(time(NULL)), rand() % 2) {
    case 0:
        ret = base64_decode(&data, big_elephant, sizeof(big_elephant) - 1);
        break;
    case 1:
        ret = base64_decode(&data, small_elephant, sizeof(small_elephant) - 1);
        break;
    default:
        errx(EXIT_FAILURE, "failed to find elephant");
        break;
    }

    if (ret > 0) {
        puts((char *)data);
        exit(EXIT_SUCCESS);
    }

    exit(EXIT_FAILURE);
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
    return symlinkat(link, repo->rootfd, pkg->filename);
}

static inline void link_pkg(const struct repo *repo, const struct pkg *pkg)
{
    if (repo->reflink) {
        if (clone_pkg(repo, pkg) < 0)
            err(EXIT_FAILURE, "failed to make reflink for %s", pkg->filename);
    } else if (symlink_pkg(repo, pkg) < 0) {
        err(EXIT_FAILURE, "failed to make symlink for %s", pkg->filename);
    }
}

static int render_db(struct repo *repo, const char *repo_name, enum contents what)
{
    _cleanup_close_ int dbfd = -1;

    dbfd = openat(repo->rootfd, repo_name, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (dbfd < 0)
        err(EXIT_FAILURE, "failed to open %s for writing", repo_name);

    if (save_database(dbfd, repo->cache, what, repo->compression, repo->poolfd) < 0)
        err(EXIT_FAILURE, "failed to write %s", repo_name);

    if (repo->sign)
        gpgme_sign(repo->rootfd, repo_name, NULL);

    return 0;
}

static inline int delete_link(const struct pkg *pkg, int dirfd)
{
    struct stat buf;
    if (fstatat(dirfd, pkg->filename, &buf, AT_SYMLINK_NOFOLLOW) < 0)
        return errno != ENOENT ? -1 : 0;
    if (S_ISLNK(buf.st_mode))
        return unlinkat(dirfd, pkg->filename, 0);
    return 0;
}

static void link_db(struct repo *repo)
{
    alpm_list_t *node;

    if (!repo->pool)
        return;

    for (node = repo->cache->list; node; node = node->next)
        link_pkg(repo, node->data);
}

static inline alpm_pkghash_t *_alpm_pkghash_replace(alpm_pkghash_t *cache, struct pkg *new,
                                                    struct pkg *old)
{
    cache = _alpm_pkghash_remove(cache, old, NULL);
    return _alpm_pkghash_add(cache, new);
}

static int reduce_repo(struct repo *repo)
{
    alpm_list_t *node;

    for (node = repo->cache->list; node; node = node->next) {
        struct pkg *pkg = node->data;

        if (faccessat(repo->poolfd, pkg->filename, F_OK, 0) < 0) {
            if (errno != ENOENT)
                err(EXIT_FAILURE, "couldn't access package %s", pkg->filename);

            trace("dropping %s\n", pkg->name);

            repo->cache = _alpm_pkghash_remove(repo->cache, pkg, NULL);
            delete_link(pkg, repo->rootfd);
            package_free(pkg);
            repo->state = REPO_DIRTY;
        }
    }

    return 0;
}

static void drop_from_repo(struct repo *repo, alpm_list_t *targets)
{
    alpm_list_t *node;

    if (!targets)
        return;

    for (node = repo->cache->list; node; node = node->next) {
        struct pkg *pkg = node->data;

        if (match_targets(pkg, targets)) {
            trace("dropping %s\n", pkg->name);

            repo->cache = _alpm_pkghash_remove(repo->cache, pkg, NULL);
            delete_link(pkg, repo->rootfd);
            package_free(pkg);
            repo->state = REPO_DIRTY;
        }
    }
}

static bool update_repo(struct repo *repo, alpm_pkghash_t *src)
{
    alpm_list_t *node;
    bool dirty = false;

    for (node = src->list; node; node = node->next) {
        struct pkg *pkg = node->data;
        struct pkg *old = _alpm_pkghash_find(repo->cache, pkg->name);
        bool replace = false;
        int vercmp;

        /* if the package isn't in the cache, add it */
        if (!old) {
            trace("adding %s %s\n", pkg->name, pkg->version);

            repo->cache = _alpm_pkghash_add(repo->cache, pkg);
            dirty = true;
            continue;
        }

        vercmp = alpm_pkg_vercmp(pkg->version, old->version);

        switch(vercmp) {
            case 1:
                trace("updating %s %s => %s\n", pkg->name, old->version, pkg->version);

                replace = true;
                break;
            case 0:
                if (pkg->mtime > old->mtime) {
                    trace("updating %s %s [newer timestamp]\n", pkg->name, pkg->version);
                    replace = true;
                } else if (pkg->builddate > old->builddate) {
                    trace("updating %s %s [newer build]\n", pkg->name, pkg->version);
                    replace = true;
                } else if (old->base64sig == NULL && pkg->base64sig) {
                    trace("adding signature for %s\n", pkg->name);
                    replace = true;
                }
                break;
            case -1:
                break;
        }

        if (replace) {
            repo->cache = _alpm_pkghash_replace(repo->cache, pkg, old);
            delete_link(pkg, repo->rootfd);
            package_free(old);
            dirty = true;
        }
    }

    return dirty;
}

/* NOTES TO SELF:
 * - do i really need to do a merge when there's no database?
 *    - how do i handle stdout then?
 * - look at states, do I really need 3?
 * - minimal stdout
 */

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
            repo->sign = true;
        }
    } else if (errno != ENOENT) {
        err(EXIT_FAILURE, "countn't access %s", name);
    }
}

static void init_repo(struct repo *repo, const char *reponame, bool files,
                      bool load_cache)
{
    repo->rootfd = open(repo->root, O_RDONLY | O_DIRECTORY);
    if (repo->rootfd < 0)
        err(EXIT_FAILURE, "failed to open root directory %s", repo->root);

    if (repo->pool) {
        repo->poolfd = open(repo->pool, O_RDONLY | O_DIRECTORY);
        if (repo->poolfd < 0)
            err(EXIT_FAILURE, "failed to open pool directory %s", repo->pool);
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

    if (repo->sign) {
        check_signature(repo, repo->dbname);
        check_signature(repo, repo->filesname);
    }

    repo->cache = _alpm_pkghash_create(100);

    if (!load_cache)
        return;

    if (load_db(repo, repo->dbname) < 0)
        return;

    if (repo->filesname)
        load_db(repo, repo->filesname);

    repo->state = REPO_CLEAN;
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
    bool files = false, rebuild = false, drop = false;

    static const struct option opts[] = {
        { "help",     no_argument,       0, 'h' },
        { "version",  no_argument,       0, 'V' },
        { "drop",     no_argument,       0, 'd' },
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

    struct repo repo = {
        .state       = REPO_NEW,
        .root        = ".",
        .compression = ARCHIVE_COMPRESSION_NONE,
        .reflink     = false,
        .sign        = false
    };

    for (;;) {
        int opt = getopt_long(argc, argv, "hVvdfsr:p:m:jJzZ", opts, NULL);
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
        case 'f':
            files = true;
            break;
        case 's':
            repo.sign = true;
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
            repo.compression = ARCHIVE_FILTER_BZIP2;
            break;
        case 'J':
            repo.compression = ARCHIVE_FILTER_XZ;
            break;
        case 'z':
            repo.compression = ARCHIVE_FILTER_GZIP;
            break;
        case 'Z':
            repo.compression = ARCHIVE_FILTER_COMPRESS;
            break;
        case 0x100:
            repo.reflink = true;
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

    rootname = get_rootname(argv[0]);
    init_repo(&repo, rootname, files, !rebuild);

    alpm_list_t *targets = parse_targets(&argv[1], argc - 1);

    if (drop) {
        drop_from_repo(&repo, targets);
    } else {
        alpm_pkghash_t *filecache = get_filecache(repo.poolfd, targets, arch);
        if (!filecache)
            err(EXIT_FAILURE, "failed to get filecache");

        reduce_repo(&repo);

        if (update_repo(&repo, filecache))
            repo.state = REPO_DIRTY;
    }

    switch (repo.state) {
    case REPO_NEW:
        trace("repo empty!\n");
        break;
    case REPO_CLEAN:
        trace("repo does not need updating\n");
        break;
    case REPO_DIRTY:
        trace("writing %s...\n", repo.dbname);
        render_db(&repo, repo.dbname, DB_DESC | DB_DEPENDS);

        if (repo.filesname) {
            trace("writing %s...\n", repo.filesname);
            render_db(&repo, repo.filesname, DB_FILES);
        }

        link_db(&repo);
        break;
    default:
        break;
    }

    return 0;
}
