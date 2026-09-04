#!/bin/bash
# Builds the Arch package in a clean archlinux container. Run from the repository root:
#   bash package/archlinux/build-in-docker.sh
# The finished .pkg.tar.zst lands in package/archlinux/out/.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$here/out"

# The repository this script lives in is mounted read-only, and makepkg clones from it
# rather than from GitHub: a fresh container would otherwise download the whole of
# FreeCAD's history, some 2 GB, on every attempt.
repo="$(cd "$here/../.." && pwd)"

docker run --rm \
  --memory=24g --memory-swap=24g \
  -v "$here:/pkg:ro" \
  -v "$here/out:/out" \
  -v "$repo:/src:ro" \
  archlinux:latest bash -euo pipefail -c '
    pacman -Syu --noconfirm --needed base-devel git sudo >/dev/null
    # makepkg will not run as root
    useradd -m builder
    echo "builder ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/builder
    cp -r /pkg /home/builder/pkg
    # Point the PKGBUILD at the mounted repository instead of the fork on GitHub
    sed -i "s|git+https://github.com/QuakeString/FreeCAD.git#branch=feature/auto-theme|git+file:///src#branch=feature/auto-theme|" /home/builder/pkg/PKGBUILD
    git config --global --add safe.directory /src
    chown -R builder:builder /home/builder/pkg
    cd /home/builder/pkg
    # A full FreeCAD compile with one job per core takes over a gigabyte per job, and on a
    # machine with many cores that is more than the memory it has: 28 jobs on 32 GB ran the
    # host out of memory and took the user session down with it. Hold ninja to a job count
    # the memory can carry - about 2 GB per job, leaving headroom for the rest of the machine.
    mem_gb=$(awk "/MemAvailable/ {print int(\$2/1048576)}" /proc/meminfo)
    jobs=$(( mem_gb / 2 - 2 ))
    [ "$jobs" -lt 2 ] && jobs=2
    cores=$(nproc); [ "$jobs" -gt "$cores" ] && jobs=$cores
    echo "==> Compiling with $jobs jobs (${mem_gb} GB available, $cores cores)"
    export MAKEFLAGS="-j$jobs"
    export NINJAFLAGS="-j$jobs"
    export CMAKE_BUILD_PARALLEL_LEVEL="$jobs"
    # -s installs the depends/makedepends, --noconfirm answers the prompts
    sudo -u builder --preserve-env=MAKEFLAGS,NINJAFLAGS,CMAKE_BUILD_PARALLEL_LEVEL makepkg -s --noconfirm --nocheck 2>&1 | tee /out/makepkg.log
    cp -v /home/builder/pkg/*.pkg.tar.zst /out/
  '
