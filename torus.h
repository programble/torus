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
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static const char DefaultDataPath[] = "torus.dat";
static const char DefaultSockPath[] = "torus.sock";
static const char DefaultFontPath[] = "default8x16.psfu";

enum {
	ColorBlack,
	ColorRed,
	ColorGreen,
	ColorYellow,
	ColorBlue,
	ColorMagenta,
	ColorCyan,
	ColorWhite,
	ColorBright,
};

static const wchar_t CP437[256] = (
	L" ☺☻♥♦♣♠•◘○◙♂♀♪♫☼"
	L"►◄↕‼¶§▬↨↑↓→←∟↔▲▼"
	L" !\"#$%&'()*+,-./"
	L"0123456789:;<=>?"
	L"@ABCDEFGHIJKLMNO"
	L"PQRSTUVWXYZ[\\]^_"
	L"`abcdefghijklmno"
	L"pqrstuvwxyz{|}~⌂"
	L"ÇüéâäàåçêëèïîìÄÅ"
	L"ÉæÆôöòûùÿÖÜ¢£¥₧ƒ"
	L"áíóúñÑªº¿⌐¬½¼¡«»"
	L"░▒▓│┤╡╢╖╕╣║╗╝╜╛┐"
	L"└┴┬├─┼╞╟╚╔╩╦╠═╬╧"
	L"╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀"
	L"αßΓπΣσµτΦΘΩδ∞φε∩"
	L"≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ "
);

enum {
	CellRows = 25,
	CellCols = 80,
};
static const size_t CellsSize = sizeof(uint8_t[CellRows][CellCols]);

static const uint8_t CellInitX = CellCols / 2;
static const uint8_t CellInitY = CellRows / 2;

struct Meta {
	time_t createTime;
	time_t modifyTime;
	time_t accessTime;
	uint32_t modifyCount;
	uint32_t accessCount;
};

struct Tile {
	alignas(4096)
	time_t createTime;
	time_t modifyTime;
	alignas(16) uint8_t cells[CellRows][CellCols];
	alignas(16) uint8_t colors[CellRows][CellCols];
	uint32_t modifyCount;
	uint32_t accessCount;
	time_t accessTime;
};
static_assert(4096 == sizeof(struct Tile), "struct Tile is page-sized");

static inline struct Meta tileMeta(const struct Tile *tile) {
	return (struct Meta) {
		.createTime = tile->createTime,
		.modifyTime = tile->modifyTime,
		.accessTime = tile->accessTime,
		.modifyCount = tile->modifyCount,
		.accessCount = tile->accessCount,
	};
}

enum {
	TileRows = 512,
	TileCols = 512,
};
static const size_t TilesSize = sizeof(struct Tile[TileRows][TileCols]);

static const uint32_t TileInitX = 0;
static const uint32_t TileInitY = 0;

static const struct {
	uint32_t tileX;
	uint32_t tileY;
} Ports[] = {
	{ TileInitX, TileInitY },
	{ TileCols * 3 / 4, TileRows * 3 / 4 }, // NW
	{ TileCols * 1 / 4, TileRows * 3 / 4 }, // NE
	{ TileCols * 1 / 4, TileRows * 1 / 4 }, // SE
	{ TileCols * 3 / 4, TileRows * 1 / 4 }, // SW
};

enum {
	MapRows = 11,
	MapCols = 11,
};

struct Map {
	time_t now;
	struct Meta min;
	struct Meta max;
	struct Meta meta[MapRows][MapCols];
};

struct ServerMessage {
	enum {
		ServerTile,
		ServerMove,
		ServerPut,
		ServerCursor,
		ServerMap,
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
			uint8_t cell;
		} put;
		struct {
			uint8_t oldCellX;
			uint8_t oldCellY;
			uint8_t newCellX;
			uint8_t newCellY;
		} cursor;
	};
};

static const uint8_t CursorNone = UINT8_MAX;

struct ClientMessage {
	enum {
		ClientMove,
		ClientFlip,
		ClientPut,
		ClientMap,
		ClientTele,
	} type;
	union {
		struct {
			int8_t dx;
			int8_t dy;
		} move;
		struct {
			uint8_t color;
			uint8_t cell;
		} put;
		uint8_t port;
	};
};
