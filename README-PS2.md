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

Or with Docker (no toolchain install needed):

```sh
docker build -t fallout1-ce-ps2 .
docker run --rm -v "$PWD:/src" fallout1-ce-ps2                         # build
docker run --rm -v "$PWD:/src" fallout1-ce-ps2 tools/make_dist.sh v0.7  # release zip
```

Release helpers in tools/:
- make_dist.sh VERSION: builds, strips and packs dist/VERSION plus a zip (ELF, README, folder layout, no game data)
- check_no_assets.sh [folder]: fails if any game data is in git or in a release folder
- emu/setup_play.sh + emu/boot_test.sh: build a headless runner from the open-source Play! emulator (it emulates the BIOS itself, no Sony BIOS needed) and boot FALLOUT.ELF with placeholder data, checking drivers, storage search, video, audio and early init
- run_smoke.sh: quick pre-release checks (no game data, valid memory card icons, ELF builds)

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

WHERE THE GAME CAN LIVE
The game looks next to FALLOUT.ELF first, then on every device below, in
folders FALLOUT, FALLOUT1, GAMES/FALLOUT, APPS/FALLOUT or the device root:
  USB sticks / USB hard drives (FAT32 or exFAT; several at once: mass0-3:)
  MX4SIO (SD card adapter in a memory card slot)
  MMCE SD memory cards (SD2PSX, MemCard PRO 2 ...: mmce0: / mmce1:)
  the internal HDD (when launched from it), PCSX2's host: folder
USB and SD devices can be slow to start: the game waits up to 8 seconds for
them. debug.log / the console says which devices were found and where
MASTER.DAT was. Discs (cdrom0:) aren't supported, as the game must write
map states into its DATA folder while you play.


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
            1 or 2, plus TV output (480i/480p/720p/1080i) and the GS renderer
            switches (map, mouse cursor, fades; after restart), picture
            filter (Sharp / Smooth / Soft) and scanlines (Off / Light /
            Medium / Strong), and "Smooth map at 720p+" (experimental:
            also smooths the GS-drawn map when the picture is scaled),
            all applied at once. A new TV
            output must be confirmed (YES) within 6 seconds, otherwise it
            switches back by itself - so a TV that can't show it is safe.
From the main menu there are also GAME PREFS (the game's own preferences)
and a MODS tab (see MODS below).
Click or press Cross on a line to change it, right-click or Circle to step
back; for a keyboard hotkey, click it and press the new key. D-pad moves
between lines and tabs. DONE saves to CONTROLS.CFG next to FALLOUT.ELF,
DEFAULTS resets the current tab, CANCEL discards changes.
Note: rebinding a letter key also changes what that key types.


MEMORY CARD SAVES
-----------------
Each save slot gets its own folder on the card (FALLOUT1-SLOT01 ...
FALLOUT1-SLOT10) with its own icon, so the PS2 browser shows every save as
"Fallout Slot N" with its description, and you can copy or delete saves one
at a time. If the card is missing or too full, or writing to it fails, the
save goes to the game folder (USB / HDD) instead and a message says so; for
the rest of that session saving and loading use the game folder.


MODS
----
Put each mod in its own folder in mods/ next to MASTER.DAT, laid out like
the game's DATA folder (mods/MYMOD/ART/..., mods/MYMOD/MAPS/...). Its files
replace the game's files of the same name. Keep save games of different mods
in different slots.

Compressed mods work without unpacking:
  mods/MYMOD.ZIP           a whole mod in one zip (stored or deflate)
  mods/MYMOD.DAT           a whole mod in one Fallout 1 .DAT
  mods/MYMOD/*.ZIP, *.DAT  archives inside a mod folder (loose files there
                           win over the archives; archives load in name order)
Inside a zip the files can be at the top level (ART/..., MAPS/...) or under a
DATA folder, even inside another folder (MyMod/DATA/ART/...). MOD.TXT and
PREVIEW.BMP can be inside the archive too. ZIP64 and encrypted zips aren't
supported. Files are unpacked into memory when the game opens them, so very
large mods use more memory and load a little slower than loose files.

The MODS screen is the MODS tab in the settings (OPTIONS) screen.
With no mods installed it shows how to add one. When mods/ has any folders:
  - a CHOOSE A MOD list appears at startup (Original game / each mod)
  - OPTIONS > MODS lists the installed mods with their artwork
    and description, which one is in use, and "Ask at startup" (turn the
    startup list off to always start with the chosen mod). A new choice
    takes effect the next time the game starts.

Optional files a mod can include for the MODS screen:
  mods/MYMOD/PREVIEW.BMP   artwork, any size (shown up to 316x237), 8/24/32-bit BMP
  mods/MYMOD/MOD.TXT       line 1: name, line 2: author / version,
                           following lines: description (wrapped to fit)
The choice is kept in mods/LAST.TXT.


ON-SCREEN KEYBOARD (press R3)
-----------------------------
D-pad ....... move selection        Cross ...... type selected key
Circle ...... backspace             Triangle ... space
Square ...... Enter                 L1 ......... Shift (next letter uppercase)
Start ....... Esc                   R3 / DONE .. close keyboard
The left stick still moves the cursor while the keyboard is open.
Skilldex: open the keyboard and type S.


LOADING SPEED
-------------
Map loading avoids a slow search the original game did for every new
object's ID, compressed game files unpack with a faster decoder, image sizes
are remembered instead of re-read, and the Pip-Boy's text is loaded once.
(Ideas from the commit notes of Nicolasalucin/fallout1-ce-ps2; the code
here was written separately.)
Memory: the ELF is built without unused code or unwind tables (about 1.5 MB
less), and the SDL screen texture (about 1.2 MB) is only made if the GS path
can't be used. debug.log shows free memory at startup, before the main menu
and after each map loads ("mem[...]").
If the sound driver doesn't start within 5 seconds the game carries on
without sound instead of freezing.


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
