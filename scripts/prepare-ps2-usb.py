#!/usr/bin/env python3
"""Prepare VVVVVV PS2 package folders with YOUR OWN VVVVVV data.zip.

    python3 prepare-ps2-usb.py <data.zip> <folder> [<folder> ...]
    python3 prepare-ps2-usb.py --check <data.zip>
    python3 prepare-ps2-usb.py --manifest-only <folder> [<folder> ...]

For each package folder (one that contains VVVVVV.elf, e.g. usb/VVVVVV):
  1. checks that data.zip is a complete VVVVVV data.zip (the 49 files the PS2
     build loads);
  2. writes data-ps2.zip into the folder: the same archive with the music
     stored uncompressed so the PS2 can stream it (repack-data-zip.py, which
     must sit next to this script; your data.zip is only read, never changed);
  3. checks data-ps2.zip (every entry present and readable, music unchanged);
  4. writes fs_smoke_expect.txt (checksums the USB self-test fs_smoke.elf
     compares against) and MANIFEST.sha256 (checksums of every file);
  5. checks the folder against FAT32 rules.
Then copy the folder(s) to the root of a FAT32 USB stick.

Needs only Python 3.8+ (Windows, Linux, macOS). Nothing is downloaded. The
game data stays yours: do not share data.zip, data-ps2.zip or prepared folders.
Used by scripts/make-usb-package.sh as well, so release packages and tester
packages are prepared by the same code.
"""
import hashlib
import importlib.util
import os
import shutil
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

GRAPHICS = ("covered elephant ending entcolours flipgamecomplete fliplevelcomplete flipsprites gamecomplete "
            "levelcomplete minimap site site2 site3 site4 sprites teleporter tiles tiles2 tiles3").split()
SOUNDS = ("blip blip2 coin combine countdown crash crashing crew1 crew2 crew3 crew4 crew5 crew6 crumble gamesaved "
          "go hurt jump jump2 newrecord preteleport rescue save souleyeminijingle teleport terminal trophy vanish").split()
NEEDED = (["vvvvvvmusic.vvv", "VVVVVV.png"] + [f"graphics/{g}.png" for g in GRAPHICS]
          + [f"sounds/{x}.wav" for x in SOUNDS])
# files fs_smoke.elf re-reads whole (paths relative to the package folder)
EXPECT_FILES = ["README-PS2.txt", "SOURCE.txt", "fonts/font.png", "lang/en/meta.xml", "VVVVVV.elf"]


def fail(msg):
    sys.exit(f"ERROR: {msg}")


def check_data_zip(path):
    """The input must be the game's data.zip with everything the PS2 build loads."""
    if not os.path.isfile(path):
        fail(f"{path}: file not found. Give the path to the data.zip of your own copy of VVVVVV.")
    try:
        names = set(zipfile.ZipFile(path).namelist())
    except zipfile.BadZipFile as e:
        fail(f"{path}: not a zip archive ({e})")
    missing = [n for n in NEEDED if n not in names]
    if missing:
        fail(f"{path}: not a complete VVVVVV data.zip, missing {len(missing)}: {', '.join(missing[:8])}"
             + (" ..." if len(missing) > 8 else ""))
    print(f"data.zip: all {len(NEEDED)} required entries present ({len(names)} entries)")


def load_repack():
    path = os.path.join(HERE, "repack-data-zip.py")
    if not os.path.isfile(path):
        fail(f"repack-data-zip.py must be next to this script ({HERE})")
    spec = importlib.util.spec_from_file_location("repack_data_zip", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def repack(data_zip, out_zip):
    """Runs repack-data-zip.py <data_zip> <out_zip> (it verifies its own output)."""
    mod = load_repack()
    argv = sys.argv
    sys.argv = ["repack-data-zip.py", data_zip, out_zip]
    try:
        mod.main()
    finally:
        sys.argv = argv


def check_ps2_zip(path):
    with zipfile.ZipFile(path) as z:
        names = set(z.namelist())
        missing = [n for n in NEEDED if n not in names]
        if missing:
            fail(f"{path}: entries missing after repack: {', '.join(missing[:8])}")
        if z.getinfo("vvvvvvmusic.vvv").compress_type != zipfile.ZIP_STORED:
            fail(f"{path}: vvvvvvmusic.vvv is not stored uncompressed")
    print(f"{os.path.basename(path)}: structure OK ({len(names)} entries, music stored)")


def write_text(path, text):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(text)


def write_expect(pkg):
    """CRC32s for fs_smoke.elf: whole small files plus the ELF, and fixed 32 KB
    chunks spread over data-ps2.zip (exercises seeking in the 63 MB file)."""
    out = ["# fs_smoke.elf re-reads these from the USB stick and compares CRC32s"]
    for rel in EXPECT_FILES:
        with open(os.path.join(pkg, *rel.split("/")), "rb") as f:
            data = f.read()
        out.append(f"file {rel} {len(data)} {zlib.crc32(data):08x}")
    zp = os.path.join(pkg, "data-ps2.zip")
    size = os.path.getsize(zp)
    with open(zp, "rb") as f:
        for i in range(24):
            off = 0 if i == 0 else size - 32768 if i == 23 else (size // 23) * i + (i * 4099) % 32768
            f.seek(off)
            data = f.read(32768)
            out.append(f"chunk data-ps2.zip {off} {len(data)} {zlib.crc32(data):08x}")
    write_text(os.path.join(pkg, "fs_smoke_expect.txt"), "\n".join(out) + "\n")


def package_files(pkg):
    """Paths relative to pkg as './a/b', sorted bytewise like `find | sort`."""
    rels = []
    for d, dirs, files in os.walk(pkg):
        for n in files:
            rel = os.path.relpath(os.path.join(d, n), pkg).replace(os.sep, "/")
            if rel != "MANIFEST.sha256":
                rels.append("./" + rel)
    return sorted(rels, key=lambda r: r.encode("utf-8"))


def write_manifest(pkg):
    lines = []
    for rel in package_files(pkg):
        h = hashlib.sha256()
        with open(os.path.join(pkg, *rel[2:].split("/")), "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        lines.append(f"{h.hexdigest()}  {rel}")
    write_text(os.path.join(pkg, "MANIFEST.sha256"), "\n".join(lines) + "\n")


def check_manifest(pkg):
    bad = []
    with open(os.path.join(pkg, "MANIFEST.sha256"), encoding="utf-8") as f:
        for line in f:
            digest, rel = line.rstrip("\n").split("  ", 1)
            h = hashlib.sha256()
            with open(os.path.join(pkg, *rel[2:].split("/")), "rb") as g:
                for chunk in iter(lambda: g.read(1 << 20), b""):
                    h.update(chunk)
            if h.hexdigest() != digest:
                bad.append(rel)
    if bad:
        fail(f"{pkg}: checksum mismatch: {', '.join(bad)}")
    print("checksums OK")


def check_fat32(pkg):
    """FAT32 rules: files < 4 GiB, no characters FAT forbids, no two names
    that differ only in case, paths within 255 characters."""
    root = os.path.abspath(pkg)
    bad, seen, total = [], {}, 0
    for d, dirs, files in os.walk(root):
        for n in dirs + files:
            p = os.path.join(d, n)
            rel = os.path.relpath(p, os.path.dirname(root))
            if any(c in n for c in '<>:"\\|?*') or n.endswith((" ", ".")):
                bad.append(f"name not allowed on FAT: {rel}")
            key = p.lower()
            if key in seen:
                bad.append(f"names differ only in case: {rel} / {seen[key]}")
            seen[key] = rel
            if len(rel) > 255:
                bad.append(f"path longer than 255: {rel}")
            if os.path.isfile(p):
                size = os.path.getsize(p)
                total += size
                if size >= 4 << 30:
                    bad.append(f"file over 4 GiB: {rel}")
    print(f"package: {len(seen)} entries, {total / 1048576:.1f} MiB")
    for b in bad:
        print("FAT32 problem:", b)
    if bad:
        sys.exit(1)


def check_folder(pkg):
    for rel in ["VVVVVV.elf", "lang", "fonts", "README-PS2.txt", "SOURCE.txt"]:
        if not os.path.exists(os.path.join(pkg, rel)):
            fail(f"{pkg}: no {rel} here; give a package folder (the one with VVVVVV.elf)")


def main(argv):
    if len(argv) >= 2 and argv[0] == "--check":
        check_data_zip(argv[1])
        return
    if len(argv) >= 2 and argv[0] == "--manifest-only":
        for pkg in argv[1:]:
            check_folder(pkg)
            write_manifest(pkg)
            check_manifest(pkg)
        return
    if len(argv) < 2 or argv[0].startswith("-"):
        sys.exit(__doc__)
    data_zip, folders = argv[0], argv[1:]
    for pkg in folders:
        check_folder(pkg)
    check_data_zip(data_zip)
    first = None
    for pkg in folders:
        out = os.path.join(pkg, "data-ps2.zip")
        if first is None:
            repack(data_zip, out)
            check_ps2_zip(out)
            first = out
        else:
            shutil.copyfile(first, out)
            print(f"copied {first} -> {out}")
        write_expect(pkg)
        write_manifest(pkg)
        check_fat32(pkg)
        check_manifest(pkg)
        print(f"ready: {pkg} (copy this folder to the root of a FAT32 USB stick)")


if __name__ == "__main__":
    main(sys.argv[1:])
