#!/bin/sh
set -e -u -x

./server.c -O3 -g
./client.c -O3 -g
./help.c -O3 -g

if [ ! -f termcap.db ]; then
    patch -p0 -o termcap < termcap.diff
    cap_mkdb termcap
fi

user=torus
ownflags='-o root -g wheel'
dirflags="-d $ownflags"
binflags="$ownflags -m 555"
libflags="$ownflags -m 444"

mkdir -p root

for dir in bin home lib libexec usr; do
    sudo install $dirflags root/$dir
done
sudo install $dirflags root/usr/share
sudo install $dirflags root/usr/share/misc
sudo install -d -o $user -g $user root/home/$user

sudo install $binflags /libexec/ld-elf.so.1 root/libexec

for lib in libc.so.7 libedit.so.7 libncurses.so.8 libncursesw.so.8; do
    sudo install $libflags /lib/$lib root/lib
done

for bin in server client help; do
    sudo install $binflags $bin root/bin
done
sudo install $binflags /bin/sh root/bin

sudo install $libflags termcap.db root/usr/share/misc

tar -c -f chroot.tar -C root bin home lib libexec usr
