USER = torus
BINS = server client help meta merge
CFLAGS += -Wall -Wextra -Wpedantic
LDLIBS = -lcurses

all: tags $(BINS)

$(BINS): torus.h

# Only necessary so GNU make doesn't try to use torus.h as a source.
.c:
	$(CC) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

termcap: termcap.diff
	patch -p0 -o termcap < termcap.diff

termcap.db: termcap
	cap_mkdb termcap

chroot.tar: server client help termcap.db
	mkdir -p root
	install -d -o root -g wheel \
	    root/bin \
	    root/home \
	    root/lib \
	    root/libexec \
	    root/usr \
	    root/usr/share \
	    root/usr/share/misc
	install -d -o $(USER) -g $(USER) root/home/$(USER)
	install -o root -g wheel -m 555 /libexec/ld-elf.so.1 root/libexec
	install -o root -g wheel -m 444 \
	    /lib/libc.so.7 \
	    /lib/libedit.so.7 \
	    /lib/libncurses.so.8 \
	    /lib/libncursesw.so.8 \
	    root/lib
	install -o root -g wheel -m 444 termcap.db root/usr/share/misc
	install -o root -g wheel -m 555 /bin/sh root/bin
	install -o root -g wheel -m 555 server client help root/bin
	tar -c -f chroot.tar -C root bin home lib libexec usr

tags: *.h *.c
	ctags -w *.h *.c

clean:
	rm -f tags $(BINS) termcap termcap.db chroot.tar

.PHONY: all clean
