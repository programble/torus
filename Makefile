CHROOT_USER = torus
CHROOT_GROUP = $(CHROOT_USER)

CFLAGS += -Wall -Wextra -Wpedantic
LDLIBS = -lm -lcurses
BINS = server client help meta merge
OBJS = $(BINS:%=%.o)

all: tags $(BINS)

$(OBJS): torus.h

tags: *.h *.c
	ctags -w *.h *.c

chroot.tar: server client help
	mkdir -p root
	install -d -o root -g wheel \
	    root/bin \
	    root/home \
	    root/lib \
	    root/libexec \
	    root/usr \
	    root/usr/share \
	    root/usr/share/misc
	install -d -o $(CHROOT_USER) -g $(CHROOT_GROUP) root/home/$(CHROOT_USER)
	cp -p -f /libexec/ld-elf.so.1 root/libexec
	cp -p -f \
	    /lib/libc.so.7 \
		/lib/libm.so.5 \
	    /lib/libedit.so.7 \
	    /lib/libncurses.so.8 \
	    /lib/libncursesw.so.8 \
	    root/lib
	cp -p -f /usr/share/misc/termcap.db root/usr/share/misc
	cp -p -f /bin/sh root/bin
	install -o root -g wheel -m 555 server client help root/bin
	tar -c -f chroot.tar -C root bin home lib libexec usr

clean:
	rm -f tags $(OBJS) $(BINS) chroot.tar
