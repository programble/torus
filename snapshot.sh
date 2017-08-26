#!/bin/sh
set -e -u

ts=$(date +'%Y.%m.%d.%H.%M')
gzip -c -9 < "$1/torus.dat" > "$2/torus.dat.$ts.gz"
$(dirname "$0")/meta < "$1/torus.dat" | gzip -c -9 > "$2/torus.csv.$ts.gz"
