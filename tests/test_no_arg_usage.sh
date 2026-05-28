#!/bin/sh
# Verify bare invocation prints the --help output and exits 0.
# Also covers invocations whose only flags describe how to run
# (e.g. -v / --verbose) but request no actual operation — those
# should be treated the same as no args.
#
# Usage: test_no_arg_usage.sh <modemu2k-binary>
set -e

if [ "$#" -lt 1 ]; then
  echo "usage: $0 <modemu2k-binary>" >&2
  exit 2
fi

bin="$1"

check_usage_exit() {
  label="$1"
  shift
  out=$("$bin" "$@")
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL ($label): expected exit 0, got $rc" >&2
    exit 1
  fi
  case "$out" in
    *"Usage:"*"--commprog"*) ;;
    *)
      echo "FAIL ($label): expected --help output in stdout, got:" >&2
      printf '%s\n' "$out" >&2
      exit 1
      ;;
  esac
}

check_usage_exit "bare"
check_usage_exit "verbose-only" -v
check_usage_exit "verbose-long-only" --verbose
