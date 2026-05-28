#!/bin/sh
# Verify -v / --verbose both install the log_fn AND open the AT%V
# verbose mask, so library narration sites (gated by VERB_MISC etc.)
# actually print. Regression test: an earlier fix wired the callback
# but left the mask at 0, silently no-opping every verboseOut() call.
#
# Strategy: feed empty stdin to "modemu2k -v -e AT". Reading 0 bytes
# from stdin triggers a verboseOut(VERB_MISC, "Pty closed...") in
# ttybuf.c, which can only reach stderr if both the callback and the
# mask are in place. Without -v, the same stderr is empty.
#
# Usage: test_verbose.sh <modemu2k-binary>
set -e

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <modemu2k-binary>" >&2
  exit 2
fi

bin="$1"

assert_verbose_narrates() {
  label="$1"
  shift
  err=$(printf '' | "$bin" "$@" -e 'AT' 2>&1 1>/dev/null)
  case "$err" in
    *"Pty closed"*) ;;
    *)
      echo "FAIL ($label): expected verbose narration in stderr, got:" >&2
      printf '%s\n' "$err" >&2
      exit 1
      ;;
  esac
}

assert_silent_without_verbose() {
  err=$(printf '' | "$bin" -e 'AT' 2>&1 1>/dev/null)
  if [ -n "$err" ]; then
    echo "FAIL (no-verbose): expected empty stderr, got:" >&2
    printf '%s\n' "$err" >&2
    exit 1
  fi
}

assert_silent_without_verbose
assert_verbose_narrates "short" -v
assert_verbose_narrates "long" --verbose
