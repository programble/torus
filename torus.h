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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define ALIGNED(x) __attribute__((aligned(x)))

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

#define CELL_ROWS (25)
#define CELL_COLS (80)
#define CELLS_SIZE (sizeof(char[CELL_ROWS][CELL_COLS]))

#define CELL_INIT_X (CELL_COLS / 2)
#define CELL_INIT_Y (CELL_ROWS / 2)

struct Tile {
    time_t create;
    time_t modify;
    char cells[CELL_ROWS][CELL_COLS] ALIGNED(16);
    uint8_t colors[CELL_ROWS][CELL_COLS] ALIGNED(16);
    uint32_t modifyCount;
    uint32_t accessCount;
    time_t access;
} ALIGNED(4096);
static_assert(sizeof(struct Tile) == 4096, "struct Tile is page-sized");
static_assert(offsetof(struct Tile, cells) == 16, "stable cells offset");
static_assert(offsetof(struct Tile, colors) == 2016, "stable colors offset");

#define TILE_ROWS (512)
#define TILE_COLS (512)
#define TILES_SIZE (sizeof(struct Tile[TILE_ROWS][TILE_COLS]))

static const struct {
    uint32_t tileX;
    uint32_t tileY;
} SPAWN[] = {
    { 0, 0 },
    { TILE_COLS * 3 / 4, TILE_ROWS * 3 / 4 }, // NW
    { TILE_COLS * 1 / 4, TILE_ROWS * 3 / 4 }, // NE
    { TILE_COLS * 1 / 4, TILE_ROWS * 1 / 4 }, // SE
    { TILE_COLS * 3 / 4, TILE_ROWS * 1 / 4 }, // SW
};

#define SPAWN_COUNT (sizeof(SPAWN) / sizeof(SPAWN[0]))

enum ServerMessageType {
    SERVER_TILE,
    SERVER_MOVE,
    SERVER_PUT,
    SERVER_CURSOR,
};

struct ServerMessage {
    enum ServerMessageType type;
    union {
        struct {
            uint8_t cellX;
            uint8_t cellY;
        } m;
        struct {
            uint8_t cellX;
            uint8_t cellY;
            uint8_t color;
            char cell;
        } p;
        struct {
            uint8_t oldCellX;
            uint8_t oldCellY;
            uint8_t newCellX;
            uint8_t newCellY;
        } c;
    } data;
};

#define CURSOR_NONE UINT8_MAX

enum ClientMessageType {
    CLIENT_MOVE,
    CLIENT_PUT,
    CLIENT_SPAWN,
};

struct ClientMessage {
    enum ClientMessageType type;
    union {
        struct {
            int8_t dx;
            int8_t dy;
        } m;
        struct {
            uint8_t color;
            char cell;
        } p;
        struct {
            uint8_t spawn;
        } s;
    } data;
};
