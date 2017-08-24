#if 0
exec cc -Wall -Wextra -pedantic $@ -lcurses -o client $0
#endif

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

#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
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

#define ESC (0x1B)
#define DEL (0x7F)

#define CH_COLOR(ch) (ch & A_BOLD ? COLOR_BRIGHT | ch >> 8 & 0xFF : ch >> 8 & 0xFF)

static int client;

static void clientMessage(const struct ClientMessage *msg) {
    ssize_t len = send(client, msg, sizeof(*msg), 0);
    if (len < 0) err(EX_IOERR, "send");
}

static void clientMove(int8_t dx, int8_t dy) {
    struct ClientMessage msg = {
        .type = CLIENT_MOVE,
        .data.m = { .dx = dx, .dy = dy },
    };
    clientMessage(&msg);
}

static void clientPut(uint8_t color, char cell) {
    struct ClientMessage msg = {
        .type = CLIENT_PUT,
        .data.p = { .color = color, .cell = cell },
    };
    clientMessage(&msg);
}

static uint8_t inputColor = COLOR_WHITE;

static void colorFg(uint8_t fg) {
    inputColor = (inputColor & 0xF8) | fg;
}

static void colorBg(uint8_t bg) {
    inputColor = (inputColor & 0x0F) | (bg << 4);
}

static void colorInvert(void) {
    inputColor =
        (inputColor & 0x08) |
        ((inputColor & 0x07) << 4) |
        ((inputColor & 0x70) >> 4);
}

static enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_REPLACE,
    MODE_PUT,
    MODE_DRAW,
} mode;
static struct {
    int8_t dx;
    int8_t dy;
    uint8_t len;
} insert = { .dx = 1 };
static char drawChar;

static void insertMode(int8_t dx, int8_t dy) {
    mode = MODE_INSERT;
    insert.dx = dx;
    insert.dy = dy;
    insert.len = 0;
}

static void swapCell(int8_t dx, int8_t dy) {
    uint8_t aColor = CH_COLOR(inch());
    char aCell = inch() & 0x7F;

    int sy, sx;
    getyx(stdscr, sy, sx);
    move(sy + dy, sx + dx);
    uint8_t bColor = CH_COLOR(inch());
    char bCell = inch() & 0x7F;
    move(sy, sx);

    clientPut(bColor, bCell);
    clientMove(dx, dy);
    clientPut(aColor, aCell);
}

static int8_t moveSpeed = 1;

static void readInput(void) {
    int c = getch();

    if (mode == MODE_INSERT) {
        if (c == ESC) {
            mode = MODE_NORMAL;
            clientMove(-insert.dx, -insert.dy);
        } else if (!insert.dx && !insert.dy) {
            switch (c) {
                case 'h': insertMode(-1,  0); break;
                case 'j': insertMode( 0,  1); break;
                case 'k': insertMode( 0, -1); break;
                case 'l': insertMode( 1,  0); break;
                case 'y': insertMode(-1, -1); break;
                case 'u': insertMode( 1, -1); break;
                case 'b': insertMode(-1,  1); break;
                case 'n': insertMode( 1,  1); break;
            }
        } else if (c == '\b' || c == DEL) {
            clientMove(-insert.dx, -insert.dy);
            clientPut(inputColor, ' ');
            insert.len--;
        } else if (c == '\n') {
            clientMove(insert.dy, insert.dx);
            clientMove(-insert.dx * insert.len, -insert.dy * insert.len);
            insert.len = 0;
        } else if (isprint(c)) {
            clientPut(inputColor, c);
            clientMove(insert.dx, insert.dy);
            insert.len++;
        }
        return;
    }

    if (mode == MODE_REPLACE) {
        if (isprint(c)) clientPut(CH_COLOR(inch()), c);
        mode = MODE_NORMAL;
        return;
    }

    if (mode == MODE_PUT) {
        if (isprint(c)) clientPut(inputColor, c);
        mode = MODE_NORMAL;
        return;
    }

    if (mode == MODE_DRAW && !drawChar) {
        if (c == ESC) mode = MODE_NORMAL;
        if (isprint(c)) {
            drawChar = c;
            clientPut(inputColor, c);
        }
        return;
    }

    switch (c) {
        case ESC: mode = MODE_NORMAL; break;

        case 'q': endwin(); exit(EX_OK);

        case 'a': clientMove(1, 0); // fallthrough
        case 'i': insertMode(1, 0); break;
        case 'I': insertMode(0, 0); break;
        case 'r': mode = MODE_REPLACE; break;
        case 'p': mode = MODE_PUT; break;
        case 'R': mode = MODE_DRAW; drawChar = 0; break;
        case 'x': clientPut(CH_COLOR(inch()), ' '); break;

        case '~':
            clientPut(inputColor, inch() & 0x7F);
            clientMove(insert.dx, insert.dy);
            break;

        case '[': if (moveSpeed > 1) moveSpeed--; break;
        case ']': if (moveSpeed < 4) moveSpeed++; break;

        case 'h': clientMove(-moveSpeed,          0); break;
        case 'j': clientMove(         0,  moveSpeed); break;
        case 'k': clientMove(         0, -moveSpeed); break;
        case 'l': clientMove( moveSpeed,          0); break;
        case 'y': clientMove(-moveSpeed, -moveSpeed); break;
        case 'u': clientMove( moveSpeed, -moveSpeed); break;
        case 'b': clientMove(-moveSpeed,  moveSpeed); break;
        case 'n': clientMove( moveSpeed,  moveSpeed); break;

        case 'H': swapCell(-1,  0); break;
        case 'J': swapCell( 0,  1); break;
        case 'K': swapCell( 0, -1); break;
        case 'L': swapCell( 1,  0); break;
        case 'Y': swapCell(-1, -1); break;
        case 'U': swapCell( 1, -1); break;
        case 'B': swapCell(-1,  1); break;
        case 'N': swapCell( 1,  1); break;

        case '`': inputColor = CH_COLOR(inch()); break;

        case '0': colorFg(COLOR_BLACK);   break;
        case '1': colorFg(COLOR_RED);     break;
        case '2': colorFg(COLOR_GREEN);   break;
        case '3': colorFg(COLOR_YELLOW);  break;
        case '4': colorFg(COLOR_BLUE);    break;
        case '5': colorFg(COLOR_MAGENTA); break;
        case '6': colorFg(COLOR_CYAN);    break;
        case '7': colorFg(COLOR_WHITE);   break;

        case ')': colorBg(COLOR_BLACK);   break;
        case '!': colorBg(COLOR_RED);     break;
        case '@': colorBg(COLOR_GREEN);   break;
        case '#': colorBg(COLOR_YELLOW);  break;
        case '$': colorBg(COLOR_BLUE);    break;
        case '%': colorBg(COLOR_MAGENTA); break;
        case '^': colorBg(COLOR_CYAN);    break;
        case '&': colorBg(COLOR_WHITE);   break;

        case '*':
        case '8': inputColor ^= COLOR_BRIGHT; break;

        case '(':
        case '9': colorInvert(); break;

        case KEY_LEFT: clientMove(-1,  0); break;
        case KEY_DOWN: clientMove( 0,  1); break;
        case KEY_UP: clientMove( 0, -1); break;
        case KEY_RIGHT: clientMove( 1,  0); break;
    }

    if (mode == MODE_DRAW && drawChar) clientPut(inputColor, drawChar);
}

static void serverPut(uint8_t x, uint8_t y, uint8_t color, char cell) {
    int attrs = COLOR_PAIR(color & ~COLOR_BRIGHT);
    if (color & COLOR_BRIGHT) attrs |= A_BOLD;
    mvaddch(y, x, attrs | cell);
}

static void serverTile(void) {
    struct Tile tile;
    ssize_t len = recv(client, &tile, sizeof(tile), 0);
    if (len < 0) err(EX_IOERR, "recv");
    if (len < (ssize_t)sizeof(tile)) {
        errx(EX_PROTOCOL, "This tile isn't big enough...");
    }

    for (int y = 0; y < CELL_ROWS; ++y) {
        for (int x = 0; x < CELL_COLS; ++x) {
            serverPut(x, y, tile.colors[y][x], tile.cells[y][x]);
        }
    }
}

static void serverCursor(uint8_t oldX, uint8_t oldY, uint8_t newX, uint8_t newY) {
    if (oldX != CURSOR_NONE) {
        move(oldY, oldX);
        addch(inch() & ~A_REVERSE);
    }
    if (newX != CURSOR_NONE) {
        move(newY, newX);
        addch(inch() | A_REVERSE);
    }
}

static void readMessage(void) {
    struct ServerMessage msg;
    ssize_t len = recv(client, &msg, sizeof(msg), 0);
    if (len < 0) err(EX_IOERR, "recv");
    if (len < (ssize_t)sizeof(msg)) errx(EX_PROTOCOL, "A message was cut short.");

    int sy, sx;
    getyx(stdscr, sy, sx);

    switch (msg.type) {
        case SERVER_TILE:
            serverTile();
            break;

        case SERVER_MOVE:
            move(msg.data.m.cellY, msg.data.m.cellX);
            refresh();
            return;

        case SERVER_PUT:
            serverPut(
                msg.data.p.cellX,
                msg.data.p.cellY,
                msg.data.p.color,
                msg.data.p.cell
            );
            break;

        case SERVER_CURSOR:
            serverCursor(
                msg.data.c.oldCellX,
                msg.data.c.oldCellY,
                msg.data.c.newCellX,
                msg.data.c.newCellY
            );
            break;

        default:
            errx(EX_PROTOCOL, "I don't know what %d means!", msg.type);
    }

    move(sy, sx);
    refresh();
}

static void drawBorder(void) {
    if (LINES < CELL_ROWS || COLS < CELL_COLS) {
        endwin();
        fprintf(stderr, "Sorry, your terminal is too small!\n");
        fprintf(stderr, "It needs to be at least 80x25 characters.\n");
        exit(EX_CONFIG);
    }
    if (LINES > CELL_ROWS) {
        mvhline(CELL_ROWS, 0, 0, CELL_COLS);
    }
    if (COLS > CELL_COLS) {
        mvvline(0, CELL_COLS, 0, CELL_ROWS);
    }
    if (LINES > CELL_ROWS && COLS > CELL_COLS) {
        mvaddch(CELL_ROWS, CELL_COLS, ACS_LRCORNER);
    }
}

static void initColors(void) {
    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Sorry, your terminal doesn't support colors!\n");
        fprintf(stderr, "If you think it does, check TERM.\n");
        exit(EX_CONFIG);
    }
    start_color();
    if (COLOR_PAIRS < 103) { // I don't know why, but that's what works.
        endwin();
        fprintf(stderr, "Sorry, your terminal doesn't support enough color pairs!\n");
        fprintf(stderr, "You probably just need to set TERM=$TERM-256color.\n");
        exit(EX_CONFIG);
    }
    for (int bg = COLOR_BLACK; bg < COLOR_BRIGHT; ++bg) {
        for (int fg = COLOR_BLACK; fg < COLOR_BRIGHT; ++fg) {
            init_pair(bg << 4 | fg, fg, bg);
        }
    }
}

int main() {
    client = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (client < 0) err(EX_OSERR, "socket");

    struct sockaddr_un addr = {
        .sun_family = AF_LOCAL,
        .sun_path = "torus.sock",
    };
    int error = connect(client, (struct sockaddr *)&addr, sizeof(addr));
    if (error) err(EX_IOERR, "torus.sock");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, true);
    set_escdelay(100);

    initColors();
    drawBorder();

    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = client, .events = POLLIN },
    };
    for (;;) {
        int nfds = poll(fds, 2, -1);
        if (nfds < 0 && errno == EINTR) continue;
        if (nfds < 0) err(EX_IOERR, "poll");

        if (fds[0].revents) readInput();
        if (fds[1].revents) readMessage();
    }
}
