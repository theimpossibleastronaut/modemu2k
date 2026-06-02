#!/bin/bash

# Set bash options:
# -e: Exit immediately if a command exits with a non-zero status.
# -v: Print each command to stderr before executing it.
set -ev

# Set default workspace if not provided
WORKSPACE=${WORKSPACE:-$(pwd)}
echo $WORKSPACE
# Check if the workspace path is absolute
if [[ "$WORKSPACE" != /* ]]; then
  echo "The workspace path must be absolute"
  exit 1
fi
test -d "$WORKSPACE"

# Set default source root if not provided
SOURCE_ROOT=${SOURCE_ROOT:-$WORKSPACE}
# Check if the source root path is absolute
if [[ "$SOURCE_ROOT" != /* ]]; then
  echo "The source root path must be absolute"
  exit 1
fi
# Verify that you're in the source root
echo $SOURCE_ROOT
test -f "$SOURCE_ROOT/src/main.c"

# Define and create application directory if it doesn't exist
# This is the directory where your project will be installed to
# and an AppImage created
APPDIR=${APPDIR:-"/tmp/$USER-AppDir"}
if [ -d "$APPDIR" ]; then
  rm -rf "$APPDIR"
else
  mkdir -v -p "$APPDIR"
fi

# Install necessary dependencies
sudo apt-get update
sudo apt-get install --no-install-recommends -y \
          meson \
          ninja-build

# Set up build directory
cd "$SOURCE_ROOT"
BUILD_DIR="$SOURCE_ROOT/_build_prep_appdir"
# Clean build directory if specified and it exists
if [ "$CLEAN_BUILD" = "true" ] && [ -d "$BUILD_DIR" ]; then
  rm -rf "$BUILD_DIR"
fi

# Setup project for building
if [ ! -d "$BUILD_DIR" ]; then
  meson setup "$BUILD_DIR" \
    -Dbuildtype=release \
    -Dstrip=true \
    -Db_sanitize=none \
    -Dhelper-scripts=true \
    -Dprefix=/usr \
    --libdir=lib
fi

# Build project
cd "$BUILD_DIR"
ninja
meson install --destdir=$APPDIR

# Set up output directory
OUT_DIR="$WORKSPACE/out"
if [ ! -d "$OUT_DIR" ]; then
  mkdir "$OUT_DIR"
fi
cd "$OUT_DIR"

# Set LinuxDeploy output version
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
# Help linuxdeploy resolve the project's own libmodemu2k.so.0 NEEDED
# entry against the just-installed copy in the AppDir (not a system lib).
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Generate AppImage using linuxdeploy
linuxdeploy \
  --appdir="$APPDIR" \
  --custom-apprun=$SOURCE_ROOT/packaging/appimage/AppRun \
  -d $SOURCE_ROOT/packaging/appimage/modemu2k.desktop \
  --icon-file=$SOURCE_ROOT/packaging/modemu2k.png \
  --icon-filename=modemu2k \
  --executable=$APPDIR/usr/bin/modemu2k

if [ "$VERSION" = "snapshot" ]; then
  TAG="snapshot"
else
  TAG="latest"
fi

ARCH=$(uname -m)
# Derive owner/repo from the GitHub Actions GITHUB_REPOSITORY env
# ("owner/repo"); fall back to the upstream defaults for local runs.
GH_OWNER=${GITHUB_REPOSITORY%%/*}
GH_REPO=${GITHUB_REPOSITORY##*/}
: "${GH_OWNER:=theimpossibleastronaut}"
: "${GH_REPO:=modemu2k}"
OUT_APPIMAGE="$GH_REPO-$VERSION-$ARCH.AppImage"
UPINFO="gh-releases-zsync|$GH_OWNER|$GH_REPO|$TAG|*$ARCH.AppImage.zsync"

appimagetool --comp zstd \
  --mksquashfs-opt \
  -Xcompression-level \
  --mksquashfs-opt 20 \
  -u "$UPINFO" \
  "$APPDIR" "$OUT_APPIMAGE"
