#!/usr/bin/env python3
"""Download a digest-pinned OCI image from ghcr.io and unpack it to a directory.

Rootless fallback for hosts without Docker: the resulting rootfs is the same
image the Docker build uses, so toolchain versions are identical.

Usage: fetch-ps2dev-rootfs.py <image-index-digest> <dest-dir>
"""
import hashlib
import io
import json
import os
import shutil
import sys
import tarfile
import urllib.request

REGISTRY = "https://ghcr.io"
REPO = "ps2dev/ps2dev"
ACCEPT = ", ".join([
    "application/vnd.oci.image.index.v1+json",
    "application/vnd.docker.distribution.manifest.list.v2+json",
    "application/vnd.oci.image.manifest.v1+json",
    "application/vnd.docker.distribution.manifest.v2+json",
])


def token():
    url = f"{REGISTRY}/token?scope=repository:{REPO}:pull"
    with urllib.request.urlopen(url) as r:
        return json.load(r)["token"]


def get(path, tok, accept=ACCEPT):
    req = urllib.request.Request(f"{REGISTRY}/v2/{REPO}/{path}",
                                 headers={"Authorization": f"Bearer {tok}", "Accept": accept})
    return urllib.request.urlopen(req)


def get_verified(path, tok, digest, accept=ACCEPT):
    with get(path, tok, accept) as r:
        data = r.read()
    algo, want = digest.split(":", 1)
    got = hashlib.new(algo, data).hexdigest()
    if got != want:
        sys.exit(f"digest mismatch for {path}: {got} != {want}")
    return data


def safe_extract(tf, dest):
    """Extract one layer, applying OCI whiteouts and refusing path escapes."""
    dest_real = os.path.realpath(dest)
    for m in tf.getmembers():
        name = os.path.normpath(m.name).lstrip("/")
        if name.startswith(".."):
            continue
        base = os.path.basename(name)
        parent = os.path.dirname(name)
        if base == ".wh..wh..opq":
            d = os.path.join(dest, parent)
            if os.path.isdir(d):
                for e in os.listdir(d):
                    p = os.path.join(d, e)
                    shutil.rmtree(p) if os.path.isdir(p) and not os.path.islink(p) else os.unlink(p)
            continue
        if base.startswith(".wh."):
            p = os.path.join(dest, parent, base[4:])
            if os.path.isdir(p) and not os.path.islink(p):
                shutil.rmtree(p)
            elif os.path.lexists(p):
                os.unlink(p)
            continue
        target = os.path.realpath(os.path.join(dest, os.path.dirname(name)))
        if not target.startswith(dest_real):
            continue
        out = os.path.join(dest, name)
        if os.path.lexists(out) and not os.path.isdir(out):
            os.unlink(out)
        m.name = name
        if m.isdev():
            continue
        tf.extract(m, dest, set_attrs=False, filter="fully_trusted")
        if m.isfile() or m.isdir():
            os.chmod(out, (m.mode & 0o7777) | 0o200)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    digest, dest = sys.argv[1], sys.argv[2]
    tok = token()
    index = json.loads(get_verified(f"manifests/{digest}", tok, digest))
    if "manifests" in index:
        m = next(x for x in index["manifests"]
                 if x.get("platform", {}).get("architecture") == "amd64"
                 and x.get("platform", {}).get("os") == "linux")
        manifest_digest = m["digest"]
        manifest = json.loads(get_verified(f"manifests/{manifest_digest}", tok, manifest_digest))
    else:
        manifest = index
    config = json.loads(get_verified(f"blobs/{manifest['config']['digest']}", tok,
                                     manifest["config"]["digest"]))
    os.makedirs(dest, exist_ok=True)
    for layer in manifest["layers"]:
        print(f"layer {layer['digest']} ({layer['size'] / 1e6:.1f} MB)", flush=True)
        blob = get_verified(f"blobs/{layer['digest']}", tok, layer["digest"])
        with tarfile.open(fileobj=io.BytesIO(blob), mode="r:*") as tf:
            safe_extract(tf, dest)
    with open(os.path.join(dest, ".image-config.json"), "w") as f:
        json.dump({"index_digest": digest, "config": config.get("config", {})}, f, indent=2)
    print("done:", dest)


if __name__ == "__main__":
    main()
