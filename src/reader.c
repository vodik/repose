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

#include "reader.h"

#include <stdlib.h>
#include <errno.h>
#include <string.h>

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

static int archive_feed_block(struct archive_reader *r)
{
    int64_t offset;
    r->ret = archive_read_data_block(r->archive, (void *)&r->block,
                                     &r->block_size, &offset);
    r->block_offset = r->block;
    return r->ret == ARCHIVE_OK ? 0 : -1;
}

static char *find_eol(struct archive_reader* r, size_t block_remaining)
{
    char *eol = memchr(r->block_offset, '\n', block_remaining);
    return eol ? eol : memchr(r->block_offset, '\0', block_remaining);
}


int archive_getline(struct archive_reader *r, char **line)
{
    char *line_offset = *line = NULL;
    size_t line_length = 0;

    for (;;) {
        if (&r->block[r->block_size] == r->block_offset) {
            if (r->ret == ARCHIVE_EOF)
                break;
            if (archive_feed_block(r) < 0)
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

        if (eol) {
            r->block_offset += len + 1;
            break;
        } else {
            r->block_offset += len;
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

    for (;;) {
        if (&r->block[r->block_size] == r->block_offset) {
            if (r->ret == ARCHIVE_EOF)
                break;
            if (archive_feed_block(r) < 0)
                return -1;
        }

        size_t block_remaining = &r->block[r->block_size] - r->block_offset;
        char *eol = find_eol(r, block_remaining);
        size_t len = (eol ? eol : &r->block[r->block_size]) - r->block_offset;

        if (line_offset - line + len + 1 > line_size)
            return -ERANGE;

        line_offset = mempcpy(line_offset, r->block_offset, len);

        if (eol) {
            r->block_offset += len + 1;
            break;
        } else {
            r->block_offset += len;
        }
    }

    *line_offset = 0;
    return line_offset - line;
}
