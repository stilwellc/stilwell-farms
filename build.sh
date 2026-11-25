#!/bin/sh
# Build Stilwell Farms -> build/stilwell_farms.gbc
# Cart: MBC5 + RAM + battery, 32KB SRAM (4 banks, what a ModRetro cartridge has: one save + the live 64x64 farm world, see save.c)
set -e
cd "$(dirname "$0")"
GBDK=${GBDK:-$HOME/Dev/tools/gbdk}
LCC="$GBDK/bin/lcc"
.venv/bin/python tools/gen_music.py && .venv/bin/python tools/gen_art.py && .venv/bin/python tools/gen_routes.py
rm -rf build/obj; mkdir -p build/obj   # fresh: objects of removed/renamed sources must not be linked
CFLAGS="-Wf--max-allocs-per-node50000 -Isrc"
# compile in parallel (one lcc per source; xargs fails the build if any of them fails)
export LCC CFLAGS
ls src/*.c | xargs -P "${JOBS:-8}" -I{} sh -c '"$LCC" $CFLAGS -c -o build/obj/$(basename {} .c).o {}'
"$LCC" -autobank -Wl-yt0x1B -Wl-ya4 -Wl-yoA -Wm-yC -Wm-yn"STILWELLFARM" -Wm-yoA -Wl-m -Wl-j \
  -o build/stilwell_farms.gbc build/obj/*.o
"$GBDK/bin/romusage" build/stilwell_farms.gbc -g 2>/dev/null | tail -20 || true
ls -la build/stilwell_farms.gbc
