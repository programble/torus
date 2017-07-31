#if 0
exec cc -Wall -Wextra -pedantic $@ -o server $0
#endif

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
}

static struct Tile *tileGet(uint32_t tileX, uint32_t tileY) {
    struct Tile *tile = &tiles[tileY * TILE_ROWS + tileX];
    if (!tile->create) {
        memset(tile->cells, ' ', CELLS_SIZE);
        memset(tile->colors, COLOR_WHITE, CELLS_SIZE);
        tile->create = time(NULL);
    }
    tile->access = time(NULL);
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
    client->tileX = TILE_INIT_X;
    client->tileY = TILE_INIT_Y;
    client->cellX = CELL_INIT_X;
    client->cellY = CELL_INIT_Y;

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

static void clientRemove(struct Client *client) {
    if (client->prev) client->prev->next = client->next;
    if (client->next) client->next->prev = client->prev;
    if (clientHead == client) clientHead = client->next;
    close(client->fd);
    free(client);
}

static bool clientSend(struct Client *client, const struct ServerMessage *msg) {
    ssize_t len = send(client->fd, msg, sizeof(*msg), 0);
    if (len < 0) {
        clientRemove(client);
        return false;
    }

    if (msg->type == SERVER_TILE) {
        struct Tile *tile = tileGet(client->tileX, client->tileY);
        len = send(client->fd, tile, sizeof(*tile), 0);
        if (len < 0) {
            clientRemove(client);
            return false;
        }
    }

    return true;
}

static bool clientCast(struct Client *origin, const struct ServerMessage *msg) {
    uint32_t tileX = origin->tileX;
    uint32_t tileY = origin->tileY;

    bool success = clientSend(origin, msg);

    for (struct Client *client = clientHead; client; client = client->next) {
        if (client == origin) continue;
        if (client->tileX != tileX) continue;
        if (client->tileY != tileY) continue;

        struct Client *next = client->next;
        if (!clientSend(client, msg)) {
            client = next;
            if (!client) break;
        }
    }

    return success;
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

    struct ServerMessage msg = { .type = SERVER_MOVE };
    msg.data.m.cellX = client->cellX;
    msg.data.m.cellY = client->cellY;
    if (!clientSend(client, &msg)) return false;

    if (client->tileX != old.tileX || client->tileY != old.tileY) {
        msg.type = SERVER_TILE;
        return clientSend(client, &msg);
    }

    return true;
}

static bool clientPut(struct Client *client, uint8_t color, char cell) {
    struct Tile *tile = tileGet(client->tileX, client->tileY);
    tile->colors[client->cellY][client->cellX] = color;
    tile->cells[client->cellY][client->cellX] = cell;

    struct ServerMessage msg = { .type = SERVER_PUT };
    msg.data.p.cellX = client->cellX;
    msg.data.p.cellY = client->cellY;
    msg.data.p.color = color;
    msg.data.p.cell = cell;
    return clientCast(client, &msg);
}

int main() {
    int error;

    signal(SIGPIPE, SIG_IGN);

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
        .fflags = 0,
        .data = 0,
        .udata = NULL,
    };
    int nevents = kevent(kq, &event, 1, NULL, 0, NULL);
    if (nevents < 0) err(EX_OSERR, "kevent");

    for (;;) {
        nevents = kevent(kq, NULL, 0, &event, 1, NULL);
        if (nevents < 0) err(EX_IOERR, "kevent");
        if (!nevents) continue;

        if (!event.udata) {
            int fd = accept(server, NULL, NULL);
            if (fd < 0) err(EX_IOERR, "accept");
            fcntl(fd, F_SETFL, O_NONBLOCK);

            struct Client *client = clientAdd(fd);

            struct kevent event = {
                .ident = fd,
                .filter = EVFILT_READ,
                .flags = EV_ADD,
                .fflags = 0,
                .data = 0,
                .udata = client,
            };
            nevents = kevent(kq, &event, 1, NULL, 0, NULL);
            if (nevents < 0) err(EX_OSERR, "kevent");

            struct ServerMessage msg = { .type = SERVER_TILE };
            if (!clientSend(client, &msg)) continue;
            clientMove(client, 0, 0);

            continue;
        }

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

        switch (msg.type) {
            case CLIENT_MOVE:
                clientMove(client, msg.data.m.dx, msg.data.m.dy);
                break;

            case CLIENT_PUT:
                clientPut(client, msg.data.p.color, msg.data.p.cell);
                break;

            default:
                clientRemove(client);
        }
    }
}
