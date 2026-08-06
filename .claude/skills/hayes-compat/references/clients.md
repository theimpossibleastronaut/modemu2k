# What real client software expects

Primary sources are on this machine. Read them rather than recalling — they are
specific, contemporaneous, and settle arguments that reasoning from convention
cannot.

## Spitfire 3.7 (DOS BBS, 1994–2010)

`~/Downloads/spitfire/SPITFIRE.DOC`, section 2.1–2.2. The most useful
conformance document available here, because it states requirements plainly and
names the failures that follow from missing them.

**Three settings it says MUST be in the init string:** `S0=0`, `V1`, `S2=1`.
All three work in modemu2k.

**It derives the caller's speed from the result code**, and prints its own table:

```
  300 Baud - CONNECT
 1200 Baud - CONNECT 1200        1200 Baud - CONNECT 1200/ARQ
 2400 Baud - CONNECT 2400        2400 Baud - CONNECT 2400/ARQ
 9600 Baud - CONNECT 9600        9600 Baud - CONNECT 9600/ARQ
14400 Baud - CONNECT 14400       14400 Baud - CONNECT 14400/ARQ
28800 Baud - CONNECT 28800       28800 Baud - CONNECT 28800/ARQ
```

The `/ARQ` suffix marks an error-correcting connection. Note the first row: a
bare `CONNECT` *is* the 300-baud signal, not an absence of information.

**Its default init string is rejected by modemu2k:**

```
ATS0=0Q0V1E0M0H0S2=1X1     -> AT command rejected
```

`E0`, `M0` and `X1` each fail on their own. `M` has no rule at all; `E0` and `X1`
fail `atcmdFake`'s range check.

**Failure messages worth recognising:**

- `Error in initialization commands.` — modemu2k's own, when `-e` gets a rejected string
- `Modem Reporting Carrier` — Spitfire's, when DCD is asserted with no call up.
  Its documented fix is `AT&C1&D2&W` from a terminal program, which modemu2k
  also rejects.

Boots with `SPITFIRE /C1`, i.e. COM1, matching `$_com1 = "vmodem"` under dosemu2.

## SVDM — Synchronet Virtual DOS Modem (Windows)

`~/src/sbbs/src/vdmodem/` — `readme.txt` for the decisions, `vdmodem.c` for the
implementation. The closest thing to a peer project: a maintained modem emulator
solving the same problem on Windows, via NTVDM and Synchronet's virtual
UART/FOSSIL driver.

Choices worth copying or arguing with:

- reports `CONNECT 9600` or bare `CONNECT` ("assumed 300 bps") depending on `ATX`
- presents a fixed 38400 DTE rate; a DOS program changing it has no effect
- ignores word length, stop bits and parity, assuming 8N1
- drops the TCP connection when the DOS program drops DTR, by default
- supports both telnet and raw TCP, inbound and outbound, IPv4 and IPv6
- inbound listening is a command-line option (`-l[addr]`, `-p<port>`), not an
  AT command — a data point for issue #82

## Other emulators, for comparison

From earlier research, none verified locally:

- **DOSBox / DOSBox-X softmodem** — listens on `listenport`, default 23, set in
  `dosbox.ini`; no AT command changes it at runtime
- **tcpser** — `-p <port>`, default 6400
- **Zimodem** (ESP8266/ESP32) — `ATA<n>` opens a listener on port *n*; an
  inbound connection rings per S0. Despite the spelling, its `ATA<n>` does not
  answer anything
- **RetroWiFiModem** — a TCP server port of 0 disables the server

## What this implies for modemu2k

Client software fails at the *first* rejected token, so a single unimplemented
value costs the whole integration. The cheapest wins are commands that cannot be
observed to have any effect on a virtual modem — `M` (speaker), `L` (volume),
`B` (protocol) — where accept-and-ignore diverges from nothing real.

The expensive ones are commands whose value the client will *act* on: `V`, `Q`,
`X`. Accepting those without implementing them trades one loud failure at
startup for a silent misinterpretation later.
