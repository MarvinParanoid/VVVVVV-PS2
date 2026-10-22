#!/usr/bin/env python3
"""Compare two 32-bit BMPs written by SDL_SaveBMP (e.g. rt_soft_game.bmp from
the host reference run and from an emulator/hardware run).

Usage: compare-frames.py <reference.bmp> <candidate.bmp>
Exit status 0 if the RGB channels match exactly, 1 otherwise.
"""
import struct
import sys


def read_bmp32(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        sys.exit(f"{path}: not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 32:
        sys.exit(f"{path}: expected 32 bpp, got {bpp}")
    top_down = height < 0
    height = abs(height)
    rows = []
    for y in range(height):
        start = offset + y * width * 4
        rows.append(data[start:start + width * 4])
    if not top_down:
        rows.reverse()
    return width, height, rows


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    w1, h1, a = read_bmp32(sys.argv[1])
    w2, h2, b = read_bmp32(sys.argv[2])
    if (w1, h1) != (w2, h2):
        print(f"size mismatch: {w1}x{h1} vs {w2}x{h2}")
        return 1
    diff = 0
    max_delta = 0
    first = None
    for y in range(h1):
        ra, rb = a[y], b[y]
        if ra == rb:
            continue
        for x in range(w1):
            pa = ra[x * 4:x * 4 + 3]  # B, G, R (alpha ignored)
            pb = rb[x * 4:x * 4 + 3]
            if pa != pb:
                diff += 1
                max_delta = max(max_delta, max(abs(i - j) for i, j in zip(pa, pb)))
                if first is None:
                    first = (x, y, tuple(pa[::-1]), tuple(pb[::-1]))
    total = w1 * h1
    print(f"{w1}x{h1}: {diff} of {total} pixels differ (RGB), max channel delta {max_delta}")
    if first:
        print(f"first difference at {first[0]},{first[1]}: ref rgb={first[2]} cand rgb={first[3]}")
    return 0 if diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
