#!/bin/bash
# Builds the Arch package in a clean archlinux container. Run from the repository root:
#   bash package/archlinux/build-in-docker.sh
# The finished .pkg.tar.zst lands in package/archlinux/out/.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$here/out"

docker run --rm \
  -v "$here:/pkg:ro" \
  -v "$here/out:/out" \
  archlinux:latest bash -euo pipefail -c '
    pacman -Syu --noconfirm --needed base-devel git sudo >/dev/null
    # makepkg will not run as root
    useradd -m builder
    echo "builder ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/builder
    cp -r /pkg /home/builder/pkg
    chown -R builder:builder /home/builder/pkg
    cd /home/builder/pkg
    # -s installs the depends/makedepends, --noconfirm answers the prompts
    sudo -u builder makepkg -s --noconfirm --nocheck 2>&1 | tee /out/makepkg.log
    cp -v /home/builder/pkg/*.pkg.tar.zst /out/
  '
