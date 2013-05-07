#include "archive_reader.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <archive.h>
#include <archive_entry.h>

struct archive_reader *archive_reader_new(struct archive *a)
{
    struct archive_reader *r = malloc(sizeof(struct archive_reader));
    *r = (struct archive_reader){
        .archive      = a,
        .block        = NULL,
        .block_offset = NULL,
        .block_size   = 0,
        .ret          = ARCHIVE_OK
    };
    return r;
}

int archive_fgets(struct archive_reader *r, char *line, size_t line_size)
{
    char *line_offset = line;
    size_t bytes_r = 0;

    for (;;) {
        size_t new, block_remaining;
        char *eol;

        /* have we processes this entire block? */
        if (r->block + r->block_size == r->block_offset) {
            int64_t offset;
            if(r->ret == ARCHIVE_EOF) {
                /* reached end of archive on the last read, now we are out of data */
                return bytes_r;
            }

            r->ret = archive_read_data_block(r->archive, (const void **)&r->block,
                                             &r->block_size, &offset);
            r->block_offset = r->block;
            block_remaining = r->block_size;

            /* error, cleanup */
            if(r->ret != ARCHIVE_OK) {
                return -1;
            }
        } else {
            block_remaining = r->block + r->block_size - r->block_offset;
        }

        /* look through the block looking for EOL characters */
        eol = memchr(r->block_offset, '\n', block_remaining);
        if (!eol)
            eol = memchr(r->block_offset, '\0', block_remaining);

        /* note: we know eol > r->block_offset and r->line_offset > line,
         * so we know the result is unsigned and can fit in size_t */
        new = eol ? (size_t)(eol - r->block_offset) : block_remaining;
        if((line_offset - line + new + 1) > line_size) {
            return -ERANGE;
        }

        if (eol) {
            size_t len = (size_t)(eol - r->block_offset);
            memcpy(line_offset, r->block_offset, len);

            line_offset[len] = '\0';
            r->block_offset = eol + 1;
            bytes_r = line_offset + len - line;
            /* this is the main return point; from here you can read b->line */
            return bytes_r;
        } else {
            /* we've looked through the whole block but no newline, copy it */
            size_t len = (size_t)(r->block + r->block_size - r->block_offset);
            line_offset = mempcpy(line_offset, r->block_offset, len);
            r->block_offset = r->block + r->block_size;
            /* there was no new data, return what is left; saved ARCHIVE_EOF will be
             * returned on next call */
            if (len == 0) {
                line_offset[0] = '\0';
                bytes_r = line_offset - line;
                return bytes_r;
            }
        }
    }

    return bytes_r;
}
