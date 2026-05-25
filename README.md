[![C/C++ CI](https://github.com/theimpossibleastronaut/modemu2k/actions/workflows/c-cpp.yml/badge.svg?branch=master)](https://github.com/theimpossibleastronaut/modemu2k/actions/workflows/c-cpp.yml)

# modemu2k

modemu2k is a fork of modemu, originally developed by Toru Egashira.

## What is modemu2k?

modemu2k is a Hayes-style AT-command modem emulator that bridges a comm
program (minicom, picocom, dosemu2, etc.) to a TCP or Telnet endpoint.
The comm program sees a pty that behaves like a serial-attached modem;
the actual traffic flows over the network. IPv4 and IPv6 supported.

Maintainer: Andy Alt <arch_stanton5995 at protonmail [dot] com>

License: GNU GPL 3

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
