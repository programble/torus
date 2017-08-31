USER = torus

all: server client help meta merge

server: server.c torus.h
	$(CC) -Wall -Wextra -Wpedantic $(CFLAGS) -o server server.c

client: client.c torus.h
	$(CC) -Wall -Wextra -Wpedantic $(CFLAGS) -lcurses -o client client.c

help: help.c torus.h
	$(CC) -Wall -Wextra -Wpedantic $(CFLAGS) -o help help.c

meta: meta.c torus.h
	$(CC) -Wall -Wextra -Wpedantic $(CFLAGS) -o meta meta.c

merge: merge.c torus.h
	$(CC) -Wall -Wextra -Wpedantic $(CFLAGS) -lcurses -o merge merge.c

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

clean:
	rm -f server client help meta merge termcap termcap.db chroot.tar

.PHONY: all clean
