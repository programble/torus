/* Copyright (C) 2018  Curtis McEnroe <june@causal.agency>
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

#define _XOPEN_SOURCE_EXTENDED

#include <curses.h>
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sysexits.h>
#include <unistd.h>
#include <wchar.h>

#include "torus.h"

#define err(...) do { endwin(); err(__VA_ARGS__); } while(0)
#define errx(...) do { endwin(); errx(__VA_ARGS__); } while (0)

#define CTRL(ch) ((ch) ^ 0x40)
enum {
	ESC = 0x1B,
	DEL = 0x7F,
};

static void curse(void) {
	setlocale(LC_CTYPE, "");

	initscr();
	start_color();
	if (!has_colors() || COLOR_PAIRS < 64) {
		endwin();
		fprintf(stderr, "Sorry, your terminal doesn't support colors!\n");
		fprintf(stderr, "If you think it should, check TERM.\n");
		exit(EX_CONFIG);
	}
	if (LINES < CELL_ROWS || COLS < CELL_COLS) {
		endwin();
		fprintf(
			stderr,
			"Sorry, your terminal is too small!\n"
			"It must be at least %ux%u characters.\n",
			CELL_COLS, CELL_ROWS
		);
		exit(EX_CONFIG);
	}

	assume_default_colors(0, 0);
	if (COLORS >= 16) {
		for (short pair = 1; pair < 0x80; ++pair) {
			init_pair(pair, pair & 0x0F, (pair & 0xF0) >> 4);
		}
	} else {
		for (short pair = 1; pair < 0100; ++pair) {
			init_pair(pair, pair & 007, (pair & 070) >> 3);
		}
	}

	color_set(COLOR_WHITE, NULL);
	bool hline = (LINES > CELL_ROWS);
	bool vline = (COLS > CELL_COLS);
	if (hline) mvhline(CELL_ROWS, 0, 0, CELL_COLS);
	if (vline) mvvline(0, CELL_COLS, 0, CELL_ROWS);
	if (hline && vline) mvaddch(CELL_ROWS, CELL_COLS, ACS_LRCORNER);
	color_set(0, NULL);

	cbreak();
	noecho();
	keypad(stdscr, true);
	set_escdelay(100);
}

static attr_t colorAttr(uint8_t color) {
	if (COLORS >= 16) return A_NORMAL;
	return (color & COLOR_BRIGHT) ? A_BOLD : A_NORMAL;
}
static short colorPair(uint8_t color) {
	if (COLORS >= 16) return color;
	return (color & 0x70) >> 1 | (color & 0x07);
}

static uint8_t cellX;
static uint8_t cellY;
static struct Tile tile;

static void tileDraw(uint8_t cellX, uint8_t cellY, attr_t attr) {
	uint8_t color = tile.colors[cellY][cellX];
	uint8_t cell = tile.cells[cellY][cellX];

	cchar_t cch;
	wchar_t wch[] = { CP437[cell], L'\0' };
	setcchar(&cch, wch, attr | colorAttr(color), colorPair(color), NULL);
	mvadd_wch(cellY, cellX, &cch);
}

static int client;

static void serverTile(void) {
	ssize_t size = recv(client, &tile, sizeof(tile), 0);
	if (size < 0) err(EX_IOERR, "recv");
	if ((size_t)size < sizeof(tile)) errx(EX_PROTOCOL, "truncated tile");

	for (uint8_t cellY = 0; cellY < CELL_ROWS; ++cellY) {
		for (uint8_t cellX = 0; cellX < CELL_COLS; ++cellX) {
			tileDraw(cellX, cellY, A_NORMAL);
		}
	}
}

static void serverMove(struct ServerMessage msg) {
	cellX = msg.move.cellX;
	cellY = msg.move.cellY;
}

static void serverPut(struct ServerMessage msg) {
	tile.colors[msg.put.cellY][msg.put.cellX] = msg.put.color;
	tile.cells[msg.put.cellY][msg.put.cellX] = msg.put.cell;
	tileDraw(msg.put.cellX, msg.put.cellY, A_NORMAL);
}

static void serverCursor(struct ServerMessage msg) {
	if (msg.cursor.oldCellX != CURSOR_NONE) {
		tileDraw(msg.cursor.oldCellX, msg.cursor.oldCellY, A_NORMAL);
	}
	if (msg.cursor.newCellX != CURSOR_NONE) {
		tileDraw(msg.cursor.newCellX, msg.cursor.newCellY, A_REVERSE);
	}
}

static void serverMap(void) {
}

static void readMessage(void) {
	struct ServerMessage msg;
	ssize_t size = recv(client, &msg, sizeof(msg), 0);
	if (size < 0) err(EX_IOERR, "recv");
	if ((size_t)size < sizeof(msg)) errx(EX_PROTOCOL, "truncated message");

	switch (msg.type) {
		break; case SERVER_TILE:   serverTile();
		break; case SERVER_MOVE:   serverMove(msg);
		break; case SERVER_PUT:    serverPut(msg);
		break; case SERVER_CURSOR: serverCursor(msg);
		break; case SERVER_MAP:    serverMap();
		break; default: errx(EX_PROTOCOL, "unknown message type %d", msg.type);
	}
	move(cellY, cellX);
}

static void clientMessage(struct ClientMessage msg) {
	ssize_t size = send(client, &msg, sizeof(msg), 0);
	if (size < 0) err(EX_IOERR, "send");
}

static void clientMove(int8_t dx, int8_t dy) {
	struct ClientMessage msg = {
		.type = CLIENT_MOVE,
		.move = { .dx = dx, .dy = dy },
	};
	clientMessage(msg);
}

static void clientFlip(void) {
	struct ClientMessage msg = { .type = CLIENT_FLIP };
	clientMessage(msg);
}

static void clientPut(uint8_t color, uint8_t cell) {
	struct ClientMessage msg = {
		.type = CLIENT_PUT,
		.put = { .color = color, .cell = cell },
	};
	clientMessage(msg);
}

static void clientMap(void) {
	struct ClientMessage msg = { .type = CLIENT_MAP };
	clientMessage(msg);
}

static struct {
	enum {
		MODE_NORMAL,
		MODE_DIRECTION,
		MODE_INSERT,
		MODE_REPLACE,
		MODE_DRAW,
	} mode;
	uint8_t color;
	uint8_t shift;
	uint8_t draw;
} input = {
	.color = COLOR_WHITE,
};

static struct {
	int8_t dx;
	int8_t dy;
	uint8_t len;
} insert;

static void insertMode(int8_t dx, int8_t dy) {
	input.mode = MODE_INSERT;
	insert.dx = dx;
	insert.dy = dy;
	insert.len = 0;
}

static void inputFg(uint8_t fg) {
	input.color = (input.color & 0x78) | (fg & 0x07);
}
static void inputBg(uint8_t bg) {
	input.color = (input.color & 0x0F) | (bg & 0x07) << 4;
}
static void inputInvert(void) {
	input.color = (input.color & 0x08)
		| (input.color & 0x70) >> 4
		| (input.color & 0x07) << 4;
}

static void inputSwap(int8_t dx, int8_t dy) {
	if ((uint8_t)(cellX + dx) >= CELL_COLS) return;
	if ((uint8_t)(cellY + dy) >= CELL_ROWS) return;

	uint8_t aColor = tile.colors[cellY][cellX];
	uint8_t aCell = tile.cells[cellY][cellX];

	uint8_t bColor = tile.colors[cellY + dy][cellX + dx];
	uint8_t bCell = tile.cells[cellY + dy][cellX + dx];

	clientPut(bColor, bCell);
	clientMove(dx, dy);
	clientPut(aColor, aCell);
}

static uint8_t inputCell(wchar_t ch) {
	if (ch < 0x80) return (uint8_t)ch + input.shift;
	for (size_t i = 0; i < ARRAY_LEN(CP437); ++i) {
		if (ch == CP437[i]) return i;
	}
	return 0;
}

static void inputKeyCode(wchar_t ch) {
	switch (ch) {
		break; case KEY_LEFT:  clientMove(-1,  0);
		break; case KEY_RIGHT: clientMove( 1,  0);
		break; case KEY_UP:    clientMove( 0, -1);
		break; case KEY_DOWN:  clientMove( 0,  1);

		break; case KEY_F(1): input.shift = 0x00;
		break; case KEY_F(2): input.shift = 0xC0;
		break; case KEY_F(3): input.shift = 0xA0;
		break; case KEY_F(4): input.shift = 0x70;
		break; case KEY_F(5): input.shift = 0x40;
	}
}

static void inputNormal(wchar_t ch) {
	switch (ch) {
		break; case CTRL('L'): clearok(curscr, true);

		break; case ESC: input.mode = MODE_NORMAL; input.shift = 0;
		break; case 'q': endwin(); exit(EX_OK);

		break; case 'g': clientFlip();
		break; case 'h': clientMove(-1,  0);
		break; case 'l': clientMove( 1,  0);
		break; case 'k': clientMove( 0, -1);
		break; case 'j': clientMove( 0,  1);
		break; case 'y': clientMove(-1, -1);
		break; case 'u': clientMove( 1, -1);
		break; case 'b': clientMove(-1,  1);
		break; case 'n': clientMove( 1,  1);

		break; case '0': inputFg(COLOR_BLACK);
		break; case '1': inputFg(COLOR_RED);
		break; case '2': inputFg(COLOR_GREEN);
		break; case '3': inputFg(COLOR_YELLOW);
		break; case '4': inputFg(COLOR_BLUE);
		break; case '5': inputFg(COLOR_MAGENTA);
		break; case '6': inputFg(COLOR_CYAN);
		break; case '7': inputFg(COLOR_WHITE);

		break; case ')': inputBg(COLOR_BLACK);
		break; case '!': inputBg(COLOR_RED);
		break; case '@': inputBg(COLOR_GREEN);
		break; case '#': inputBg(COLOR_YELLOW);
		break; case '$': inputBg(COLOR_BLUE);
		break; case '%': inputBg(COLOR_MAGENTA);
		break; case '^': inputBg(COLOR_CYAN);
		break; case '&': inputBg(COLOR_WHITE);

		break; case '8': case '*': input.color ^= COLOR_BRIGHT;
		break; case '9': case '(': inputInvert();
		break; case '`': input.color = tile.colors[cellY][cellX];

		break; case 'H': inputSwap(-1,  0);
		break; case 'L': inputSwap( 1,  0);
		break; case 'K': inputSwap( 0, -1);
		break; case 'J': inputSwap( 0,  1);
		break; case 'Y': inputSwap(-1, -1);
		break; case 'U': inputSwap( 1, -1);
		break; case 'B': inputSwap(-1,  1);
		break; case 'N': inputSwap( 1,  1);

		break; case 'x': clientPut(tile.colors[cellY][cellX], ' ');
		break; case '~': {
			clientPut(input.color, tile.cells[cellY][cellX]);
			clientMove(1, 0);
		}

		break; case CTRL('A'): {
			clientPut(tile.colors[cellY][cellX], tile.cells[cellY][cellX] + 1);
		}
		break; case CTRL('X'): {
			clientPut(tile.colors[cellY][cellX], tile.cells[cellY][cellX] - 1);
		}

		break; case 'i': insertMode(1, 0);
		break; case 'a': clientMove(1, 0); insertMode(1, 0);
		break; case 'I': input.mode = MODE_DIRECTION;
		break; case 'r': input.mode = MODE_REPLACE;
		break; case 'R': input.mode = MODE_DRAW; input.draw = 0;
	}
}

static void inputDirection(wchar_t ch) {
	switch (ch) {
		break; case ESC: input.mode = MODE_NORMAL;
		break; case 'h': insertMode(-1,  0);
		break; case 'l': insertMode( 1,  0);
		break; case 'k': insertMode( 0, -1);
		break; case 'j': insertMode( 0,  1);
		break; case 'y': insertMode(-1, -1);
		break; case 'u': insertMode( 1, -1);
		break; case 'b': insertMode(-1,  1);
		break; case 'n': insertMode( 1,  1);
	}
}

static void inputInsert(wchar_t ch) {
	switch (ch) {
		break; case ESC: {
			input.mode = MODE_NORMAL;
			clientMove(-insert.dx, -insert.dy);
		}
		break; case '\b': case DEL: {
			clientMove(-insert.dx, -insert.dy);
			clientPut(input.color, ' ');
			insert.len--;
		}
		break; case '\n': {
			clientMove(insert.dy, insert.dx);
			clientMove(insert.len * -insert.dx, insert.len * -insert.dy);
			insert.len = 0;
		}
		break; default: {
			uint8_t cell = inputCell(ch);
			if (!cell) break;
			clientPut(input.color, cell);
			clientMove(insert.dx, insert.dy);
			insert.len++;
		}
	}
}

static void inputReplace(wchar_t ch) {
	if (ch != ESC) {
		uint8_t cell = inputCell(ch);
		if (!cell) return;
		clientPut(tile.colors[cellY][cellX], cell);
	}
	input.mode = MODE_NORMAL;
}

static void inputDraw(wchar_t ch) {
	if (ch == ESC) {
		input.mode = MODE_NORMAL;
		return;
	}
	if (input.draw) {
		inputNormal(ch);
	} else {
		input.draw = inputCell(ch);
	}
	clientPut(input.color, input.draw);
}

static void readInput(void) {
	wint_t ch;
	if (KEY_CODE_YES == get_wch(&ch)) {
		inputKeyCode(ch);
		return;
	}
	switch (input.mode) {
		break; case MODE_NORMAL:    inputNormal(ch);
		break; case MODE_DIRECTION: inputDirection(ch);
		break; case MODE_INSERT:    inputInsert(ch);
		break; case MODE_REPLACE:   inputReplace(ch);
		break; case MODE_DRAW:      inputDraw(ch);
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
	if (error) err(EX_NOINPUT, "torus.sock");

	curse();

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

		refresh();
	}
}
