#!/bin/sh
# Integration tests for modemu2k CLI option handling:
#   - mutually-exclusive mode flags (-c/-d/-l/-s) reject conflicts
#     with a non-zero exit and a message naming both flags
#   - info flags (-h, -v, -w) exit 0 with the expected text on stdout
#
# Usage: test_options.sh <modemu2k-binary>
set -e

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <modemu2k-binary>" >&2
  exit 2
fi

bin="$1"
fails=0

fail() {
  echo "FAIL: $1" >&2
  fails=$((fails + 1))
}

# --- Conflict matrix -------------------------------------------------------
# Each row: "<flag-with-arg-or-bare> <flag-with-arg-or-bare>". We pass two
# mode-setting options together and expect modemu2k to exit with status 2
# and a message mentioning the word "exclusive".
#
# -c, -d, -l take an argument; -s is bare. Combinations cover every pair
# of distinct mode flags.

check_conflict() {
  desc="$1"
  shift
  out=$("$bin" "$@" 2>&1) && rc=0 || rc=$?
  if [ "$rc" -eq 0 ]; then
    fail "$desc: expected non-zero exit, got 0 (output: $out)"
    return
  fi
  case "$out" in
    *exclusive*) ;;
    *) fail "$desc: expected 'exclusive' in stderr, got: $out" ;;
  esac
}

check_conflict "-c + -d"  -c "true %s" -d /dev/null
check_conflict "-c + -l"  -c "true %s" -l 19990
check_conflict "-c + -s"  -c "true %s" -s
check_conflict "-d + -l"  -d /dev/null -l 19990
check_conflict "-d + -s"  -d /dev/null -s
check_conflict "-l + -s"  -l 19990 -s

# Repeating the same mode flag is *not* a conflict (last value wins, as
# with most getopt-style tools).
out=$("$bin" -l 19990 -l 19991 -h 2>&1) && rc=0 || rc=$?
if [ "$rc" -ne 0 ]; then
  fail "repeated -l should not be a conflict (rc=$rc, out=$out)"
fi

# --- Info flags ------------------------------------------------------------

check_info() {
  desc="$1"
  flag="$2"
  needle="$3"
  out=$("$bin" "$flag" 2>&1) && rc=0 || rc=$?
  if [ "$rc" -ne 0 ]; then
    fail "$desc ($flag): expected exit 0, got $rc"
    return
  fi
  case "$out" in
    *"$needle"*) ;;
    *) fail "$desc ($flag): expected '$needle' in output, got: $out" ;;
  esac
}

check_info "help"     -h "--commprog"
check_info "help"     -h "--answer"
check_info "version"  -v "modemu2k"
check_info "warranty" -w "WARRANTY"

# --- Result ---------------------------------------------------------------

if [ "$fails" -gt 0 ]; then
  echo "FAIL: $fails check(s) failed" >&2
  exit 1
fi
exit 0
