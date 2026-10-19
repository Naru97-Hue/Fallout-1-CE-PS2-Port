#!/bin/sh
# Builds a headless PS2 runner from the open-source Play! emulator. Play!
# emulates the PS2 BIOS itself (HLE), so no Sony BIOS file is needed.
#   tools/emu/setup_play.sh [dir]     (default: ./play)  -> <dir>/build-at/elfrun
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
DIR=${1:-play}
[ -d "$DIR" ] || git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/jpd002/Play-.git "$DIR"
cp "$HERE/ElfRun.cpp" "$DIR/tools/AutoTest/ElfRun.cpp"
grep -q elfrun "$DIR/tools/AutoTest/CMakeLists.txt" || printf '\nadd_executable(elfrun ElfRun.cpp)\ntarget_link_libraries(elfrun PlayCore ${AUTOTEST_PROJECT_LIBS})\n' >> "$DIR/tools/AutoTest/CMakeLists.txt"
cmake -S "$DIR/tools/AutoTest" -B "$DIR/build-at" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$DIR/build-at" --target elfrun
echo "built $DIR/build-at/elfrun"
