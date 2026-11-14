#!/bin/sh
# Build Stilwell Farms -> build/stilwell_farms.gbc
set -e
cd "$(dirname "$0")"
GBDK=${GBDK:-$HOME/Dev/tools/gbdk}
LCC="$GBDK/bin/lcc"
.venv/bin/python tools/gen_music.py && .venv/bin/python tools/gen_art.py
mkdir -p build/obj
CFLAGS="-Wf--max-allocs-per-node50000 -Isrc"
for f in src/*.c; do
  "$LCC" $CFLAGS -c -o build/obj/$(basename "$f" .c).o "$f"
done
"$LCC" -autobank -Wl-yt0x1B -Wl-ya1 -Wl-yoA -Wm-yC -Wm-yn"STILWELLFARM" -Wm-yoA -Wl-m \
  -o build/stilwell_farms.gbc build/obj/*.o
"$GBDK/bin/romusage" build/stilwell_farms.gbc -g 2>/dev/null | tail -12 || true
ls -la build/stilwell_farms.gbc
