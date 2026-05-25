#!/bin/sh
# Verify bare invocation prints the --help output and exits 0.
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

# Sanity-check that --help-style output landed on stdout. Look for the
# "Usage:" banner and one option line that's stable across releases.
case "$out" in
  *"Usage:"*"--commprog"*) ;;
  *)
    echo "FAIL: expected --help output in stdout, got:" >&2
    printf '%s\n' "$out" >&2
    exit 1
    ;;
esac
