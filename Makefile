CHROOT_USER = torus
CHROOT_GROUP = $(CHROOT_USER)

CFLAGS += -Wall -Wextra -Wpedantic
LDFLAGS += -static
LDLIBS = -lcursesw -lutil -lz
BINS = server client image meta merge
OBJS = $(BINS:%=%.o)

all: tags $(BINS)

.o:
	$(CC) $(LDFLAGS) $< $(LDLIBS) -o $@

$(OBJS): torus.h

client.o: help.h

help.h:
	head -c 4096 torus.dat \
		| file2c -s -x 'static const uint8_t HelpData[] = {' '};' \
		> help.h
	echo 'static const struct Tile *Help = (const struct Tile *)HelpData;' \
		>> help.h

tags: *.h *.c
	ctags -w *.h *.c

chroot.tar: server client
	mkdir -p root
	install -d -o root -g wheel \
	    root/bin \
	    root/home \
	    root/usr \
	    root/usr/share \
	    root/usr/share/misc \
		root/var \
		root/var/run
	install -d -o $(CHROOT_USER) -g $(CHROOT_GROUP) root/home/$(CHROOT_USER)
	install -d -o $(CHROOT_USER) -g $(CHROOT_GROUP) root/var/run/torus
	cp -a -f /usr/share/locale root/usr/share
	cp -p -f /usr/share/misc/termcap.db root/usr/share/misc
	cp -p -f /rescue/sh root/bin
	install -o root -g wheel -m 555 server client root/bin
	tar -c -f chroot.tar -C root bin home usr var

clean:
	rm -f tags $(OBJS) $(BINS) chroot.tar

README: torus.1
	mandoc torus.1 | col -b -x > README
