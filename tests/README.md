# Tests

## Running

From the build directory:

```sh
meson test -C builddir              # run everything
meson test -C builddir test_atcmd   # single test by name
./builddir/tests/test_connect       # or run the binary directly
```

`ninja test` (without meson wrapper) also works but emits less detail
on failure.

## Layout

The suite mixes three test kinds:

| File pattern | Kind | Driver |
|---|---|---|
| `tests/test_*.c` | C white-box units | linked against `libmodemu2k` with `-DUSE_AS_TEST_LIB` |
| `tests/test_*.py` | Black-box integration | run against the built `modemu2k` binary |
| `tests/test_*.sh` | CLI smoke tests | shell-level checks of `--help`, option parsing, etc. |

Adding a new test in any kind is one line in `tests/meson.build` — the
`test_cases`, `py_test_cases`, or `sh_test_cases` list.

### White-box vs black-box

C tests link against `libmodemu2k` compiled with `-DUSE_AS_TEST_LIB`,
which exposes internal symbols (`atcmdSSet`, `cmdLex`, `telOptHandle`,
`sockBufWReset`, etc.) so units can be driven directly. They include
`m2k_private.h` and `m2k_ctx.h`.

Examples under `examples/` are the opposite: they link against the
*public* dep only and serve as a black-box compile check on the
installed API surface. They are not run as tests but they *are* built,
which catches API signature drift.

## Manual testing against a real telnet peer

`docker/` ships a throwaway busybox `telnetd` container that performs
real NVT/IAC negotiation — useful for exercising the telnet
option-handling paths against a live peer rather than a stub:

```sh
docker compose -f docker/docker-compose.yml up --build
# from a modemu2k session:
ATD"localhost 2323"
```

See `docker/README.md` for details.

## Coverage

The suite ships no coverage configuration in `meson.build` because
`b_coverage` is a built-in meson option, set per build directory at
configure time. Treat it as a periodic snapshot tool, not a default
build mode — instrumentation slows the build and isn't useful unless
you're about to look at the numbers.

```sh
meson setup _build-cov -Db_coverage=true
meson test -C _build-cov
gcovr --root . --filter src/ --filter modemu2k.h --print-summary
```

For an HTML report:

```sh
gcovr --root . --filter src/ --filter modemu2k.h --html-details _build-cov/coverage.html
```

`gcovr` is the recommended frontend; raw `gcov` also works but emits
per-file `.gcov` files rather than a roll-up. On Debian/Ubuntu install
`gcovr`; on Arch it's in the `extra` repo. Fall back to
`pip install --user gcovr` if your distro lags.

To target a specific weakness, filter to one file:

```sh
gcovr --root . --filter src/telopt.c --print-summary
```
