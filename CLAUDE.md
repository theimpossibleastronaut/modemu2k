# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```sh
meson setup builddir
ninja -C builddir
```

Run tests:
```sh
meson test -C builddir
```

Run a single test:
```sh
meson test -C builddir test_connect      # by name; or run the binary directly:
./builddir/tests/test_connect
```

Install (optional). The `m2k-minicom` / `m2k-picocom` helper scripts
are gated behind `-Dhelper-scripts=true`; without it, `ninja install`
ships only the binary, headers, man page, and (with `-Dgen-docs=true`)
the HTML docs.
```sh
meson configure builddir -Dhelper-scripts=true   # if you want the wrappers
ninja -C builddir install
```

Build options (`meson configure builddir` to view all):
- `docdir` (string) — documentation install path
- `gen-docs` (boolean, default false) — build API docs with Doxygen
- `helper-scripts` (boolean, default false) — install the m2k-minicom / m2k-picocom wrappers to `bindir`
- `docker-test` (boolean, default false) — register `test_docker_telnet` (runs a busybox telnetd container against modemu2k). The test still skips at runtime if docker isn't usable without sudo.

### Publishing docs

Generated HTML lands in `builddir/html/`. When `-Dgen-docs=true`, the docs are built as part of `ninja` and installed under `<docdir>/html/` by `ninja install` — packagers can ship them by enabling the option.

The `gh-pages` branch (versioned `vX.Y.Z/` layout) is updated automatically by the `Publish docs` GitHub Actions workflow on every push to `master`. There is intentionally no `paths:` filter on the trigger — too many files feed the docs (header, README, QUICKSTART, ChangeLog, man page, doxygen/, examples/, snippet-source test files) and keeping the filter in sync was fragile. The workflow's final step diffs the rebuilt HTML against `gh-pages`'s existing copy and skips the commit when nothing changed, so unrelated pushes spend ~30–60s rebuilding identical output and exit without churning `gh-pages`.

The workflow pins Doxygen to the upstream binary (`DOXYGEN_VERSION` in `.github/workflows/docs.yml`) because ubuntu-latest's apt doxygen lags by years and emits a different HTML structure (no `#doc-content` wrapper, plain `<h1>` instead of `<h1 class="doxsection">`) that silently breaks layout selectors in `doxygen/doxygen-custom.css`. Local renders match the live site only when both use the same doxygen version.

To regenerate locally for inspection:

```sh
meson configure builddir -Dgen-docs=true
ninja -C builddir docs   # or just `ninja -C builddir` — alias target
```

The `gh-pages` branch's `index.html` (the version landing page) is still updated by hand when adding a new version link.

Tracked in discussion #74 — "Docs publishing: should v0.2.x/ track tags instead of master HEAD?" (https://github.com/theimpossibleastronaut/modemu2k/discussions/74)

#### `@since` version annotations (adopted July 2026)

Because the docs publish one rolling page per *minor* (`vMAJOR.MINOR.x/`),
a reader of the single 0.2.x page can't tell whether a function shipped
in the 0.2.0 they have or only in a later 0.2.x patch. Convention:
annotate each public symbol added after 0.2.0 in `modemu2k.h` with
Doxygen's built-in `@since X.Y.Z` tag, inline in the docblock
(position-independent; renders as a plain "Since" paragraph; no
`ALIASES`/upstream config needed, won't trip a Doxygen warning).

Applied as of the 0.2.3 dev cycle: the `m2k_set_force_verbose`/`_get_`
pair carries `@since 0.2.2`; the 0.2.3 additions (`m2k_setup_answer`,
`m2k_get_answer_fd`, `m2k_describe_state`, `m2k_log_level_t`,
`m2k_set_log_level`/`_get_`) carry `@since 0.2.3`. The 0.2.0 base
surface is deliberately untagged. **Tag every new public symbol from
now on.** The "Slated for removal in 0.3.0" deprecation prose could
likewise be formalized as `@deprecated` someday; not done yet.

### Doc inputs and the @snippet pattern

The doxygen build pulls content from multiple places, all listed in the `depend_files:` of the `doxygen-html` custom target in `meson.build`:

- `modemu2k.h` — public API, with docblocks. `@example main.c` pulls in the bundled CLI source as an example page.
- `README.md`, `QUICKSTART.md` — main page + a Related Pages entry.
- `ChangeLog` — wrapped by `doxygen/changelog.md` (`@verbinclude ChangeLog`) so the source-of-truth file stays single-copy.
- `modemu2k.1` — wrapped by `doxygen/manpage.md` (`@htmlinclude modemu2k.1.fragment.html`). The fragment is produced at build time by `doxygen/man-to-fragment.sh`, which runs `groff -Thtml -mandoc` and strips the standalone `<html>/<head>/<body>` wrapper so the result splices cleanly into the doxygen page.
- `examples/*.c` and selected test files (e.g. `tests/test_connect.c`) — referenced from the header via `@snippet path/to/file.c tag`. The example files are compiled by `examples/meson.build` as a signature-drift guard; the test file is already compiled and run. Both mean the snippets in the published docs can never go stale relative to compilable, working code.

`examples/meson.build` exposes `example_sources` (a meson `files()` list) for the top-level `depend_files`, so adding a new example is one line in `example_cases`.

`doxygen/header.html` is a customised copy of the doxygen 1.16 default. It adds `class="m2k"` on `<html>` and a tiny script that sets `.m2k-mainpage` so `doxygen-custom.css` can scope main-page-only rules. `doxygen/DoxygenLayout.xml` hides nav tabs for things this project has none of (classes, structs, namespaces, etc.). `doxygen/meson.build` exposes `doxygen_inputs` similarly to `examples/`.

## Architecture

modemu2k is a **PTY-based modem emulator**: it presents a fake serial/modem interface (via a PTY) to a comm program, while proxying the actual connection over TCP/telnet.

### Core flow (`src/m2k_step.c`)

The state machine has five states (`m2k_step_state` in `m2k_private.h`): `CMD`, `DIAL`, `ANSWER`, `ONLINE`, `DONE`. The public stepping primitives are:

- `m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms)` — fill caller's pollfd array + timeout for the current state.
- `m2k_step(ctx, fds, nfds)` — process the events the caller's poll reported, advance one iteration. Handles state transitions internally (ATD starts a non-blocking dial → DIAL → ONLINE; ATA or S0 auto-answer → ANSWER → ONLINE; ATO → ONLINE; PTY closed → DONE; sock dead or `m2k_escape()` → CMD; `m2k_hangup()` during DIAL or ANSWER → CMD).
- `m2k_run(ctx)` — convenience poll-loop wrapper around the above. Used by the standalone CLI.

Per-iteration work in each state is in `cmdPollfds`/`cmdIter`, `dialPollfds`/`dialIter`, `answerPollfds`/`answerIter`, and `onlinePollfds`/`onlineIter` (`m2k_step.c`). `m2k_api.c` keeps lifecycle (`m2k_new`/`m2k_free`), getters/setters, and the deprecated sync API; `m2k_setup.c` holds the `m2k_setup_*` family (split in issue #71). The legacy `onlineMode()` wrapper is retained because the synchronous public `m2k_online()` API still calls it. The non-blocking dial path uses `m2k_sockDialStart` / `m2k_sockDialProgress` / `m2k_sockDialAbort` in `sock.c`; the synchronous `m2k_sockDial` is still there for the synchronous public `m2k_dial()`.

### Embed-mode I/O

Host applications that already own the terminal can bypass `tty.{rfd,wfd}` reads/writes entirely:

- `m2k_setup_app_io(ctx)` — flip the `ctx->step.app_io` flag; subsequent `m2k_get_pollfds` returns only the socket fd (no TTY fds).
- `m2k_write_from_app(ctx, buf, len, &consumed)` — host pushes bytes into `ttyBufR` (partial-accept; returns `M2K_ERR_WOULDBLOCK` when saturated).
- `m2k_read_to_app(ctx, buf, max, &len_out)` — host drains `ttyBufW`.

The escape-from-online path is reachable programmatically via `m2k_escape(ctx)` (sets a flag the next `onlineIter` consumes — no `+++` bytes need to flow through I/O).

### Embed-mode predicates and control lines

Helpers for hosts driving mode/DCD-aware UI:

- `m2k_is_online(ctx)` / `m2k_has_carrier(ctx)` — state and connection predicates (carrier survives a `+++` escape; mode does not).
- `m2k_run_done(ctx)` — true when the state machine has reached `M2K_STATE_DONE` (PTY closed; host can dispose the ctx).
- `m2k_get_listen_fd(ctx)` — returns the bound TTY-side listener fd after `m2k_setup_listen`, so a host event loop can poll for the incoming connection before calling `m2k_listen_accept`.
- `m2k_get_answer_fd(ctx)` — returns the bound line-side listener fd after `m2k_setup_answer` (the RING/ATA/S0 answer path from issue #67), so a host can poll for an incoming "call" itself. Not to be confused with the TTY-side `-l` listener above.
- `m2k_set_dtr` / `m2k_get_dtr` / `m2k_set_rts` / `m2k_get_rts` — DTE control lines. DTR 1→0 while a connection is live hangs up (&D2-equivalent); RTS is informational state only.

### Error reporting and logging

Complementary surfaces:

- `m2k_set_log_fn(ctx, fn, ud)` — narration callback for all log messages.
- `m2k_set_error_buffer(ctx, buf, size)` — curl `CURLOPT_ERRORBUFFER`-style detailed message for the most recent error, written via the `m2k_err_set()` helper (`src/verbose.c`). Unaffected by successful calls.
- `m2k_set_log_level(ctx, level)` — severity threshold (`M2K_LOG_ERROR` < `WARN` < `INFO` (default) < `DEBUG` < `TRACE`, in `ctx->log.level`, ATZ-immune). Every internal log site is tagged via `m2k_log(ctx, level, fmt, ...)`. The Hayes-side `AT%V` category mask (bit0 misc, bit1 telopt, bit2 byte traffic) coexists: a narration site delivers if its category bit is set, or `force_verbose` is on, or the level threshold covers it (byte traffic needs TRACE, the rest DEBUG). The CLI maps `-v` → DEBUG, `-vv` → TRACE.

`m2k_err_t` codes: append-only; new values go to the end so cached numeric values stay stable. `M2K_ERR_WOULDBLOCK` and `M2K_ERR_AT` were the two most recent additions.

### Source modules

| File | Role |
|------|------|
| `src/cmdlex.l` | Flex lexer that parses the AT command set; compiled to `cmdlex.c` at build time |
| `src/atcmd.c` | AT command handlers (`atcmdD`, `atcmdH`, `atcmdZ`, S-register ops, etc.); state in `Atcmd atcmd` / `atcmdNV` |
| `src/sock.c` | TCP socket dial (`m2k_sockDial`, plus non-blocking `m2k_sockDialStart`/`Progress`/`Abort`), listen/accept, close, shutdown; uses `getaddrinfo` for IPv4/IPv6 |
| `src/sockbuf.c` | Buffered socket I/O (`st_sockBufR`, `st_sockBufW`) |
| `src/ttybuf.c` | Buffered TTY I/O (`st_ttyBufR`, `st_ttyBufW`) |
| `src/telopt.c` | Telnet option negotiation (WILL/WONT/DO/DONT/SB); state in `st_telOpt telOpt` |
| `src/comm_program.c` | Fork/exec the comm program with the PTY slave path |
| `src/stty.c` | Raw-mode TTY setup for stdin/stdout mode |
| `src/cmdarg.c` | CLI argument parsing into `st_cmdarg` |
| `src/timeval.c` | `timeval` arithmetic helpers used for `+++` escape timing |
| `src/verbose.c` | Debug/verbose logging |
| `src/utils.c` | `chk_alloc` wrapper |

### Public vs internal headers

- `modemu2k.h` at the project **root** — the public API consumed by clients and `main.c`. Wrapped in `extern "C"` guards for C++ consumers. Installed to `<prefix>/include/modemu2k/`.
- `src/m2k_private.h`, `src/m2k_ctx.h` — internal structs (`Atcmd`, `st_sock`, `st_telOpt`/`TelOptStates`, the I/O buffers, the m2k context layout). Not installed.

Tests under `tests/` link against `libmodemu2k` compiled with `-DUSE_AS_TEST_LIB`, which exposes the internal symbols they need to white-box-test (atcmd parsing, telnet state machine, etc.). Examples under `examples/` link against the public dep only — they're a black-box compile check of the API surface.

### Library

`src/meson.build` builds `libmodemu2k` via meson's `library()` (default-library kind — usually shared, picked by the platform default). The top-level `meson.build` then links `main.c` + `cmdarg.c` against it for the CLI binary.

### PTY / TTY modes

Controlled by `cmdarg.ttymode` (see `src/cmdarg.h`):
- `CA_STDINOUT` — no PTY; reads/writes stdin/stdout directly (standalone use)
- `CA_COMM_PROGRAM` — allocates PTY master, forks and execs the comm program with the slave path
- `CA_SHOWDEV` — allocates PTY master, prints slave device name, exits
- `CA_DEVGIVEN` — opens a caller-specified PTY device
- `CA_LISTEN` — binds a TCP port and uses the accepted socket as the TTY (intended for dosemu2 plugin use)

`-c`, `-d`, `-l`, and `-s` (which set these modes) are mutually exclusive — passing more than one exits with status 2 and a message naming both flags, instead of silently honoring the last one. `-a/--answer <port>` (the line-side RING/ATA/S0 listener) is orthogonal and combines with any mode.

### No-argument behavior

When run with no arguments (`argc == 1`), `main.c` calls `showUsage()` (from `cmdarg.c`, exposed via `cmdarg.h`) — printing the same output as `-h` / `--help` — and exits 0 instead of entering the interactive `CA_STDINOUT` AT command session. `CA_STDINOUT` is reachable **only via `-e`**; flags that describe *how* to run (currently just `-v` / `--verbose`) but don't request an operation are treated the same as no args. Mode flags (`-c/-d/-l/-s`) of course override this.

`showUsage()` emits ANSI color (cyan section labels, yellow flag names, green example commands) when stdout is a TTY and `NO_COLOR` is unset; piped/redirected output stays plain.

### Building as a subproject

When consumed via `subproject('modemu2k')` from a parent meson project, the top-level `meson.build` gates the CLI binary, header install, helper scripts, tests, man page install, and doc-data files behind `if not meson.is_subproject()`. Only the library (via `dep_modemu2k` from `src/meson.build`) and the `examples/` directory (for `example_sources` exposure) load. `subdir('examples')` is intentionally outside the subproject guard so the doc target's `depend_files` can always reference `example_sources`; the example executables themselves are guarded inside `examples/meson.build`.

The header uses `add_project_arguments` (not `add_global_arguments`, which meson forbids in subprojects) for `-D_GNU_SOURCE`.

### Native libmodemu2k integration in minicom (and similar hosts)

The library side is in place: `m2k_get_pollfds` / `m2k_step` / `m2k_run_done` for the event loop, plus `m2k_setup_app_io` + `m2k_write_from_app` / `m2k_read_to_app` for host-owned TTY I/O, plus the predicates / control lines listed above. ATD is non-blocking under `m2k_step` (M2K_STATE_DIAL), so dialing no longer freezes the host event loop. The remaining work is host-side — a `--modem-emu` flag (or equivalent) in minicom that swaps its `portfd`-based read/write/dial path for libmodemu2k calls. End-to-end validation against a real comm program is the open task; no API blockers are known.

### Testing

See `tests/README.md` for the C / Python / shell test layout, the white-box `USE_AS_TEST_LIB` pattern, and the `docker/` busybox telnetd container for manual telnet-negotiation testing.

#### Coverage snapshot

`b_coverage` is a built-in meson option, set per build directory at configure time. Treat it as a periodic snapshot tool, not a default build mode — instrumentation slows the build and isn't useful unless you're about to look at the numbers. There's no coverage configuration in `meson.build` because of this.

```sh
meson setup _build-cov -Db_coverage=true
meson test -C _build-cov
gcovr --root . --filter src/ --filter modemu2k.h --print-summary
```

For an HTML report:

```sh
gcovr --root . --filter src/ --filter modemu2k.h --html-details _build-cov/coverage.html
```

`gcovr` is the recommended frontend; raw `gcov` also works but emits per-file `.gcov` files rather than a roll-up. On Debian/Ubuntu install `gcovr`; on Arch it's in the `extra` repo. Fall back to `pip install --user gcovr` if your distro lags.

To target a specific weakness, filter to one file:

```sh
gcovr --root . --filter src/telopt.c --print-summary
```

### Refactor deferred: ttybuf.c / sockbuf.c shape duplication

`src/ttybuf.c` and `src/sockbuf.c` mirror each other heavily — same reset / has-data / get1 / read / write / put1 shape across both, only differing in `read↔recv` / `write↔send` and the ttybuf side carrying extras (`prevT`/`newT` timing for `+++` detection, `stop` flow-control flag, two-stage limit check, `putTtyN`). Looked at in May 2026; declined to refactor.

Reason: the cleanest abstraction (a generic `struct iobuf` + function-pointer reader/writer) would obscure whether a given callsite uses `read()` or `recv()`, which matters when reading strace / debugging. The duplication is bounded (≈100 lines × 2) and isn't actively painful. Re-evaluate only if a third buffer type lands (TLS layer, etc.) or the surface otherwise grows.

### Live strace recipe (modemu2k + minicom together)

For diagnosing a stuck dial, hung ZModem transfer, or other "what's actually flowing between minicom, modemu2k, and the BBS?" question. **Two captures depending on what you need**:

**Filter-based (smaller, focused) — for narrow questions:**

```sh
sudo strace -f -p $(pgrep -n modemu2k) -p $(pgrep -n minicom) -tt -e trace=read,write,poll,select,sendto,recvfrom,sendmsg,recvmsg,sendmmsg,recvmmsg,execve,wait4,exit_group -o /tmp/m2k-dl.strace
```

**No filter (full capture) — for "where did the socket bytes go?" mysteries:**

```sh
sudo strace -f -p $(pgrep -n modemu2k) -p $(pgrep -n minicom) -tt -o /tmp/m2k-dl.strace
```

When to pick which:
- Default to filter-based. It's smaller, faster to grep, and covers the common diagnostic questions.
- Reach for no-filter if the filter-based capture shows suspicious gaps — e.g., the PTY side has heavy traffic but the socket side looks dead (it can't actually be dead; you're not tracing the right syscall). Has happened during a 2.5MB QWK download where the filter caught zero sendto/recvfrom despite the transfer completing cleanly. Cause not yet pinned down; glibc may be dispatching socket I/O through a syscall that isn't in the obvious list. No-filter mode is the escape hatch.

Why each filter entry:

- modemu2k's `sockbuf.c` uses `send()`/`recv()`. Glibc usually maps these to `sendto`/`recvfrom` syscalls on Linux x86_64, but the QWK-download case above suggests this isn't always so. Including `sendmsg`/`recvmsg`/`sendmmsg`/`recvmmsg` covers the most likely alternative paths.
- Filters that only include `read,write` make the socket side invisible — you'll see PTY traffic but not what's actually on the wire. We chased a "stuck download" detour because of this once.
- `-f` follows forks. Without it, when minicom forks `lrzsz-rz` / `lrzsz-sz` for a transfer, the child's syscalls vanish and you can't see why the transfer stalled.
- `execve` shows what binary minicom actually invoked (catches "I configured `lrzsz-sz` but my $PATH gave me bare `sz`"-class bugs).
- `wait4` / `exit_group` show child lifecycle for transfer processes.

Gotcha: **only one strace can attach to a given PID at a time**. If a prior strace is still attached (even backgrounded or in another terminal), the new one fails with `ptrace(PTRACE_SEIZE, NNNN): Operation not permitted`. Easy to misdiagnose as YAMA; usually it's just a leftover process. `pkill strace` first if in doubt.

### Reading verbose logs

`modemu2k -v` narrates AT command dispatch as `dispatch: <line>`. When the dispatch line shows stray letters before the AT (e.g. `dispatch: zzzzATD"bbs.example.com`), it's almost always Esc+<key> meta-prefix flakiness in the comm program — the user is hammering Esc+Z (minicom's menu) and the meta-timer is missing some of the presses, so the bare `z` leaks through into modemu2k's CMD-mode line buffer. The AT lexer ignores leading garbage so dialing still works; the prefix is just noise. Not a modemu2k bug.

## Packaging

Per-distro packaging files live under `packaging/`: `debian/`, `appimage/`, plus `file_id.diz`. Maintained *outside* this tree: the AUR `PKGBUILD` (`~/src/Packaging/AUR/modemu2k/`), the SlackBuild (`~/src/slackbuilds/network/modemu2k/` — moved out July 2026), and the OpenSUSE spec (`~/src/OpenSuse/home:andy5995:branches:network:utilities/modemu2k/`). Cross-distro packaging knowledge (containers, lintian gotchas, SBo conventions, the Debian `packaging/debian/ → ./debian/` move, etc.) lives in the gClaude packaging skill.

- `packaging/debian/modemu2k.lintian-overrides` silences `syntax-error-in-debian-changelog`. The trigger is the upstream `ChangeLog` (project's date-stanza format) installed as `changelog.gz` — see the gClaude Debian entry for the mechanic.
