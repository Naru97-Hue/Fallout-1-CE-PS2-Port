#!/bin/sh
# Boots FALLOUT.ELF in the Play! runner with synthetic placeholder data (fonts,
# art, message files - no game content) and checks that startup gets through
# storage search, drivers, video (GS), audio and the game's early init.
#   tools/emu/boot_test.sh <elfrun> <FALLOUT.ELF> [seconds]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ELFRUN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1"); ELF=$2; SECS=${3:-90}
W=$(mktemp -d); [ -n "${KEEP:-}" ] || trap 'rm -rf "$W"' EXIT; echo "work dir: $W"
mkdir -p "$W/host/data" "$W/g/ART" "$W/c/ART/CRITTERS" "$W/g/TEXT/ENGLISH/GAME"
cp "$ELF" "$W/host/FALLOUT.ELF"
for i in 0 1 2 3 4 5; do python3 "$HERE/mkfont.py" "$W/g/FONT$i.FON"; done
for d in ITEMS SCENERY WALLS TILES MISC INTRFACE INVEN HEADS BACKGRND SKILLDEX; do
    mkdir -p "$W/g/ART/$d"; python3 "$HERE/mkfrm.py" "$W/g/ART/$d/DUMMY.FRM" 32 32
    python3 -c "open('$W/g/ART/$d/$d.LST','w',newline='').write('dummy.frm\r\n'*700)"
done
printf 'hmjmps,11,1\r\n' > "$W/c/ART/CRITTERS/CRITTERS.LST"; python3 "$HERE/mkfrm.py" "$W/c/ART/CRITTERS/HMJMPSAA.FRM" 32 32
python3 - "$W/g/TEXT/ENGLISH/GAME" <<'PY'
import sys
body = ''.join('{%d}{}{Msg %d}\r\n' % (i, i) for i in range(3000))
for n in "dbox lsgame combat combatai editor intrface inventry item misc options perk pipboy script skill skilldex stat trait worldmap map proto scrname pro_crit pro_item pro_misc pro_scen pro_tile pro_wall".split():
    open(f"{sys.argv[1]}/{n.upper()}.MSG", 'w', newline='').write(body)
PY
python3 "$HERE/mkdat.py" "$W/g" "$W/host/master.dat" >/dev/null
python3 "$HERE/mkdat.py" "$W/c" "$W/host/critter.dat" >/dev/null
cp "$W/host/master.dat" "$W/host/MASTER.DAT"; cp "$W/host/critter.dat" "$W/host/CRITTER.DAT"
(cd "$W" && timeout $((SECS + 60)) "$ELFRUN" "$W/host/FALLOUT.ELF" "$SECS" "$W/host" > "$W/out.txt" 2>&1) || true
grep -v '^$' "$W/out.txt" | grep -E 'found MASTER|mem\[|direct GS|audio:|art cache|>gmouse_init|Failed on|crash|EXCEPTION' || true
fail=0
for want in "found MASTER.DAT" "direct GS present" "audio: device" ">gmouse_init"; do
    grep -q "$want" "$W/out.txt" || { echo "MISSING: $want"; fail=1; }
done
grep -qiE "ps2crash: (caught|exception)|EXCEPTION" "$W/out.txt" && { echo "CRASH"; fail=1; }
[ $fail -eq 0 ] && echo "OK: boots in the emulator through video, audio and early game init (stops at the proto files placeholder data lacks)"
exit $fail
