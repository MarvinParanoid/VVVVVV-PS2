#!/usr/bin/env python3
"""Lossless repack of a VVVVVV data.zip for the PS2 build: the music blob(s)
are STORED (no deflate) so they can be streamed and seeked on the PS2.

    repack-data-zip.py <data.zip> [<out.zip>]     (default out: data-ps2.zip next to the input)

Why: the music blob is ~59.7 MB deflated vs 61.7 MB stored (3% saving), but a
deflated entry can only be "seeked" by inflating from its start. On the EE
(PCSX2) reaching a track 57 MB into the blob took 7.6 s; stored, ~0.07 ms.

Guarantees:
- The input archive is never modified (it is only opened for reading, and an
  output path that resolves to the input is refused).
- Every entry's bytes are copied unchanged; only the music blob(s) change
  compression method. SHA-256 of each music blob is computed from the input
  and from the written output and must match, otherwise the output is deleted.
- The output is written to a temporary file and renamed only after the checks.
"""
import hashlib
import os
import shutil
import sys
import zipfile

STORED_ENTRIES = ("vvvvvvmusic.vvv", "mmmmmm.vvv")


def sha256_of_entry(zf, name):
    h = hashlib.sha256()
    with zf.open(name) as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) == 3 else os.path.join(os.path.dirname(os.path.abspath(src)), "data-ps2.zip")
    if os.path.exists(dst) and os.path.samefile(src, dst):
        sys.exit("refusing to overwrite the input archive; choose another output path")
    tmp = dst + ".part"

    with zipfile.ZipFile(src) as zi:
        names = {i.filename for i in zi.infolist()}
        music = [n for n in STORED_ENTRIES if n in names]
        if not music:
            sys.exit(f"{src}: no vvvvvvmusic.vvv / mmmmmm.vvv entry found")
        before = {n: sha256_of_entry(zi, n) for n in music}
        with zipfile.ZipFile(tmp, "w") as zo:
            for info in zi.infolist():
                ni = zipfile.ZipInfo(info.filename, info.date_time)
                ni.external_attr = info.external_attr
                ni.compress_type = zipfile.ZIP_STORED if info.filename in music else info.compress_type
                with zi.open(info) as fi, zo.open(ni, "w", force_zip64=info.file_size > 0x7FFFFFFF) as fo:
                    shutil.copyfileobj(fi, fo, 1 << 20)

    with zipfile.ZipFile(tmp) as zc:
        bad = zc.testzip()
        ok = bad is None
        for n in music:
            info = zc.getinfo(n)
            after = sha256_of_entry(zc, n)
            stored = info.compress_type == zipfile.ZIP_STORED
            print(f"{n}: {info.file_size} bytes, {'stored' if stored else 'COMPRESSED'}")
            print(f"  sha256 input  {before[n]}")
            print(f"  sha256 output {after}")
            ok = ok and stored and after == before[n]
    if not ok:
        os.remove(tmp)
        sys.exit(f"verification failed (CRC error in {bad})" if bad else "verification failed: blob changed or not stored")
    os.replace(tmp, dst)
    print(f"written {dst} (input unchanged: {src})")


if __name__ == "__main__":
    main()
