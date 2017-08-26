/*
 * Copyright (c) 2017, Curtis McEnroe <curtis@cmcenroe.me>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <stdio.h>
#include <sysexits.h>
#include <unistd.h>

#include "torus.h"

int main() {
    printf("tileX,tileY,createTime,modifyCount,modifyTime,accessCount,accessTime\n");
    for (int i = 0;; ++i) {
        struct Tile tile;
        ssize_t len = read(STDIN_FILENO, &tile, sizeof(tile));
        if (len < 0) err(EX_IOERR, "read");
        if (!len) return EX_OK;

        printf(
            "%d,%d,%ld,%u,%ld,%u,%ld\n",
            i % TILE_COLS,
            i / TILE_COLS,
            tile.create,
            tile.modifyCount,
            tile.modify,
            tile.accessCount,
            tile.access
        );
    }
}
