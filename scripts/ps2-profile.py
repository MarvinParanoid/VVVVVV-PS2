#!/usr/bin/env python3
"""Symbolise EE program-counter samples from the PS2 scene benchmark.

    ps2-profile.py <VVVVVV.elf> <prof_NN_scene.txt> [more...] [--top N]
    ps2-profile.py <VVVVVV.elf> --addr <hex> [<hex>...]   (e.g. EPC/RA from the exception screen)

prof_*.txt lines are "<hex pc> <count>" (src/PS2Bench.cpp samples the
interrupted PC at ~1 kHz from an EE timer alarm, all threads). Samples outside
the ELF's text are reported as "kernel/idle" (EE kernel, idle thread, e.g.
waiting for vsync or a semaphore). Functions are grouped into coarse buckets
by name so scenes can be compared. Needs only Python and c++filt.
"""
import bisect
import collections
import struct
import subprocess
import sys


def read_symbols(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        sys.exit(f"{path}: not a little-endian ELF32")
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    syms = []
    text_lo, text_hi = None, None
    shstr = sections[shstrndx]
    for sh in sections:
        name_off = shstr[4] + sh[0]
        name = data[name_off:data.index(b"\0", name_off)].decode()
        if name == ".text":
            text_lo, text_hi = sh[3], sh[3] + sh[5]
        if sh[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]
        for off in range(sh[4], sh[4] + sh[5], 16):
            st_name, st_value, st_size, st_info, _, st_shndx = struct.unpack_from("<IIIBBH", data, off)
            if st_info & 0xF != 2 or st_value == 0:  # STT_FUNC
                continue
            s = strtab[4] + st_name
            syms.append((st_value, st_size, data[s:data.index(b"\0", s)].decode(errors="replace")))
    syms.sort()
    names = [s[2] for s in syms]
    demangled = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True).stdout.split("\n")
    if len(demangled) >= len(names):
        syms = [(a, sz, demangled[i]) for i, (a, sz, _) in enumerate(syms)]
    return syms, (text_lo, text_hi)


def bucket(fn):
    f = fn.lower()
    if fn == "kernel/idle":
        return "kernel/idle (vsync/sema waits, kernel)"
    if "stb_vorbis" in f or f.startswith(("vorbis_", "decode_", "imdct", "inverse_mdct", "codebook", "residue", "draw_line", "do_floor", "get_window", "predict_point", "start_page", "getn", "get8", "get32", "get_bits", "prep_huffman", "vorbis")):
        return "music decode (stb_vorbis)"
    if f.startswith(("faudio", "faudio_")) or "faudio" in f:
        return "FAudio mixing"
    if f.startswith(("sdl_blit", "blit", "sdl_lowerblit", "sdl_upperblit", "sdl_softstretch", "sdl_fillrect", "sdl_blendfillrect", "sdl_maprgb")) or "blit" in f:
        return "SDL blit/fill (pixel work)"
    if f.startswith(("sw_", "sdl_render", "sdl_queue", "queue", "sdl_setrendertarget", "flushrendercommands", "prep", "settexture", "setdrawstate")) or "render" in f and f.startswith("sdl"):
        return "SDL renderer API/queue"
    if f.startswith(("gskit", "ps2_", "dmakit", "dma")):
        return "gsKit/GS upload"
    if f.startswith(("memcpy", "memset", "memmove", "sdl_memcpy", "sdl_memset")):
        return "memcpy/memset"
    if f.startswith(("font::", "print", "utf8")) or "font::" in f:
        return "VVVVVV text"
    if any(k in fn for k in ("Graphics::", "render", "Render", "draw", "tower")):
        return "VVVVVV draw code"
    if any(k in fn for k in ("entclass", "entityclass", "Game::", "mapclass", "scriptclass", "gamelogic", "Logic")):
        return "VVVVVV logic"
    if f.startswith(("malloc", "free", "_malloc", "_free", "realloc", "calloc", "sdl_malloc", "sdl_free")):
        return "allocator"
    if f.startswith(("__", "_")) and ("df" in f or "sf" in f):
        return "soft-float helpers"
    return "other"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    top = 25
    if "--top" in sys.argv:
        top = int(sys.argv[sys.argv.index("--top") + 1])
        args.remove(str(top))
    if len(args) < 2 and "--addr" not in sys.argv:
        sys.exit(__doc__)
    syms, (lo, hi) = read_symbols(args[0])
    addrs = [s[0] for s in syms]
    if "--addr" in sys.argv:
        for a in args[1:]:
            pc = int(a, 16)
            i = bisect.bisect_right(addrs, pc) - 1
            name = f"{syms[i][2]} + 0x{pc - syms[i][0]:x}" if i >= 0 and lo is not None and lo <= pc < hi else "(outside .text)"
            print(f"{pc:08x}  {name}")
        return
    for prof in args[1:]:
        per_fn = collections.Counter()
        total = 0
        for line in open(prof):
            pc_s, n_s = line.split()
            pc, n = int(pc_s, 16), int(n_s)
            total += n
            if lo is None or not (lo <= pc < hi):
                per_fn["kernel/idle"] += n
                continue
            i = bisect.bisect_right(addrs, pc) - 1
            name = syms[i][2] if i >= 0 else "?"
            per_fn[name] += n
        buckets = collections.Counter()
        for fn, n in per_fn.items():
            buckets[bucket(fn)] += n
        print(f"== {prof}: {total} samples")
        for b, n in buckets.most_common():
            print(f"  {100.0 * n / total:5.1f}%  {b}")
        print("  -- top functions --")
        for fn, n in per_fn.most_common(top):
            print(f"  {100.0 * n / total:5.1f}%  {fn[:110]}")


if __name__ == "__main__":
    main()
