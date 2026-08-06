---
name: hayes-compat
description: Hayes/AT command-set conformance for modemu2k — what real modems did, what real client software expects, and how to measure what modemu2k currently accepts. Use this whenever touching src/cmdlex.l, src/atcmd.c, result codes, S-registers, or the escape sequence; whenever a comm program, BBS, or terminal emulator rejects modemu2k or behaves oddly against it; and whenever deciding whether to accept a new AT command or value. Also use it before claiming modemu2k "supports" or "ignores" any AT command, because the answer is usually neither.
---

# Hayes compatibility

modemu2k pretends to be a modem. The value of that pretence depends entirely on
whether the software on the other end believes it — and that software was
written between 1985 and 1998 against real hardware, so its expectations are
fixed and undocumented in the code that must satisfy them.

This skill exists because that knowledge is expensive to rediscover. Working it
out once took a chipset vendor's 166-page command reference, a 1994 BBS manual,
a Windows modem emulator's source, and a set of probe runs against the binary.
All four are on this machine; see "Primary sources" below.

Keep two questions apart, because conflating them produces confident nonsense:
what a modem is *specified* to do, and what one particular client *expects*.
The Conexant manual answers the first. Spitfire's manual answers the second, for
Spitfire only.

## Start by measuring, not remembering

modemu2k's accepted command set is narrower than the man page implies and
different from what you would guess. Before asserting anything about it:

```
scripts/at-probe.sh [path-to-modemu2k]
```

It runs each standard AT token through the binary and prints accepted or
rejected. Defaults to `_build-debug/modemu2k`. Run it after any `cmdlex.l`
change — the accepted set is the contract with every client program, and it is
easy to widen or narrow it by accident.

## The one mechanism that explains most surprises

Commands modemu2k does not implement are not ignored. They are **range-checked
and rejected**:

```c
<AT>[eE][0-9]?  { if (atcmdFake(ctx, yytext, "1")) return cmdstat = CMDST_ERROR; }
```

`atcmdFake(ctx, text, vals)` accepts the command only when its numeric argument
appears in `vals`. So `ATE1` is accepted and `ATE0` is an **ERROR**, because
modemu2k always echoes and will not pretend otherwise.

That is defensible: silently accepting `ATE0` would promise behaviour that never
arrives. But it collides with how init strings are written. Nobody writes `ATE1`
in one — the whole point of an init string is setting the *non-default* value.
A single unimplemented value fails the entire line, and the client sees `ERROR`.

Two distinct failure modes follow, and they need different fixes:

- **Command has a rule, value not in `vals`** — `E0`, `V0`, `Q1`, `X1`. Widening
  is a one-character edit to the `vals` string, but only do it where modemu2k's
  actual behaviour matches the value being claimed.
- **Command has no rule at all** — `M`, `B`, `L`, `N`, `&C`, `&D`, `&F`, `&K`.
  These hit the lexer's catch-all. Adding one means deciding what it should do.

Before widening any `vals`, ask what the client will assume once it gets `OK`.
Accepting `ATV0` and then sending verbose result codes anyway is worse than
rejecting it, because the client will parse the wrong thing forever rather than
failing once at startup.

## Result codes are an interface, not a message

`putTtyCmdstat()` emits `OK`, `ERROR`, `CONNECT`, `NO CARRIER`, `RING`. Client
software parses these. Two consequences that are easy to miss:

**`CONNECT` carries a speed only above `ATX0`.** The spec defines five levels
and **defaults to X4**; only X0 gives a bare `CONNECT`. modemu2k accepts `X0`
and rejects `X1`-`X4` — it accepts the one level a real modem does not start at,
so a client that states the default explicitly gets an error, and one that wants
a rate reported cannot ask.

What a client concludes from a speedless `CONNECT` is a client-side convention:
Spitfire reads it as 300 bps and says so in a table. Common, but not something
the modem asserts — keep the two claims apart when writing docs.

**Anything else on that line is data to a program.** modemu2k currently follows
`CONNECT` with two lines of human advice about `+++` and `ATO`. To a person at
minicom that is the feature working; to a BBS it is unsolicited input arriving
before the caller has typed anything. When adding narration, ask which side of
that line it lands on.

## S-registers modemu2k honours

More than the man page suggests, and several are load-bearing:

| register | meaning | why it matters |
|---|---|---|
| S0 | rings before auto-answer | 0 = don't answer; the BBS answers with ATA |
| S1 | rings counted this call | also gates the RING cadence |
| S2 | **escape character**, default 43 (`+`) | a host moves or disables it so caller data cannot escape |
| S3 / S4 | CR / LF characters | used throughout the result-code path |
| S5 | backspace character | command-line editing |
| S7 | seconds to wait for carrier, default 50 | dial and answer timeouts |
| S12 | escape guard time | silence required either side of the escape |

`S2` deserves attention, and modemu2k is half-right on it. Moving the escape
character works — `CHAR_ESC(ctx)` reads `s[2]` at every detector site — so any
message hardcoding `'+++'` is wrong once a client sets it.

But the spec says **a value over 127 disables the escape entirely**, and
modemu2k has no such check: `ATS2=255` moves the escape character to 0xFF rather
than turning it off, and 0xFF is telnet's IAC. Disabling the escape is how a
host stops a caller hijacking its modem, so a client that asks for it and is
silently refused ends up less safe than one that never asked.

## Control lines

`&C1` (DCD follows carrier) and `&D2` (dropping DTR hangs up) are what BBS
software relies on to notice a call ended. modemu2k implements the *behaviour*
of both — DTR 1→0 hangs up, and carrier state is exposed — but rejects the
*commands*, since neither has a lexer rule.

Asserting DCD while idle is a specific, named failure: Spitfire refuses to start
and reports "Modem Reporting Carrier". If a client complains about carrier at
startup, check what the DCD line reports with no call up.

## Reference material

Read these when the question goes past the summary above:

- `references/commands.md` — the full AT set, which values modemu2k accepts, and
  what each command means to a real modem
- `references/s-registers.md` — every register, defaults, and who reads them
- `references/clients.md` — what specific client software requires, with the
  local paths to primary sources

## Primary sources on this machine

Prefer these to recollection or web search. They are contemporaneous and
specific:

- `~/Documents/modem/Hayes_100498D.pdf` — **the normative one.** Conexant *Commands
  Reference Manual* (2001), 166 pages, a chipset vendor's reference for the
  extended Hayes set: every command with its defined values and defaults, the
  full result-code table against `ATX` levels, and every S-register. Extract
  with `pdftotext -layout` and grep rather than reading it whole. Prefer this to
  any client's documentation for what a modem is *supposed* to do
- the HTML reference saved beside it in `~/Documents/modem/` — a second,
  independent Hayes command list. Useful as a cross-check: it agrees with the
  Conexant manual on E, Q, V and the X levels, and it documents `ATW`, which
  selects whether `CONNECT` reports the DTE or the DCE speed — the question
  behind "what rate should a virtual modem claim"
- `~/Downloads/spitfire/SPITFIRE.DOC` — a 1994 DOS BBS manual. Useful for what
  one real client *expects and does*, not for what is correct: required
  registers, its own result-code interpretation, per-modem init strings, and the
  errors it prints when unhappy
- `~/src/sbbs/src/vdmodem/` — Synchronet's SVDM, a maintained modem emulator
  solving the same problem on Windows. `readme.txt` documents its choices
  (connect rates, DTR handling, what it ignores); `vdmodem.c` is the
  implementation
- `~/src/dosemu2/src/base/serial/` — the UART, FOSSIL and int14h emulation
  modemu2k sits behind under dosemu2

Other emulators worth comparing against, from earlier research: DOSBox's softmodem
(listens on a config port, default 23, no AT command changes it), tcpser (`-p`,
default 6400), Zimodem (`ATA<n>` opens a listener), RetroWiFiModem (port 0
disables the server).

## Deciding what to implement

The tempting move is to accept everything and ignore it. Resist that: a client
that gets `OK` will act as though the setting took effect, and the resulting bug
appears much later and far away. Three honest options, in order of preference:

1. Implement the behaviour, then accept the command.
2. Reject the command, so the client fails at init where the sysop can see it.
3. Accept and document the divergence loudly — only where rejecting would block
   otherwise-working software, and the setting is genuinely cosmetic.

When unsure which values a real modem accepted, check SVDM or the Spitfire
appendix rather than guessing — both list real init strings from real hardware.
