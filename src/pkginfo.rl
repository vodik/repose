#include "pkginfo.h"

#include <err.h>
#include <archive.h>
#include "package.h"

%%{
    machine pkginfo;

    action store {
        parser->store[parser->pos++] = fc;
        if (parser->pos == LINE_MAX) {
            errx(1, "desc line too long");
        }
    }

    action emit {
        const char *entry = parser->store;
        const size_t entry_len = parser->pos;
        parser->store[parser->pos] = 0;
        parser->pos = 0;

        package_set(pkg, parser->entry, entry, entry_len);
    }

    header = 'pkgname'     %{ parser->entry = PKG_PKGNAME; }
           | 'pkgbase'     %{ parser->entry = PKG_PKGBASE; }
           | 'pkgver'      %{ parser->entry = PKG_VERSION; }
           | 'pkgdesc'     %{ parser->entry = PKG_DESCRIPTION; }
           | 'url'         %{ parser->entry = PKG_URL; }
           | 'builddate'   %{ parser->entry = PKG_BUILDDATE; }
           | 'packager'    %{ parser->entry = PKG_PACKAGER; }
           | 'size'        %{ parser->entry = PKG_ISIZE; }
           | 'arch'        %{ parser->entry = PKG_ARCH; }
           | 'group'       %{ parser->entry = PKG_GROUPS; }
           | 'license'     %{ parser->entry = PKG_LICENSE; }
           | 'replaces'    %{ parser->entry = PKG_REPLACES; }
           | 'depend'      %{ parser->entry = PKG_DEPENDS; }
           | 'conflict'    %{ parser->entry = PKG_CONFLICTS; }
           | 'provides'    %{ parser->entry = PKG_PROVIDES; }
           | 'optdepend'   %{ parser->entry = PKG_OPTDEPENDS; }
           | 'makedepend'  %{ parser->entry = PKG_MAKEDEPENDS; }
           | 'checkdepend' %{ parser->entry = PKG_CHECKDEPENDS; };

    entry = header ' = ' [^\n]+ @store %emit '\n';
    comment = '#' [^\n]* '\n';

    main := ( entry | comment )*;
}%%

%%write data nofinal;

void pkginfo_parser_init(struct pkginfo_parser *parser)
{
    *parser = (struct pkginfo_parser){0};
    %%access parser->;
    %%write init;
}

ssize_t pkginfo_parser_feed(struct pkginfo_parser *parser, struct pkg *pkg,
                         char *buf, size_t buf_len)
{
    char *p = buf;
    char *pe = p + buf_len;

    %%access parser->;
    %%write exec;

    (void)pkginfo_en_main;
    if (parser->cs == pkginfo_error)
        return -1;

    return buf_len;
}

static int archive_read(struct archive *archive, char **buf, size_t *buf_len)
{
    for (;;) {
        int status = archive_read_data_block(archive, (void *)buf,
                                             buf_len, &(int64_t){0});
        if (status == ARCHIVE_RETRY)
            continue;
        if (status <= ARCHIVE_WARN)
            warnx("%s", archive_error_string(archive));
        return status;
    }
}

void read_pkginfo(struct archive *archive, struct pkg *pkg)
{
    char *buf;
    struct pkginfo_parser parser;
    pkginfo_parser_init(&parser);

     for (;;) {
         size_t nbytes_r;
         archive_read(archive, &buf, &nbytes_r);
         pkginfo_parser_feed(&parser, pkg, buf, nbytes_r);
         break;
    }
}
