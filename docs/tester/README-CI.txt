VVVVVV for PlayStation 2 - automated development build (CI)
=============================================================

THIS IS AN UNTESTED DEVELOPMENT BUILD. It was built automatically from one
commit of https://github.com/MarvinParanoid/VVVVVV-PS2 (BUILD-INFO.txt names
the commit and the build ID). Nobody has run this exact build. The port has
been played on one console model (see docs/known-issues.md in the
repository), but a CI build is not a release candidate. For a planned test
session, use a release-candidate archive; this archive is for developers and
for checking a specific change.

This is an unofficial, altered version based on the VVVVVV source code
(https://github.com/TerryCavanagh/VVVVVV). VVVVVV is copyright Terry
Cavanagh. This port is not made, approved or endorsed by Terry Cavanagh,
by Sony, or by anyone else involved with VVVVVV. VVVVVV is a commercial game;
if you enjoy it, please buy it at https://thelettervsixtim.es

NO GAME DATA IS INCLUDED. You need the data.zip from your own copy of
VVVVVV. Do not share data.zip, data-ps2.zip or the prepared folder.


Quick start
-----------
1. Unzip this archive. Check it if you like: in the unzipped folder,
   "sha256sum -c SHA256SUMS" (Linux) must print OK for every file.

2. Add your game data (Python 3.8 or newer):

     Windows:      py tools\prepare-ps2-usb.py C:\path\to\data.zip usb\VVVVVV-CI
     Linux/macOS:  python3 tools/prepare-ps2-usb.py /path/to/data.zip usb/VVVVVV-CI

   It must print "ready: ...". Your data.zip is not changed.

3. Copy usb/VVVVVV-CI to the ROOT of a FAT32 USB stick. It keeps its own
   saves and logs, so it can sit next to other VVVVVV folders.

4. On the PS2 (wLaunchELF or similar):
   - run mass:/VVVVVV-CI/fs_smoke.elf first: it must end with "ALL OK";
   - then mass:/VVVVVV-CI/VVVVVV.elf. The start-up list ends with the build
     ID from BUILD-INFO.txt.
   The game outputs NTSC 640x448i only, also on PAL consoles.

README-PS2.txt inside the folder explains the overlay (L3+R3), the
benchmark files in optional/ and which log files to send back.


Reporting results
-----------------
Open an issue with the "Hardware test report" template and paste the first
lines of BUILD-INFO.txt (build ID and commit):
  https://github.com/MarvinParanoid/VVVVVV-PS2/issues/new/choose
Never attach data.zip, data-ps2.zip or the game folder.


Files in this archive
---------------------
README.txt                 this file
BUILD-INFO.txt             build ID, source commit, CI run, ELF checksums
SHA256SUMS                 checksums of every file in the archive
LICENSE-VVVVVV.md          the VVVVVV source code licence
THIRD-PARTY-NOTICES.txt    licences of the libraries inside the ELFs
tools/                     prepare-ps2-usb.py and repack-data-zip.py
usb/VVVVVV-CI/             the game, fs_smoke.elf and the open-source
                           lang/ and fonts/ files; no game data
