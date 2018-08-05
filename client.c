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

#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sysexits.h>
#include <unistd.h>

#include "torus.h"

enum {
	ESC = 0x1B,
	DEL = 0x7F,
};

static int client;

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

static void clientPut(uint8_t color, char cell) {
	struct ClientMessage msg = {
		.type = CLIENT_PUT,
		.put = { .color = color, .cell = cell },
	};
	clientMessage(msg);
}

static void clientSpawn(uint8_t spawn) {
	struct ClientMessage msg = {
		.type = CLIENT_SPAWN,
		.spawn = spawn,
	};
	clientMessage(msg);
}

static void clientMap(void) {
	struct ClientMessage msg = { .type = CLIENT_MAP };
	clientMessage(msg);
}

static void colorPairs(void) {
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
}

static chtype colorAttr(uint8_t color) {
	if (COLORS >= 16) return COLOR_PAIR(color);
	chtype bold = (color & COLOR_BRIGHT) ? A_BOLD : A_NORMAL;
	short pair = (color & 0x70) >> 1 | (color & 0x07);
	return bold | COLOR_PAIR(pair);
}

static uint8_t attrColor(chtype attr) {
	if (COLORS >= 16) return PAIR_NUMBER(attr);
	uint8_t bright = (attr & A_BOLD) ? COLOR_BRIGHT : 0;
	short pair = PAIR_NUMBER(attr);
	return (pair & 070) << 1 | bright | (pair & 007);
}

static struct {
	int8_t speed;
	uint8_t color;
	enum {
		MODE_NORMAL,
		MODE_MAP,
		MODE_INSERT,
		MODE_REPLACE,
		MODE_PUT,
		MODE_DRAW,
	} mode;
	int8_t dx;
	int8_t dy;
	uint8_t len;
	char draw;
} input = {
	.speed = 1,
	.color = COLOR_WHITE,
	.dx = 1,
};

static void colorFg(uint8_t fg) {
	input.color = (input.color & 0xF8) | fg;
}

static void colorBg(uint8_t bg) {
	input.color = (input.color & 0x0F) | (bg << 4);
}

static void colorInvert(void) {
	input.color =
		(input.color & 0x08) |
		((input.color & 0x07) << 4) |
		((input.color & 0x70) >> 4);
}

static void insertMode(int8_t dx, int8_t dy) {
	input.mode = MODE_INSERT;
	input.dx = dx;
	input.dy = dy;
	input.len = 0;
}

static void swapCell(int8_t dx, int8_t dy) {
	uint8_t aColor = attrColor(inch());
	char aCell = inch() & A_CHARTEXT;

	int sy, sx;
	getyx(stdscr, sy, sx);
	move(sy + dy, sx + dx);
	uint8_t bColor = attrColor(inch());
	char bCell = inch() & A_CHARTEXT;
	move(sy, sx);

	clientPut(bColor, bCell);
	clientMove(dx, dy);
	clientPut(aColor, aCell);
}

static void inputNormal(int c) {
	switch (c) {
		break; case ESC: input.mode = MODE_NORMAL;

		break; case 'q': endwin(); exit(EX_OK);
		break; case 'Q': {
			if ((input.color & 0x07) < ARRAY_LEN(SPAWNS)) {
				clientSpawn(input.color & 0x07);
			} else {
				clientSpawn(0);
			}
		}
		break; case 'm': clientMap();

		break; case 'i': insertMode(1, 0);
		break; case 'a': clientMove(1, 0); insertMode(1, 0);
		break; case 'I': insertMode(0, 0);
		break; case 'r': input.mode = MODE_REPLACE;
		break; case 'p': input.mode = MODE_PUT;
		break; case 'R': input.mode = MODE_DRAW; input.draw = 0;
		break; case 'x': clientPut(attrColor(inch()), ' ');

		break; case '~': {
			clientPut(input.color, inch() & A_CHARTEXT);
			clientMove(input.dx, input.dy);
		}

		break; case '[': if (input.speed > 1) input.speed--;
		break; case ']': if (input.speed < 4) input.speed++;

		break; case 'h': clientMove(-input.speed,            0);
		break; case 'j': clientMove(           0,  input.speed);
		break; case 'k': clientMove(           0, -input.speed);
		break; case 'l': clientMove( input.speed,            0);
		break; case 'y': clientMove(-input.speed, -input.speed);
		break; case 'u': clientMove( input.speed, -input.speed);
		break; case 'b': clientMove(-input.speed,  input.speed);
		break; case 'n': clientMove( input.speed,  input.speed);

		break; case 'H': swapCell(-1,  0);
		break; case 'J': swapCell( 0,  1);
		break; case 'K': swapCell( 0, -1);
		break; case 'L': swapCell( 1,  0);
		break; case 'Y': swapCell(-1, -1);
		break; case 'U': swapCell( 1, -1);
		break; case 'B': swapCell(-1,  1);
		break; case 'N': swapCell( 1,  1);

		break; case '`': input.color = attrColor(inch());

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

		break; case '*': case '8': input.color ^= COLOR_BRIGHT;

		break; case '(': case '9': colorInvert();

		break; case KEY_LEFT:  clientMove(-1,  0);
		break; case KEY_DOWN:  clientMove( 0,  1);
		break; case KEY_UP:    clientMove( 0, -1);
		break; case KEY_RIGHT: clientMove( 1,  0);
	}
}

static void inputMap(void) {
	input.mode = MODE_NORMAL;
	curs_set(1);
	touchwin(stdscr);
}

static void inputInsert(int c) {
	if (c == ESC) {
		input.mode = MODE_NORMAL;
		clientMove(-input.dx, -input.dy);
	} else if (!input.dx && !input.dy) {
		switch (c) {
			break; case 'h': insertMode(-1,  0);
			break; case 'j': insertMode( 0,  1);
			break; case 'k': insertMode( 0, -1);
			break; case 'l': insertMode( 1,  0);
			break; case 'y': insertMode(-1, -1);
			break; case 'u': insertMode( 1, -1);
			break; case 'b': insertMode(-1,  1);
			break; case 'n': insertMode( 1,  1);
		}
	} else if (c == '\b' || c == DEL) {
		clientMove(-input.dx, -input.dy);
		clientPut(input.color, ' ');
		input.len--;
	} else if (c == '\n') {
		clientMove(input.dy, input.dx);
		clientMove(-input.dx * input.len, -input.dy * input.len);
		input.len = 0;
	} else if (isprint(c)) {
		clientPut(input.color, c);
		clientMove(input.dx, input.dy);
		input.len++;
	}
}

static void inputReplace(int c) {
	if (isprint(c)) clientPut(attrColor(inch()), c);
	input.mode = MODE_NORMAL;
}

static void inputPut(int c) {
	if (isprint(c)) clientPut(input.color, c);
	input.mode = MODE_NORMAL;
}

static void inputDraw(int c) {
	if (input.draw) {
		inputNormal(c);
		clientPut(input.color, input.draw);
	} else if (isprint(c)) {
		input.draw = c;
		clientPut(input.color, c);
	} else if (c == ESC) {
		input.mode = MODE_NORMAL;
	}
}

static void readInput(void) {
	int c = getch();
	switch (input.mode) {
		break; case MODE_NORMAL:  inputNormal(c);
		break; case MODE_MAP:     inputMap();
		break; case MODE_INSERT:  inputInsert(c);
		break; case MODE_REPLACE: inputReplace(c);
		break; case MODE_PUT:     inputPut(c);
		break; case MODE_DRAW:    inputDraw(c);
	}
}

static void serverPut(uint8_t x, uint8_t y, uint8_t color, char cell) {
	mvaddch(y, x, colorAttr(color) | cell);
}

static void serverTile(void) {
	struct Tile tile;
	ssize_t size = recv(client, &tile, sizeof(tile), 0);
	if (size < 0) err(EX_IOERR, "recv");
	if ((size_t)size < sizeof(tile)) {
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

static WINDOW *mapFrame;
static WINDOW *mapWindow;

static const char MAP_CELLS[] = " -~=+:$%#";
static const uint8_t MAP_COLORS[] = {
	COLOR_BLUE,    COLOR_BRIGHT | COLOR_BLUE,
	COLOR_CYAN,    COLOR_BRIGHT | COLOR_CYAN,
	COLOR_GREEN,   COLOR_BRIGHT | COLOR_GREEN,
	COLOR_YELLOW,  COLOR_BRIGHT | COLOR_YELLOW,
	COLOR_RED,     COLOR_BRIGHT | COLOR_RED,
	COLOR_MAGENTA, COLOR_BRIGHT | COLOR_MAGENTA,
	COLOR_WHITE,   COLOR_BRIGHT | COLOR_WHITE,
};

static void serverMap(void) {
	struct Map map;
	ssize_t size = recv(client, &map, sizeof(map), 0);
	if (size < 0) err(EX_IOERR, "recv");
	if ((size_t)size < sizeof(map)) errx(EX_PROTOCOL, "This map is incomplete...");

	uint32_t countMax = 0;
	time_t timeNow = time(NULL);
	time_t timeMin = timeNow;
	for (int y = 0; y < MAP_ROWS; ++y) {
		for (int x = 0; x < MAP_COLS; ++x) {
			struct MapTile tile = map.tiles[y][x];
			if (countMax < tile.modifyCount) countMax = tile.modifyCount;
			if (tile.modifyTime && timeMin > tile.modifyTime) {
				timeMin = tile.modifyTime;
			}
		}
	}

	for (int y = 0; y < MAP_ROWS; ++y) {
		for (int x = 0; x < MAP_COLS; ++x) {
			struct MapTile tile = map.tiles[y][x];

			double count = (tile.modifyCount && countMax > 1)
				? log(tile.modifyCount) / log(countMax)
				: 0.0;
			double time = (tile.modifyTime && timeNow - timeMin)
				? (double)(tile.modifyTime - timeMin) / (double)(timeNow - timeMin)
				: 0.0;
			count *= ARRAY_LEN(MAP_CELLS) - 2;
			time *= ARRAY_LEN(MAP_COLORS) - 1;

			char cell = MAP_CELLS[(int)round(count)];
			chtype attr = colorAttr(MAP_COLORS[(int)round(time)]);
			if (y == MAP_ROWS / 2 && x == MAP_COLS / 2) {
				attr |= A_REVERSE;
			}

			wmove(mapWindow, y, 3 * x);
			waddch(mapWindow, attr | cell);
			waddch(mapWindow, attr | cell);
			waddch(mapWindow, attr | cell);
		}
	}

	input.mode = MODE_MAP;
	curs_set(0);
}

static void readMessage(void) {
	struct ServerMessage msg;
	ssize_t size = recv(client, &msg, sizeof(msg), 0);
	if (size < 0) err(EX_IOERR, "recv");
	if ((size_t)size < sizeof(msg)) errx(EX_PROTOCOL, "A message was cut short.");

	int sy, sx;
	getyx(stdscr, sy, sx);
	switch (msg.type) {
		break; case SERVER_TILE: serverTile();
		break; case SERVER_MOVE: move(msg.move.cellY, msg.move.cellX); return;
		break; case SERVER_PUT: {
			serverPut(
				msg.put.cellX,
				msg.put.cellY,
				msg.put.color,
				msg.put.cell
			);
		}
		break; case SERVER_CURSOR: {
			serverCursor(
				msg.cursor.oldCellX,
				msg.cursor.oldCellY,
				msg.cursor.newCellX,
				msg.cursor.newCellY
			);
		}
		break; case SERVER_MAP: serverMap();
		break; default: errx(EX_PROTOCOL, "I don't know what %d means!", msg.type);
	}
	move(sy, sx);
}

static void draw(void) {
	wnoutrefresh(stdscr);
	if (input.mode == MODE_MAP) {
		touchwin(mapFrame);
		touchwin(mapWindow);
		wnoutrefresh(mapFrame);
		wnoutrefresh(mapWindow);
	}
	doupdate();
}

static void curse(void) {
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, true);
	set_escdelay(100);

	if (!has_colors()) {
		endwin();
		fprintf(
			stderr,
			"Sorry, your terminal doesn't support colors!\n"
			"If you think it does, check TERM.\n"
		);
		exit(EX_CONFIG);
	}
	start_color();
	if (COLOR_PAIRS < 64) {
		endwin();
		fprintf(
			stderr,
			"Sorry, your terminal doesn't support enough color pairs!\n"
			"If you think it does, check TERM.\n"
		);
		exit(EX_CONFIG);
	}
	colorPairs();

	if (LINES < CELL_ROWS || COLS < CELL_COLS) {
		endwin();
		fprintf(stderr, "Sorry, your terminal is too small!\n");
		fprintf(stderr, "It needs to be at least 80x25 characters.\n");
		exit(EX_CONFIG);
	}

	attrset(colorAttr(COLOR_WHITE));
	if (LINES > CELL_ROWS) {
		mvhline(CELL_ROWS, 0, 0, CELL_COLS);
	}
	if (COLS > CELL_COLS) {
		mvvline(0, CELL_COLS, 0, CELL_ROWS);
	}
	if (LINES > CELL_ROWS && COLS > CELL_COLS) {
		mvaddch(CELL_ROWS, CELL_COLS, ACS_LRCORNER);
	}
	attrset(A_NORMAL);

	mapFrame = newwin(
		MAP_ROWS + 2,
		3 * MAP_COLS + 2,
		CELL_INIT_Y - MAP_ROWS / 2 - 1,
		CELL_INIT_X - 3 * MAP_COLS / 2 - 1
	);
	mapWindow = newwin(
		MAP_ROWS,
		3 * MAP_COLS,
		CELL_INIT_Y - MAP_ROWS / 2,
		CELL_INIT_X - 3 * MAP_COLS / 2
	);
	wattrset(mapFrame, colorAttr(COLOR_WHITE));
	box(mapFrame, 0, 0);
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
		draw();
	}
}
