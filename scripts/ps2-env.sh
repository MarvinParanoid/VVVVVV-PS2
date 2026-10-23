#!/usr/bin/env bash
# Run a command inside the pinned PS2DEV build environment, with the repo at /src.
#
#   scripts/ps2-env.sh <command> [args...]
#
# Uses Docker when it is installed and usable (image built from docker/Dockerfile).
# Otherwise falls back to the *same* image unpacked rootless and run with bubblewrap
# (scripts/fetch-ps2dev-rootfs.py), so toolchain bits are identical either way.
# Force a backend with PS2ENV_BACKEND=docker|bwrap.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIGEST="sha256:5476fdfbf34d35eb644fde0a6f6533fb554a4a8171ce53dad7cd1df6077dfb05"
SHORT="${DIGEST#sha256:}"; SHORT="${SHORT:0:8}"
IMAGE_TAG="vvvvvv-ps2-dev:${SHORT}"
ROOTFS="${XDG_CACHE_HOME:-$HOME/.cache}/vvvvvv-ps2/rootfs-${SHORT}"
APK_PKGS="build-base cmake make git pkgconf python3"
PS2_PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/ps2dev/bin:/usr/local/ps2dev/ee/bin:/usr/local/ps2dev/iop/bin:/usr/local/ps2dev/dvp/bin:/usr/local/ps2dev/ps2sdk/bin"

backend="${PS2ENV_BACKEND:-}"
if [[ -z "$backend" ]]; then
    if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
        backend=docker
    else
        backend=bwrap
    fi
fi

case "$backend" in
docker)
    if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
        docker build -t "$IMAGE_TAG" "$REPO/docker" >&2
    fi
    # HOME=/tmp as in the bwrap backend: the host user does not exist in the image
    exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$REPO:/src" -w /src \
        ${DOCKER_TTY:+-it} "$IMAGE_TAG" "$@"
    ;;
bwrap)
    if [[ ! -f "$ROOTFS/.image-config.json" ]]; then
        echo "[ps2-env] unpacking $DIGEST to $ROOTFS" >&2
        python3 -I "$REPO/scripts/fetch-ps2dev-rootfs.py" "$DIGEST" "$ROOTFS" >&2
    fi
    if [[ ! -f "$ROOTFS/.apk-done" ]]; then
        echo "[ps2-env] installing build tools into rootfs: $APK_PKGS" >&2
        bwrap --bind "$ROOTFS" / --dev /dev --proc /proc --tmpfs /tmp \
            --ro-bind /etc/resolv.conf /etc/resolv.conf \
            --unshare-user --uid 0 --gid 0 --share-net --setenv PATH "$PS2_PATH" \
            /bin/sh -c "apk add --no-cache $APK_PKGS && apk info -v > /.apk-versions.txt" >&2
        touch "$ROOTFS/.apk-done"
    fi
    mkdir -p "$ROOTFS/src"
    exec bwrap --ro-bind "$ROOTFS" / --dev /dev --proc /proc --tmpfs /tmp \
        --bind "$REPO" /src --chdir /src \
        --unshare-all --die-with-parent \
        --clearenv --setenv PATH "$PS2_PATH" --setenv HOME /tmp \
        --setenv PS2DEV /usr/local/ps2dev --setenv PS2SDK /usr/local/ps2dev/ps2sdk \
        --setenv GSKIT /usr/local/ps2dev/gsKit \
        "$@"
    ;;
*)
    echo "unknown PS2ENV_BACKEND=$backend" >&2
    exit 2
    ;;
esac
