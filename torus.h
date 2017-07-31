#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define ALIGNED(x) __attribute__((aligned(x)))

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
    time_t access;
    char cells[CELL_ROWS][CELL_COLS] ALIGNED(16);
    uint8_t colors[CELL_ROWS][CELL_COLS] ALIGNED(16);
    uint32_t accessCount;
} ALIGNED(4096);
static_assert(sizeof(struct Tile) == 4096, "struct Tile is page-sized");
static_assert(offsetof(struct Tile, cells) == 16, "stable cells offset");
static_assert(offsetof(struct Tile, colors) == 2016, "stable colors offset");

#define TILE_ROWS (512)
#define TILE_COLS (512)
#define TILES_SIZE (sizeof(struct Tile[TILE_ROWS][TILE_COLS]))

#define TILE_INIT_X (0)
#define TILE_INIT_Y (0)

enum ServerMessageType {
    SERVER_TILE,
    SERVER_MOVE,
    SERVER_PUT,
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
    } data;
};

enum ClientMessageType {
    CLIENT_MOVE,
    CLIENT_PUT,
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
    } data;
};
