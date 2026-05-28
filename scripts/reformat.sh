#!/bin/sh
# Reformat hand-written C sources with GNU indent. Intended to be run
# once before a release rather than as part of normal development.
#
# Skips src/cmdlex.c — it's flex-generated and would be rewritten the
# next time meson regenerates it (see src/meson.build).
#
# Run from the project root.

set -eu

if ! command -v indent >/dev/null 2>&1; then
  echo "reformat.sh: 'indent' not installed." >&2
  echo "  Arch/Manjaro:   sudo pacman -S indent" >&2
  echo "  Debian/Ubuntu:  sudo apt install indent" >&2
  echo "  Fedora/openSUSE:sudo dnf install indent  (or zypper)" >&2
  echo "  Slackware:      slackpkg install indent" >&2
  exit 1
fi

cd "$(dirname "$0")/.."

INDENT_OPTS="-ci2 -bl -bli0 -nut -npcs"

for f in src/*.c src/*.h modemu2k.h; do
  [ "$f" = src/cmdlex.c ] && continue
  indent $INDENT_OPTS "$f"
done

echo "Reformatted. Review with 'git diff' before committing."
