#if 0
exec cc -Wall -Wextra -pedantic $@ -lcurses -o client $0
#endif

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
    struct ClientMessage msg = { .type = CLIENT_MOVE };
    msg.data.m.dx = dx;
    msg.data.m.dy = dy;
    clientMessage(&msg);
}

static void clientColor(uint8_t color) {
    struct ClientMessage msg = { .type = CLIENT_COLOR };
    msg.data.c = color;
    clientMessage(&msg);
}

static void clientPut(char cell) {
    struct ClientMessage msg = { .type = CLIENT_PUT };
    msg.data.p = cell;
    clientMessage(&msg);
}

static enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_REPLACE,
    MODE_DRAW,
} mode;
static struct {
    int8_t dx;
    int8_t dy;
    uint8_t len;
} insert;
static char drawChar;

static void insertMode(int8_t dx, int8_t dy) {
    mode = MODE_INSERT;
    insert.dx = dx;
    insert.dy = dy;
    insert.len = 0;
}

static void swapCell(int8_t dx, int8_t dy) {
    int sy, sx;
    getyx(stdscr, sy, sx);

    move(sy + dy, sx + dx);
    char swapCell = inch() & 0x7F;
    uint8_t swapColor = CH_COLOR(inch());

    move(sy, sx);
    char cell = inch() & 0x7F;
    uint8_t color = CH_COLOR(inch());

    clientColor(swapColor);
    clientPut(swapCell);
    clientMove(dx, dy);
    clientColor(color);
    clientPut(cell);
}

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
            clientPut(' ');
            insert.len--;
        } else if (c == '\n') {
            clientMove(insert.dy, insert.dx);
            for (uint8_t i = 0; i < insert.len; ++i) {
                clientMove(-insert.dx, -insert.dy);
            }
            insert.len = 0;
        } else if (isprint(c)) {
            clientPut(c);
            clientMove(insert.dx, insert.dy);
            insert.len++;
        }
        return;
    }

    if (mode == MODE_REPLACE) {
        if (isprint(c)) clientPut(c);
        mode = MODE_NORMAL;
        return;
    }

    if (mode == MODE_DRAW && !drawChar) {
        if (c == ESC) mode = MODE_NORMAL;
        if (isprint(c)) {
            drawChar = c;
            clientPut(c);
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
        case 'R': mode = MODE_DRAW; drawChar = 0; break;
        case 'x': clientPut(' '); break;
        case '~': clientPut(inch() & 0x7F); clientMove(1, 0); break;

        case 'h': clientMove(-1,  0); break;
        case 'j': clientMove( 0,  1); break;
        case 'k': clientMove( 0, -1); break;
        case 'l': clientMove( 1,  0); break;
        case 'y': clientMove(-1, -1); break;
        case 'u': clientMove( 1, -1); break;
        case 'b': clientMove(-1,  1); break;
        case 'n': clientMove( 1,  1); break;

        case 'H': swapCell(-1,  0); break;
        case 'J': swapCell( 0,  1); break;
        case 'K': swapCell( 0, -1); break;
        case 'L': swapCell( 1,  0); break;
        case 'Y': swapCell(-1, -1); break;
        case 'U': swapCell( 1, -1); break;
        case 'B': swapCell(-1,  1); break;
        case 'N': swapCell( 1,  1); break;

        case '`': clientColor(CH_COLOR(inch())); break;

        case '1': clientColor(COLOR_RED); break;
        case '2': clientColor(COLOR_GREEN); break;
        case '3': clientColor(COLOR_YELLOW); break;
        case '4': clientColor(COLOR_BLUE); break;
        case '5': clientColor(COLOR_MAGENTA); break;
        case '6': clientColor(COLOR_CYAN); break;
        case '7': clientColor(COLOR_WHITE); break;

        case '!': clientColor(COLOR_BRIGHT | COLOR_RED); break;
        case '@': clientColor(COLOR_BRIGHT | COLOR_GREEN); break;
        case '#': clientColor(COLOR_BRIGHT | COLOR_YELLOW); break;
        case '$': clientColor(COLOR_BRIGHT | COLOR_BLUE); break;
        case '%': clientColor(COLOR_BRIGHT | COLOR_MAGENTA); break;
        case '^': clientColor(COLOR_BRIGHT | COLOR_CYAN); break;
        case '&': clientColor(COLOR_BRIGHT | COLOR_WHITE); break;

        case KEY_LEFT: clientMove(-1,  0); break;
        case KEY_DOWN: clientMove( 0,  1); break;
        case KEY_UP: clientMove( 0, -1); break;
        case KEY_RIGHT: clientMove( 1,  0); break;
    }

    if (mode == MODE_DRAW && drawChar) clientPut(drawChar);
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
        fprintf(stderr, "I only need 16, I promise.\n");
        exit(EX_CONFIG);
    }
    start_color();
    for (int fg = COLOR_RED; fg < COLOR_BRIGHT; ++fg) {
        init_pair(fg, fg, COLOR_BLACK);
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
