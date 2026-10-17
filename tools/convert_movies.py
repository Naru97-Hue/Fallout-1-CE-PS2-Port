#!/usr/bin/env python3
"""Convert Fallout's cutscenes (.MVE) to the PS2 port's movie format.

The PS2 build plays MOVIES/<NAME>.M2V (MPEG-2 video, decoded by the PS2's IPU)
with MOVIES/<NAME>.PCM (raw signed 16-bit little-endian stereo, 22050 Hz).

Usage:
    python convert_movies.py <Fallout folder> [output folder]

<Fallout folder> is your installed game (with MASTER.DAT, and DATA\\ART\\CUTS
if your version has loose movies). The MOVIES folder is created in the output
folder (default: the Fallout folder). Requires ffmpeg on your PATH.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import f1dat  # noqa: E402


def find_mves(game_dir, work_dir):
    found = {}
    # Loose files first (DATA\ART\CUTS), they override MASTER.DAT.
    for root, _, files in os.walk(game_dir):
        if "cuts" not in root.lower():
            continue
        for name in files:
            if name.lower().endswith(".mve"):
                found[name.lower()] = os.path.join(root, name)
    master = os.path.join(game_dir, "MASTER.DAT")
    if os.path.exists(master):
        for name in f1dat.extract(master, "ART\\CUTS\\", work_dir, ext=".MVE"):
            found.setdefault(name.lower(), os.path.join(work_dir, name))
    return found


def convert(src, out_dir):
    name = os.path.splitext(os.path.basename(src))[0].upper()
    m2v = os.path.join(out_dir, name + ".M2V")
    pcm = os.path.join(out_dir, name + ".PCM")
    base = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", src]
    subprocess.run(base + ["-an", "-vf", "fps=30000/1001,format=yuv420p", "-c:v", "mpeg2video",
                           "-q:v", "3", "-g", "15", "-bf", "0", "-f", "mpeg2video", m2v], check=True)
    # Some movies have no sound track; that's fine (they play silently).
    subprocess.run(base + ["-vn", "-ac", "2", "-ar", "22050", "-f", "s16le", pcm], check=False)
    if os.path.exists(pcm) and os.path.getsize(pcm) == 0:
        os.remove(pcm)
    print("converted", name)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    if shutil.which("ffmpeg") is None:
        print("ffmpeg was not found on your PATH: install it first.")
        return 1
    game_dir = sys.argv[1]
    out_dir = os.path.join(sys.argv[2] if len(sys.argv) > 2 else game_dir, "MOVIES")
    os.makedirs(out_dir, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        mves = find_mves(game_dir, work)
        if not mves:
            print("No .MVE movies found in", game_dir)
            return 1
        for path in sorted(mves.values()):
            convert(path, out_dir)
    print("Done:", out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
