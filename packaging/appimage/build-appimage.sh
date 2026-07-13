#!/bin/sh

# Build a truly-portable AppImage of modemu2k using sharun + uruntime +
# DwarFS (pkgforge-dev method). It bundles the libc and dynamic linker, so
# the result runs on any Linux distro (musl, very old glibc, ...).
#
# Meant to run on an Arch base (see ../../.github/workflows/appimage.yml).
# Build deps are installed by the workflow via pacman; this script builds
# modemu2k from source, installs it into the system /usr, then bundles the
# installed binary with quick-sharun.

set -eux

ARCH="$(uname -m)"

# VERSION is exported by CI (tag name or "snapshot"); fall back for local runs.
VERSION="${VERSION:-snapshot}"

# quick-sharun is fetched from pkgforge-dev rather than vendored, so we
# always track the upstream bundling logic.
SHARUN="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/refs/heads/main/useful-tools/quick-sharun.sh"

# Source root is two levels up from this script (packaging/appimage/).
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
test -f "$SOURCE_ROOT/src/main.c"

WORKSPACE="${WORKSPACE:-$SOURCE_ROOT}"
BUILD_DIR="$SOURCE_ROOT/_build_appdir"
APPDIR="${APPDIR:-/tmp/modemu2k-AppDir}"
OUTPATH="$WORKSPACE/out"
WORKDIR="$SOURCE_ROOT/packaging/appimage"

rm -rf "$APPDIR" "$BUILD_DIR"
mkdir -p "$APPDIR" "$OUTPATH"

# --- build modemu2k and install into the system /usr ------------------------
meson setup "$BUILD_DIR" \
  -Dbuildtype=release \
  -Dstrip=true \
  -Db_sanitize=none \
  -Dprefix=/usr \
  --libdir=lib

ninja -C "$BUILD_DIR"
meson install -C "$BUILD_DIR"

# --- bundle with sharun and pack the AppImage --------------------------------
export APPDIR
export ICON="$SOURCE_ROOT/packaging/modemu2k.png"
export DESKTOP="$SOURCE_ROOT/packaging/appimage/modemu2k.desktop"
export OUTPATH
export VERSION

# Derive owner/repo from the GitHub Actions GITHUB_REPOSITORY env
# ("owner/repo"); fall back to the upstream defaults for local runs.
GH_OWNER=${GITHUB_REPOSITORY-}; GH_OWNER=${GH_OWNER%%/*}
GH_REPO=${GITHUB_REPOSITORY-}; GH_REPO=${GH_REPO##*/}
: "${GH_OWNER:=theimpossibleastronaut}"
: "${GH_REPO:=modemu2k}"
export OUTNAME="$GH_REPO-$VERSION-$ARCH.AppImage"

# Update info for gh-releases-zsync. Tagged builds track the "latest" release,
# snapshot builds track the rolling "snapshot" prerelease (matches appimage.yml).
if [ "$VERSION" = "snapshot" ]; then
  TAG="snapshot"
else
  TAG="latest"
fi
export UPINFO="gh-releases-zsync|$GH_OWNER|$GH_REPO|$TAG|*$ARCH.AppImage.zsync"

cd "$WORKDIR"

wget --retry-connrefused --tries=30 "$SHARUN" -O "$WORKDIR/quick-sharun"
chmod +x "$WORKDIR/quick-sharun"

# modemu2k's closure is just libmodemu2k + libc, so no debloated packages or
# GL deployment are needed. quick-sharun bundles the lib closure (including
# the libc and dynamic linker) and generates the sharun AppRun; then turn
# the AppDir into a DwarFS AppImage. The uruntime default (URUNTIME_EXTRACT=3)
# already falls back to extract-and-run on hosts without FUSE.
./quick-sharun /usr/bin/modemu2k
./quick-sharun --make-appimage

ls -lh "$OUTPATH"
