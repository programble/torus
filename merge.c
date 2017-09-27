/* Copyright (c) 2017, Curtis McEnroe <curtis@cmcenroe.me>
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
#include <unistd.h>

#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_MAGENTA
#undef COLOR_CYAN
#undef COLOR_WHITE
#include "torus.h"

static ssize_t writeAll(int fd, const char *buf, size_t len) {
    ssize_t writeLen;
    while (0 < (writeLen = write(fd, buf, len))) {
        buf += writeLen;
        len -= writeLen;
    }
    return writeLen;
}

static void drawTile(int offsetY, const struct Tile *tile) {
    for (int y = 0; y < CELL_ROWS; ++y) {
        for (int x = 0; x < CELL_COLS; ++x) {
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

    int fileA = open(argv[1], O_RDONLY);
    if (fileA < 0) err(EX_IOERR, "%s", argv[1]);

    int fileB = open(argv[2], O_RDONLY);
    if (fileB < 0) err(EX_IOERR, "%s", argv[2]);

    int fileC = open(argv[3], O_WRONLY | O_CREAT, 0644);
    if (fileC < 0) err(EX_IOERR, "%s", argv[3]);

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
        ssize_t lenA = read(fileA, &tileA, sizeof(tileA));
        if (lenA < 0) err(EX_IOERR, "%s", argv[1]);

        ssize_t lenB = read(fileB, &tileB, sizeof(tileB));
        if (lenB < 0) err(EX_IOERR, "%s", argv[2]);

        if (!lenA && !lenB) break;
        if (!lenA || !lenB) errx(EX_IOERR, "different size inputs");

        const struct Tile *tileC = (tileA.access > tileB.access) ? &tileA : &tileB;

        if (tileA.modify != tileB.modify) {
            drawTile(0, &tileA);
            drawTile(CELL_ROWS + 1, &tileB);
            move(CELL_ROWS * 2 + 2, 0);
            refresh();

            int c;
            do { c = getch(); } while (c != 'a' && c != 'b');
            tileC = (c == 'a') ? &tileA : &tileB;
        }

        ssize_t lenC = writeAll(fileC, (char *)tileC, sizeof(*tileC));
        if (lenC < 0) err(EX_IOERR, "%s", argv[3]);
    }

    endwin();
    return EX_OK;
}
