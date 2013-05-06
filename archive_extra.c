#include "archive_extra.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <archive.h>
#include <archive_entry.h>

int archive_fgets(struct archive *a, struct archive_read_buffer *b, size_t entry_size)
{
    /* ensure we start populating our line buffer at the beginning */
    b->line_offset = b->line;

    while(1) {
        size_t new, block_remaining;
        char *eol;

        /* have we processed this entire block? */
        if(b->block + b->block_size == b->block_offset) {
            int64_t offset;
            if(b->ret == ARCHIVE_EOF) {
                /* reached end of archive on the last read, now we are out of data */
                return b->ret;
            }

            /* zero-copy - this is the entire next block of data. */
            b->ret = archive_read_data_block(a, (const void **)&b->block,
                                             &b->block_size, &offset);
            b->block_offset = b->block;
            block_remaining = b->block_size;

            /* error, cleanup */
            if(b->ret != ARCHIVE_OK) {
                return b->ret;
            }
        } else {
            block_remaining = b->block + b->block_size - b->block_offset;
        }

        /* look through the block looking for EOL characters */
        eol = memchr(b->block_offset, '\n', block_remaining);
        if(!eol) {
            eol = memchr(b->block_offset, '\0', block_remaining);
        }

        /* note: we know eol > b->block_offset and b->line_offset > b->line,
         * so we know the result is unsigned and can fit in size_t */
        new = eol ? (size_t)(eol - b->block_offset) : block_remaining;
        if((b->line_offset - b->line + new + 1) > entry_size) {
            return -ERANGE;
        }

        if(eol) {
            size_t len = (size_t)(eol - b->block_offset);
            memcpy(b->line_offset, b->block_offset, len);
            b->line_offset[len] = '\0';
            b->block_offset = eol + 1;
            b->real_line_size = b->line_offset + len - b->line;
            /* this is the main return point; from here you can read b->line */
            return ARCHIVE_OK;
        } else {
            /* we've looked through the whole block but no newline, copy it */
            size_t len = (size_t)(b->block + b->block_size - b->block_offset);
            b->line_offset = mempcpy(b->line_offset, b->block_offset, len);
            b->block_offset = b->block + b->block_size;
            /* there was no new data, return what is left; saved ARCHIVE_EOF will be
             * returned on next call */
            if(len == 0) {
                b->line_offset[0] = '\0';
                b->real_line_size = b->line_offset - b->line;
                return ARCHIVE_OK;
            }
        }
    }

    return b->ret;
}
