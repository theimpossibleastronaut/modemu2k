[![C/C++ CI](https://github.com/theimpossibleastronaut/modemu2k/actions/workflows/c-cpp.yml/badge.svg?branch=master)](https://github.com/theimpossibleastronaut/modemu2k/actions/workflows/c-cpp.yml)

# modemu2k

modemu2k is a fork of modemu, originally developed by Toru Egashira.

## What is modemu2k?

modemu2k is a Hayes-style AT-command modem emulator that bridges a
serial-style interface to a TCP or Telnet endpoint. It ships as a C
library (libmodemu2k) exposing the modem state machine, plus a small
CLI built on top that allocates a PTY and either reads stdin/stdout
directly, forks a comm program (minicom, picocom) on the slave, or
accepts an incoming TCP connection as the TTY. IPv4 and IPv6.

Maintainer: Andy Alt

License: GNU GPL 2 or later

* [Source repository](https://github.com/theimpossibleastronaut/modemu2k)
* [API documentation](https://theimpossibleastronaut.github.io/modemu2k/)
* [Issues and bug reporting](https://github.com/theimpossibleastronaut/modemu2k/issues)

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

## Downloads

* [Release page](https://github.com/theimpossibleastronaut/modemu2k/releases)

[![Packaging status](https://repology.org/badge/vertical-allrepos/modemu2k.svg)](https://repology.org/project/modemu2k/versions)

## Acknowledgments

Several library-API conventions in `modemu2k.h` are modeled after
[libcurl](https://curl.se/libcurl/): the caller-supplied detailed
error buffer (`m2k_set_error_buffer` mirrors `CURLOPT_ERRORBUFFER`),
the `strerror`-style code-to-message helper (`m2k_strerror`), the
log-callback pattern (`m2k_set_log_fn` mirrors `CURLOPT_DEBUGFUNCTION`),
and the explicit-fd event-loop split between `m2k_run` (blocking) and
`m2k_step` / `m2k_get_pollfds` (host-owned event loop, analogous to
curl's easy vs. multi interfaces). No code is copied — the conventions
are.
