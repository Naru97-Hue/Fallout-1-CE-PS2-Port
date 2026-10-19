#!/bin/sh
# Quick checks before a release, no PS2 needed:
#   1. no game data in the repository
#   2. the memory card icon writer produces valid icon.sys / icon files
#   3. (with the ps2dev toolchain) the ELF builds and contains the PS2 pieces
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== assets"
( cd "$ROOT" && tools/check_no_assets.sh )

echo "== memory card icons"
${CXX:-c++} -D__PS2__ -I"$ROOT/tools/smoke/stub" -I"$ROOT/src" \
    "$ROOT/tools/smoke/icon_test.cc" "$ROOT/src/plib/gnw/ps2card.cc" -o "$TMP/icon_test" -lm
"$TMP/icon_test" "$TMP/icons"
python3 "$ROOT/tools/smoke/check_icons.py" "$TMP/icons"

echo "== compressed mods (.ZIP / .DAT)"
python3 "$ROOT/tools/smoke/make_test_archives.py" "$TMP"
${CXX:-c++} -D__PS2__ -include "$ROOT/tools/smoke/stub/funopen_shim.h" -I"$ROOT/tools/smoke/stub" -I"$ROOT/src" \
    "$ROOT/tools/smoke/modarc_test.cc" "$ROOT/src/plib/gnw/ps2modarc.cc" -o "$TMP/modarc_test" -lz
"$TMP/modarc_test" "$TMP/TEST.ZIP" "$TMP/TEST.DAT"

if command -v mips64r5900el-ps2-elf-gcc >/dev/null 2>&1 && [ -d "$ROOT/build-ps2" ]; then
    echo "== PS2 build"
    cmake --build "$ROOT/build-ps2" -j"$(nproc 2>/dev/null || echo 4)" >/dev/null
    for sym in ps2card_write_icons ps2mods_boot_menu ps2controls_screen ps2video_apply; do
        mips64r5900el-ps2-elf-nm -C "$ROOT/build-ps2/fallout-ce" | grep -q "$sym" || { echo "missing $sym" >&2; exit 1; }
    done
    echo "OK: ELF builds ($(wc -c < "$ROOT/build-ps2/fallout-ce") bytes)"
else
    echo "== PS2 build skipped (no toolchain / build-ps2)"
fi
echo "All smoke checks passed."
