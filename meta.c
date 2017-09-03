/*
 * Copyright (c) 2017, Curtis McEnroe <curtis@cmcenroe.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
