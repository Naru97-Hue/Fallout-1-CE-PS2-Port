#!/bin/sh
# Builds a release folder and zip: the stripped ELF, the README and a
# folder layout to copy the game into. No game data is included.
#
#   tools/make_dist.sh v0.7          # -> dist/v0.7/ and dist/fallout1-ce-ps2-v0.7.zip
#
# Needs the ps2dev toolchain in PATH (or run it inside the Docker image, see
# README-PS2.md) and an existing build-ps2 (cmake --build build-ps2).
set -eu

VERSION=${1:?usage: tools/make_dist.sh <version>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD:-$ROOT/build-ps2}
DIST=$ROOT/dist/$VERSION
STRIP=${STRIP:-mips64r5900el-ps2-elf-strip}

cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"

rm -rf "$DIST"
mkdir -p "$DIST/FALLOUT/mods"
"$STRIP" -o "$DIST/FALLOUT/FALLOUT.ELF" "$BUILD/fallout-ce"
cp "$ROOT/README-PS2.md" "$DIST/README-PS2.md"
cp "$ROOT/LICENSE.md" "$DIST/LICENSE.md"
[ -f "$ROOT/tools/convert_movies.py" ] && mkdir -p "$DIST/tools" && cp "$ROOT/tools/convert_movies.py" "$ROOT/tools/f1dat.py" "$DIST/tools/"

cat > "$DIST/FALLOUT/PUT_GAME_FILES_HERE.txt" <<'TXT'
Copy these from your own copy of Fallout into this folder:
  MASTER.DAT, CRITTER.DAT, the DATA folder (and MOVIES after converting them,
  see README-PS2.md).
Optional: put mods in mods/<MODNAME>/ - a boot menu will offer them.
TXT

"$ROOT/tools/check_no_assets.sh" "$DIST"

( cd "$ROOT/dist" && rm -f "fallout1-ce-ps2-$VERSION.zip" && \
  (command -v zip >/dev/null && zip -qr "fallout1-ce-ps2-$VERSION.zip" "$VERSION" || \
   python3 -c "import shutil,sys; shutil.make_archive(sys.argv[1], 'zip', '.', sys.argv[2])" "fallout1-ce-ps2-$VERSION" "$VERSION") )

ls -la "$DIST/FALLOUT" "$ROOT/dist/fallout1-ce-ps2-$VERSION.zip"
