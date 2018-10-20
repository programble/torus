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

#ifdef __FreeBSD__
#include <libutil.h>
#endif

#include "torus.h"

static struct Tile *tiles;

static void tilesMap(const char *path) {
	int fd = open(path, O_CREAT | O_RDWR, 0644);
	if (fd < 0) err(EX_CANTCREAT, "%s", path);

	int error = ftruncate(fd, TilesSize);
	if (error) err(EX_IOERR, "%s", path);

	tiles = mmap(NULL, TilesSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (tiles == MAP_FAILED) err(EX_OSERR, "mmap");
	close(fd);

	error = madvise(tiles, TilesSize, MADV_RANDOM);
	if (error) err(EX_OSERR, "madvise");

#ifdef MADV_NOCORE
	error = madvise(tiles, TilesSize, MADV_NOCORE);
	if (error) err(EX_OSERR, "madvise");
#endif
}

static struct Tile *tileGet(uint32_t tileX, uint32_t tileY) {
	struct Tile *tile = &tiles[tileY * TileRows + tileX];
	if (!tile->meta.createTime) {
		memset(tile->cells, ' ', CellsSize);
		memset(tile->colors, ColorWhite, CellsSize);
		tile->meta.createTime = time(NULL);
	}
	return tile;
}

static struct Tile *tileAccess(uint32_t tileX, uint32_t tileY) {
	struct Tile *tile = tileGet(tileX, tileY);
	tile->meta.accessTime = time(NULL);
	tile->meta.accessCount++;
	return tile;
}

static struct Tile *tileModify(uint32_t tileX, uint32_t tileY) {
	struct Tile *tile = tileGet(tileX, tileY);
	tile->meta.modifyTime = time(NULL);
	tile->meta.modifyCount++;
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
	client->tileX = TileInitX;
	client->tileY = TileInitY;
	client->cellX = CellInitX;
	client->cellY = CellInitY;

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

static bool clientSend(const struct Client *client, struct ServerMessage msg) {
	ssize_t size = send(client->fd, &msg, sizeof(msg), 0);
	if (size < 0) return false;

	if (msg.type == ServerTile) {
		struct Tile *tile = tileAccess(client->tileX, client->tileY);
		size = send(client->fd, tile, sizeof(*tile), 0);
		if (size < 0) return false;
	}

	return true;
}

static void clientCast(const struct Client *origin, struct ServerMessage msg) {
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
		.type = ServerCursor,
		.cursor = {
			.oldCellX = client->cellX, .oldCellY = client->cellY,
			.newCellX = CursorNone,    .newCellY = CursorNone,
		},
	};
	clientCast(client, msg);

	close(client->fd);
	free(client);
}

static bool clientCursors(const struct Client *client) {
	struct ServerMessage msg = {
		.type = ServerCursor,
		.cursor = { .oldCellX = CursorNone, .oldCellY = CursorNone },
	};

	for (struct Client *friend = clientHead; friend; friend = friend->next) {
		if (friend == client) continue;
		if (friend->tileX != client->tileX) continue;
		if (friend->tileY != client->tileY) continue;

		msg.cursor.newCellX = friend->cellX;
		msg.cursor.newCellY = friend->cellY;
		if (!clientSend(client, msg)) return false;
	}
	return true;
}

static bool clientUpdate(struct Client *client, const struct Client *old) {
	struct ServerMessage msg = {
		.type = ServerMove,
		.move = { .cellX = client->cellX, .cellY = client->cellY },
	};
	if (!clientSend(client, msg)) return false;

	if (client->tileX != old->tileX || client->tileY != old->tileY) {
		msg.type = ServerTile;
		if (!clientSend(client, msg)) return false;

		if (!clientCursors(client)) return false;

		msg = (struct ServerMessage) {
			.type = ServerCursor,
			.cursor = {
				.oldCellX = old->cellX, .oldCellY = old->cellY,
				.newCellX = CursorNone, .newCellY = CursorNone,
			},
		};
		clientCast(old, msg);

		msg = (struct ServerMessage) {
			.type = ServerCursor,
			.cursor = {
				.oldCellX = CursorNone,    .oldCellY = CursorNone,
				.newCellX = client->cellX, .newCellY = client->cellY,
			},
		};
		clientCast(client, msg);

	} else {
		msg = (struct ServerMessage) {
			.type = ServerCursor,
			.cursor = {
				.oldCellX = old->cellX,    .oldCellY = old->cellY,
				.newCellX = client->cellX, .newCellY = client->cellY,
			},
		};
		clientCast(client, msg);
	}

	return true;
}

static bool clientMove(struct Client *client, int8_t dx, int8_t dy) {
	struct Client old = *client;

	if (dx > CellCols - client->cellX) dx = CellCols - client->cellX;
	if (dx < -client->cellX - 1)       dx = -client->cellX - 1;
	if (dy > CellRows - client->cellY) dy = CellRows - client->cellY;
	if (dy < -client->cellY - 1)       dy = -client->cellY - 1;

	client->cellX += dx;
	client->cellY += dy;

	if (client->cellX == CellCols) {
		client->tileX++;
		client->cellX = 0;
	}
	if (client->cellX == UINT8_MAX) {
		client->tileX--;
		client->cellX = CellCols - 1;
	}
	if (client->cellY == CellRows) {
		client->tileY++;
		client->cellY = 0;
	}
	if (client->cellY == UINT8_MAX) {
		client->tileY--;
		client->cellY = CellRows - 1;
	}

	if (client->tileX == TileCols)  client->tileX = 0;
	if (client->tileX == UINT32_MAX) client->tileX = TileCols - 1;
	if (client->tileY == TileRows)  client->tileY = 0;
	if (client->tileY == UINT32_MAX) client->tileY = TileRows - 1;

	assert(client->cellX < CellCols);
	assert(client->cellY < CellRows);
	assert(client->tileX < TileCols);
	assert(client->tileY < TileRows);

	return clientUpdate(client, &old);
}

static bool clientFlip(struct Client *client) {
	struct Client old = *client;
	client->tileX = (client->tileX + TileCols / 2) % TileCols;
	client->tileY = (client->tileY + TileRows / 2) % TileRows;
	return clientUpdate(client, &old);
}

static bool clientPut(const struct Client *client, uint8_t color, uint8_t cell) {
	struct Tile *tile = tileModify(client->tileX, client->tileY);
	tile->colors[client->cellY][client->cellX] = color;
	tile->cells[client->cellY][client->cellX] = cell;

	struct ServerMessage msg = {
		.type = ServerPut,
		.put = {
			.cellX = client->cellX,
			.cellY = client->cellY,
			.color = color,
			.cell = cell,
		},
	};
	bool success = clientSend(client, msg);
	clientCast(client, msg);
	return success;
}

static bool clientMap(const struct Client *client) {
	int32_t mapY = (int32_t)client->tileY - MapRows / 2;
	int32_t mapX = (int32_t)client->tileX - MapCols / 2;

	time_t now = time(NULL);
	struct Map map = {
		.now = now,
		.min = {
			.createTime = now,
			.modifyTime = now,
			.accessTime = now,
			.modifyCount = UINT32_MAX,
			.accessCount = UINT32_MAX,
		},
	};

	for (int32_t y = 0; y < MapRows; ++y) {
		for (int32_t x = 0; x < MapCols; ++x) {
			uint32_t tileY = ((mapY + y) % TileRows + TileRows) % TileRows;
			uint32_t tileX = ((mapX + x) % TileCols + TileCols) % TileCols;
			struct Meta meta = tiles[tileY * TileRows + tileX].meta;

			if (meta.createTime) {
				if (meta.createTime < map.min.createTime) {
					map.min.createTime = meta.createTime;
				}
				if (meta.createTime > map.max.createTime) {
					map.max.createTime = meta.createTime;
				}
			}
			if (meta.modifyTime) {
				if (meta.modifyTime < map.min.modifyTime) {
					map.min.modifyTime = meta.modifyTime;
				}
				if (meta.modifyTime > map.max.modifyTime) {
					map.max.modifyTime = meta.modifyTime;
				}
			}
			if (meta.accessTime) {
				if (meta.accessTime < map.min.accessTime) {
					map.min.accessTime = meta.accessTime;
				}
				if (meta.accessTime > map.max.accessTime) {
					map.max.accessTime = meta.accessTime;
				}
			}
			if (meta.modifyCount < map.min.modifyCount) {
				map.min.modifyCount = meta.modifyCount;
			}
			if (meta.modifyCount > map.max.modifyCount) {
				map.max.modifyCount = meta.modifyCount;
			}
			if (meta.accessCount < map.min.accessCount) {
				map.min.accessCount = meta.accessCount;
			}
			if (meta.accessCount > map.max.accessCount) {
				map.max.accessCount = meta.accessCount;
			}

			map.meta[y][x] = meta;
		}
	}

	struct ServerMessage msg = { .type = ServerMap };
	if (!clientSend(client, msg)) return false;
	if (0 > send(client->fd, &map, sizeof(map), 0)) return false;
	return true;
}

int main(int argc, char *argv[]) {
	int error;

	const char *dataPath = "torus.dat";
	const char *sockPath = "torus.sock";
	const char *pidPath = NULL;
	int opt;
	while (0 < (opt = getopt(argc, argv, "d:p:s:"))) {
		switch (opt) {
			break; case 'd': dataPath = optarg;
			break; case 'p': pidPath = optarg;
			break; case 's': sockPath = optarg;
			break; default:  return EX_USAGE;
		}
	}

#ifdef __FreeBSD__
	struct pidfh *pid = NULL;
	if (pidPath) {
		pid = pidfile_open(pidPath, 0600, NULL);
		if (!pid) err(EX_CANTCREAT, "%s", pidPath);
	}
#endif

	tilesMap(dataPath);

	int server = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (server < 0) err(EX_OSERR, "socket");

	error = unlink(sockPath);
	if (error && errno != ENOENT) err(EX_IOERR, "%s", sockPath);

	struct sockaddr_un addr = { .sun_family = AF_LOCAL };
	strlcpy(addr.sun_path, sockPath, sizeof(addr.sun_path));
	error = bind(server, (struct sockaddr *)&addr, SUN_LEN(&addr));
	if (error) err(EX_CANTCREAT, "%s", sockPath);

#ifdef __FreeBSD__
	if (pid) {
		error = daemon(0, 0);
		if (error) err(EX_OSERR, "daemon");
		pidfile_write(pid);
	}
#endif

	error = listen(server, 0);
	if (error) err(EX_OSERR, "listen");

	int kq = kqueue();
	if (kq < 0) err(EX_OSERR, "kqueue");

	struct kevent event;
	EV_SET(&event, server, EVFILT_READ, EV_ADD, 0, 0, 0);
	int nevents = kevent(kq, &event, 1, NULL, 0, NULL);
	if (nevents < 0) err(EX_OSERR, "kevent");

	for (;;) {
		nevents = kevent(kq, NULL, 0, &event, 1, NULL);
		if (nevents < 0) err(EX_IOERR, "kevent");

		if (!event.udata) {
			int fd = accept(server, NULL, NULL);
			if (fd < 0) err(EX_IOERR, "accept");
			fcntl(fd, F_SETFL, O_NONBLOCK);

			int on = 1;
			error = setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
			if (error) err(EX_IOERR, "setsockopt");

			int size = 2 * sizeof(struct Tile);
			error = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
			if (error) err(EX_IOERR, "setsockopt");

			struct Client *client = clientAdd(fd);

			EV_SET(&event, fd, EVFILT_READ, EV_ADD, 0, 0, client);
			nevents = kevent(kq, &event, 1, NULL, 0, NULL);
			if (nevents < 0) err(EX_IOERR, "kevent");

			struct ServerMessage msg = { .type = ServerTile };
			bool success = clientSend(client, msg)
				&& clientMove(client, 0, 0)
				&& clientCursors(client);
			if (!success) clientRemove(client);

			continue;
		}

		struct Client *client = (struct Client *)event.udata;
		if (event.flags & EV_EOF) {
			clientRemove(client);
			continue;
		}

		struct ClientMessage msg;
		ssize_t size = recv(client->fd, &msg, sizeof(msg), 0);
		if (size != sizeof(msg)) {
			clientRemove(client);
			continue;
		}

		bool success = false;
		switch (msg.type) {
			break; case ClientMove: {
				success = clientMove(client, msg.move.dx, msg.move.dy);
			}
			break; case ClientFlip: {
				success = clientFlip(client);
			}
			break; case ClientPut: {
				success = clientPut(client, msg.put.color, msg.put.cell);
			}
			break; case ClientMap: {
				success = clientMap(client);
			}
		}
		if (!success) clientRemove(client);
	}
}
