/* Copyright (C) 2017  Curtis McEnroe <june@causal.agency>
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

#include "torus.h"

int main() {
	printf("tileX,tileY,createTime,modifyCount,modifyTime,accessCount,accessTime\n");
	for (int i = 0;; ++i) {
		struct Tile tile;
		size_t count = fread(&tile, sizeof(tile), 1, stdin);
		if (ferror(stdin)) err(EX_IOERR, "(stdin)");
		if (!count) return EX_OK;

		printf(
			"%d,%d,%jd,%u,%jd,%u,%jd\n",
			i % TILE_COLS,
			i / TILE_COLS,
			tile.meta.createTime,
			tile.meta.modifyCount,
			tile.meta.modifyTime,
			tile.meta.accessCount,
			tile.meta.accessTime
		);
	}
}
