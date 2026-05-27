# Hacking on modemu2k

[TOC]

Practical notes for working on the codebase — entry points, the
contracts the public API depends on, and sketches of how a host
program could embed `libmodemu2k`. The build and test instructions
live in `README.md` and `tests/README.md`; this file is for what
those don't cover.

## Where to start reading

If you've never read the code before, this order keeps the surface
manageable:

1. **`modemu2k.h`** — the entire public API on one page, with
   docblocks. Skim this first; it answers "what can callers do."
2. **`src/main.c`** — ~100 lines, links against the library, shows
   the recommended call sequence (`m2k_new` → `m2k_setup_*` →
   `m2k_run` → `m2k_free`). The CLI is the simplest possible
   consumer.
3. **`src/m2k_api.c`** — the state machine. The four state-enter
   helpers (`stepEnterCmd`, `stepEnterDial`, `stepEnterOnline`, and
   the implicit `M2K_STATE_DONE` transition) and `m2k_step()`'s
   switch are the spine of the library.
4. **`src/cmdlex.l`** + **`src/atcmd.c`** — the AT command set.
   The lexer (Flex) produces a `Cmdstat` enum; `atcmd.c` has the
   per-verb handlers (`atcmdH`, `atcmdZ`, S-register ops, etc.).

The internal headers `src/m2k_private.h` and `src/m2k_ctx.h` define
everything that lives in the opaque `m2k_t`. Read them when you need
to know what state the context carries; don't read them to understand
the public contract.

## Code map

| File | Role |
|---|---|
| `src/m2k_api.c` | State machine, public-API entry points, the synchronous-vs-step wrappers |
| `src/cmdlex.l` | Flex grammar for the AT command set (generated to `cmdlex.c`) |
| `src/atcmd.c` | AT verb handlers + S-register state in `Atcmd` |
| `src/sock.c` | TCP dial (blocking `m2k_sockDial` + non-blocking `m2k_sockDialStart`/`Progress`/`Abort`), listen/accept |
| `src/sockbuf.c` / `src/ttybuf.c` | Buffered I/O for socket and TTY sides |
| `src/telopt.c` | Telnet option negotiation (WILL/WONT/DO/DONT/SB) |
| `src/comm_program.c` | Fork/exec a comm program (minicom etc.) on the PTY slave |
| `src/stty.c` | Raw-mode terminal setup for stdin/stdout mode |
| `src/cmdarg.c` | CLI argument parsing — only used by the binary, not the library |
| `src/verbose.c` | Logging callback + the curl-style error buffer writer |
| `src/utils.c` | `m2k_alloc` / `m2k_calloc` macros with call-site logging |

## Adding things

### A new AT command

1. Add a lex rule in `src/cmdlex.l` matching the verb and returning a
   `Cmdstat` (or dispatching to a handler that returns one).
2. Add the handler in `src/atcmd.c` if it's non-trivial. Handlers
   return `0` on success, `1` on rejection — the lexer turns that
   into `CMDST_OK` / `CMDST_ERROR`.
3. Add at least one case in `tests/test_atcmd.c` exercising the
   happy path and the range-check rejection. White-box style; you
   can call the handler directly.
4. If the command is user-facing, mention it in `man/modemu2k.1`.

### A new public API function

1. Declare in `modemu2k.h` with the `M2K_API` prefix and a docblock.
2. Define in `src/m2k_api.c` (or the topical file if it's narrowly
   scoped, e.g. socket-related code in `sock.c`).
3. If the function introduces a new `m2k_err_t` code, **append it**
   to the enum. Never reorder existing values; consumers may have
   cached the numeric values across releases.
4. Add a `tests/test_*.c` case. White-box tests can include
   `m2k_private.h`/`m2k_ctx.h` to reach into context state.
5. Add a one-line entry to `ChangeLog` under the (in-progress)
   section at the top.

## Contracts to preserve

These constraints are load-bearing for callers; breaking them silently
will surprise someone.

- **`m2k_err_t` is append-only.** New codes go at the end. Existing
  numeric positions are part of the ABI.
- **All state is per-ctx.** Two coexisting `m2k_t` instances must
  not share or clobber each other. No file-scope mutable state in
  the library. (Commit `645ab30` fixed the last remaining
  violation — telnet option negotiation tables — by moving them
  into `ctx->telOpt`.)
- **Threading: single-threaded per ctx.** The library is not
  internally synchronized. A caller may use multiple `m2k_t`
  contexts from different threads as long as no single ctx is
  touched concurrently. There is no `m2k_lock` / `m2k_unlock`.
- **Opaque types.** `m2k_t` is opaque to consumers. They should
  never `#include` `m2k_ctx.h` or peek at struct layout. The test
  suite does, on purpose, via `-DUSE_AS_TEST_LIB`.
- **The `M2K_API` marker is required on every public function**, so
  that a future `-fvisibility=hidden` build (or a Windows DLL build)
  keeps internals internal.

## Integration sketches (aspirational)

> **These are approach sketches, not validated implementations.**
> They describe what an integration *would* look like, based on
> reading the upstream code and `libmodemu2k`'s current API. Neither
> sketch has been built end-to-end against a real upstream tree. If
> you're considering doing the work, treat these as starting points
> for design, not as instructions.

### Replacing the modemu plugin in dosemu2

dosemu2 currently bundles a vendored copy of the original `modemu`
under `src/plugin/modemu/` (~10 source files: `modemu.c`,
`atcmd.c`, `sock.c`, `telopt.c`, etc., all wrapped in `#ifdef
DOSEMU`). The plugin is wired into dosemu2's I/O dispatcher via
`ioselect.h`.

In broad strokes, replacing it with `libmodemu2k`:

- Delete the vendored sources, link against the shared `libmodemu2k`
  (or vendor it via meson subproject).
- The dosemu2 side keeps its current entry hook (whatever calls
  `modemu_init` today). Replace it with a `m2k_new()` + the
  appropriate `m2k_setup_*()` choice (likely `m2k_setup_app_io`
  since dosemu2 owns the serial-port emulation).
- For each dosemu2 event loop tick, call `m2k_get_pollfds()` to
  learn which fds modemu2k wants watched and splice them into
  dosemu2's existing select/poll set. After `select`/`poll`
  returns, call `m2k_step()` with the same fd array and the
  reported `revents`.
- Replace the plugin's serial-byte path (whatever currently feeds
  modem state into the 8250 emulation in `src/dosext/serial/`)
  with `m2k_write_from_app` (DOS side → modem) and
  `m2k_read_to_app` (modem → DOS side).
- The dosemu2 side's DTR handling maps to `m2k_set_dtr()`. The
  &D2 hangup-on-DTR-drop behavior is already implemented in
  modemu2k.

The biggest unknown is whether dosemu2's main loop architecture is
compatible with calling into a library that owns its own state
machine. Worth a serious design conversation with dosemu2
maintainers before any code is written.

### Linking libmodemu2k into minicom

minicom currently centers everything on a single file descriptor
`portfd` (the serial device or socket). It's referenced ~50 times
in `src/main.c` plus appearances in `dial.c`, `updown.c`,
`functions.c`. Read/write/`select` use this fd directly.

A `--modem-emu` mode that swaps the fd-based path for `libmodemu2k`:

- Add a new config field for "use modemu2k instead of an OS serial
  device." When set, `m2k_new()` + `m2k_setup_app_io()` at the
  same point where minicom would have opened `portfd`.
- minicom's main loop (around `src/main.c:108` and the surrounding
  select-loop) needs to call `m2k_get_pollfds()` and merge those
  fds with its own, instead of putting `portfd` in the poll set.
- Reads that today look like `read(portfd, buf, n)` become
  `m2k_read_to_app(ctx, buf, n, &len)`. **Important**: when an
  incoming socket burst fills the modem's internal TTY-bound
  buffer past what one `read_to_app` can return (host's `buf` is
  smaller than the burst), the next `poll()` must run with a zero
  timeout. None of the modem's fds will fire to wake it — the
  bytes are buffered inside libmodemu2k. Check
  `m2k_has_pending_output()` and force `timeout=0` whenever it's
  true; otherwise effective throughput collapses to roughly
  "host buffer size per poll timeout." The same applies right
  after a `m2k_write_from_app()` keystroke: the bytes sit in the
  modem's tty-input buffer until the next `m2k_step()` flushes
  them to the socket, so the post-write poll should also use
  zero timeout for one iteration.
- Writes become `m2k_write_from_app(ctx, buf, n, &consumed)`,
  with the `M2K_ERR_WOULDBLOCK` return treated as "pause output,
  retry after next step."
- `m_dtrtoggle(portfd, ...)` calls map to `m2k_set_dtr()`.
- The dial UI talks AT commands directly today; in modemu2k mode
  it'd still work, since modemu2k accepts AT commands from the
  app-supplied byte stream the same way it would from a real
  serial connection.

The harder questions: minicom's terminal state (raw mode, hardware
flow control) presumes a real TTY behind `portfd`. Modemu2k's
embed mode side-steps that, but the existing minicom code paths
that call `m_sethwf` / `m_break` / `m_flush` would become no-ops
and need either stubs or compile-time gating.

## Cross-references

- **Building, testing, coverage:** `tests/README.md` covers the
  C/Python/shell layout, the `USE_AS_TEST_LIB` white-box pattern,
  the docker telnetd container for manual negotiation testing, and
  how to take a `gcovr` snapshot.
- **Build options, doxygen workflow, packaging gates:** see the
  top-level `README.md` and the `packaging/` directory.
- **API conventions borrowed from other libraries:** README's
  Acknowledgments section credits libcurl, libuv, libssh2, SQLite,
  and stb for the specific patterns adopted.
