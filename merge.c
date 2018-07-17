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

#include <curses.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <sysexits.h>

#include "torus.h"

static void drawTile(int offsetY, const struct Tile *tile) {
	for (uint8_t y = 0; y < CELL_ROWS; ++y) {
		for (uint8_t x = 0; x < CELL_COLS; ++x) {
			uint8_t color = tile->colors[y][x];
			char cell = tile->cells[y][x];

			int attrs = COLOR_PAIR(color & ~COLOR_BRIGHT);
			if (color & COLOR_BRIGHT) attrs |= A_BOLD;
			mvaddch(offsetY + y, x, attrs | cell);
		}
	}
}

int main(int argc, char *argv[]) {
	if (argc != 4) return EX_USAGE;

	FILE *fileA = fopen(argv[1], "r");
	if (!fileA) err(EX_NOINPUT, "%s", argv[1]);

	FILE *fileB = fopen(argv[2], "r");
	if (!fileB) err(EX_NOINPUT, "%s", argv[2]);

	FILE *fileC = fopen(argv[3], "w");
	if (!fileC) err(EX_CANTCREAT, "%s", argv[3]);

	initscr();
	cbreak();
	noecho();
	keypad(stdscr, true);
	set_escdelay(100);

	start_color();
	for (int bg = COLOR_BLACK; bg < COLOR_BRIGHT; ++bg) {
		for (int fg = COLOR_BLACK; fg < COLOR_BRIGHT; ++fg) {
			init_pair(bg << 4 | fg, fg, bg);
		}
	}

	mvhline(CELL_ROWS, 0, 0, CELL_COLS);
	mvhline(CELL_ROWS * 2 + 1, 0, 0, CELL_COLS);
	mvvline(0, CELL_COLS, 0, CELL_ROWS * 2 + 1);
	mvaddch(CELL_ROWS, CELL_COLS, ACS_RTEE);
	mvaddch(CELL_ROWS * 2 + 1, CELL_COLS, ACS_LRCORNER);

	struct Tile tileA, tileB;
	for (;;) {
		size_t countA = fread(&tileA, sizeof(tileA), 1, fileA);
		if (ferror(fileA)) err(EX_IOERR, "%s", argv[1]);

		size_t countB = fread(&tileB, sizeof(tileB), 1, fileB);
		if (ferror(fileB)) err(EX_IOERR, "%s", argv[2]);

		if (!countA && !countB) break;
		if (!countA || !countB) errx(EX_DATAERR, "different size inputs");

		const struct Tile *tileC = (tileA.accessTime > tileB.accessTime)
			? &tileA
			: &tileB;

		if (tileA.modifyTime != tileB.modifyTime) {
			drawTile(0, &tileA);
			drawTile(CELL_ROWS + 1, &tileB);
			move(CELL_ROWS * 2 + 2, 0);
			refresh();

			int c;
			do { c = getch(); } while (c != 'a' && c != 'b');
			tileC = (c == 'a') ? &tileA : &tileB;
		}

		fwrite(tileC, sizeof(*tileC), 1, fileC);
		if (ferror(fileC)) err(EX_IOERR, "%s", argv[3]);
	}

	endwin();
	return EX_OK;
}
