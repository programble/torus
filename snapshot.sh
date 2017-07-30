#!/bin/sh
set -e -u

ts=`date +'%Y.%m.%d.%H.%M'`
gzip -c -9 < "$1/torus.dat" > "$2/torus.dat.$ts.gz"
