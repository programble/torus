/* Copyright (C) 2018, 2019  C. McEnroe <june@causal.agency>
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

#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <sys/capsicum.h>
#endif

#ifdef HAVE_KCGI
#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
#include <kcgi.h>
#endif

// XXX: Include this after kcgi.h to avoid conflicts.
// <https://github.com/kristapsdz/kcgi/pull/58>
#include <stdnoreturn.h>

#include "png.h"
#include "torus.h"

static const uint8_t Palette[16][3] = {
	{ 0x00, 0x00, 0x00 },
	{ 0xAA, 0x00, 0x00 },
	{ 0x00, 0xAA, 0x00 },
	{ 0xAA, 0x55, 0x00 },
	{ 0x00, 0x00, 0xAA },
	{ 0xAA, 0x00, 0xAA },
	{ 0x00, 0xAA, 0xAA },
	{ 0xAA, 0xAA, 0xAA },
	{ 0x55, 0x55, 0x55 },
	{ 0xFF, 0x55, 0x55 },
	{ 0x55, 0xFF, 0x55 },
	{ 0xFF, 0xFF, 0x55 },
	{ 0x55, 0x55, 0xFF },
	{ 0xFF, 0x55, 0xFF },
	{ 0x55, 0xFF, 0xFF },
	{ 0xFF, 0xFF, 0xFF },
};

static struct {
	uint32_t magic;
	uint32_t version;
	uint32_t size;
	uint32_t flags;
	struct {
		uint32_t len;
		uint32_t size;
		uint32_t height;
		uint32_t width;
	} glyph;
} font;

static uint8_t *glyphs;

static void fontLoad(const char *path) {
	FILE *file = fopen(path, "r");
	if (!file) err(EX_NOINPUT, "%s", path);

	size_t len = fread(&font, sizeof(font), 1, file);
	if (ferror(file)) err(EX_IOERR, "%s", path);
	if (len < 1) errx(EX_DATAERR, "%s: truncated header", path);

	if (font.magic != 0x864AB572 || font.size != sizeof(font)) {
		errx(EX_DATAERR, "%s: invalid header", path);
	}

	glyphs = calloc(font.glyph.len, font.glyph.size);
	if (!glyphs) err(EX_OSERR, "calloc");

	len = fread(glyphs, font.glyph.size, font.glyph.len, file);
	if (ferror(file)) err(EX_IOERR, "%s", path);
	if (len < font.glyph.len) errx(EX_DATAERR, "%s: truncated glyphs", path);
	fclose(file);
}

static struct Tile (*tiles)[TileRows][TileCols];

static void tilesMap(const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) err(EX_NOINPUT, "%s", path);

	struct stat stat;
	int error = fstat(fd, &stat);
	if (error) err(EX_IOERR, "%s", path);

	if ((size_t)stat.st_size < TilesSize) {
		errx(EX_DATAERR, "%s: truncated tiles", path);
	}

	tiles = mmap(NULL, TilesSize, PROT_READ, MAP_SHARED, fd, 0);
	if (tiles == MAP_FAILED) err(EX_OSERR, "mmap");
	close(fd);

	error = madvise(tiles, TilesSize, MADV_RANDOM);
	if (error) err(EX_OSERR, "madvise");
}

static void render(FILE *stream, uint32_t tileX, uint32_t tileY) {
	uint32_t width = CellCols * font.glyph.width;
	uint32_t height = CellRows * font.glyph.height;

	pngHead(stream, width, height, 8, PNGIndexed);
	pngPalette(stream, (uint8_t *)Palette, sizeof(Palette));

	uint8_t data[height][1 + width];
	memset(data, PNGNone, sizeof(data));

	uint32_t widthBytes = (font.glyph.width + 7) / 8;
	uint8_t (*bits)[font.glyph.len][font.glyph.height][widthBytes];
	bits = (void *)glyphs;

	struct Tile *tile = &(*tiles)[tileY][tileX];
	for (uint32_t cellY = 0; cellY < CellRows; ++cellY) {
		for (uint32_t cellX = 0; cellX < CellCols; ++cellX) {
			uint8_t cell = tile->cells[cellY][cellX];
			uint8_t fg = tile->colors[cellY][cellX] & 0x0F;
			uint8_t bg = tile->colors[cellY][cellX] >> 4;

			uint32_t glyphX = font.glyph.width * cellX;
			uint32_t glyphY = font.glyph.height * cellY;
			for (uint32_t y = 0; y < font.glyph.height; ++y) {
				for (uint32_t x = 0; x < font.glyph.width; ++x) {
					uint8_t bit = (*bits)[cell][y][x / 8] >> (7 - x % 8) & 1;
					data[glyphY + y][1 + glyphX + x] = (bit ? fg : bg);
				}
			}
		}
	}

	pngData(stream, (uint8_t *)data, sizeof(data));
	pngTail(stream);
}

#ifdef HAVE_KCGI

enum { KeyX, KeyY, KeysLen };
static const struct kvalid Keys[KeysLen] = {
	[KeyX] = { .name = "x", .valid = kvalid_int },
	[KeyY] = { .name = "y", .valid = kvalid_int },
};

enum { PageIndex, PagesLen };
static const char *Pages[PagesLen] = {
	[PageIndex] = "index",
};

static noreturn void errkcgi(int eval, enum kcgi_err code, const char *str) {
	errx(eval, "%s: %s", str, kcgi_strerror(code));
}

static int streamWrite(void *cookie, const char *buf, int len) {
	struct kreq *req = cookie;
	enum kcgi_err error = khttp_write(req, buf, (size_t)len);
	if (error) errkcgi(EX_IOERR, error, "khttp_write");
	return len;
}

static void worker(void) {
	struct kfcgi *fcgi;
	enum kcgi_err error = khttp_fcgi_init(
		&fcgi, Keys, KeysLen, Pages, PagesLen, PageIndex
	);
	if (error) errkcgi(EX_CONFIG, error, "khttp_fcgi_init");

	for (;;) {
		struct kreq req;
		error = khttp_fcgi_parse(fcgi, &req);
		if (error) errkcgi(EX_DATAERR, error, "khttp_fcgi_parse");

		uint32_t tileX = TileInitX;
		uint32_t tileY = TileInitY;
		if (req.fieldmap[KeyX]) {
			tileX = (uint32_t)req.fieldmap[KeyX]->parsed.i % TileCols;
		}
		if (req.fieldmap[KeyY]) {
			tileY = (uint32_t)req.fieldmap[KeyY]->parsed.i % TileRows;
		}

		error = khttp_head(
			&req, kresps[KRESP_STATUS], "%s", khttps[KHTTP_200]
		);
		if (error) errkcgi(EX_IOERR, error, "khttp_head");

		error = khttp_head(
			&req, kresps[KRESP_CONTENT_TYPE], "%s", kmimetypes[KMIME_IMAGE_PNG]
		);
		if (error) errkcgi(EX_IOERR, error, "khttp_head");

		error = khttp_body(&req);
		if (error) errkcgi(EX_IOERR, error, "khttp_body");

		FILE *stream = fwopen(&req, streamWrite);
		if (!stream) err(EX_OSERR, "fwopen");

		render(stream, tileX, tileY);

		fclose(stream);
		khttp_free(&req);
	}
}

#endif /* HAVE_KCGI */

int main(int argc, char *argv[]) {
	bool kcgi = false;
	const char *fontPath = "default8x16.psfu";
	const char *dataPath = "torus.dat";
	uint32_t tileX = TileInitX;
	uint32_t tileY = TileInitY;

	int opt;
	while (0 < (opt = getopt(argc, argv, "d:f:kx:y:"))) {
		switch (opt) {
			break; case 'd': dataPath = optarg;
			break; case 'f': fontPath = optarg;
			break; case 'k': kcgi = true;
			break; case 'x': tileX = strtoul(optarg, NULL, 0) % TileCols;
			break; case 'y': tileY = strtoul(optarg, NULL, 0) % TileRows;
			break; default:  return EX_USAGE;
		}
	}

	fontLoad(fontPath);
	tilesMap(dataPath);

#ifdef __FreeBSD__
	int error = cap_enter();
	if (error) err(EX_OSERR, "cap_enter");
#endif

#ifdef HAVE_KCGI
	if (kcgi) worker();
#endif

	render(stdout, tileX, tileY);
}
