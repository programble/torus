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

#include <arpa/inet.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sysexits.h>
#include <unistd.h>
#include <zlib.h>

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

static uint32_t crc;
static void pngWrite(const void *ptr, size_t size) {
	fwrite(ptr, size, 1, stdout);
	if (ferror(stdout)) err(EX_IOERR, "(stdout)");
	crc = crc32(crc, ptr, size);
}
static void pngInt(uint32_t host) {
	uint32_t net = htonl(host);
	pngWrite(&net, 4);
}
static void pngChunk(char type[static 4], uint32_t size) {
	pngInt(size);
	crc = crc32(0, Z_NULL, 0);
	pngWrite(type, 4);
}

int main(int argc, char *argv[]) {
	const char *fontPath = "default8x16.psfu";
	const char *dataPath = "torus.dat";
	uint32_t tileX = TileInitX;
	uint32_t tileY = TileInitY;

	int opt;
	while (0 < (opt = getopt(argc, argv, "d:f:x:y:"))) {
		switch (opt) {
			break; case 'd': dataPath = optarg;
			break; case 'f': fontPath = optarg;
			break; case 'x': tileX = strtoul(optarg, NULL, 0) % TileCols;
			break; case 'y': tileY = strtoul(optarg, NULL, 0) % TileRows;
			break; default:  return EX_USAGE;
		}
	}

	FILE *file = fopen(fontPath, "r");
	if (!file) err(EX_NOINPUT, "%s", fontPath);

	struct {
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
	} psf;
	size_t len = fread(&psf, sizeof(psf), 1, file);
	if (ferror(file)) err(EX_IOERR, "%s", fontPath);
	if (len < 1) errx(EX_DATAERR, "%s: truncated header", fontPath);

	uint8_t glyphs[psf.glyph.len][psf.glyph.height][(psf.glyph.width + 7) / 8];
	len = fread(glyphs, psf.glyph.size, psf.glyph.len, file);
	if (ferror(file)) err(EX_IOERR, "%s", fontPath);
	if (len < 1) errx(EX_DATAERR, "%s: truncated glyphs", fontPath);
	fclose(file);

	int fd = open(dataPath, O_RDONLY);
	if (fd < 0) err(EX_NOINPUT, "%s", dataPath);

	struct Tile *tile = mmap(
		NULL, sizeof(struct Tile),
		PROT_READ, MAP_SHARED,
		fd, sizeof(struct Tile) * (TileRows * tileY + tileX)
	);
	if (tile == MAP_FAILED) err(EX_IOERR, "mmap");
	close(fd);

	pngWrite("\x89PNG\r\n\x1A\n", 8);

	uint32_t width = CellCols * psf.glyph.width;
	uint32_t height = CellRows * psf.glyph.height;

	pngChunk("IHDR", 13);
	pngInt(width);
	pngInt(height);
	pngWrite("\x08\x03\x00\x00\x00", 5);
	pngInt(crc);

	pngChunk("PLTE", sizeof(Palette));
	pngWrite(Palette, sizeof(Palette));
	pngInt(crc);

	uint8_t data[height][1 + width];
	memset(data, 0, sizeof(data));

	for (uint32_t y = 0; y < CellRows; ++y) {
		for (uint32_t x = 0; x < CellCols; ++x) {
			uint8_t cell = tile->cells[y][x];
			uint8_t fg = tile->colors[y][x] & 0x0F;
			uint8_t bg = tile->colors[y][x] >> 4;

			uint32_t row = psf.glyph.height * y;
			uint32_t col = psf.glyph.width * x;
			for (uint8_t gy = 0; gy < psf.glyph.height; ++gy) {
				for (uint8_t gx = 0; gx < psf.glyph.width; ++gx) {
					uint8_t bit = glyphs[cell][gy][gx / 8] >> (7 - gx % 8) & 1;
					data[row + gy][1 + col + gx] = (bit ? fg : bg);
				}
			}
		}
	}

	uLong size = compressBound(sizeof(data));
	uint8_t deflate[size];
	int error = compress(deflate, &size, (Byte *)data, sizeof(data));
	if (error != Z_OK) errx(EX_SOFTWARE, "compress: %d", error);

	pngChunk("IDAT", size);
	pngWrite(deflate, size);
	pngInt(crc);

	pngChunk("IEND", 0);
	pngInt(crc);

	return EX_OK;
}
