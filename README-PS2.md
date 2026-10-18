# Fallout Community Edition — PlayStation 2 port

> ⚠️ **Experimental.** Tested in the PCSX2 emulator only, not yet on a real
> PS2. Expect bugs, slowdowns and missing features; keep backups of your saves.
>
> **Unofficial fan project, made with AI.** This PlayStation 2 port was written
> with the help of an AI assistant (Claude, by Anthropic). It is not affiliated
> with or endorsed by Bethesda, Interplay, or the fallout1-ce developers. No game
> data is included: you need your own legally owned copy of Fallout.

An experimental PS2 port of [alexbatalov/fallout1-ce](https://github.com/alexbatalov/fallout1-ce).
Tested in PCSX2; not yet confirmed on real hardware. You need your own copy of
Fallout (e.g. GOG) — no game data is included.

## Building

Requires the [ps2dev](https://github.com/ps2dev/ps2dev) toolchain (ps2sdk, gsKit,
SDL2 PS2 port).

```sh
export PS2DEV=/path/to/ps2dev PS2SDK=$PS2DEV/ps2sdk GSKIT=$PS2DEV/gsKit
export PATH=$PS2DEV/bin:$PS2DEV/ee/bin:$PS2SDK/bin:$PATH
cmake -S . -B build-ps2 -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-ps2 -j8
mips64r5900el-ps2-elf-strip -o FALLOUT.ELF build-ps2/fallout-ce
```

## Movies and audio

**Game audio** (music, sound effects, speech) works out of the box: the port
plays the original files from `MASTER.DAT` / `DATA` through the PS2's sound
processor. Nothing needs converting.

**Cutscenes** need converting once, because the PS2 can't decode the PC's
`.MVE` format. The port plays them with the PS2's video decoder (IPU) from a
`MOVIES` folder next to `FALLOUT.ELF`:

- `MOVIES/<NAME>.M2V` – MPEG-2 video
- `MOVIES/<NAME>.PCM` – sound: raw signed 16-bit little-endian stereo, 22050 Hz

The included tool does this for you. You need [Python 3](https://www.python.org/)
and [ffmpeg](https://ffmpeg.org/) on your PATH:

```sh
python tools/convert_movies.py "C:\GOG Games\Fallout" "E:\Fallout\PS2"
```

It takes the movies from `MASTER.DAT` (or loose `DATA\ART\CUTS\*.MVE` files),
converts them, and writes the `MOVIES` folder into the second folder you give
(the folder with `FALLOUT.ELF`). Movies without a converted file are skipped,
so the game still runs if you leave this out.

`tools/f1dat.py` is the small extractor it uses; it can also pull any other
file out of a Fallout 1 `.DAT` archive.

## Install, controls and options

```
INSTALL (USB stick, FAT32 or exFAT)
-----------------------------------
Make a folder, e.g.  mass:/FALLOUT/  and copy into it:

    FALLOUT.ELF            (this build)
    fallout.cfg            (from your GOG install)
    MASTER.DAT             (from your GOG install)
    CRITTER.DAT            (from your GOG install)
    DATA\                  (whole folder, from your GOG install)

Do NOT copy the Windows files (falloutw.exe, ddraw.dll, f1_res.*, goggame-*).
The PS2 build ignores f1_res.ini and always runs at 640x480.

Launch FALLOUT.ELF from that folder with wLaunchELF. The game looks for its
data next to the ELF. Save games go to DATA\SAVEGAME on the stick.

Save games: the map states inside each save slot are stored compressed on
PS2 (much smaller and faster to save/load). Saves from older PS2 builds still
load; PS2 saves can't be copied to the PC version.

An internal HDD (hdd0:) or MX4SIO SD card should also work, since SDL's PS2
startup loads those drivers too.


CONTROLS (DualShock 2)
----------------------
Left stick ........ move mouse cursor
Cross (X) ......... left click
Circle (O) ........ right click (cycle cursor mode)
Square ............ Enter
Triangle .......... Inventory (I)
L1 ................ Enter combat (A)
R1 ................ End turn (Space)
L2 ................ Character screen (C)
R2 ................ Pip-Boy (P)
Start ............. Esc (options / cancel)
Select ............ Automap (Tab)
L3 ................ Switch active hand (B)
R3 ................ Open / close on-screen keyboard
D-pad ............. Jump the cursor to the next button, person, item, door
                    or ladder in that direction (console-style); move the
                    cursor to a screen edge to scroll the map
Right stick up/down  Zoom in/out on the map (1x-2x, view follows the cursor);
                     mouse wheel in menus, inventory, dialogue and Pip-Boy

TV OUTPUT (hold L1 + R1, press Select)
-------------------------------------
Cycles 480i -> 480p -> 720p -> 1080i. Press X within 6 seconds to keep the
new mode (saved in PS2VIDEO.CFG), O or waiting switches back. The game still renders 640x480; the PS2's GS
scales it to the chosen mode. 720p/1080i use 16-bit colour to fit in VRAM.
480p/720p/1080i need component cables (YPbPr) on real hardware. If your TV
shows nothing, wait or delete PS2VIDEO.CFG to return to 480i.


USB KEYBOARD AND MOUSE
----------------------
Plug a USB keyboard and/or mouse into the PS2 (in PCSX2: Settings >
Controllers > USB Port 1/2 > HID Keyboard / HID Mouse). They work alongside
the DualShock: all the PC hotkeys, typing save names, and pointing/clicking
with the mouse (wheel scrolls lists). No setup needed.

Everything is hot-swappable: plug or unplug the DualShock, keyboard or mouse
at any time, even mid-game. A re-plugged DualShock is switched back to analog
mode automatically, and buttons held when a device is pulled are released.

SETTINGS (controls, layouts, video)
-----------------------------------
Open it from the main menu (OPTIONS) or in game from the Options menu
(Start > SETTINGS). Tabs:
  CONTROLS  remap any DualShock button (except the D-pad), rebind the
            keyboard hotkeys, swap the mouse buttons or give the middle
            button an action, cursor speed, stick dead zone, zoom direction
            and what the D-pad does (jump to targets / arrow keys)
  GAMEPAD   picture of the DualShock showing what every button does now
  KEYBOARD  picture of a keyboard with the game's hotkeys marked
  SYSTEM    save/load games on the game folder (USB/HDD) or memory card
            1 or 2 (saves go in a FALLOUT folder on the card), plus TV output (480i/480p/720p/1080i) and the GS renderer
            switches (map, mouse cursor, fades; after restart). A new TV
            output must be confirmed (YES) within 6 seconds, otherwise it
            switches back by itself - so a TV that can't show it is safe.
From the main menu there is also GAME PREFS (the game's own preferences).
Click or press Cross on a line to change it, right-click or Circle to step
back; for a keyboard hotkey, click it and press the new key. D-pad moves
between lines and tabs. DONE saves to CONTROLS.CFG next to FALLOUT.ELF,
DEFAULTS resets the current tab, CANCEL discards changes.
Note: rebinding a letter key also changes what that key types.


ON-SCREEN KEYBOARD (press R3)
-----------------------------
D-pad ....... move selection        Cross ...... type selected key
Circle ...... backspace             Triangle ... space
Square ...... Enter                 L1 ......... Shift (next letter uppercase)
Start ....... Esc                   R3 / DONE .. close keyboard
The left stick still moves the cursor while the keyboard is open.
Skilldex: open the keyboard and type S.


GRAPHICS (GS renderer)
----------------------
The PS2's Graphics Synthesizer draws the map: floor tiles, objects
(characters, items, walls, scenery), roofs and the shadow along the map
edges, with the game's lighting and see-through effects done in hardware.
The EE only draws what sits on top (selection outlines, floating text,
projectiles) and the menus/windows. Colours match the PC version; glass,
steam and energy fields are blended by the GS.
The GS also draws the mouse cursor, does fades to and from black, and only
receives the parts of the screen (menus, windows) that changed.
To go back to the EE drawing everything (for comparison or if something
looks wrong), create an empty file named GSMAP.OFF (or GSFLOOR.OFF) next to
FALLOUT.ELF. debug.log gets a "perf:" line every 5 seconds.


PCSX2
-----
PCSX2's host: device is the folder the ELF is in, so FALLOUT.ELF must sit
next to MASTER.DAT and the MOVIES folder (E:\Fallout\PS2).


REBUILDING
----------
    git clone https://github.com/alexbatalov/fallout1-ce
    cd fallout1-ce && git checkout 0609bcf
    git apply fallout1-ce-ps2.patch
    # with PS2DEV/PS2SDK set up (github.com/ps2dev/ps2dev):
    cmake -S . -B build-ps2 -DCMAKE_TOOLCHAIN_FILE=$PS2DEV/share/ps2dev.cmake -DCMAKE_BUILD_TYPE=Release
    cmake --build build-ps2
    mips64r5900el-ps2-elf-strip -o FALLOUT.ELF build-ps2/fallout-ce
```
