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
| `tests/test_*.c` | C units (mostly white-box; a few use only the public header) | linked against `libmodemu2k` with `-DUSE_AS_TEST_LIB` |
| `tests/test_*.py` | Black-box integration | run against the built `modemu2k` binary |
| `tests/test_*.sh` | CLI smoke tests | shell-level checks of `--help`, option parsing, etc. |

Adding a new test in any kind is one line in `tests/meson.build` — the
`test_cases`, `py_test_cases`, or `sh_test_cases` list (plus
`helper_cases` if the test links `test_helpers.c`). Tests carry meson
suite labels: `meson test -C builddir --suite unit` (or `integration`,
`cli`) runs one kind.

Every C test includes `tests/test.h` first. That header undefines
`NDEBUG`, so `assert()` stays active even in a release build. The C
tests use `assert()` to check syscalls and API return values, so a new
test must include `test.h` before anything else, or its checks would
compile out under `-DNDEBUG`.

### White-box vs black-box

C tests link against `libmodemu2k` compiled with `-DUSE_AS_TEST_LIB`,
which exposes internal symbols (`atcmdSSet`, `cmdLex`, `telOptHandle`,
`sockBufWReset`, etc.) so units can be driven directly. They include
`m2k_private.h` and `m2k_ctx.h`.

Examples under `examples/` are the opposite: they link against the
*public* dep only and serve as a black-box compile check on the
installed API surface. They are not run as tests but they *are* built,
which catches API signature drift.

## Docker-based integration test

`test_docker_telnet.py` brings the `docker/` busybox `telnetd` container
up automatically and dials it through the `modemu2k` binary.

**Opt in at configure time:**

```sh
meson configure builddir -Ddocker-test=true
meson test -C builddir test_docker_telnet
```

The test is disabled by default so the standard `meson test` run
doesn't depend on the docker daemon being available. When the option
is enabled, the test still skips itself (exit 77, which meson treats
as SKIP per the autotools convention) when:

- the `docker` binary isn't on `PATH`, or
- `docker info` returns non-zero — meaning the daemon is down, or the
  current user isn't in the `docker` group and would need `sudo`.

The skip-check probes `docker info` as the unprivileged user; it never
attempts `sudo docker`, since that would touch the sudo timestamp /
auth log and silently elevate the test runner. If you want the test
to run on your machine, configure docker to work without sudo (add
yourself to the `docker` group, or use rootless docker).

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

## Diagnosing a live session

When a real BBS session misbehaves (stuck dial, hung file transfer,
"where did the bytes go?"), trace modemu2k and the comm program
together. modemu2k uses `send()`/`recv()` for the socket, so a
`read,write`-only filter makes the wire side invisible — include the
socket syscalls or skip the filter entirely.

Full capture (simplest; use when the socket side looks suspiciously
quiet under a filter):

```sh
sudo strace -f -p $(pgrep -n modemu2k) -p $(pgrep -n minicom) \
  -tt -o /tmp/m2k.strace
```

Filtered (smaller, for narrower questions):

```sh
sudo strace -f -p $(pgrep -n modemu2k) -p $(pgrep -n minicom) \
  -tt -e trace=read,write,poll,select,sendto,recvfrom,sendmsg,recvmsg,sendmmsg,recvmmsg,execve,wait4,exit_group \
  -o /tmp/m2k.strace
```

`-f` follows forks so the `lrzsz-rz` / `lrzsz-sz` transfer child is
captured. For a pure wire view, run tcpdump in parallel:

```sh
sudo tcpdump -i any -w /tmp/bbs.pcap host <bbs-host> and port <port>
```

Only one tracer can attach to a PID at a time — a leftover `strace`
makes a new one fail with `PTRACE_SEIZE … Operation not permitted`
(easy to misread as a YAMA restriction). `pkill strace` if unsure.

