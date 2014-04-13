/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) Simon Gomizelj, 2014
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <getopt.h>
#include <archive.h>
#include <archive_entry.h>

#include <fcntl.h>
#include <unistd.h>

#include "database.h"
#include "memblock.h"
#include "files.h"
#include "util.h"
#include "base64.h"

#include <alpm_list.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include "pkghash.h"
#include "filters.h"
#include "termio.h"

enum state {
    REPO_NEW,
    REPO_CLEAN,
    REPO_DIRTY
};

static bool drop = false;
static const char *dbname = NULL, *filesname = NULL, *arch = NULL;
static alpm_pkghash_t *filecache = NULL;
static struct utsname uts;
static bool make_compat = false;

struct repo {
    enum state state;
    const char *pool;
    int rootfd;
    int poolfd;

    int compression;
    bool rebuild;
    bool files;
};

static _noreturn_ void usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <database> [pkgs|deltas ...]\n", program_invocation_short_name);
    fputs("Options\n"
          " -h, --help            display this help and exit\n"
          " -v, --version         display version\n"
          " -f, --files           also build the .files database\n"
          " -r, --root=PATH       set the root for the repository\n"
          " -p, --pool=PATH       set the pool to find packages in\n"
          " -m, --arch=ARCH       the architecture of the database\n"
          " -j, --bzip2           filter the archive through bzip2\n"
          " -J, --xz              filter the archive through xz\n"
          " -z, --gzip            filter the archive through gzip\n"
          " -Z, --compress        filter the archive through compress\n"
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

    exit(ret);
}

static void parse_args(int *argc, char **argv[], struct repo *repo)
{
    const char *root = ".";
    static const struct option opts[] = {
        { "help",     no_argument,       0, 'h' },
        { "version",  no_argument,       0, 'v' },
        { "files",    no_argument,       0, 'f' },
        { "drop",     no_argument,       0, 'd' },
        { "root",     required_argument, 0, 'r' },
        { "pool",     required_argument, 0, 'p' },
        { "arch",     required_argument, 0, 'm' },
        { "bzip2",    no_argument,       0, 'j' },
        { "xz",       no_argument,       0, 'J' },
        { "gzip",     no_argument,       0, 'z' },
        { "compress", no_argument,       0, 'Z' },
        { "rebuild",  no_argument,       0, 0x100 },
        { "compat",   no_argument,       0, 0x101 },
        { "elephant", no_argument,       0, 0x102 },
        { 0, 0, 0, 0 }
    };

    for (;;) {
        int opt = getopt_long(*argc, *argv, "hvfdr:p:m:jJzZ", opts, NULL);
        if (opt < 0)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'v':
            printf("%s %s\n",  program_invocation_short_name, REPOSE_VERSION);
            exit(EXIT_SUCCESS);
        case 'f':
            repo->files = true;
            break;
        case 'd':
            drop = true;
            break;
        case 'r':
            root = optarg;
            break;
        case 'p':
            repo->pool = optarg;
            break;
        case 'm':
            arch = optarg;
            break;
        case 'j':
            repo->compression = ARCHIVE_FILTER_BZIP2;
            break;
        case 'J':
            repo->compression = ARCHIVE_FILTER_XZ;
            break;
        case 'z':
            repo->compression = ARCHIVE_FILTER_GZIP;
            break;
        case 'Z':
            repo->compression = ARCHIVE_FILTER_COMPRESS;
            break;
        case 0x100:
            repo->rebuild = true;
            break;
        case 0x101:
            make_compat = true;
            break;
        case 0x102:
            elephant();
            break;
        }
    }

    *argc -= optind;
    *argv += optind;

    repo->rootfd = open(root, O_RDONLY | O_DIRECTORY);
    if (repo->rootfd < 0)
        err(EXIT_FAILURE, "failed to open root directory %s", root);

    if (repo->pool) {
        repo->poolfd = open(repo->pool, O_RDONLY | O_DIRECTORY);
        if (repo->poolfd < 0)
            err(EXIT_FAILURE, "failed to open pool directory %s", repo->pool);
    } else {
        repo->poolfd = repo->rootfd;
    }

    if (!arch) {
        uname(&uts);
        arch = uts.machine;
    }
}

static inline int make_link(const struct pkg *pkg, int dirfd, const char *source)
{
    _cleanup_free_ char *link = joinstring(source, "/", pkg->filename, NULL);
    return symlinkat(link, dirfd, pkg->filename);
}

static inline int compat_link(int rootdb, const char *reponame, int compression)
{
    static const char *ext[] = {
        [ARCHIVE_FILTER_NONE]     = "",
        [ARCHIVE_FILTER_BZIP2]    = ".bz2",
        [ARCHIVE_FILTER_XZ]       = ".xz",
        [ARCHIVE_FILTER_GZIP]     = ".gz",
        [ARCHIVE_FILTER_COMPRESS] = ".Z"
    };

    _cleanup_free_ char *link = joinstring(reponame, ".tar", ext[compression], NULL);
    return symlinkat(reponame, rootdb, link);
}

static int load_db(const char *name, struct repo *repo)
{
    _cleanup_close_ int dbfd = openat(repo->rootfd, name, O_RDONLY);
    if (dbfd < 0) {
        if (errno != ENOENT)
            err(EXIT_FAILURE, "failed to open database %s", name);
        return -1;
    } else if (load_database(dbfd, &filecache) < 0) {
        warn("failed to open %s database", name);
    } else {
        repo->state = REPO_CLEAN;
    }

    return 0;
}

static int render_db(struct repo *repo, const char *name, enum contents what)
{
    _cleanup_close_ int dbfd = openat(repo->rootfd, name, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (dbfd < 0)
        err(EXIT_FAILURE, "failed to open %s for writing", name);

    if (save_database(dbfd, filecache, what, repo->compression, repo->poolfd) < 0)
        err(EXIT_FAILURE, "failed to write %s", name);

    if (make_compat && compat_link(repo->rootfd, name, repo->compression) < 0) {
        if (errno != EEXIST)
            warn("failed to make compatability symlink to %s", name);
    }

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

    for (node = filecache->list; node; node = node->next)
        make_link(node->data, repo->rootfd, repo->pool);
}

static int load_repo(const char *rootname, struct repo *repo)
{
    dbname    = joinstring(rootname, ".db", NULL);
    filesname = joinstring(rootname, ".files", NULL);
    filecache = _alpm_pkghash_create(100);

    if (repo->rebuild)
        return 0;

    load_db(dbname, repo);
    if (load_db(filesname, repo) == 0)
        repo->files = true;

    return 0;
}

static inline alpm_pkghash_t *_alpm_pkghash_replace(alpm_pkghash_t *cache, struct pkg *new,
                                                    struct pkg *old)
{
    cache = _alpm_pkghash_remove(cache, old, NULL);
    return _alpm_pkghash_add(cache, new);
}

static int reduce_repo(struct repo *repo, alpm_pkghash_t **cache)
{
    alpm_list_t *node;

    for (node = (*cache)->list; node; node = node->next) {
        struct pkg *pkg = node->data;

        if (faccessat(repo->poolfd, pkg->filename, F_OK, 0) < 0) {
            if (errno != ENOENT)
                err(EXIT_FAILURE, "couldn't access package %s", pkg->filename);
            printf("dropping %s\n", pkg->name);
            *cache = _alpm_pkghash_remove(*cache, pkg, NULL);
            delete_link(pkg, repo->rootfd);
            package_free(pkg);
            repo->state = REPO_DIRTY;
        }
    }

    return 0;
}

static void drop_from_repo(struct repo *repo, alpm_pkghash_t **cache, alpm_list_t *targets)
{
    alpm_list_t *node;

    if (!targets)
        return;

    for (node = (*cache)->list; node; node = node->next) {
        struct pkg *pkg = node->data;

        if (match_targets(pkg, targets)) {
            printf("dropping %s\n", pkg->name);
            *cache = _alpm_pkghash_remove(*cache, pkg, NULL);
            delete_link(pkg, repo->rootfd);
            package_free(pkg);
            repo->state = REPO_DIRTY;
        }
    }
}

static bool update_repo(struct repo *repo, alpm_pkghash_t *src, alpm_pkghash_t **dest)
{
    alpm_list_t *node;
    bool dirty = false;

    for (node = src->list; node; node = node->next) {
        struct pkg *pkg = node->data;
        struct pkg *old = _alpm_pkghash_find(*dest, pkg->name);
        bool replace = false;
        int vercmp;

        /* if the package isn't in the cache, add it */
        if (!old) {
            printf("adding %s %s\n", pkg->name, pkg->version);
            *dest = _alpm_pkghash_add(*dest, pkg);
            dirty = true;
            continue;
        }

        vercmp = alpm_pkg_vercmp(pkg->version, old->version);

        switch(vercmp) {
            case 1:
                printf("updating %s %s => %s\n", pkg->name, old->version, pkg->version);
                replace = true;
                break;
            case 0:
                if (pkg->builddate > old->builddate) {
                    printf("updating %s %s [newer build]\n", pkg->name, pkg->version);
                    replace = true;
                } else if (old->base64sig == NULL && pkg->base64sig) {
                    printf("adding signature for %s\n", pkg->name);
                    replace = true;
                }
                break;
            case -1:
                break;
        }

        if (replace) {
            *dest = _alpm_pkghash_replace(*dest, pkg, old);
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

int main(int argc, char *argv[])
{
    const char *rootname;

    struct repo repo = {
        .state       = REPO_NEW,
        .compression = ARCHIVE_COMPRESSION_NONE
    };

    parse_args(&argc, &argv, &repo);
    rootname = argv[0];

    if (argc == 0)
        errx(1, "incorrect number of arguments provided");

    alpm_list_t *targets = parse_targets(argv + 1, argc - 1);

    if (isatty(fileno(stdout)))
        enable_colors();

    load_repo(rootname, &repo);

    if (drop) {
        drop_from_repo(&repo, &filecache, targets);
    } else {
        alpm_pkghash_t *pkgcache = get_filecache(repo.poolfd, targets, arch);
        if (!pkgcache)
            err(EXIT_FAILURE, "failed to get filecache");

        reduce_repo(&repo, &filecache);

        if (update_repo(&repo, pkgcache, &filecache))
            repo.state = REPO_DIRTY;
    }

    switch (repo.state) {
    case REPO_NEW:
        printf("repo empty!\n");
        return 1;
    case REPO_CLEAN:
        printf("repo does not need updating\n");
        break;
    case REPO_DIRTY:
        colon_printf("Writing databases to disk...\n");

        printf("writing %s...\n", dbname);
        render_db(&repo, dbname, DB_DESC | DB_DEPENDS);
        link_db(&repo);

        if (repo.files) {
            printf("writing %s...\n", filesname);
            render_db(&repo, filesname, DB_FILES);
        }

        printf("repo updated successfully\n");
        break;
    default:
        break;
    }

    return 0;
}
