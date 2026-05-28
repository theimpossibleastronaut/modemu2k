#!/bin/sh
# Reformat hand-written C sources with clang-format using the
# project's .clang-format config. Intended to be run once before
# a release rather than as part of normal development.
#
# Skipped:
#   src/cmdlex.c — flex-generated; meson regenerates it when flex is
#                  found (see src/meson.build).
#   modemu2k.h   — hand-aligned public header with doxygen blocks,
#                  enum-with-trailing-comments, extern "C", and
#                  visibility-macro #ifdefs. Wrap problem sections
#                  in `// clang-format off` ... `// clang-format on`
#                  if you ever want to include it.
#
# Run from the project root.

set -eu

if ! command -v clang-format >/dev/null 2>&1; then
  echo "reformat.sh: 'clang-format' not installed." >&2
  echo "  Arch/Manjaro:    sudo pacman -S clang" >&2
  echo "  Debian/Ubuntu:   sudo apt install clang-format" >&2
  echo "  Fedora/openSUSE: sudo dnf install clang-tools-extra  (or zypper)" >&2
  echo "  Slackware:       slackpkg install llvm" >&2
  exit 1
fi

cd "$(dirname "$0")/.."

for f in src/*.c src/*.h; do
  [ "$f" = src/cmdlex.c ] && continue
  clang-format -i "$f"
done

echo "Reformatted. Review with 'git diff' before committing."
