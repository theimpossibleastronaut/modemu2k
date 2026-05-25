# Getting started with modemu2k

modemu2k is a Hayes-style AT-command modem emulator that bridges a comm
program to a TCP or Telnet endpoint. To a comm program it looks like a
serial-attached modem; the actual traffic flows over the network.

## Important things to know first

- **`AT%Q` quits modemu2k from a command prompt.**
  `Ctrl-C` does **not** abort modemu2k.
- When running modemu2k *under* a comm program (e.g. minicom), use that
  program's normal quit key — modemu2k exits along with it.
- modemu2k defaults to a 7-bit cooked connection. For 8-bit-clean
  transfers (ZModem, Kermit, binary protocols, BBSs that need raw
  bytes), enable binary mode with `AT%B0=1%B1=1&W`. See the `%B`
  description in `man modemu2k`.
- Dialing takes a quoted address. To specify a non-default port, use a
  **space** between host and port (not a colon):

  ```
  atd"bbs.example.org 2030
  ```

## Standalone (stdin/stdout)

To talk to modemu2k directly without a comm program, invoke it with any
flag — for example `-e` to run a startup AT string:

```sh
modemu2k -e "AT"
```

modemu2k reads AT commands from stdin and writes responses to stdout.
Dial out with:

```
atd"localhost 8023
```

You'll see the remote side's prompt. On disconnect you get
`NO CARRIER`. Quit with:

```
at%q
```

## With a comm program (the usual mode)

Tell modemu2k to fork a comm program and hand it the PTY slave path via
`%s`:

```sh
modemu2k -c "minicom -l -tansi -con -p %s"
```

For 8-bit-clean transfers, run the startup AT string at the same time:

```sh
modemu2k -e "AT%B0=1%B1=1&W" -c "minicom -l -tansi -con -p %s"
```

A convenience wrapper, `m2k-minicom`, runs the above invocation for
you. It's in `scripts/m2k-minicom.in`; install it by configuring with
`-Dhelper-scripts=true` before `ninja install`. A matching
`m2k-picocom` ships under the same option.

From inside minicom (or picocom, etc.), dial in the same AT style:

```
atd"trmb.ca 2030
```

To quit modemu2k, just quit the comm program — modemu2k tears down with
it.

## Listen mode (incoming connections)

Run modemu2k as a TCP listener that uses the accepted socket as its
TTY. The connecting client drives modemu2k with AT commands. Intended
mainly for plugging into emulators like dosemu2.

```sh
modemu2k -l 19876
```

The process accepts one connection on port 19876, then runs the modem
state machine over it until the peer disconnects.

## Hanging up and escaping

- **Escape to command mode while online:** type `+++` (with the
  modem-style timing pause). modemu2k will return to command mode and
  display `OK`.
- **Return to online mode:** `ATO`.
- **Hang up the current connection:** escape to command mode, then
  `ATH`.

## Where to look next

- `man modemu2k` — full option reference and AT command list.
- README — project overview, building, downloads.
- API documentation — generated from `modemu2k.h` doc-comments,
  published at
  <https://theimpossibleastronaut.github.io/modemu2k/>.
