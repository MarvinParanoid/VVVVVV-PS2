VVVVVV for PlayStation 2 - unofficial homebrew port
=====================================================

This is an unofficial, non-commercial, altered version based on the VVVVVV
source code (https://github.com/TerryCavanagh/VVVVVV). VVVVVV is copyright
Terry Cavanagh. This port is not made, approved or endorsed by Terry
Cavanagh, by Sony, or by anyone else involved with VVVVVV. VVVVVV is a
commercial game; if you enjoy it, please buy it at https://thelettervsixtim.es

NO GAME DATA IS INCLUDED. You need the data.zip from your own, legally
obtained copy of VVVVVV. The tool in tools/ converts it on your PC. Do not
share data.zip, data-ps2.zip or a prepared VVVVVV folder: they contain the
game's graphics, sound and music.

Tested so far: one console (PS2 Slim SCPH-7500x, EU), Free McBoot, one USB
stick, official DualShock 2. Other models and setups may behave differently.
BUILD-INFO.txt says which build this is and whether this exact build has
been run on a console.


What you need
-------------
- A PS2 that runs homebrew: Free McBoot (FMCB) or PS2BBL, with wLaunchELF
  (uLaunchELF) or another ELF launcher that can open files on mass:/.
- A USB stick formatted FAT32 with an MBR partition table, about 200 MB
  free. Small, older sticks work best with the PS2's USB 1.1 ports.
- A PC with Python 3.8 or newer (https://www.python.org; on Windows tick
  "Add python.exe to PATH" during setup).
- data.zip from your own copy of VVVVVV:
    * Steam, GOG, itch.io, Humble: in the game's install folder (Steam:
      right-click VVVVVV > Manage > Browse local files > data.zip); or
    * for personal use, the free "VVVVVV: Make and Play Edition"
      (https://thelettervsixtim.es/makeandplay/) also contains a data.zip.
- A TV and cable that show NTSC 60 Hz: the game always outputs 640x448 at
  60 Hz, also on PAL consoles. RGB SCART or component video are safest.


Installation
------------
1. Unzip this archive on your PC. Optional check (Linux/macOS):
     sha256sum -c SHA256SUMS        (every line must say OK)

2. Add your game data. Open a terminal in the unzipped folder (Windows: in
   Explorer, type "cmd" in the address bar and press Enter), then run:

     Windows:      py tools\prepare-ps2-usb.py C:\path\to\data.zip usb\VVVVVV
     Linux/macOS:  python3 tools/prepare-ps2-usb.py /path/to/data.zip usb/VVVVVV

   It checks your data.zip, writes data-ps2.zip and checksum files into
   usb/VVVVVV and must end with "ready: ...". Your data.zip is not changed.
   If it prints ERROR, the message says what is wrong.

3. Copy the folder usb/VVVVVV to the ROOT of the USB stick, so that the
   stick contains VVVVVV/VVVVVV.elf. Eject the stick safely.

4. On the PS2, start wLaunchELF (from the FMCB menu), then:
     FileBrowser > mass:/ > VVVVVV/ > fs_smoke.elf
   This USB self-test takes about 20 seconds and must end with "ALL OK".
   Reset the console (or press the reset button), start wLaunchELF again,
   then run:
     FileBrowser > mass:/ > VVVVVV/ > VVVVVV.elf

   A white-on-black start-up list appears, then the game. On the very first
   start the game asks for the language; only English is included, so press
   Cross. Saves, settings and a log file are written to the stick, inside
   the VVVVVV folder (savedata/), so the stick must stay writable.

If the game has trouble with music, VVVVVV-nomusic.elf is the same game
without music.


Controls
--------
Left stick / D-pad  move               Cross    flip / confirm
Circle              back / pause       Triangle map
Square              interact           R1       restart
L3 + R3 (press both sticks down)       diagnostics overlay on/off


Problems
--------
- mass:/ is empty or the folder is missing: the stick is not FAT32 with an
  MBR partition table, or the PS2 does not support it. Try another (small,
  old) stick and the other USB port.
- fs_smoke.elf does not say ALL OK: photograph the screen; the game will
  probably not run from this stick.
- Black screen with a picture on other software: the TV or cable does not
  accept NTSC 60 Hz.
- A FATAL ERROR or CPU EXCEPTION screen: photograph it; the log file
  VVVVVV/vvvvvv_ps2.log on the stick helps.
- More: VVVVVV/README-PS2.txt (inside the folder) and
  https://github.com/MarvinParanoid/VVVVVV-PS2/blob/main/docs/installing.md


Reporting results
-----------------
Reports from real consoles, working or not, are very welcome:
  https://github.com/MarvinParanoid/VVVVVV-PS2/issues/new/choose
("Hardware test report"). Include the console model (sticker on the
bottom), the USB stick, and the first lines of BUILD-INFO.txt. Attach
vvvvvv_ps2.log and fs_smoke.log from the stick if you like. Never attach
data.zip, data-ps2.zip or the game folder.


Files in this archive
---------------------
README.txt                 this file
BUILD-INFO.txt             build ID, source commit, checksums of the programs
SHA256SUMS                 checksums of every file in this archive
LICENSE-VVVVVV.md          the VVVVVV source code licence (applies to the game)
THIRD-PARTY-NOTICES.txt    licences of the libraries inside the programs
LICENSES/                  full licence texts referred to by the notices
tools/                     prepare-ps2-usb.py and repack-data-zip.py (Python)
usb/VVVVVV/                the game folder for the stick, without game data

Source code, build instructions and how to rebuild or relink the programs:
https://github.com/MarvinParanoid/VVVVVV-PS2 (BUILD-INFO.txt names the commit).
