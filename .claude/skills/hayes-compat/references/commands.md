# AT commands: the spec, and what modemu2k accepts

Normative column from the Conexant *Commands Reference Manual*
(`~/Documents/modem/Hayes_100498D.pdf`, section 3) — a chipset vendor's reference for
the extended Hayes set, and the closest thing to an authority available here.
Cross-checked against the HTML reference beside it in `~/Documents/modem/`; the
two agree on E, Q, V and the X levels, including that X0 enables exactly result
codes 0-4.

Where vendors differ they differ in the margins — `ATO` is one, described as
retraining control in one source and return-to-online in the other — so treat a
single source's account of an uncommon command as one implementation rather than
the rule.

The modemu2k column was measured with `scripts/at-probe.sh`, because the man
page's "No operation" is misleading: an unimplemented *value* is rejected, not
ignored.

## The rejection mechanism

`src/cmdlex.l` dispatches most unimplemented commands through:

```c
atcmdFake(ctx, yytext, vals)   /* accepts only if the numeric arg is in vals */
```

The third argument is the whitelist, so `atcmdFake(ctx, yytext, "1")` on the `E`
rule accepts `ATE1` and returns ERROR for `ATE0`. Commands with no rule at all
hit the lexer catch-all and are also ERROR.

**The whitelists frequently hold the wrong value.** Several accept only the
value modemu2k's behaviour matches, which is often *not* the spec default — so
a client that resets to defaults and states them explicitly gets an error. `ATX`
is the worst case (below).

## Basic set

| cmd | spec meaning | spec default | modemu2k |
|---|---|---|---|
| `ATA` | answer now | — | accepted |
| `ATD` | dial | — | accepted with a dial string |
| `ATE`*n* | command echo off (0) / on (1) | 1 | **E1 only** — always echoes |
| `ATH0` | hang up | — | accepted |
| `ATI`*n* | identification | — | `I4`–`I7`; bare `ATI` rejected |
| `ATO0` | return to online data state | — | accepted |
| `ATP` / `ATT` | pulse / tone dial | — | accepted, ignored (no line) |
| `ATQ`*n* | result codes on (0) / quiet (1) | 0 | **Q0 only** — always reports |
| `ATV`*n* | numeric (0) / verbose (1) result codes | 1 | **V1 only** — always verbose |
| `ATX`*n* | extended result codes, 0–4 | **4** | **X0 only** — see below |
| `ATZ` | reset to saved profile | — | accepted |
| `AT&W` | save profile | — | accepted (RAM only, lost at exit) |

## ATX is backwards, and it is the expensive one

The spec defines five levels, and **4 is the default**:

| n | behaviour |
|---|---|
| 0 | `CONNECT` on entering online data state. No dial tone or busy reporting. |
| 1 | `CONNECT <text>` — with speed. No dial tone or busy reporting. |
| 2 | `CONNECT <text>` + dial tone detection |
| 3 | `CONNECT <text>` + busy detection |
| 4 | `CONNECT <text>` + both. **Default.** |

modemu2k accepts `X0` and rejects `X1`–`X4`, i.e. it accepts only the level a
real modem does *not* start at. Any client that states the default explicitly
fails, and any client that wants a speed reported fails.

So the normative statement is narrower than "a bare CONNECT means 300 baud":
**X0 means CONNECT carries no speed indication.** What the client concludes from
a speedless CONNECT is up to the client — Spitfire's manual reads it as 300 baud
(see `clients.md`), which is the common interpretation but is a client-side
convention, not something the modem asserts.

For a virtual modem, dial tone and busy detection are meaningless — there is no
line — so `X0` through `X4` differ, for modemu2k's purposes, only in whether
`CONNECT` carries a rate.

## Result codes and the X levels that enable them

From Table 3-1. modemu2k emits `OK`, `CONNECT`, `RING`, `NO CARRIER`, `ERROR` —
numeric 0–4, and the table shows those five are enabled at *every* X level, so
the set modemu2k emits is valid at X0.

| n | code | enabled at |
|---|---|---|
| 0 | OK | all |
| 1 | CONNECT | all |
| 2 | RING | all |
| 3 | NO CARRIER | all |
| 4 | ERROR | all |
| 5 | CONNECT 1200 | X1+ |
| 6 | NO DIALTONE | X2+ |
| 7 | BUSY | X3+ |
| 8 | NO ANSWER | all |
| 9–22 | CONNECT 600 … CONNECT 230400 | X1+ |

`ATV0` selects the numeric form, which is why the numbers matter: a client that
sets `V0` expects `3` rather than `NO CARRIER`. modemu2k rejects `V0`, which is
the honest response given it always sends text.

## ATW decides *which* speed CONNECT reports

The HTML reference in `~/Documents/modem/` adds a
command the Conexant extract did not surface, and it matters for any decision
about what rate to claim:

| n | reports |
|---|---|
| W0, W1 | the **DTE** speed — the serial rate between computer and modem |
| W2 | the **DCE** speed — the rate negotiated on the line |

So "what speed should CONNECT report" has two defensible answers even on real
hardware, and a client can ask for either. For a virtual modem the DTE rate is
the honest one — there is no line, and dosemu2 presents a fixed UART rate — but
a client asking for `W2` is asking about a thing that does not exist.

SVDM sidesteps this by reporting a fixed `CONNECT 9600` and presenting a fixed
38400 DTE rate. modemu2k rejects `ATW` entirely (no lexer rule).

## Absent entirely — no lexer rule

`ATB` (protocol), `ATL` (speaker volume), `ATM` (speaker control), `ATN`,
`ATW` (which speed CONNECT reports), `ATY` (long space disconnect), `AT&C` (DCD
behaviour), `AT&D` (DTR behaviour), `AT&F` (factory defaults), `AT&K` (flow
control), `AT&Q`, `AT&S`.

`M` and `L` control a speaker that does not exist, so accepting and ignoring
them diverges from nothing observable. `&C` and `&D` are the opposite: modemu2k
already implements the behaviour both describe — DTR drop hangs up, carrier
state is tracked — so accepting the commands would be honest rather than a lie.
`&F` is worth considering too, since a client that cannot reset to defaults may
not proceed.

## Extensions (`AT%`, modemu2k's own)

`%B` telnet binary, `%D` dial-cancel, `%L` line mode, `%Q` quit, `%R` raw mode,
`%T` terminal type, `%V` verbose mask. Non-standard, so safe to extend — and
useless for compatibility, since no period client will ever send one.

## S-register syntax

`ATS`*n*`=`*m* sets, `ATS`*n*`?` queries; *n* above 255 is rejected and values
are bytes. This is why a TCP port cannot live in an S-register, which is what
sent issue #82 looking for another mechanism.
