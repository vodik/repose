#include "reader.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>

struct archive_reader *archive_reader_new(struct archive *a)
{
    struct archive_reader *r = malloc(sizeof(struct archive_reader));
    *r = (struct archive_reader){
        .archive = a,
        .status = ARCHIVE_OK
    };
    return r;
}

static int archive_feed_block(struct archive_reader *r)
{
    for (;;) {
        int64_t offset;
        int status = archive_read_data_block(r->archive, (void *)&r->block,
                                             &r->block_size, &offset);
        r->block_offset = r->block;

        if (status != ARCHIVE_RETRY)
            return status;
    }
}

static char *find_eol(struct archive_reader *r, size_t block_remaining)
{
    char *eol = memchr(r->block_offset, '\n', block_remaining);
    return eol ? eol : memchr(r->block_offset, '\0', block_remaining);
}

int archive_getline(struct archive_reader *r, char **line)
{
    char *line_offset = *line = NULL;
    size_t line_length = 0;

    if (r->status != ARCHIVE_OK)
        return -1;

    for (;;) {
        if (&r->block[r->block_size] == r->block_offset) {
            if (r->status == ARCHIVE_EOF)
                break;
            r->status = archive_feed_block(r);
            if (r->status != ARCHIVE_OK)
                return -1;
        }

        size_t block_remaining = &r->block[r->block_size] - r->block_offset;
        char *eol = find_eol(r, block_remaining);
        size_t len = (eol ? eol : &r->block[r->block_size]) - r->block_offset;

        if (eol && !len)
            break;

        *line = realloc(*line, line_length + len + 1);
        line_offset = mempcpy(&(*line)[line_length], r->block_offset, len);
        line_length += len;

        r->block_offset += len;
        if (eol) {
            r->block_offset += 1;
            break;
        }
    }

    if (!line_offset)
        return 0;

    *line_offset = 0;
    return line_offset - *line;
}

int archive_fgets(struct archive_reader *r, char *line, size_t line_size)
{
    char *line_offset = line;

    if (r->status != ARCHIVE_OK)
        return -1;

    for (;;) {
        if (&r->block[r->block_size] == r->block_offset) {
            if (r->status == ARCHIVE_EOF)
                break;
            r->status = archive_feed_block(r);
            if (r->status != ARCHIVE_OK)
                return -1;
        }

        size_t block_remaining = &r->block[r->block_size] - r->block_offset;
        char *eol = find_eol(r, block_remaining);
        size_t len = (eol ? eol : &r->block[r->block_size]) - r->block_offset;

        if (line_offset - line + len + 1 > line_size)
            return -ERANGE;

        line_offset = mempcpy(line_offset, r->block_offset, len);

        r->block_offset += len;
        if (eol) {
            r->block_offset += 1;
            break;
        }
    }

    *line_offset = 0;
    return line_offset - line;
}
