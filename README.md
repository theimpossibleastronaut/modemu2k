[![C/C++ CI](https://github.com/theimpossibleastronaut/modemu2k/actions/workflows/c-cpp.yml/badge.svg?branch=master)](https://github.com/theimpossibleastronaut/modemu2k/actions/workflows/c-cpp.yml)

# modemu2k

modemu2k is a Hayes-style AT-command modem emulator that bridges a
serial-style interface to a TCP or Telnet endpoint. It ships as a C
library (libmodemu2k) exposing the modem state machine, plus a small
CLI built on top that allocates a PTY and either reads stdin/stdout
directly, forks a comm program (minicom, picocom) on the slave, or
accepts an incoming TCP connection as the TTY. IPv4 and IPv6.

It is based on modemu, originally developed by Toru Egashira
(1995–1996).

Maintainer: Andy Alt

License: GNU GPL 2 or later

* [Source repository](https://github.com/theimpossibleastronaut/modemu2k)
* [API documentation](https://theimpossibleastronaut.github.io/modemu2k/)
* [Issues and bug reporting](https://github.com/theimpossibleastronaut/modemu2k/issues)
* [Downloads](https://github.com/theimpossibleastronaut/modemu2k/releases/latest)

## Build

```sh
meson setup builddir
ninja -C builddir
```

Run `meson configure builddir` to see extra options.
`ninja install` is optional. The `m2k-minicom` / `m2k-picocom`
wrapper scripts are not installed by default; pass
`-Dhelper-scripts=true` to opt in.

## Usage

See **[QUICKSTART.md](QUICKSTART.md)** for the actual how-to: standalone
mode, running under minicom, listen mode, the binary-mode toggle for
file transfers, escaping with `+++`, and quitting cleanly.

The `modemu2k(1)` man page has the full option and AT-command reference.

## Live Telnet Boards

(BBSes in this list may also be capable of ssh or web-based protocols;
look them up in your browser if you would like more info.)

* The Rusty Mailbox — trmb.ca 2030
* [Synchronet BBS List](https://www.synchro.net/sbbslist.html) — a
  directory of currently-reachable telnet BBSes
* [The Telnet BBS Guide](https://www.telnetbbsguide.com/)

## Acknowledgments

Several library-API conventions in `modemu2k.h` are modeled after
mature C libraries — the patterns, not the code:

- **[libcurl](https://curl.se/libcurl/)** — caller-supplied detailed
  error buffer (`m2k_set_error_buffer` mirrors `CURLOPT_ERRORBUFFER`),
  `strerror`-style code-to-message helper, log-callback pattern
  (`CURLOPT_DEBUGFUNCTION`), and the blocking/non-blocking split
  between `m2k_run` and `m2k_step` (curl's easy vs. multi interfaces).
- **[libuv](https://libuv.org/) / [libcurl](https://curl.se/libcurl/) /
  [libssh2](https://www.libssh2.org/)** — `M2K_API` visibility/export
  macro (mirrors `UV_EXTERN` / `CURL_EXTERN` / `LIBSSH2_API`) for
  Windows DLL support and ELF visibility hardening.
- **[SQLite](https://www.sqlite.org/) /
  [libssh2](https://www.libssh2.org/)** — versioned header (macros)
  + runtime `m2k_version()` for header/library skew detection.
- **[libssh2](https://www.libssh2.org/)** — `M2K_ERR_WOULDBLOCK`
  return code for non-blocking flow control (mirrors
  `LIBSSH2_ERROR_EAGAIN`), distinguishing flow-control from hard
  errors.
