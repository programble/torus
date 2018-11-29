CHROOT_USER = torus
CHROOT_GROUP = $(CHROOT_USER)

CFLAGS += -std=c11 -Wall -Wextra -Wpedantic
LDFLAGS = -static
LDLIBS = -lcursesw -lutil

-include config.mk

BINS = client image merge meta server
OBJS = $(BINS:%=%.o)

all: tags $(BINS)

$(OBJS): torus.h

client.o: help.h

image.o: png.h

.o:
	$(CC) $(LDFLAGS) $< $(LDLIBS) -o $@

tags: *.h *.c
	ctags -w *.h *.c

chroot.tar: client server
	install -d -o root -g wheel \
		root \
		root/bin \
		root/home \
		root/usr/share/misc \
		root/var/run
	install -d -o $(CHROOT_USER) -g $(CHROOT_GROUP) root/home/$(CHROOT_USER)
	install -d -o $(CHROOT_USER) -g $(CHROOT_GROUP) root/var/run/torus
	cp -af /usr/share/locale root/usr/share
	cp -fp /usr/share/misc/termcap.db root/usr/share/misc
	cp -fp /rescue/sh root/bin
	install client server root/bin
	tar -cf chroot.tar -C root bin home usr var

install: chroot.tar rc.torus
	tar -xf chroot.tar -C /home/$(CHROOT_USER)
	install rc.torus /usr/local/etc/rc.d/torus

clean:
	rm -f $(OBJS) $(BINS) tags root chroot.tar

help.h:
	head -c 4096 torus.dat \
		| file2c -sx 'static const uint8_t HelpData[] = {' '};' \
		> help.h
	echo 'static const struct Tile *Help = (const struct Tile *)HelpData;' \
		>> help.h

README: torus.1
	mandoc torus.1 | col -bx > README
