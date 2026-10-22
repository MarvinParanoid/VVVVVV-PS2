# PS2 build environment for the VVVVVV PS2 port.
# Base: official ps2dev image (toolchain GCC 15.2 + ps2sdk + ps2sdk-ports incl. SDL 2.32.10),
# pinned by index digest. Update deliberately; record the new digest in docs.
FROM ghcr.io/ps2dev/ps2dev@sha256:5476fdfbf34d35eb644fde0a6f6533fb554a4a8171ce53dad7cd1df6077dfb05

# The upstream image ships only the toolchain runtime; add the build tools
# (same set ps2sdk-ports uses in its own Dockerfile).
RUN apk add --no-cache build-base cmake make git pkgconf python3

WORKDIR /src
