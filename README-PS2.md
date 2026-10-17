# Fallout Community Edition — PlayStation 2 port

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
D-pad ............. Scroll the map
Right stick up/down  Zoom in/out on the map (1x-2x, view follows the cursor);
                     mouse wheel in menus, inventory, dialogue and Pip-Boy

TV OUTPUT (hold L1 + R1, press Select)
-------------------------------------
Cycles 480i -> 480p -> 720p -> 1080i. The choice is saved in PS2VIDEO.CFG
and used on the next boot. The game still renders 640x480; the PS2's GS
scales it to the chosen mode. 720p/1080i use 16-bit colour to fit in VRAM.
480p/720p/1080i need component cables (YPbPr) on real hardware. If your TV
shows nothing, wait or delete PS2VIDEO.CFG to return to 480i.


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
