#!/usr/bin/env python3
"""Check a tester package before anyone downloads it.

    check-package.py tree <dir>
    check-package.py zip <file.zip> [--build ID] [--commit SHA]

tree: content rules only, run while the package tree is being assembled
      (scripts/make-public-test-release.sh): no game data, no BIOS or memory
      card images, no secrets, local paths or build caches.
zip:  the finished archive: the content rules on every entry, safe entry
      paths, SHA256SUMS against the entries, the required files of each USB
      folder, a structural check of every PS2 ELF and the tools' syntax.
      --build ID: every USB folder must be build ID (no other build, e.g. a
      frozen release candidate, may be packaged under a CI build's name).
      --commit SHA: the folders' SOURCE.txt and BUILD-INFO.txt must name this
      repository commit, without "-dirty".

Exits non-zero with a list of every problem found. Standard library only.
"""
import hashlib
import os
import re
import struct
import sys
import zipfile

MAX_FILE = 8 << 20
# game data, captures, BIOS dumps, memory card and disc images
BAD_EXT = (".zip", ".vvv", ".wav", ".ogg", ".mp3", ".bin", ".rom", ".nvm", ".mec", ".mcd", ".ps2",
           ".img", ".iso", ".chd", ".p2s", ".ppm", ".bmp", ".pyc")
BAD_PARTS = {".git", "__pycache__", "CMakeFiles", "logs", ".claude", ".vscode", ".idea"}
BAD_NAMES = {"CMakeCache.txt", ".env", ".netrc", "id_rsa", "id_ed25519", "data.zip", "data-ps2.zip",
             "vvvvvv_ps2.log", "bench_results.txt", "fs_smoke.log"}
SECRET_RE = re.compile(rb"(gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|-----BEGIN [A-Z ]*PRIVATE KEY-----)")
LOCAL_PATH_RE = re.compile(rb"/home/[A-Za-z0-9._-]+/|/Users/[A-Za-z0-9._-]+/|[A-Z]:\\\\Users\\\\")
# files every USB folder of a package must contain
FOLDER_FILES = ["VVVVVV.elf", "VVVVVV-nomusic.elf", "fs_smoke.elf", "SOURCE.txt", "README-PS2.txt",
                "lang/en/meta.xml", "fonts/font.png", "optional/overlay.txt"]
TOP_FILES = ["README.txt", "BUILD-INFO.txt", "SHA256SUMS", "LICENSE-VVVVVV.md", "THIRD-PARTY-NOTICES.txt",
             "tools/prepare-ps2-usb.py", "tools/repack-data-zip.py"]


def content_problems(rel, data):
    """rel: path inside the package ("/"-separated); data: the file's bytes."""
    out = []
    parts = rel.split("/")
    name = parts[-1]
    low = name.lower()
    if low.endswith(BAD_EXT) or name in BAD_NAMES or BAD_PARTS.intersection(parts):
        out.append(f"{rel}: not allowed in a package (game data, image, cache or local file)")
    elif low.endswith(".png") and "fonts" not in parts[:-1]:
        out.append(f"{rel}: PNG outside fonts/ (may be game graphics)")
    elif low.startswith("scph") or "bios" in low:
        out.append(f"{rel}: looks like a BIOS file")
    if len(data) > MAX_FILE:
        out.append(f"{rel}: {len(data)} bytes, over 8 MiB (may be game data)")
    if SECRET_RE.search(data):
        out.append(f"{rel}: contains something that looks like a token or private key")
    m = LOCAL_PATH_RE.search(data)
    if m:
        out.append(f"{rel}: contains a local path ({m.group().decode(errors='replace')}...)")
    return out


def elf_problems(rel, data):
    """A loadable PS2 EE executable: ELF32, little-endian MIPS, EXEC, PT_LOAD inside the file."""
    if len(data) < 52 or data[:4] != b"\x7fELF":
        return [f"{rel}: not an ELF file"]
    if data[4] != 1 or data[5] != 1:
        return [f"{rel}: not a 32-bit little-endian ELF"]
    e_type, e_machine, _, e_entry, e_phoff = struct.unpack_from("<HHIII", data, 16)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 42)
    out = []
    if e_type != 2 or e_machine != 8:
        out.append(f"{rel}: type {e_type} machine {e_machine}, want an executable for MIPS (2, 8)")
    if e_entry == 0 or e_phnum == 0 or e_phoff + e_phnum * e_phentsize > len(data):
        out.append(f"{rel}: no entry point or program headers")
        return out
    loads = 0
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIIII", data, e_phoff + i * e_phentsize)
        if p_type == 1:
            loads += 1
            if p_offset + p_filesz > len(data):
                out.append(f"{rel}: segment {i} extends past the end of the file (truncated?)")
            if p_filesz and p_vaddr < 0x100000:
                out.append(f"{rel}: segment {i} loads at {p_vaddr:#x}, inside the EE kernel area")
    if not loads:
        out.append(f"{rel}: no loadable segment")
    return out


def check_tree(root):
    problems = []
    for d, dirs, files in os.walk(root):
        for n in dirs + files:
            p = os.path.join(d, n)
            if os.path.islink(p):
                problems.append(f"{os.path.relpath(p, root)}: symbolic link")
        for n in files:
            p = os.path.join(d, n)
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            with open(p, "rb") as f:
                problems += content_problems(rel, f.read())
    return problems


def source_lines(text, key):
    return [ln.split(" ", 1)[1].strip() for ln in text.splitlines() if ln.startswith(key + " ")]


def check_zip(path, build, commit):
    problems = []
    z = zipfile.ZipFile(path)
    infos = [i for i in z.infolist() if not i.is_dir()]
    tops = {i.filename.split("/", 1)[0] for i in infos}
    if len(tops) != 1:
        return [f"want one top-level folder, found {sorted(tops)}"]
    top = tops.pop()
    files = {}
    for i in infos:
        n = i.filename
        if n.startswith("/") or "\\" in n or ".." in n.split("/") or "/" not in n:
            problems.append(f"{n}: unsafe entry path")
            continue
        if (i.external_attr >> 16) & 0o170000 == 0o120000:
            problems.append(f"{n}: symbolic link")
        rel = n.split("/", 1)[1]
        files[rel] = z.read(i)
        problems += content_problems(rel, files[rel])

    for f in TOP_FILES:
        if f not in files:
            problems.append(f"missing {f}")
    folders = sorted({r.split("/")[1] for r in files if r.startswith("usb/") and r.count("/") >= 2})
    if not folders:
        problems.append("no folder under usb/")

    # SHA256SUMS covers every other file, and nothing else
    if "SHA256SUMS" in files:
        listed = {}
        for ln in files["SHA256SUMS"].decode().splitlines():
            h, _, p = ln.partition("  ")
            listed[p[2:] if p.startswith("./") else p] = h
        for rel, data in files.items():
            if rel == "SHA256SUMS":
                continue
            if rel not in listed:
                problems.append(f"{rel}: not in SHA256SUMS")
            elif listed[rel] != hashlib.sha256(data).hexdigest():
                problems.append(f"{rel}: SHA256SUMS mismatch")
        for rel in listed.keys() - files.keys():
            problems.append(f"SHA256SUMS lists {rel}, which is not in the archive")

    for rel, data in files.items():
        if rel.endswith(".elf"):
            problems += elf_problems(rel, data)
        elif rel.endswith(".py"):
            try:
                compile(data, rel, "exec")
            except SyntaxError as e:
                problems.append(f"{rel}: {e}")

    for folder in folders:
        base = f"usb/{folder}/"
        if f"{base}VVVVVV.elf" not in files:
            continue  # e.g. GSPOC: technical experiments, no game
        for f in FOLDER_FILES:
            if base + f not in files:
                problems.append(f"missing {base}{f}")
        src = files.get(base + "SOURCE.txt", b"").decode(errors="replace")
        if any(ln.startswith("WARNING") for ln in src.splitlines()):
            problems.append(f"{base}SOURCE.txt has a WARNING (stale build tree)")
        builds = source_lines(src, "build")
        if build and builds != [build]:
            problems.append(f"{base}SOURCE.txt: build {builds}, want [{build!r}]")
        if commit:
            # "git describe --always --dirty": a bare hash, or <tag>-<n>-g<hash>
            repos = source_lines(src, "repo")
            m = re.fullmatch(r"(?:.*-g)?([0-9a-f]{7,40})", repos[0]) if len(repos) == 1 else None
            if not m or not commit.startswith(m.group(1)):
                problems.append(f"{base}SOURCE.txt: repo {repos}, want commit {commit[:12]} without -dirty")
    info = files.get("BUILD-INFO.txt", b"").decode(errors="replace")
    if commit and commit not in info:
        problems.append(f"BUILD-INFO.txt does not name commit {commit}")
    if build and build not in info:
        problems.append(f"BUILD-INFO.txt does not name build {build}")

    if not problems:
        elfs = sum(r.endswith(".elf") for r in files)
        print(f"{os.path.basename(path)}: OK ({len(files)} files, {elfs} ELFs, folders: {', '.join(folders)})")
    return problems


def main(argv):
    if len(argv) >= 2 and argv[0] == "tree":
        problems = check_tree(argv[1])
        if not problems:
            print("content check: no game data, BIOS files, secrets, local paths or caches")
    elif len(argv) >= 2 and argv[0] == "zip":
        opts = dict(zip(argv[2::2], argv[3::2]))
        unknown = set(opts) - {"--build", "--commit"}
        if unknown or len(argv) % 2:
            sys.exit(__doc__)
        problems = check_zip(argv[1], opts.get("--build"), opts.get("--commit"))
    else:
        sys.exit(__doc__)
    if problems:
        sys.exit("package check FAILED:\n  " + "\n  ".join(problems))


if __name__ == "__main__":
    main(sys.argv[1:])
