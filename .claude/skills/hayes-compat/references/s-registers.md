# S-registers

Two columns that must not be confused: what a real modem does (from the Conexant
*Commands Reference Manual*, `~/Documents/modem/Hayes_100498D.pdf`, section 3.6)
and what modemu2k does (from reading `src/`). Where they differ, the manual is the
specification and modemu2k is the thing that may need fixing.

## Registers modemu2k reads

| reg | meaning | spec default | spec range | modemu2k |
|---|---|---|---|---|
| S0 | rings before auto-answer; 0 disables | 0 | 0–255 | honoured |
| S1 | ring counter | 0 | 0–255 | honoured; see below |
| S2 | escape character | 43 (`+`) | 0–255 | honoured, **except >127** |
| S3 | line termination character | 13 (CR) | 0–127 | honoured (`CHAR_CR`) |
| S4 | response formatting character | 10 (LF) | 0–127 | honoured (`CHAR_LF`) |
| S5 | command line editing character | 8 (BS) | 0–127 | honoured (`CHAR_BS`) |
| S7 | wait for carrier after dial | **50** | 1–255 | honoured |
| S12 | escape prompt delay (guard time) | 50 | — | honoured |

Registers modemu2k accepts and ignores: everything else, including S6, S8, S10,
S11, S18, S29, S30. A client setting those gets `OK` and no effect — the one
place modemu2k silently diverges rather than rejecting.

## S2 over 127 should disable the escape — modemu2k does not

The manual is explicit:

> S2 holds the decimal value of the ASCII character used as the escape
> character. **A value over 127 disables the escape process**, i.e., no escape
> character will be recognized.

modemu2k's detector compares `c == CHAR_ESC(ctx)` with no range check, so
`ATS2=200` does not disable the escape — it moves the escape character to byte
200. `ATS2=255` is worse than useless: it makes the escape character 0xFF, which
is telnet's IAC.

This matters because disabling the escape is the standard way for a host to stop
a caller hijacking its modem, and `S2=255` is how a sysop would write it. A
client that does this and believes it worked is less safe than one that never
tried. Spitfire's `S2=1` works only because it happens to move the character
rather than disable it.

## S1 is cleared on a timer, not just at hangup

> S1 is incremented each time the modem detects a ring signal on the telephone
> line. S1 is cleared if no rings occur over an eight second interval.

modemu2k clears it when the caller goes away rather than on an eight-second
timer. The observable difference is narrow, but a client that polls `ATS1?` to
decide whether a call is still ringing sees a different decay.

Related: modemu2k keeps `answer.rings` separately from `S1` for the public
`m2k_get_ring_count()`. That split exists because the two genuinely diverge —
going off-hook for an outbound dial must clear the reported count without
restarting a waiting caller's auto-answer countdown.

## S7's default is 50, not 30

Worth noting when writing tests or documentation that quotes a default. `ATS7=1`
is the useful test setting: a failed `ATA` reports `NO CARRIER` in about a second
instead of the best part of a minute.

## S3, S4 and the result-code frame

S3 and S4 are not decoration — the manual defines result codes as header, text
and trailer, where the trailer is "the character having the ordinal value of
parameter S3 followed by the character having the ordinal value of parameter
S4". That is why the code says `CHAR_CR(ctx)` and `CHAR_LF(ctx)` rather than
`'\r'` and `'\n'`, and why hardcoding those literals in any new result-code path
would be a conformance bug rather than a style choice.

The manual also notes that changing S4 mid-command-line affects the result code
issued for that same line.
