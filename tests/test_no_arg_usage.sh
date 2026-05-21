#!/bin/sh
# Verify the no-argument startup hint.
#
# Usage: test_no_arg_usage.sh <modemu2k-binary>
set -e

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <modemu2k-binary>" >&2
  exit 2
fi

bin="$1"
out=$("$bin")
rc=$?

if [ "$rc" -ne 0 ]; then
  echo "FAIL: expected exit 0, got $rc" >&2
  exit 1
fi

case "$out" in
  *"Run 'modemu2k --help' for full options."*) ;;
  *)
    echo "FAIL: expected --help hint in stdout, got:" >&2
    printf '%s\n' "$out" >&2
    exit 1
    ;;
esac
