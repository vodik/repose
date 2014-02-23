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

#include "pkghash.h"
#include <alpm_list.h>

bool rebuild = false;
const char *pool = ".", *root = ".";
int compression = ARCHIVE_COMPRESSION_NONE;

static _printf_(1,2) void colon_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    /* fputs(cfg.colstr.colon, stdout); */
    fputs(":: ", stdout);
    vprintf(fmt, args);
    /* fputs(cfg.colstr.nocolor, stdout); */
    va_end(args);

    fflush(stdout);
}

static _noreturn_ void usage(FILE *out)
{
    fprintf(out, "usage: %s [options] <database> [pkgs|deltas ...]\n", program_invocation_short_name);
    fputs("Options\n"
          " -h, --help            display this help and exit\n"
          " -v, --version         display version\n"
          " -r, --root=ROOT       set the root for the repository\n"
          " -p, --pool=POOL       set the pool to find packages in\n"
          " -j, --bzip2           filter the archive through bzip2\n"
          " -J, --xz              filter the archive through xz\n"
          " -z, --gzip            filter the archive through gzip\n"
          " -Z, --compress        filter the archive through compress\n"
          "     --rebuild         force rebuild the repo\n", out);

    exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void parse_args(int *argc, char **argv[])
{
    static const struct option opts[] = {
        { "help",     no_argument,       0, 'h' },
        { "version",  no_argument,       0, 'v' },
        { "root",     required_argument, 0, 'r' },
        { "pool",     required_argument, 0, 'p' },
        { "bzip2",    no_argument,       0, 'j' },
        { "xz",       no_argument,       0, 'J' },
        { "gzip",     no_argument,       0, 'z' },
        { "compress", no_argument,       0, 'Z' },
        { "rebuild",  no_argument,       0, 0x100 },
        { 0, 0, 0, 0 }
    };

    for (;;) {
        int opt = getopt_long(*argc, *argv, "hvr:p:jJzZ", opts, NULL);
        if (opt < 0)
            break;

        switch (opt) {
        case 'h':
            usage(stdout);
            break;
        case 'v':
            printf("%s %s\n",  program_invocation_short_name, REPOSE_VERSION);
            exit(EXIT_SUCCESS);
        case 'r':
            root = optarg;
            break;
        case 'p':
            pool = optarg;
            break;
        case 'j':
            compression = ARCHIVE_FILTER_BZIP2;
            break;
        case 'J':
            compression = ARCHIVE_FILTER_XZ;
            break;
        case 'z':
            compression = ARCHIVE_FILTER_GZIP;
            break;
        case 'Z':
            compression = ARCHIVE_FILTER_COMPRESS;
            break;
        case 0x100:
            rebuild = true;
            break;
        }
    }

    *argc -= optind;
    *argv += optind;
}

enum state {
    REPO_NEW,
    REPO_CLEAN,
    REPO_DIRTY
};

struct repo {
    int rootfd;
    enum state state;
    const char *dbname;
    alpm_pkghash_t *filecache;
};

static int load_repo(struct repo *repo, const char *dbname)
{
    int rootfd = open(root, O_RDONLY | O_DIRECTORY);
    if (rootfd < 0)
        err(1, "failed to open root directory %s", root);

    *repo = (struct repo){
        .state     = REPO_NEW,
        .rootfd    = rootfd,
        .dbname    = joinstring(dbname, ".db", NULL),
        .filecache = _alpm_pkghash_create(100)
    };

    if (rebuild)
        return 0;

    _cleanup_close_ int dbfd = openat(rootfd, repo->dbname, O_RDONLY);
    if (dbfd < 0) {
        if (errno != ENOENT)
            err(EXIT_FAILURE, "failed to open database %s", repo->dbname);
    } else {
        printf("LOADING filecache\n");
        if (load_database(dbfd, &repo->filecache) < 0)
            warn("failed to open database");
        else
            repo->state = REPO_CLEAN;
    }

    return 0;
}

static inline alpm_pkghash_t *_alpm_pkghash_replace(alpm_pkghash_t *cache, struct pkg *new,
                                                    struct pkg *old)
{
    cache = _alpm_pkghash_remove(cache, old, NULL);
    return _alpm_pkghash_add(cache, new);
}

static int reduce_database(int dirfd, alpm_pkghash_t **filecache)
{
    alpm_list_t *node;

    for (node = (*filecache)->list; node; node = node->next) {
        struct pkg *pkg = node->data;

        if (faccessat(dirfd, pkg->filename, F_OK, 0) < 0) {
            if (errno != ENOENT)
                err(EXIT_FAILURE, "couldn't access package %s", pkg->filename);
            printf("dropping %s\n", pkg->name);
            *filecache = _alpm_pkghash_remove(*filecache, pkg, NULL);
            package_free(pkg);
            continue;
        }
    }

    return 0;
}

static bool merge_database(alpm_pkghash_t *pkgcache, alpm_pkghash_t **filecache)
{
    alpm_list_t *node;
    bool dirty = false;

    for (node = pkgcache->list; node; node = node->next) {
        struct pkg *pkg = node->data;
        struct pkg *old = _alpm_pkghash_find(*filecache, pkg->name);
        bool replace = false;
        int vercmp;

        /* if the package isn't in the cache, add it */
        if (!old) {
            printf("adding %s %s\n", pkg->name, pkg->version);
            *filecache = _alpm_pkghash_add(*filecache, pkg);
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
            *filecache = _alpm_pkghash_replace(*filecache, pkg, old);
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
 */

int main(int argc, char *argv[])
{
    struct repo repo;
    const char *dbname;

    parse_args(&argc, &argv);
    dbname = argv[0];

    if (argc != 1)
        errx(1, "incorrect number of arguments provided");

    load_repo(&repo, dbname);

    _cleanup_close_ int poolfd = open(pool, O_RDONLY | O_DIRECTORY);
    if (poolfd < 0)
        err(1, "failed to open pool directory %s", pool);

    alpm_pkghash_t *pkgcache = get_filecache(poolfd);
    if (!pkgcache)
        err(1, "failed to get filecache");

    reduce_database(poolfd, &repo.filecache);

    if (merge_database(pkgcache, &repo.filecache))
        repo.state = REPO_DIRTY;

    switch (repo.state) {
    case REPO_NEW:
        printf("repo empty!\n");
        return 1;
    case REPO_CLEAN:
        printf("repo does not need updating\n");
        break;
    case REPO_DIRTY:
        colon_printf("Writing databases to disk...\n");
        printf("writing %s...\n", repo.dbname);

        {
            _cleanup_close_ int dbfd = open(repo.dbname, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (dbfd < 0)
                err(EXIT_FAILURE, "failed to open %s for writing", repo.dbname);

            if (save_database(dbfd, repo.filecache, compression) < 0)
                err(EXIT_FAILURE, "failed to write %s", repo.dbname);
        }

        printf("repo updated successfully\n");
        break;
    default:
        break;
    }

    return 0;
}
