# ATA answer-side modem semantics — design

Issue: https://github.com/theimpossibleastronaut/modemu2k/issues/67
Date: 2026-07-12
Status: approved by Andy (brainstorming session)

## Problem

modemu2k handles dial-out (`ATD` → TCP connect with telnet negotiation)
but has no answer side. A BBS that expects a real modem on a COM port
(dosemu2, Wine, or a native program on the PTY) cannot receive incoming
TCP "calls".

The existing `-l <port>` listener is **not** the answer side: it is a
TTY-side listener — the accepted TCP connection replaces the PTY as the
DTE (the dosemu2 plugin path, `m2k_listen_accept` adopts the fd as
`tty.rfd/wfd`). ATA answering needs a **line-side** listener: the
incoming TCP connection becomes the remote peer (`ctx->sock`), while the
BBS sits on the PTY. The issue's proposal to change `-l` conflated the
two; this design leaves `-l`, `m2k_setup_listen`, and
`m2k_listen_accept` completely untouched, which also dissolves the
issue's backwards-compatibility question.

## Decisions made

- New CLI flag `-a <port>` / `--answer <port>`, composable with the TTY
  mode flags. `-l` is not modified.
- Full modem semantics: unsolicited `RING`, ring count in S1, S0
  auto-answer, so a BBS configured for a real modem (`ATS0=1`) works
  unmodified.
- `ATA` with no pending caller waits up to **S7** seconds for one
  (`NO CARRIER` on expiry). S7 is the existing configuration knob —
  settable interactively, via `-e`, via `MODEMU2k`, or via
  `m2k_atcmd()`. `S7=0` degenerates to "accept only an already-pending
  caller, else immediate `NO CARRIER`". No new knob.
- `ATH`/`ATZ`/peer-close touch only the live connection; the answer
  listener stays bound (a real modem's hangup does not remove the phone
  line). Listener closes in `m2k_free`.
- Implementation shape: new `M2K_STATE_ANSWER` state mirroring
  `M2K_STATE_DIAL` (approach A). Overloading DIAL with an "answering"
  flag was rejected for readability; a synchronous accept inside
  `cmdIter` was rejected because it blocks the host event loop and
  breaks embed mode.

## Runtime design

### Context additions (`m2k_ctx.h`)

- `int answer_fd` — line-side listener, −1 when unbound, closed in
  `m2k_free`.
- `struct timeval answer_deadline` — S7 window for the ANSWER state.
- Ring-cadence state: `struct timeval ring_next`; the ring count lives
  in the existing S1 register (`ctx->atcmd.s[1]`).

### ATA dispatch (CMD state)

New lexer rule `<AT>[aA]` → new `CMDST_ATA`. In `cmdIter`:

- No listener bound, or `sock.alive` already → `ERROR`.
- Caller already pending in the accept queue → accept immediately,
  `CONNECT`, → ONLINE.
- Otherwise → enter ANSWER with `answer_deadline = now + S7` seconds.
  `S7=0` → immediate `NO CARRIER`, stay in CMD.

### `M2K_STATE_ANSWER` (mirrors DIAL)

- `answerPollfds`: answer fd `POLLIN`, TTY `POLLIN` (keypress cancels,
  like dial-cancel), timeout = remaining S7 window.
- `answerIter`: caller arrives → accept, `CONNECT`, → ONLINE; keypress
  or deadline → `NO CARRIER`, → CMD.
- `m2k_hangup()` during ANSWER aborts back to CMD, same as for DIAL.

### Accept path

A new keep-listener-open variant of `m2k_sockAccept` (the existing one
closes the server fd — correct for the one-shot TTY listener, wrong
here). The accepted fd is adopted into `ctx->sock` (alive=1) and goes
through the normal `stepEnterOnline`, so telnet option negotiation runs
as usual. Right for real telnet callers; a raw-TCP caller sees IAC
bytes, same as today's no-port `ATD`, with `AT%R1` as the existing
escape hatch. Listener backlog stays 1 — one line, one call; the kernel
queues/refuses extras, and after hangup the next `ATA`/ring cycle
serves the next caller.

### RING + S0 auto-answer (CMD state)

`cmdPollfds` additionally watches `answer_fd` when bound and no
connection is alive. While a caller is pending:

- Emit `RING` (new `CMDST_RING` result string) every 6 seconds (ring
  cadence constant), incrementing S1 per ring.
- When `S0 > 0 && S1 >= S0` → answer automatically via the same accept
  path.
- S1 resets to 0 on answer or when the pending caller gives up (accept
  queue empties).

## Surface

### CLI (`cmdarg.c`, `main.c`)

- `-a <port>` / `--answer <port>` → `cmdarg.answer_port`. Composes with
  any TTY mode (`-c/-d/-l/-s`, or `-e` stdin/stdout); not added to the
  mode flags' mutual-exclusion set. `main.c` calls
  `m2k_setup_answer(ctx, port)` after TTY setup.
- `-a` alone (no TTY mode, no `-e`) is treated like bare `-v`: print
  usage, exit 0.
- Usage text gains the flag plus one example
  (`modemu2k -a 2030 -c "..."`).

### Public API (`modemu2k.h`)

- `m2k_err_t m2k_setup_answer(m2k_t *ctx, const char *port)` — bind the
  line-side listener (reuses `m2k_sockListen`); `M2K_ERR_SOCKET` on
  failure. A second call while a listener is already bound is an error.
- `int m2k_get_answer_fd(const m2k_t *ctx)` — mirror of
  `m2k_get_listen_fd`; −1 when unbound. Lets embed hosts poll for the
  RING event themselves.
- No new `m2k_err_t` codes. Docblocks in house style; the `@since`
  convention question stays deferred per CLAUDE.md.

Embed mode needs no special casing: in `app_io`, `RING` arrives through
`m2k_read_to_app` as TTY output and `ATA` goes in through
`m2k_write_from_app`; `cmdPollfds`/`answerPollfds` expose the answer fd
to the host's poll.

## Error handling

- Bind failure at startup: fatal in the CLI (same as `-l` today).
- `ATA` with no listener or while connected: `ERROR`.
- Accept failure (caller vanished between RING and accept,
  `ECONNABORTED`): `NO CARRIER`, back to CMD, listener intact — not
  fatal.

## Testing

White-box C tests in the existing `USE_AS_TEST_LIB` pattern,
`tests/test_answer.c`:

1. `ATA` with no listener → `ERROR`.
2. Bind via `m2k_setup_answer`, connect a client socket, `ATA` →
   `CONNECT`, bytes bridge both ways.
3. `ATA` with no caller and `S7=0` (or ~1 s) → `NO CARRIER`, still in
   CMD, listener still bound.
4. Pending caller in CMD mode → `RING` appears in TTY output, S1
   increments; with `ATS0=1` → auto-`CONNECT` without `ATA`.
5. Peer disconnect → `NO CARRIER` → second client + second `ATA`
   succeeds without re-bind.
6. `ATH` closes the connection; listener remains bound
   (`m2k_get_answer_fd` still ≥ 0).

Existing `-l` tests must pass untouched. The end-to-end acceptance run
from the issue (comm program + `nc`) is a manual check, not a scripted
test.
