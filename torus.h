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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define PACKED __attribute__((packed))
#define ALIGNED(x) __attribute__((aligned(x)))

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#undef COLOR_BLACK
#undef COLOR_RED
#undef COLOR_GREEN
#undef COLOR_YELLOW
#undef COLOR_BLUE
#undef COLOR_MAGENTA
#undef COLOR_CYAN
#undef COLOR_WHITE

enum {
	COLOR_BLACK,
	COLOR_RED,
	COLOR_GREEN,
	COLOR_YELLOW,
	COLOR_BLUE,
	COLOR_MAGENTA,
	COLOR_CYAN,
	COLOR_WHITE,
	COLOR_BRIGHT,
};

enum {
	CELL_ROWS = 24,
	CELL_COLS = 80,
};
static const size_t CELLS_SIZE = sizeof(char[CELL_ROWS][CELL_COLS]);

static const uint8_t CELL_INIT_X = CELL_COLS / 2;
static const uint8_t CELL_INIT_Y = CELL_ROWS / 2;

struct Meta {
	time_t createTime;
	time_t modifyTime;
	time_t accessTime;
	uint32_t modifyCount;
	uint32_t accessCount;
};

struct ALIGNED(4096) Tile {
	char cells[CELL_ROWS][CELL_COLS];
	uint8_t colors[CELL_ROWS][CELL_COLS];
	struct Meta meta;
};
static_assert(4096 == sizeof(struct Tile), "struct Tile is page-sized");

enum {
	TILE_ROWS = 64,
	TILE_COLS = 64,
};
static const size_t TILES_SIZE = sizeof(struct Tile[TILE_ROWS][TILE_COLS]);

static const uint32_t TILE_INIT_X = TILE_COLS / 2;
static const uint32_t TILE_INIT_Y = TILE_ROWS / 2;

enum {
	MAP_ROWS = 11,
	MAP_COLS = 11,
};

struct Map {
	struct Meta meta[MAP_ROWS][MAP_COLS];
};

struct ServerMessage {
	enum PACKED {
		SERVER_TILE,
		SERVER_MOVE,
		SERVER_PUT,
		SERVER_CURSOR,
		SERVER_MAP,
	} type;
	union {
		struct {
			uint8_t cellX;
			uint8_t cellY;
		} move;
		struct {
			uint8_t cellX;
			uint8_t cellY;
			uint8_t color;
			char cell;
		} put;
		struct {
			uint8_t oldCellX;
			uint8_t oldCellY;
			uint8_t newCellX;
			uint8_t newCellY;
		} cursor;
	};
};

static const uint8_t CURSOR_NONE = UINT8_MAX;

struct ClientMessage {
	enum PACKED {
		CLIENT_MOVE,
		CLIENT_PUT,
		CLIENT_MAP,
	} type;
	union {
		struct {
			int8_t dx;
			int8_t dy;
		} move;
		struct {
			uint8_t color;
			char cell;
		} put;
	};
};
