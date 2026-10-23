VVVVVV for PlayStation 2 - unofficial hardware-test release
==============================================================

THIS HAS NOT RUN ON A REAL PS2 YET. Everything was tested only in the PCSX2
emulator. This release exists to find out how it behaves on real consoles.
Results of any kind (it works, it crashes, it shows a black screen) help.

This is an unofficial, altered version based on the VVVVVV source code
(https://github.com/TerryCavanagh/VVVVVV). VVVVVV is copyright Terry
Cavanagh. This port is not made, approved or endorsed by Terry Cavanagh,
by Sony, or by anyone else involved with VVVVVV. VVVVVV is a commercial game;
if you enjoy it, please buy it at https://thelettervsixtim.es

NO GAME DATA IS INCLUDED. You need the data.zip from your own copy of
VVVVVV. The tool below converts it on your PC. Do not share data.zip,
data-ps2.zip or the prepared folders: they contain the game's graphics,
sound and music.


What you need
-------------
- A PS2 that can run homebrew: Free McBoot (FMCB), PS2BBL or similar, with
  wLaunchELF (or another ELF launcher that can open files on mass:/).
- A USB stick, FAT32 with an MBR partition table, about 200 MB free.
  Small/old sticks tend to work best with the PS2's USB 1.1 ports.
- A PC with Python 3.8 or newer (https://www.python.org; on Windows tick
  "Add python.exe to PATH" during setup). No other software is needed.
- VVVVVV's data.zip from your own copy:
    * Steam/GOG/itch.io/Humble: in the game's install folder (e.g. Steam:
      right-click VVVVVV > Manage > Browse local files > data.zip), or
    * for personal use, the free "VVVVVV: Make and Play Edition"
      (https://thelettervsixtim.es/makeandplay/) also ships a data.zip, as
      the VVVVVV source licence describes. Only the full game's data.zip has
      been tested so far.
- A TV/cable that shows NTSC 60 Hz: the game always outputs NTSC 640x448i,
  also on PAL consoles (RGB SCART or component are safest).


Quick start
-----------
1. Unzip this archive anywhere on your PC.

2. Prepare the game folders with your data.zip. Open a terminal in the
   unzipped folder (Windows: in Explorer type "cmd" in the address bar):

     Windows:      py tools\prepare-ps2-usb.py C:\path\to\data.zip usb\VVVVVV usb\VVVVVV-P31DEV
     Linux/macOS:  python3 tools/prepare-ps2-usb.py /path/to/data.zip usb/VVVVVV usb/VVVVVV-P31DEV

   It checks your data.zip, writes data-ps2.zip and checksum files into both
   folders and must print "ready: ..." twice. Your data.zip is not changed.
   If it reports an ERROR, the message says what is wrong.

3. Copy the three folders inside usb\ to the ROOT of the USB stick:

     <stick>/VVVVVV/            H1-RC2, the baseline build      (test first)
     <stick>/VVVVVV-P31DEV/     the experimental build           (test second)
     <stick>/GSPOC/             optional technical experiments

   Eject the stick safely.

4. Follow TESTING.txt. The first session takes about 15 minutes.


Controls
--------
Left stick / D-pad  move           Cross    flip / confirm
Circle              back / pause   Triangle map
Square              interact       R1       restart
L3 + R3 (press both sticks)        performance overlay on/off


Reporting results
-----------------
Open an issue with the "Hardware test report" template:
  https://github.com/MarvinParanoid/VVVVVV-PS2/issues/new/choose
TESTING.txt lists what to note and which files to attach. Never attach
data.zip, data-ps2.zip or the game folders.


Files in this archive
---------------------
README.txt, TESTING.txt        these instructions
BUILD-INFO.txt, SHA256SUMS     build identities and checksums
LICENSE-VVVVVV.md              VVVVVV's source licence (applies to the game code)
THIRD-PARTY-NOTICES.txt        licences of the libraries inside the ELF files
tools/                         prepare-ps2-usb.py + repack-data-zip.py (Python)
usb/                           the folders for the stick (without game data)
Source code: https://github.com/MarvinParanoid/VVVVVV-PS2
