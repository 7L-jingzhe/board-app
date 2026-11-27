#!/usr/bin/env bash
# Build a small aarch64 sysroot with glibc 2.27 (Ubuntu 18.04 packages).
#
# The board is Buildroot 2018.02 with glibc 2.29. WSL's cross gcc produces
# binaries for glibc 2.38, which the board cannot load. Linking against this
# sysroot produces a normal dynamic binary that runs on the board.
#
# Usage:
#   bash board_app/tools/setup_bionic_sysroot.sh [destination]
#
# Default destination: $HOME/aarch64-bionic (CMake auto-detects it).
# Requires: curl, dpkg-deb, sed. Does not need root.
set -euo pipefail

DEST="${1:-$HOME/aarch64-bionic}"
DEBS="${TMPDIR:-/tmp}/board-app-bionic-debs"
BASE="http://archive.ubuntu.com/ubuntu/pool/main/c/cross-toolchain-base"
PKGS=(
  "libc6-dev-arm64-cross_2.27-3ubuntu1cross1.1_all.deb"
  "libc6-arm64-cross_2.27-3ubuntu1cross1.1_all.deb"
  "linux-libc-dev-arm64-cross_4.15.0-35.38cross1.1_all.deb"
)

mkdir -p "$DEBS" "$DEST"
for pkg in "${PKGS[@]}"; do
  if [ ! -f "$DEBS/$pkg" ]; then
    echo "download $pkg"
    curl -fsSL -o "$DEBS/$pkg" "$BASE/$pkg"
  fi
  dpkg-deb -x "$DEBS/$pkg" "$DEST"
done

# The cross packages use /usr/aarch64-linux-gnu; make a flat sysroot layout
# that gcc's --sysroot/-B/-L options can consume.
mkdir -p "$DEST/usr"
ln -sfn "$DEST/usr/aarch64-linux-gnu/include" "$DEST/usr/include"

# Linker scripts contain absolute /usr/aarch64-linux-gnu paths. Prefix them
# with the sysroot location so the host linker can resolve them.
grep -l "GNU ld script" "$DEST"/usr/aarch64-linux-gnu/lib/*.so 2>/dev/null |
  xargs -r sed -i "s#/usr/aarch64-linux-gnu/lib/#$DEST/usr/aarch64-linux-gnu/lib/#g"

echo "sysroot ready: $DEST"
echo "rebuild with: cmake -S . -B build_board_app && cmake --build build_board_app -j8"
