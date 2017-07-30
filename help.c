#if 0
exec cc -Wall -Wextra -pedantic $@ -o help $0
#endif

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
    struct ClientMessage msg = { .type = CLIENT_MOVE };
    msg.data.m.dx = dx;
    msg.data.m.dy = dy;
    clientMessage(&msg);
}

static void clientColor(uint8_t color) {
    struct ClientMessage msg = { .type = CLIENT_COLOR };
    msg.data.c = color;
    clientMessage(&msg);
}

static void clientPut(char cell) {
    struct ClientMessage msg = { .type = CLIENT_PUT };
    msg.data.p = cell;
    clientMessage(&msg);
}

#define DELAY (100000)

static void clear(uint8_t width, uint8_t height) {
    uint8_t x = 0;
    for (uint8_t y = 0; y < height; ++y) {
        if (x) {
            for (; x > 0; --x) {
                clientPut(' ');
                clientMove(-1, 0);
                usleep(DELAY / 10);
            }
            clientPut(' ');
            x = 0;
        } else {
            for (; x < width; ++x) {
                clientPut(' ');
                clientMove(1, 0);
                usleep(DELAY / 10);
            }
            clientPut(' ');
            x = width;
        }
        clientMove(0, 1);
    }
    clientMove(-x, -height);
}

static void white(void) {
    clientColor(COLOR_WHITE);
}

static void brite(void) {
    clientColor(COLOR_BRIGHT | COLOR_WHITE);
}

static int8_t lineLen;

static void string(const char *str) {
    for (; *str; ++str) {
        clientPut(*str);
        clientMove(1, 0);
        lineLen++;
        usleep(DELAY);
    }
}

static void enter(void) {
    clientMove(-lineLen, 1);
    lineLen = 0;
    usleep(DELAY);
}

static void mvPut(int8_t dx, int8_t dy, char cell) {
    clientMove(dx, dy);
    clientPut(cell);
    usleep(DELAY);
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

    for (;;) {
        clear(28, 11);
        clientMove(2, 1);

        white(); string("Welcome to ");
        brite(); string("ascii.town");
        white(); string("!");
        enter();

        white(); mvPut( 2,  4, 'o');
        white(); mvPut( 1,  0, '-');
        brite(); mvPut( 1,  0, 'l');
        white(); mvPut(-1,  1, '\\');
        brite(); mvPut( 1,  1, 'n');
        white(); mvPut(-2, -1, '|');
        brite(); mvPut( 0,  1, 'j');
        white(); mvPut(-1, -1, '/');
        brite(); mvPut(-1,  1, 'b');
        white(); mvPut( 1, -2, '-');
        brite(); mvPut(-1,  0, 'h');
        white(); mvPut( 1, -1, '\\');
        brite(); mvPut(-1, -1, 'y');
        white(); mvPut( 2,  1, '|');
        brite(); mvPut( 0, -1, 'k');
        white(); mvPut( 1,  1, '/');
        brite(); mvPut( 1, -1, 'u');

        clientMove(5, 0);
        brite(); string("q");
        white(); string(" quit");
        enter();
        brite(); string("i");
        white(); string(" insert");
        enter();
        brite(); string("r");
        white(); string(" replace");
        enter();
        brite(); string("R");
        white(); string(" draw");
        enter();
        brite(); string("~");
        white(); string(" color");
        enter();

        clientMove(13, -6);
        clientColor(COLOR_RED);     mvPut(0, 0, '1');
        clientColor(COLOR_GREEN);   mvPut(0, 1, '2');
        clientColor(COLOR_YELLOW);  mvPut(0, 1, '3');
        clientColor(COLOR_BLUE);    mvPut(0, 1, '4');
        clientColor(COLOR_MAGENTA); mvPut(0, 1, '5');
        clientColor(COLOR_CYAN);    mvPut(0, 1, '6');
        clientColor(COLOR_WHITE);   mvPut(0, 1, '7');
        clientColor(COLOR_BRIGHT | COLOR_WHITE);   mvPut(2,  0, '&');
        clientColor(COLOR_BRIGHT | COLOR_CYAN);    mvPut(0, -1, '^');
        clientColor(COLOR_BRIGHT | COLOR_MAGENTA); mvPut(0, -1, '%');
        clientColor(COLOR_BRIGHT | COLOR_BLUE);    mvPut(0, -1, '$');
        clientColor(COLOR_BRIGHT | COLOR_YELLOW);  mvPut(0, -1, '#');
        clientColor(COLOR_BRIGHT | COLOR_GREEN);   mvPut(0, -1, '@');
        clientColor(COLOR_BRIGHT | COLOR_RED);     mvPut(0, -1, '!');

        clientMove(-26, -3);

        sleep(30);
    }
}