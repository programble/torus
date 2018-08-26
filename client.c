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
#include "help.h"

#define err(...) do { endwin(); err(__VA_ARGS__); } while(0)
#define errx(...) do { endwin(); errx(__VA_ARGS__); } while (0)

#define DIV_ROUND(a, b) (((a) + (b) / 2) / (b))

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

static void drawCell(
	const struct Tile *tile, uint8_t cellX, uint8_t cellY, attr_t attr
) {
	uint8_t color = tile->colors[cellY][cellX];
	uint8_t cell = tile->cells[cellY][cellX];

	cchar_t cch;
	wchar_t wch[] = { CP437[cell], L'\0' };
	setcchar(&cch, wch, attr | colorAttr(color), colorPair(color), NULL);
	mvadd_wch(cellY, cellX, &cch);
}

static void drawTile(const struct Tile *tile) {
	for (uint8_t cellY = 0; cellY < CELL_ROWS; ++cellY) {
		for (uint8_t cellX = 0; cellX < CELL_COLS; ++cellX) {
			drawCell(tile, cellX, cellY, A_NORMAL);
		}
	}
}

static int client;

static uint8_t cellX;
static uint8_t cellY;
static struct Tile tile;

static void serverTile(void) {
	ssize_t size = recv(client, &tile, sizeof(tile), 0);
	if (size < 0) err(EX_IOERR, "recv");
	if ((size_t)size < sizeof(tile)) errx(EX_PROTOCOL, "truncated tile");
	drawTile(&tile);
}

static void serverMove(struct ServerMessage msg) {
	cellX = msg.move.cellX;
	cellY = msg.move.cellY;
}

static void serverPut(struct ServerMessage msg) {
	tile.colors[msg.put.cellY][msg.put.cellX] = msg.put.color;
	tile.cells[msg.put.cellY][msg.put.cellX] = msg.put.cell;
	drawCell(&tile, msg.put.cellX, msg.put.cellY, A_NORMAL);
}

static void serverCursor(struct ServerMessage msg) {
	if (msg.cursor.oldCellX != CURSOR_NONE) {
		drawCell(&tile, msg.cursor.oldCellX, msg.cursor.oldCellY, A_NORMAL);
	}
	if (msg.cursor.newCellX != CURSOR_NONE) {
		drawCell(&tile, msg.cursor.newCellX, msg.cursor.newCellY, A_REVERSE);
	}
}

static const uint8_t MAP_X = (CELL_COLS / 2) - (3 * MAP_COLS / 2);
static const uint8_t MAP_Y = (CELL_ROWS / 2) - (MAP_ROWS / 2);

static const wchar_t MAP_CELLS[5] = L" ░▒▓█";
static const uint8_t MAP_COLORS[] = {
	COLOR_BLUE, COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW, COLOR_RED,
};

static void serverMap(void) {
	int t = MAP_Y - 1;
	int l = MAP_X - 1;
	int b = MAP_Y + MAP_ROWS;
	int r = MAP_X + 3 * MAP_COLS;
	color_set(colorPair(COLOR_WHITE), NULL);
	mvhline(t, MAP_X, ACS_HLINE, 3 * MAP_COLS);
	mvhline(b, MAP_X, ACS_HLINE, 3 * MAP_COLS);
	mvvline(MAP_Y, l, ACS_VLINE, MAP_ROWS);
	mvvline(MAP_Y, r, ACS_VLINE, MAP_ROWS);
	mvaddch(t, l, ACS_ULCORNER);
	mvaddch(t, r, ACS_URCORNER);
	mvaddch(b, l, ACS_LLCORNER);
	mvaddch(b, r, ACS_LRCORNER);
	color_set(0, NULL);

	struct Map map;
	ssize_t size = recv(client, &map, sizeof(map), 0);
	if (size < 0) err(EX_IOERR, "recv");
	if ((size_t)size < sizeof(map)) errx(EX_PROTOCOL, "truncated map");

	if (0 == map.max.modifyCount) return;
	if (0 == map.now - map.min.createTime) return;

	for (uint8_t y = 0; y < MAP_ROWS; ++y) {
		for (uint8_t x = 0; x < MAP_COLS; ++x) {
			struct Meta meta = map.meta[y][x];

			uint32_t count = DIV_ROUND(
				(ARRAY_LEN(MAP_CELLS) - 1) * meta.modifyCount,
				map.max.modifyCount
			);
			uint32_t time = DIV_ROUND(
				(ARRAY_LEN(MAP_COLORS) - 1) * (meta.modifyTime - map.min.createTime),
				map.now - map.min.createTime
			);
			if (!meta.modifyTime) time = 0;

			wchar_t cell = MAP_CELLS[count];
			uint8_t color = MAP_COLORS[time];
			wchar_t tile[] = { cell, cell, cell, L'\0' };
			attr_set(colorAttr(color), colorPair(color), NULL);
			mvaddwstr(MAP_Y + y, MAP_X + 3 * x, tile);
		}
	}
	attr_set(A_NORMAL, 0, NULL);
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
		MODE_HELP,
		MODE_MAP,
		MODE_DIRECTION,
		MODE_INSERT,
		MODE_REPLACE,
		MODE_DRAW,
		MODE_LINE,
	} mode;
	uint8_t color;
	uint8_t shift;
	uint8_t draw;
} input = {
	.color = COLOR_WHITE,
};

static struct {
	uint8_t color;
	uint8_t cell;
} copy;

static struct {
	int8_t dx;
	int8_t dy;
	uint8_t len;
} insert;

static void modeNormal(void) {
	curs_set(1);
	move(cellY, cellX);
	input.mode = MODE_NORMAL;
}
static void modeHelp(void) {
	curs_set(0);
	drawTile(HELP);
	input.mode = MODE_HELP;
}
static void modeMap(void) {
	curs_set(0);
	clientMap();
	input.mode = MODE_MAP;
}
static void modeDirection(void) {
	input.mode = MODE_DIRECTION;
}
static void modeInsert(int8_t dx, int8_t dy) {
	insert.dx = dx;
	insert.dy = dy;
	insert.len = 0;
	input.mode = MODE_INSERT;
}
static void modeReplace(void) {
	input.mode = MODE_REPLACE;
}
static void modeDraw(void) {
	input.draw = 0;
	input.mode = MODE_DRAW;
}
static void modeLine(void) {
	input.mode = MODE_LINE;
}

static void colorFg(uint8_t fg) {
	input.color = (input.color & 0x78) | (fg & 0x07);
}
static void colorBg(uint8_t bg) {
	input.color = (input.color & 0x0F) | (bg & 0x07) << 4;
}

static uint8_t colorInvert(uint8_t color) {
	return (color & 0x08)
		| (color & 0x70) >> 4
		| (color & 0x07) << 4;
}

static void cellCopy(void) {
	copy.color = tile.colors[cellY][cellX];
	copy.cell = tile.cells[cellY][cellX];
}

static void cellSwap(int8_t dx, int8_t dy) {
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
	if (ch == ' ') return ' ';
	if (ch < 0x80) return (uint8_t)ch + input.shift;
	for (size_t i = 0; i < ARRAY_LEN(CP437); ++i) {
		if (ch == CP437[i]) return i;
	}
	return 0;
}

static void inputNormal(bool keyCode, wchar_t ch) {
	if (keyCode) {
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
		return;
	}

	switch (ch) {
		break; case CTRL('L'): clearok(curscr, true);

		break; case ESC: modeNormal(); input.shift = 0;
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

		break; case '0': colorFg(COLOR_BLACK);
		break; case '1': colorFg(COLOR_RED);
		break; case '2': colorFg(COLOR_GREEN);
		break; case '3': colorFg(COLOR_YELLOW);
		break; case '4': colorFg(COLOR_BLUE);
		break; case '5': colorFg(COLOR_MAGENTA);
		break; case '6': colorFg(COLOR_CYAN);
		break; case '7': colorFg(COLOR_WHITE);

		break; case ')': colorBg(COLOR_BLACK);
		break; case '!': colorBg(COLOR_RED);
		break; case '@': colorBg(COLOR_GREEN);
		break; case '#': colorBg(COLOR_YELLOW);
		break; case '$': colorBg(COLOR_BLUE);
		break; case '%': colorBg(COLOR_MAGENTA);
		break; case '^': colorBg(COLOR_CYAN);
		break; case '&': colorBg(COLOR_WHITE);

		break; case '8': input.color ^= COLOR_BRIGHT;
		break; case '9': input.color = colorInvert(input.color);
		break; case '`': input.color = tile.colors[cellY][cellX];

		break; case 'H': cellSwap(-1,  0);
		break; case 'L': cellSwap( 1,  0);
		break; case 'K': cellSwap( 0, -1);
		break; case 'J': cellSwap( 0,  1);
		break; case 'Y': cellSwap(-1, -1);
		break; case 'U': cellSwap( 1, -1);
		break; case 'B': cellSwap(-1,  1);
		break; case 'N': cellSwap( 1,  1);

		break; case 's': cellCopy();
		break; case 'x': cellCopy(); clientPut(copy.color, ' ');
		break; case 'p': clientPut(copy.color, copy.cell);

		break; case '~': {
			cellCopy();
			clientPut(input.color, tile.cells[cellY][cellX]);
			clientMove(1, 0);
		}
		break; case '*': {
			clientPut(
				tile.colors[cellY][cellX] ^ COLOR_BRIGHT,
				tile.cells[cellY][cellX]
			);
			clientMove(1, 0);
		}
		break; case '(': {
			clientPut(
				colorInvert(tile.colors[cellY][cellX]),
				tile.cells[cellY][cellX]
			);
			clientMove(1, 0);
		}

		break; case CTRL('A'): {
			clientPut(tile.colors[cellY][cellX], tile.cells[cellY][cellX] + 1);
		}
		break; case CTRL('X'): {
			clientPut(tile.colors[cellY][cellX], tile.cells[cellY][cellX] - 1);
		}

		break; case '?': modeHelp();
		break; case 'm': modeMap();
		break; case 'I': modeDirection();
		break; case 'i': modeInsert(1, 0);
		break; case 'a': modeInsert(1, 0); clientMove(1, 0);
		break; case 'r': modeReplace(); cellCopy();
		break; case 'R': modeDraw();
		break; case '.': modeLine();
	}
}

static void inputHelp(bool keyCode, wchar_t ch) {
	(void)keyCode;
	(void)ch;
	if (tile.meta.createTime) drawTile(&tile);
	modeNormal();
}

static void inputMap(bool keyCode, wchar_t ch) {
	(void)keyCode;
	(void)ch;
	drawTile(&tile);
	modeNormal();
}

static void inputDirection(bool keyCode, wchar_t ch) {
	if (keyCode) return;
	switch (ch) {
		break; case ESC: modeNormal();
		break; case 'h': modeInsert(-1,  0);
		break; case 'l': modeInsert( 1,  0);
		break; case 'k': modeInsert( 0, -1);
		break; case 'j': modeInsert( 0,  1);
		break; case 'y': modeInsert(-1, -1);
		break; case 'u': modeInsert( 1, -1);
		break; case 'b': modeInsert(-1,  1);
		break; case 'n': modeInsert( 1,  1);
	}
}

static void inputInsert(bool keyCode, wchar_t ch) {
	if (keyCode) {
		inputNormal(keyCode, ch);
		return;
	}
	switch (ch) {
		break; case ESC: {
			clientMove(-insert.dx, -insert.dy);
			modeNormal();
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

static void inputReplace(bool keyCode, wchar_t ch) {
	if (keyCode) {
		inputNormal(keyCode, ch);
		return;
	}
	if (ch != ESC) {
		uint8_t cell = inputCell(ch);
		if (!cell) return;
		clientPut(tile.colors[cellY][cellX], cell);
	}
	modeNormal();
}

static void inputDraw(bool keyCode, wchar_t ch) {
	if (!keyCode && ch == ESC) {
		modeNormal();
		return;
	}
	if (input.draw) {
		inputNormal(keyCode, ch);
	} else {
		if (keyCode) {
			inputNormal(keyCode, ch);
			return;
		}
		input.draw = inputCell(ch);
	}
	clientPut(input.color, input.draw);
}

static uint8_t lineCell(uint8_t cell, int8_t dx, int8_t dy) {
	if (dx < 0) {
		switch (CP437[cell]) {
			default:   return inputCell(L'→');
			case L'←': return inputCell(L'─'); case L'─': return 0;
			case L'↑': return inputCell(L'┐'); case L'┐': return 0;
			case L'↓': return inputCell(L'┘'); case L'┘': return 0;
			case L'│': return inputCell(L'┤'); case L'┤': return 0;
			case L'└': return inputCell(L'┴'); case L'┴': return 0;
			case L'┌': return inputCell(L'┬'); case L'┬': return 0;
			case L'├': return inputCell(L'┼'); case L'┼': return 0;
		}
	} else if (dx > 0) {
		switch (CP437[cell]) {
			default:   return inputCell(L'←');
			case L'→': return inputCell(L'─'); case L'─': return 0;
			case L'↑': return inputCell(L'┌'); case L'┌': return 0;
			case L'↓': return inputCell(L'└'); case L'└': return 0;
			case L'│': return inputCell(L'├'); case L'├': return 0;
			case L'┘': return inputCell(L'┴'); case L'┴': return 0;
			case L'┐': return inputCell(L'┬'); case L'┬': return 0;
			case L'┤': return inputCell(L'┼'); case L'┼': return 0;
		}
	} else if (dy < 0) {
		switch (CP437[cell]) {
			default:   return inputCell(L'↓');
			case L'↑': return inputCell(L'│'); case L'│': return 0;
			case L'←': return inputCell(L'└'); case L'└': return 0;
			case L'→': return inputCell(L'┘'); case L'┘': return 0;
			case L'─': return inputCell(L'┴'); case L'┴': return 0;
			case L'┌': return inputCell(L'├'); case L'├': return 0;
			case L'┐': return inputCell(L'┤'); case L'┤': return 0;
			case L'┬': return inputCell(L'┼'); case L'┼': return 0;
		}
	} else if (dy > 0) {
		switch (CP437[cell]) {
			default:   return inputCell(L'↑');
			case L'↓': return inputCell(L'│'); case L'│': return 0;
			case L'←': return inputCell(L'┌'); case L'┌': return 0;
			case L'→': return inputCell(L'┐'); case L'┐': return 0;
			case L'─': return inputCell(L'┬'); case L'┬': return 0;
			case L'└': return inputCell(L'├'); case L'├': return 0;
			case L'┘': return inputCell(L'┤'); case L'┤': return 0;
			case L'┴': return inputCell(L'┼'); case L'┼': return 0;
		}
	}
	return 0;
}

static void inputLine(bool keyCode, wchar_t ch) {
	int8_t dx = 0;
	int8_t dy = 0;
	if (keyCode) {
		switch (ch) {
			break; case KEY_LEFT:  dx = -1;
			break; case KEY_RIGHT: dx =  1;
			break; case KEY_UP:    dy = -1;
			break; case KEY_DOWN:  dy =  1;
			break; default: return;
		}
	} else {
		switch (ch) {
			break; case ESC: case '.': modeNormal(); return;
			break; case 'h': dx = -1;
			break; case 'l': dx =  1;
			break; case 'k': dy = -1;
			break; case 'j': dy =  1;
			break; default: return;
		}
	}
	if ((uint8_t)(cellX + dx) >= CELL_COLS) return;
	if ((uint8_t)(cellY + dy) >= CELL_ROWS) return;

	uint8_t leave = lineCell(tile.cells[cellY][cellX], dx, dy);
	uint8_t enter = lineCell(tile.cells[cellY + dy][cellX + dx], -dx, -dy);

	if (leave) clientPut(input.color, leave);
	clientMove(dx, dy);
	if (enter) clientPut(input.color, enter);
}

static void readInput(void) {
	wint_t ch;
	bool keyCode = (KEY_CODE_YES == get_wch(&ch));
	switch (input.mode) {
		break; case MODE_NORMAL:    inputNormal(keyCode, ch);
		break; case MODE_HELP:      inputHelp(keyCode, ch);
		break; case MODE_MAP:       inputMap(keyCode, ch);
		break; case MODE_DIRECTION: inputDirection(keyCode, ch);
		break; case MODE_INSERT:    inputInsert(keyCode, ch);
		break; case MODE_REPLACE:   inputReplace(keyCode, ch);
		break; case MODE_DRAW:      inputDraw(keyCode, ch);
		break; case MODE_LINE:      inputLine(keyCode, ch);
	}
}

int main() {
	curse();
	modeHelp();
	readInput();

	client = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (client < 0) err(EX_OSERR, "socket");

	struct sockaddr_un addr = {
		.sun_family = AF_LOCAL,
		.sun_path = "torus.sock",
	};
	int error = connect(client, (struct sockaddr *)&addr, sizeof(addr));
	if (error) err(EX_NOINPUT, "torus.sock");

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
