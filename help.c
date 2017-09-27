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

#include <err.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sysexits.h>
#include <unistd.h>

#include "torus.h"

static int client;

static void clientMessage(const struct ClientMessage *msg) {
    ssize_t len = send(client, msg, sizeof(*msg), 0);
    if (len < 0) err(EX_IOERR, "send");
}

static void clientMove(int8_t dx, int8_t dy) {
    struct ClientMessage msg = {
        .type = CLIENT_MOVE,
        .data.m = { .dx = dx, .dy = dy },
    };
    clientMessage(&msg);
}

static void clientPut(uint8_t color, char cell) {
    struct ClientMessage msg = {
        .type = CLIENT_PUT,
        .data.p = { .color = color, .cell = cell },
    };
    clientMessage(&msg);
}

#define DELAY (50000)

#define R (COLOR_RED)
#define G (COLOR_GREEN)
#define Y (COLOR_YELLOW)
#define B (COLOR_BLUE)
#define M (COLOR_MAGENTA)
#define C (COLOR_CYAN)
#define W (COLOR_WHITE)
#define I (COLOR_BRIGHT | COLOR_WHITE)

static void h(void) { clientMove(-1,  0); usleep(DELAY); }
static void j(void) { clientMove( 0,  1); usleep(DELAY); }
static void k(void) { clientMove( 0, -1); usleep(DELAY); }
static void l(void) { clientMove( 1,  0); usleep(DELAY); }
static void y(void) { clientMove(-1, -1); usleep(DELAY); }
static void u(void) { clientMove( 1, -1); usleep(DELAY); }
static void b(void) { clientMove(-1,  1); usleep(DELAY); }
static void n(void) { clientMove( 1,  1); usleep(DELAY); }

static void p(uint8_t color, char cell) {
    clientPut(color, cell);
    usleep(DELAY);
}

static uint8_t len;

static void s(uint8_t color, const char *str) {
    for (; *str; ++len, ++str) {
        clientPut(color, *str);
        clientMove(1, 0);
        usleep(DELAY);
    }
}

static void r(void) {
    clientMove(-len, 1);
    usleep(DELAY);
    len = 0;
}

int main() {
    client = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (client < 0) err(EX_OSERR, "socket");

    struct sockaddr_un addr = {
        .sun_family = AF_LOCAL,
        .sun_path = "torus.sock",
    };
    int error = connect(client, (struct sockaddr *)&addr, sizeof(addr));
    if (error) err(EX_IOERR, "torus.sock");

    pid_t pid = fork();
    if (pid < 0) err(EX_OSERR, "fork");

    if (!pid) {
        for (;;) {
            char buf[4096];
            ssize_t len = recv(client, buf, sizeof(buf), 0);
            if (len < 0) err(EX_IOERR, "recv");
            if (!len) return EX_OK;
        }
    }

    clientMove(-CELL_INIT_X, -CELL_INIT_Y);
    clientMove(28, 0);

    for (;;) {
        for (int i = 0; i < 10; ++i) {
            clientMove(0, 1);
            usleep(DELAY / 5);
        }
        for (int i = 0; i < 11; ++i) {
            for (int j = 0; j < 28; ++j) {
                clientPut(W, ' ');
                if (i % 2) clientMove(1, 0);
                else clientMove(-1, 0);
                usleep(DELAY / 5);
            }
            clientPut(W, ' ');
            if (i != 10) clientMove(0, -1);
            usleep(DELAY / 5);
        }

        j(); l(); l();
        s(W, "Welcome to "); s(I, "ascii.town"); s(W, "!"); r();
        r(); r();

        n(); n(); s(W, "o-"); s(I, "l");
        h(); b(); p(W, '\\'); n(); p(I, 'n');
        y(); h(); p(W, '|'); j(); p(I, 'j');
        y(); p(W, '/'); b(); p(I, 'b');
        k(); u(); p(W, '-'); h(); p(I, 'h');
        u(); p(W, '\\'); y(); p(I, 'y');
        n(); l(); p(W, '|'); k(); p(I, 'k');
        n(); p(W, '/');  u(); p(I, 'u');

        u(); s(W, "    "); len = 0;

        s(I, "q "); s(W, "quit");    r();
        s(I, "i "); s(W, "insert");  r();
        s(I, "r "); s(W, "replace"); r();
        s(I, "R "); s(W, "draw");    r();
        s(I, "~ "); s(W, "color");   r();
        s(I, "` "); s(W, "pipette"); r();
        s(I, "* "); s(W, "bright");

        s(W, "     "); len = 0;

        clientPut(W, '7'); k();
        clientPut(C, '6'); k();
        clientPut(M, '5'); k();
        clientPut(B, '4'); k();
        clientPut(Y, '3'); k();
        clientPut(G, '2'); k();
        clientPut(R, '1'); k();

        l(); n();

        clientPut(R << 4, '!'); j();
        clientPut(G << 4, '@'); j();
        clientPut(Y << 4, '#'); j();
        clientPut(B << 4, '$'); j();
        clientPut(M << 4, '%'); j();
        clientPut(C << 4, '^'); j();
        clientPut(W << 4, '&'); j();

        h(); k(); k(); k(); k(); k(); k(); k(); k(); k(); h();

        sleep(30);

        u(); l(); l(); l();
    }
}
