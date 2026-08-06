#!/bin/sh
# Probe which AT commands and values a modemu2k binary accepts.
#
# The accepted set is the contract with every client program, and it is not
# what the man page implies: commands modemu2k does not implement are rejected
# rather than ignored (see atcmdFake in src/cmdlex.l). Run this after any
# cmdlex.l change to see what actually moved.
#
#   scripts/at-probe.sh [path-to-modemu2k]
#
# Defaults to _build-debug/modemu2k relative to the repo root.

set -u

M2K="${1:-_build-debug/modemu2k}"

if [ ! -x "$M2K" ]; then
    echo "no modemu2k binary at: $M2K" >&2
    echo "usage: $0 [path-to-modemu2k]" >&2
    exit 2
fi

# One token per line. Grouped so the output reads as a report rather than a
# list: basic commands first, then the values init strings actually set, then
# the & and % families, then whole init strings from real client software.
BASIC="A D E E0 E1 H H0 I I4 O O0 P T V V0 V1 Q Q0 Q1 X X0 X1 X4 Z"
UNIMPL="B0 B1 L0 L3 M0 M1 N0 N1 W0 Y0"
AMP="&C0 &C1 &D0 &D2 &F &K3 &Q5 &S0 &W"
SREG="S0=0 S0=1 S0=255 S1? S2=1 S2=43 S3=13 S7=30 S12=50 S255=0"
PCT="%B0=0 %D1 %L0 %R1 %T0 %V3"

probe() {
    out=$("$M2K" -e "AT$1" </dev/null 2>&1)
    if printf '%s' "$out" | grep -q "rejected"; then
        printf '  %-10s REJECTED\n' "AT$1"
    else
        printf '  %-10s ok\n' "AT$1"
    fi
}

section() {
    printf '\n%s\n' "$1"
    shift
    for t in $*; do probe "$t"; done
}

printf 'AT conformance probe: %s\n' "$M2K"
printf '%s\n' "$("$M2K" -V 2>&1 | head -1)"

section "Basic command set" $BASIC
section "Commands with no lexer rule (expected: all rejected)" $UNIMPL
section "Ampersand family" $AMP
section "S-registers" $SREG
section "modemu2k extensions" $PCT

# Whole init strings matter more than single tokens: one unimplemented value
# fails the entire line, and that is how a client actually meets modemu2k.
printf '\nReal init strings\n'
probe 'S0=0Q0V1E0M0H0S2=1X1'
probe 'S0=0B0&B1&N0&H1&R1&Y1Q0V1E0M0H0S2=1X6'
probe '&C1&D2&W'
probe 'Z'
probe 'S0=1'

printf '\nA rejected init string means the client sees ERROR and may refuse to\n'
printf 'start. Check which token caused it before widening anything.\n'
