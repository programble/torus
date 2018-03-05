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

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

#include "torus.h"

static struct Tile *tiles;

static void tilesMap(void) {
    int fd = open("torus.dat", O_CREAT | O_RDWR, 0644);
    if (fd < 0) err(EX_IOERR, "torus.dat");

    int error = ftruncate(fd, TILES_SIZE);
    if (error) err(EX_IOERR, "ftruncate");

    tiles = mmap(NULL, TILES_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (tiles == MAP_FAILED) err(EX_OSERR, "mmap");

    error = madvise(tiles, TILES_SIZE, MADV_RANDOM);
    if (error) err(EX_OSERR, "madvise");

#ifdef MADV_NOCORE
    error = madvise(tiles, TILES_SIZE, MADV_NOCORE);
    if (error) err(EX_OSERR, "madvise");
#endif
}

static struct Tile *tileGet(uint32_t tileX, uint32_t tileY) {
    struct Tile *tile = &tiles[tileY * TILE_ROWS + tileX];
    if (!tile->createTime) {
        memset(tile->cells, ' ', CELLS_SIZE);
        memset(tile->colors, COLOR_WHITE, CELLS_SIZE);
        tile->createTime = time(NULL);
    }
    return tile;
}

static struct Tile *tileAccess(uint32_t tileX, uint32_t tileY) {
    struct Tile *tile = tileGet(tileX, tileY);
    tile->accessTime = time(NULL);
    tile->accessCount++;
    return tile;
}

static struct Tile *tileModify(uint32_t tileX, uint32_t tileY) {
    struct Tile *tile = tileGet(tileX, tileY);
    tile->modifyTime = time(NULL);
    tile->modifyCount++;
    return tile;
}

static struct Client {
    int fd;

    uint32_t tileX;
    uint32_t tileY;
    uint8_t cellX;
    uint8_t cellY;

    struct Client *prev;
    struct Client *next;
} *clientHead;

static struct Client *clientAdd(int fd) {
    struct Client *client = malloc(sizeof(*client));
    if (!client) err(EX_OSERR, "malloc");

    client->fd = fd;
    client->tileX = UINT32_MAX;
    client->tileY = UINT32_MAX;
    client->cellX = UINT8_MAX;
    client->cellY = UINT8_MAX;

    client->prev = NULL;
    if (clientHead) {
        clientHead->prev = client;
        client->next = clientHead;
    } else {
        client->next = NULL;
    }
    clientHead = client;

    return client;
}

static bool clientSend(const struct Client *client, const struct ServerMessage *msg) {
    ssize_t len = send(client->fd, msg, sizeof(*msg), 0);
    if (len < 0) return false;

    if (msg->type == SERVER_TILE) {
        struct Tile *tile = tileAccess(client->tileX, client->tileY);
        len = send(client->fd, tile, sizeof(*tile), 0);
        if (len < 0) return false;
    }

    return true;
}

static void clientCast(const struct Client *origin, const struct ServerMessage *msg) {
    for (struct Client *client = clientHead; client; client = client->next) {
        if (client == origin) continue;
        if (client->tileX != origin->tileX) continue;
        if (client->tileY != origin->tileY) continue;
        clientSend(client, msg);
    }
}

static void clientRemove(struct Client *client) {
    if (client->prev) client->prev->next = client->next;
    if (client->next) client->next->prev = client->prev;
    if (clientHead == client) clientHead = client->next;

    struct ServerMessage msg = {
        .type = SERVER_CURSOR,
        .data.c = {
            .oldCellX = client->cellX, .oldCellY = client->cellY,
            .newCellX = CURSOR_NONE,   .newCellY = CURSOR_NONE,
        },
    };
    clientCast(client, &msg);

    close(client->fd);
    free(client);
}

static bool clientCursors(const struct Client *client) {
    struct ServerMessage msg = {
        .type = SERVER_CURSOR,
        .data.c = { .oldCellX = CURSOR_NONE, .oldCellY = CURSOR_NONE },
    };

    for (struct Client *friend = clientHead; friend; friend = friend->next) {
        if (friend == client) continue;
        if (friend->tileX != client->tileX) continue;
        if (friend->tileY != client->tileY) continue;

        msg.data.c.newCellX = friend->cellX;
        msg.data.c.newCellY = friend->cellY;
        if (!clientSend(client, &msg)) return false;
    }
    return true;
}

static bool clientUpdate(struct Client *client, struct Client *old) {
    struct ServerMessage msg = {
        .type = SERVER_MOVE,
        .data.m = { .cellX = client->cellX, .cellY = client->cellY },
    };
    if (!clientSend(client, &msg)) return false;

    if (client->tileX != old->tileX || client->tileY != old->tileY) {
        msg.type = SERVER_TILE;
        if (!clientSend(client, &msg)) return false;

        if (!clientCursors(client)) return false;

        msg = (struct ServerMessage) {
            .type = SERVER_CURSOR,
            .data.c = {
                .oldCellX = old->cellX,  .oldCellY = old->cellY,
                .newCellX = CURSOR_NONE, .newCellY = CURSOR_NONE,
            },
        };
        clientCast(old, &msg);

        msg = (struct ServerMessage) {
            .type = SERVER_CURSOR,
            .data.c = {
                .oldCellX = CURSOR_NONE,   .oldCellY = CURSOR_NONE,
                .newCellX = client->cellX, .newCellY = client->cellY,
            },
        };
        clientCast(client, &msg);

    } else {
        msg = (struct ServerMessage) {
            .type = SERVER_CURSOR,
            .data.c = {
                .oldCellX = old->cellX,    .oldCellY = old->cellY,
                .newCellX = client->cellX, .newCellY = client->cellY,
            },
        };
        clientCast(client, &msg);
    }

    return true;
}

static bool clientSpawn(struct Client *client, uint8_t spawn) {
    if (spawn >= SPAWN_COUNT) return false;
    struct Client old = *client;
    client->tileX = SPAWN[spawn].tileX;
    client->tileY = SPAWN[spawn].tileY;
    client->cellX = CELL_INIT_X;
    client->cellY = CELL_INIT_Y;
    return clientUpdate(client, &old);
}

static bool clientMove(struct Client *client, int8_t dx, int8_t dy) {
    struct Client old = *client;

    if (dx > CELL_COLS - client->cellX) dx = CELL_COLS - client->cellX;
    if (dx < -client->cellX - 1)        dx = -client->cellX - 1;
    if (dy > CELL_ROWS - client->cellY) dy = CELL_ROWS - client->cellY;
    if (dy < -client->cellY - 1)        dy = -client->cellY - 1;

    client->cellX += dx;
    client->cellY += dy;

    if (client->cellX == CELL_COLS) { client->tileX++; client->cellX = 0; }
    if (client->cellX == UINT8_MAX) { client->tileX--; client->cellX = CELL_COLS - 1; }
    if (client->cellY == CELL_ROWS) { client->tileY++; client->cellY = 0; }
    if (client->cellY == UINT8_MAX) { client->tileY--; client->cellY = CELL_ROWS - 1; }

    if (client->tileX == TILE_COLS)  client->tileX = 0;
    if (client->tileX == UINT32_MAX) client->tileX = TILE_COLS - 1;
    if (client->tileY == TILE_ROWS)  client->tileY = 0;
    if (client->tileY == UINT32_MAX) client->tileY = TILE_ROWS - 1;

    assert(client->cellX < CELL_COLS);
    assert(client->cellY < CELL_ROWS);
    assert(client->tileX < TILE_COLS);
    assert(client->tileY < TILE_ROWS);

    return clientUpdate(client, &old);
}

static bool clientPut(const struct Client *client, uint8_t color, char cell) {
    struct Tile *tile = tileModify(client->tileX, client->tileY);
    tile->colors[client->cellY][client->cellX] = color;
    tile->cells[client->cellY][client->cellX] = cell;

    struct ServerMessage msg = {
        .type = SERVER_PUT,
        .data.p = {
            .cellX = client->cellX,
            .cellY = client->cellY,
            .color = color,
            .cell = cell,
        },
    };
    bool success = clientSend(client, &msg);
    clientCast(client, &msg);
    return success;
}

int main() {
    int error;

    tilesMap();

    int server = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (server < 0) err(EX_OSERR, "socket");

    error = unlink("torus.sock");
    if (error && errno != ENOENT) err(EX_IOERR, "torus.sock");

    struct sockaddr_un addr = {
        .sun_family = AF_LOCAL,
        .sun_path = "torus.sock",
    };
    error = bind(server, (struct sockaddr *)&addr, sizeof(addr));
    if (error) err(EX_IOERR, "torus.sock");

    error = listen(server, 0);
    if (error) err(EX_OSERR, "listen");

    int kq = kqueue();
    if (kq < 0) err(EX_OSERR, "kqueue");

    struct kevent event = {
        .ident = server,
        .filter = EVFILT_READ,
        .flags = EV_ADD,
    };
    int nevents = kevent(kq, &event, 1, NULL, 0, NULL);
    if (nevents < 0) err(EX_OSERR, "kevent");

    for (;;) {
        nevents = kevent(kq, NULL, 0, &event, 1, NULL);
        if (nevents < 0) err(EX_IOERR, "kevent");
        if (!nevents) continue;

        if (event.udata) {
            struct Client *client = event.udata;
            if (event.flags & EV_EOF) {
                clientRemove(client);
                continue;
            }

            struct ClientMessage msg;
            ssize_t len = recv(client->fd, &msg, sizeof(msg), 0);
            if (len != sizeof(msg)) {
                clientRemove(client);
                continue;
            }

            bool success = false;
            if (msg.type == CLIENT_MOVE) {
                success = clientMove(client, msg.data.m.dx, msg.data.m.dy);
            } else if (msg.type == CLIENT_PUT) {
                success = clientPut(client, msg.data.p.color, msg.data.p.cell);
            } else if (msg.type == CLIENT_SPAWN) {
                success = clientSpawn(client, msg.data.s.spawn);
            }
            if (!success) clientRemove(client);

            continue;
        }

        int fd = accept(server, NULL, NULL);
        if (fd < 0) err(EX_IOERR, "accept");
        fcntl(fd, F_SETFL, O_NONBLOCK);

        int on = 1;
        error = setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
        if (error) err(EX_IOERR, "setsockopt");

        struct Client *client = clientAdd(fd);

        struct kevent event = {
            .ident = fd,
            .filter = EVFILT_READ,
            .flags = EV_ADD,
            .udata = client,
        };
        nevents = kevent(kq, &event, 1, NULL, 0, NULL);
        if (nevents < 0) err(EX_OSERR, "kevent");

        if (!clientSpawn(client, 0)) clientRemove(client);
    }
}
