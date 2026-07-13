# ATA Answer-Side Modem Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let modemu2k answer incoming TCP "calls" like a real modem: `-a <port>` binds a line-side listener; `RING` is emitted while a caller waits; `ATA` (or `S0` auto-answer) accepts the call. Issue #67.

**Architecture:** New line-side listener fd (`answer_fd`) in the context, distinct from the TTY-side `listen_fd` used by `-l` (which is untouched). A new internal state `M2K_STATE_ANSWER` mirrors `M2K_STATE_DIAL`: `ATA` with no pending caller waits up to S7 seconds, non-blocking, cancellable. RING cadence and S0 auto-answer run in the CMD state.

**Tech Stack:** C, meson/ninja, flex lexer (`cmdlex.l`), existing white-box test pattern (`-DUSE_AS_TEST_LIB`).

**Spec:** `docs/superpowers/specs/2026-07-12-ata-answer-design.md` (approved). Read it first.

## Global Constraints

- Build dir: `_build-debug`, created with `meson setup _build-debug -Db_sanitize=address,undefined -Dc_args=-Werror` if absent.
- Run tests with `meson test -C _build-debug <name> -v`; full suite `meson test -C _build-debug`.
- Match existing code style exactly (GNU-ish braces on their own line, 2-space indent). No stylistic churn on lines you don't otherwise change.
- Code comments: rare and short; only non-obvious *why*. Public-header docblocks may be full doxygen.
- `m2k_err_t` codes are append-only; this plan adds none.
- Cmdstat enum values: append only at the end (`putTtyCmdstat` indexes an array by value).
- Commit messages: one-line subject; no `Claude-Session:` trailer ever; `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` trailer only when the diff exceeds 10 lines. Never push.
- Existing tests must keep passing after every task, especially `test_listen`, `test_listen_ipv6`, `test_listen_integration`.
- After any `meson.build` edit, run `meson fmt -eir` from the project root.
- Use `rmw` (not `rm`) if a file ever needs deleting.

---

### Task 1: Line-side listener plumbing (`m2k_setup_answer` / `m2k_get_answer_fd`)

**Files:**
- Modify: `src/m2k_ctx.h` (add fields after `listen_fd`, line ~24)
- Modify: `src/m2k_api.c` (`m2k_new`, `m2k_free`, new functions after `m2k_listen_accept`, line ~676)
- Modify: `src/m2k_private.h` (declare `m2k_sockAcceptKeep` next to `m2k_sockAccept`, line ~427)
- Modify: `src/sock.c` (add `m2k_sockAcceptKeep` after `m2k_sockAccept`, line ~156)
- Modify: `modemu2k.h` (prototypes + docblocks after `m2k_listen_accept`, line ~481)
- Create: `examples/m2k_setup_answer.c`
- Modify: `examples/meson.build` (add `'m2k_setup_answer'` to `example_cases`)
- Create: `tests/test_answer.c`
- Modify: `tests/meson.build` (add `'answer'` to `test_cases`)

**Interfaces:**
- Consumes: existing `m2k_sockListen(ctx, port)` (returns fd or −1), `m2k_err_set`.
- Produces: `m2k_err_t m2k_setup_answer(m2k_t *ctx, const char *port)`; `int m2k_get_answer_fd(const m2k_t *ctx)`; `int m2k_sockAcceptKeep(m2k_t *ctx, int server_fd)` (internal); ctx fields `int answer_fd`, `struct timeval answer_deadline`, `struct timeval ring_next`. Tasks 3–4 rely on all of these names exactly.

- [ ] **Step 1: Write the failing test**

Create `tests/test_answer.c`:

```c
/* Answer-side (ATA/RING/S0) tests — line-side listener bound by
   m2k_setup_answer; incoming TCP connections are "calls". */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>

/* Discover the ephemeral port of the bound answer listener. */
static int
answer_port(m2k_t *ctx)
{
  struct sockaddr_storage ss;
  socklen_t slen = sizeof ss;
  assert(getsockname(m2k_get_answer_fd(ctx), (struct sockaddr *) &ss, &slen) == 0);
  if (ss.ss_family == AF_INET6)
    return ntohs(((struct sockaddr_in6 *) &ss)->sin6_port);
  return ntohs(((struct sockaddr_in *) &ss)->sin_port);
}

static void
test_setup_answer_binds(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_get_answer_fd(ctx) == -1);
  assert(m2k_setup_answer(ctx, "0") == M2K_OK);
  assert(m2k_get_answer_fd(ctx) >= 0);
  assert(answer_port(ctx) > 0);
  /* Second bind while one is active is an error; the first survives. */
  int fd = m2k_get_answer_fd(ctx);
  assert(m2k_setup_answer(ctx, "0") != M2K_OK);
  assert(m2k_get_answer_fd(ctx) == fd);
  m2k_free(ctx);
}

int
main(void)
{
  test_setup_answer_binds();
  return 0;
}
```

Register it in `tests/meson.build` — in the `test_cases` list add (alphabetical, before `'app_io'`):

```meson
test_cases = [
  'answer',
  'app_io',
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C _build-debug`
Expected: FAIL — compile/link errors: `m2k_setup_answer` and `m2k_get_answer_fd` undeclared.

- [ ] **Step 3: Implement**

`src/m2k_ctx.h` — after the `listen_fd` member (keep its comment style):

```c
  int answer_fd;                 /* Line-side listener from m2k_setup_answer
                                       (ATA/RING answer path); -1 when unbound.
                                       Survives hangup; closed by m2k_free(). */
  struct timeval answer_deadline; /* S7 wait window for M2K_STATE_ANSWER. */
  struct timeval ring_next;      /* Next RING emission while a caller waits. */
```

`src/m2k_api.c` — in `m2k_new()`, right after `ctx->listen_fd = -1;`:

```c
  ctx->answer_fd = -1;
```

In `m2k_free()`, right after the `listen_fd` close:

```c
  if (ctx->answer_fd != -1)
    close(ctx->answer_fd);
```

After `m2k_listen_accept()`:

```c
m2k_err_t
m2k_setup_answer(m2k_t *ctx, const char *port)
{
  if (ctx->answer_fd != -1)
  {
    m2k_err_set(ctx, "m2k_setup_answer: answer listener already bound\n");
    return M2K_ERR_SOCKET;
  }
  int fd = m2k_sockListen(ctx, port);
  if (fd == -1)
    return M2K_ERR_SOCKET;
  ctx->answer_fd = fd;
  return M2K_OK;
}

int
m2k_get_answer_fd(const m2k_t *ctx)
{
  return ctx->answer_fd;
}
```

`src/sock.c` — after `m2k_sockAccept()`. Unlike `m2k_sockAccept`, the listener stays open (the "phone line" persists across calls); `SO_OOBINLINE` matches what the dial path sets on its socket:

```c
int
m2k_sockAcceptKeep(m2k_t *ctx, int server_fd)
{
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof addr;
  int client_fd = accept(server_fd, (struct sockaddr *) &addr, &addrlen);
  if (client_fd == -1)
  {
    m2k_err_set(ctx, "accept: %s\n", strerror(errno));
    return -1;
  }
  int tmp = 1;
  setsockopt(client_fd, SOL_SOCKET, SO_OOBINLINE, &tmp, sizeof(tmp));
  return client_fd;
}
```

`src/m2k_private.h` — next to the `m2k_sockAccept` declaration:

```c
int m2k_sockAcceptKeep(m2k_t *ctx, int server_fd);
```

`modemu2k.h` — after the `m2k_listen_accept` prototype:

```c
/**
 * @brief Bind the line-side "answer" listener on @p port.
 *
 * Incoming TCP connections on this port are treated as modem calls:
 * while one is pending, command mode emits RING (S-register 1 counts
 * the rings); the ATA command — or S0 auto-answer when S0 > 0 —
 * accepts the call and goes online, exactly as if ATD had connected
 * outward. The listener persists across hangups (a real modem's
 * hangup does not remove the phone line) and is closed by m2k_free().
 *
 * Not to be confused with m2k_setup_listen(), which accepts a TCP
 * connection to use as the TTY (DTE side).
 *
 * @param ctx  Modem context.
 * @param port Service name or decimal port number to listen on.
 * @return M2K_OK on success; M2K_ERR_SOCKET if a listener is already
 *         bound or on bind / listen / getaddrinfo failure.
 *
 * @snippet examples/m2k_setup_answer.c setup_answer
 */
M2K_API m2k_err_t   m2k_setup_answer(m2k_t *ctx, const char *port);

/**
 * @brief Expose the answer listener's fd (after m2k_setup_answer()).
 *
 * Lets an embedding host poll for the incoming-call event itself
 * (the RING indication also flows through the TTY output).
 *
 * @return The bound answer listener fd, or -1 when none is bound.
 */
M2K_API int         m2k_get_answer_fd(const m2k_t *ctx);
```

Create `examples/m2k_setup_answer.c`:

```c
/* Compile-checked example for the m2k_setup_answer() docblock. */
#include <modemu2k.h>

int
main(void)
{
  m2k_t *ctx = m2k_new();
  if (ctx == NULL)
    return 1;
  //! [setup_answer]
  if (m2k_setup_answer(ctx, "2030") != M2K_OK)
  {
    m2k_free(ctx);
    return 1;
  }
  /* Callers now RING in command mode; ATA (or ATS0=1) answers. */
  //! [setup_answer]
  m2k_free(ctx);
  return 0;
}
```

In `examples/meson.build`, add `'m2k_setup_answer',` to the `example_cases` list (alphabetical order with the existing entries).

- [ ] **Step 4: Run test to verify it passes**

Run: `meson fmt -eir` (meson.build files changed), then `ninja -C _build-debug && meson test -C _build-debug test_answer -v`
Expected: PASS. Also run the full suite: `meson test -C _build-debug` — all pass.

- [ ] **Step 5: Commit**

```bash
git add src/m2k_ctx.h src/m2k_api.c src/sock.c src/m2k_private.h modemu2k.h examples/m2k_setup_answer.c examples/meson.build tests/test_answer.c tests/meson.build
git commit -m "feat(api): add m2k_setup_answer/m2k_get_answer_fd line-side listener

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: Lex ATA; add RING result code

**Files:**
- Modify: `src/m2k_private.h` (Cmdstat enum, line ~411)
- Modify: `src/cmdlex.l` (new `<AT>` rule, near line 90)
- Modify: `src/m2k_api.c` (`putTtyCmdstat` string table line ~322; `m2k_atcmd` switch line ~445; `cmdDispatchIfReady` switch line ~950)
- Test: `tests/test_answer.c`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `CMDST_ATA` and `CMDST_RING` enum values (appended after `CMDST_PTY_CLOSED`); `putTtyCmdstat(ctx, CMDST_RING)` prints `RING`. Task 3's `m2k_step` handles `CMDST_ATA`; Task 4 emits `CMDST_RING`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_answer.c` (before `main`), and call it from `main`:

`m2k_atcmd(ctx, "ATA")` returns `M2K_ERR_AT` both before this task (lexer catch-all) and after (new not-actionable case, like ATD/ATO), so the test must distinguish via the error message:

```c
static void
test_ata_lexes(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[256] = "";
  m2k_set_error_buffer(ctx, errbuf, sizeof errbuf);
  /* ATA is a stepping-loop command, not actionable synchronously —
     same contract as ATD/ATO in m2k_atcmd(). */
  assert(m2k_atcmd(ctx, "ATA") == M2K_ERR_AT);
  assert(strstr(errbuf, "ATA is not actionable") != NULL);
  /* Garbage after AT still errors (lexer catch-all intact). */
  assert(m2k_atcmd(ctx, "ATY") == M2K_ERR_AT);
  m2k_free(ctx);
}
```

In `main`, after `test_setup_answer_binds();`:

```c
  test_ata_lexes();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C _build-debug && meson test -C _build-debug test_answer -v`
Expected: FAIL — errbuf contains the catch-all's `AT command rejected`, not the ATA-specific message.

- [ ] **Step 3: Implement**

`src/m2k_private.h` — append to the `Cmdstat` enum after `CMDST_PTY_CLOSED` (append-only; `putTtyCmdstat` indexes by value):

```c
  CMDST_PTY_CLOSED,
  CMDST_ATA,
  CMDST_RING
```

`src/cmdlex.l` — add after the `<AT>[dD]` rule:

```lex
<AT>[aA]	{ cmdstat = CMDST_ATA; }
```

`src/m2k_api.c`:

1. `putTtyCmdstat` — extend `cmdstatStr[]` (order must match the enum):

```c
  static const char *cmdstatStr[] = {
    "OK",
    "ERROR",
    "CONNECT",
    "NO CARRIER",
    "",
    "",
    "",
    "",
    "",     /* CMDST_ATA — never printed */
    "RING",
  };
```

2. `m2k_atcmd` — add a case next to `CMDST_ATD`/`CMDST_ATO`:

```c
  case CMDST_ATA:
    m2k_err_set(ctx, "ATA is not actionable from m2k_atcmd; use the step loop\n");
    return M2K_ERR_AT;
```

3. `cmdDispatchIfReady` — add `CMDST_ATA` to the pass-through group:

```c
  case CMDST_ATD:
  case CMDST_ATO:
  case CMDST_ATA:
    return stat;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `ninja -C _build-debug && meson test -C _build-debug test_answer -v`
Expected: PASS. Full suite: `meson test -C _build-debug` — all pass (notably `test_atcmd`).

- [ ] **Step 5: Commit**

```bash
git add src/m2k_private.h src/cmdlex.l src/m2k_api.c tests/test_answer.c
git commit -m "feat(atcmd): lex ATA; add RING result code

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: ANSWER state — ATA accepts a call, waits up to S7

**Files:**
- Modify: `src/m2k_private.h` (state enum, line ~106)
- Modify: `src/m2k_api.c` (helpers near the dial helpers; `stepStateName`; `stepEnterDial` area; `m2k_hangup`; `m2k_get_pollfds`; `m2k_step`)
- Test: `tests/test_answer.c`

**Interfaces:**
- Consumes: `ctx->answer_fd` (Task 1), `m2k_sockAcceptKeep` (Task 1), `CMDST_ATA` (Task 2), existing `telOptReset`, `sockInit`, `stepEnterOnline`, `stepEnterCmd`, `timevalCmp`, `timevalSub`, `findPollfd`, `appendTtyPollfds`, `putTtyCmdstat`.
- Produces: `M2K_STATE_ANSWER`; static helpers `answerPending(ctx)`, `answerAccept(ctx)` (0 success / −1 fail), `stepEnterAnswer(ctx)`, `answerPollfds(...)`, `answerIter(...)`. Task 4 reuses `answerPending` and `answerAccept`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_answer.c`. First, shared step helpers (place after `answer_port`, copied from the `test_dial_step.c` pattern plus an output collector):

```c
static char outbuf[8192];
static size_t outlen;

static void
out_reset(void)
{
  outlen = 0;
  outbuf[0] = '\0';
}

static void
step_once(m2k_t *ctx)
{
  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int timeout_ms;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
  if (timeout_ms < 0 || timeout_ms > 100) timeout_ms = 100;
  if (nfds > 0)
    poll(fds, nfds, timeout_ms);
  assert(m2k_step(ctx, fds, nfds) == M2K_OK);
  size_t len = 0;
  m2k_read_to_app(ctx, outbuf + outlen, sizeof outbuf - outlen - 1, &len);
  outlen += len;
  outbuf[outlen] = '\0';
}

static void
push_line(m2k_t *ctx, const char *line)
{
  char buf[128];
  int n = snprintf(buf, sizeof buf, "%s\r", line);
  assert(n > 0 && (size_t) n < sizeof buf);
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, buf, (size_t) n, &consumed) == M2K_OK);
  assert(consumed == (size_t) n);
}

static m2k_t *
new_answer_ctx(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  assert(m2k_atcmd(ctx, "AT%R1") == M2K_OK); /* raw: no IAC noise in bridge checks */
  assert(m2k_setup_answer(ctx, "0") == M2K_OK);
  out_reset();
  return ctx;
}

static int
connect_client(int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = htons((unsigned short) port);
  assert(connect(fd, (struct sockaddr *) &addr, sizeof addr) == 0);
  return fd;
}
```

Then the tests:

```c
static void
test_ata_no_listener_errors(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  out_reset();
  push_line(ctx, "ATA");
  for (int i = 0; i < 10 && strstr(outbuf, "ERROR") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "ERROR") != NULL);
  assert(!m2k_is_online(ctx));
  m2k_free(ctx);
}

static void
test_ata_accepts_pending_caller(void)
{
  m2k_t *ctx = new_answer_ctx();
  int client = connect_client(answer_port(ctx));

  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  assert(m2k_has_carrier(ctx));
  assert(strstr(outbuf, "CONNECT") != NULL);

  /* Bytes bridge both ways (raw mode, no telnet negotiation). */
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, "hello", 5, &consumed) == M2K_OK);
  for (int i = 0; i < 20; i++)
    step_once(ctx);
  char rbuf[16] = "";
  assert(recv(client, rbuf, sizeof rbuf - 1, 0) == 5);
  assert(memcmp(rbuf, "hello", 5) == 0);

  assert(send(client, "world", 5, 0) == 5);
  out_reset();
  for (int i = 0; i < 20 && strstr(outbuf, "world") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "world") != NULL);

  /* Answer listener survived the accept. */
  assert(m2k_get_answer_fd(ctx) >= 0);

  close(client);
  m2k_free(ctx);
}

static void
test_ata_no_caller_s7_zero_nocarrier(void)
{
  m2k_t *ctx = new_answer_ctx();
  assert(m2k_atcmd(ctx, "ATS7=0") == M2K_OK);
  push_line(ctx, "ATA");
  for (int i = 0; i < 10 && strstr(outbuf, "NO CARRIER") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "NO CARRIER") != NULL);
  assert(!m2k_is_online(ctx));
  assert(m2k_get_answer_fd(ctx) >= 0);
  m2k_free(ctx);
}

static void
test_ata_s7_timeout_nocarrier(void)
{
  m2k_t *ctx = new_answer_ctx();
  assert(m2k_atcmd(ctx, "ATS7=1") == M2K_OK);
  push_line(ctx, "ATA");
  /* ~2 s of stepping at <=100 ms per step covers the 1 s window. */
  for (int i = 0; i < 40 && strstr(outbuf, "NO CARRIER") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "NO CARRIER") != NULL);
  assert(!m2k_is_online(ctx));
  /* Back in CMD: a caller arriving now can still be answered. */
  int client = connect_client(answer_port(ctx));
  out_reset();
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  close(client);
  m2k_free(ctx);
}

static void
test_second_call_after_hangup(void)
{
  m2k_t *ctx = new_answer_ctx();
  int c1 = connect_client(answer_port(ctx));
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));

  /* Peer disconnect -> NO CARRIER -> CMD. */
  close(c1);
  out_reset();
  for (int i = 0; i < 50 && m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(!m2k_is_online(ctx));
  assert(strstr(outbuf, "NO CARRIER") != NULL);

  /* Second caller, second ATA, no re-bind. */
  int c2 = connect_client(answer_port(ctx));
  out_reset();
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  assert(strstr(outbuf, "CONNECT") != NULL);
  close(c2);
  m2k_free(ctx);
}
```

In `main`, after `test_ata_lexes();`:

```c
  test_ata_no_listener_errors();
  test_ata_accepts_pending_caller();
  test_ata_no_caller_s7_zero_nocarrier();
  test_ata_s7_timeout_nocarrier();
  test_second_call_after_hangup();
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `ninja -C _build-debug && meson test -C _build-debug test_answer -v`
Expected: FAIL — `test_ata_no_listener_errors` first: `ATA` reaches `m2k_step` as `CMDST_ATA`, which hits the `default:` arm and prints nothing.

- [ ] **Step 3: Implement**

`src/m2k_private.h` — extend the state enum (internal; renumbering is fine but keep DONE last for readability):

```c
typedef enum
{
  M2K_STATE_CMD,    /* command mode — reading AT commands from the TTY */
  M2K_STATE_DIAL,   /* non-blocking dial in progress — sock.fd is mid-connect */
  M2K_STATE_ANSWER, /* ATA waiting up to S7 seconds for an incoming caller */
  M2K_STATE_ONLINE, /* online mode — relaying between TTY and socket */
  M2K_STATE_DONE    /* PTY closed; m2k_run_done() returns true */
} m2k_step_state;
```

`src/m2k_api.c` — all additions live next to their dial-side twins.

1. `stepStateName` — add:

```c
  case M2K_STATE_ANSWER:
    return "ANSWER";
```

2. After `stepEnterDial`:

```c
static void
stepEnterAnswer(m2k_t *ctx)
{
  verboseOut(ctx, VERB_MISC, "state: %s -> ANSWER\r\n", stepStateName(ctx->step_state));
  /* Drop any pending TTY input — semantics of going off-hook. */
  ttyBufRReset(ctx);
  cmdBufReset(&ctx->step_cmdbuf);
  gettimeofday(&ctx->answer_deadline, NULL);
  ctx->answer_deadline.tv_sec += ctx->atcmd.s[7];
  ctx->step_state = M2K_STATE_ANSWER;
}
```

3. Near `dialPollfds`/`dialIter`, the answer helpers:

```c
/* Non-blocking "is a caller waiting in the accept queue?" check. */
static int
answerPending(m2k_t *ctx)
{
  if (ctx->answer_fd == -1 || ctx->sock.alive)
    return 0;
  struct pollfd p = {.fd = ctx->answer_fd, .events = POLLIN, .revents = 0};
  return poll(&p, 1, 0) == 1 && (p.revents & POLLIN);
}

/* Accept the pending caller and adopt it as the connection. Returns 0
   on success, -1 on accept failure (the listener stays bound). */
static int
answerAccept(m2k_t *ctx)
{
  int fd = m2k_sockAcceptKeep(ctx, ctx->answer_fd);
  if (fd == -1)
    return -1;
  telOptReset(ctx);
  sockInit(&ctx->sock);
  ctx->sock.fd = fd;
  ctx->sock.alive = 1;
  ctx->atcmd.s[1] = 0; /* call answered — ring counter rests */
  return 0;
}

/* Answer mode: wait for the listener to become readable, plus the TTY
   rfd (a keypress cancels, like dial cancel). Timeout runs to the S7
   deadline. */
static void
answerPollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  size_t n = 0;

  fds[n].fd = ctx->answer_fd;
  fds[n].events = POLLIN;
  fds[n].revents = 0;
  n++;

  appendTtyPollfds(ctx, fds, &n, POLLIN, 0);
  *nfds_inout = n;

  struct timeval now, remaining;
  gettimeofday(&now, NULL);
  if (timevalCmp(&now, &ctx->answer_deadline) >= 0)
  {
    *timeout_ms = 0;
    return;
  }
  remaining = ctx->answer_deadline;
  timevalSub(&remaining, &now);
  long ms = remaining.tv_sec * 1000L + remaining.tv_usec / 1000L;
  if (ms <= 0) ms = 1;
  *timeout_ms = (int) ms;
}

/* Returns 1 (accepted — caller transitions to ONLINE), 0 (continue
   polling), -1 (canceled or timed out — caller emits NOCARRIER + CMD). */
static int
answerIter(m2k_t *ctx, struct pollfd *fds, size_t nfds)
{
  if (!ctx->app_io)
  {
    struct pollfd *p = findPollfd(fds, nfds, ctx->tty.rfd);
    if (p && (p->revents & READ_EV))
      return -1;
  }
  struct pollfd *p = findPollfd(fds, nfds, ctx->answer_fd);
  if (p && (p->revents & READ_EV))
    return answerAccept(ctx) == 0 ? 1 : -1;

  struct timeval now;
  gettimeofday(&now, NULL);
  if (timevalCmp(&now, &ctx->answer_deadline) >= 0)
    return -1;
  return 0;
}
```

4. `m2k_hangup` — after the DIAL block, before `sockShutdown`:

```c
  if (ctx->step_state == M2K_STATE_ANSWER)
  {
    putTtyCmdstat(ctx, CMDST_NOCARRIER);
    stepEnterCmd(ctx);
    return M2K_OK;
  }
```

5. `m2k_get_pollfds` — add:

```c
  case M2K_STATE_ANSWER:
    answerPollfds(ctx, fds, nfds_inout, timeout_ms);
    return M2K_OK;
```

6. `m2k_step` — in the `M2K_STATE_CMD` case's inner switch, after the `CMDST_ATO` case:

```c
    case CMDST_ATA:
      if (ctx->sock.alive || ctx->answer_fd == -1)
      {
        putTtyCmdstat(ctx, CMDST_ERROR);
        return M2K_OK;
      }
      if (answerPending(ctx))
      {
        if (answerAccept(ctx) == 0)
          stepEnterOnline(ctx);
        else
          putTtyCmdstat(ctx, CMDST_NOCARRIER);
      }
      else if (ctx->atcmd.s[7] == 0)
      {
        putTtyCmdstat(ctx, CMDST_NOCARRIER);
      }
      else
      {
        stepEnterAnswer(ctx);
      }
      return M2K_OK;
```

And a new top-level case after `M2K_STATE_DIAL`:

```c
  case M2K_STATE_ANSWER:
  {
    int r = answerIter(ctx, fds, nfds);
    if (r == 1)
    {
      stepEnterOnline(ctx);
    }
    else if (r == -1)
    {
      putTtyCmdstat(ctx, CMDST_NOCARRIER);
      stepEnterCmd(ctx);
    }
    return M2K_OK;
  }
```

Also update the state-diagram comment above `stepStateName` — add these lines to the existing block:

```
       CMD    ── ATA (caller)   ──▶ ONLINE (CONNECT)
       CMD    ── ATA (none)     ──▶ ANSWER (wait ≤ S7)
       ANSWER ── caller arrives ──▶ ONLINE (CONNECT)
       ANSWER ── S7 / keypress  ──▶ CMD (NO CARRIER)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `ninja -C _build-debug && meson test -C _build-debug test_answer -v`
Expected: PASS (all 7 test functions). Full suite: `meson test -C _build-debug` — all pass.

- [ ] **Step 5: Commit**

```bash
git add src/m2k_private.h src/m2k_api.c tests/test_answer.c
git commit -m "feat(api): ATA answers a pending caller or waits up to S7 (ANSWER state)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: RING cadence and S0 auto-answer in CMD state

**Files:**
- Modify: `src/m2k_api.c` (`cmdPollfds`; new `cmdRingCheck` helper; `m2k_step` CMD case)
- Test: `tests/test_answer.c`

**Interfaces:**
- Consumes: `answerPending`, `answerAccept`, `ctx->ring_next` (Tasks 1/3); `CMDST_RING` (Task 2). S-registers: `ctx->atcmd.s[0]` = S0 auto-answer ring count, `ctx->atcmd.s[1]` = S1 ring counter.
- Produces: `RING_INTERVAL_SEC` constant; ring behavior relied on by the CLI use case, no code consumers after this.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_answer.c`:

```c
static void
test_ring_emitted_for_pending_caller(void)
{
  m2k_t *ctx = new_answer_ctx();
  int client = connect_client(answer_port(ctx));

  for (int i = 0; i < 50 && strstr(outbuf, "RING") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "RING") != NULL);
  assert(!m2k_is_online(ctx)); /* S0=0: never auto-answers */
  assert(ctx->atcmd.s[1] == 1); /* one ring so far (cadence is 6 s) */

  /* Explicit ATA answers the ringing call. */
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  assert(ctx->atcmd.s[1] == 0); /* counter rests once answered */

  close(client);
  m2k_free(ctx);
}

static void
test_s0_auto_answer(void)
{
  m2k_t *ctx = new_answer_ctx();
  assert(m2k_atcmd(ctx, "ATS0=1") == M2K_OK);
  int client = connect_client(answer_port(ctx));

  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  assert(strstr(outbuf, "RING") != NULL);
  assert(strstr(outbuf, "CONNECT") != NULL);

  close(client);
  m2k_free(ctx);
}

static void
test_ath_keeps_listener(void)
{
  m2k_t *ctx = new_answer_ctx();
  int client = connect_client(answer_port(ctx));
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));

  /* +++ escape back to CMD, then ATH hangs up the call only. */
  assert(m2k_escape(ctx) == M2K_OK);
  for (int i = 0; i < 10 && m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(!m2k_is_online(ctx));
  push_line(ctx, "ATH");
  for (int i = 0; i < 10 && m2k_has_carrier(ctx); i++)
    step_once(ctx);
  assert(!m2k_has_carrier(ctx));
  assert(m2k_get_answer_fd(ctx) >= 0);

  close(client);
  m2k_free(ctx);
}
```

In `main`, after `test_second_call_after_hangup();`:

```c
  test_ring_emitted_for_pending_caller();
  test_s0_auto_answer();
  test_ath_keeps_listener();
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `ninja -C _build-debug && meson test -C _build-debug test_answer -v`
Expected: FAIL in `test_ring_emitted_for_pending_caller` — no `RING` ever appears (loop exhausts 50 iterations).

- [ ] **Step 3: Implement**

`src/m2k_api.c`:

1. Near the top with the other file-local defines (below the `READ_EV`/`WRITE_EV` defines is fine):

```c
/* Seconds between RING emissions while a caller waits (US ring cadence). */
#define RING_INTERVAL_SEC 6
```

2. Replace `cmdPollfds` with (the app_io early-return folds into the shared answer-fd handling — capacity stays within M2K_MAX_POLLFDS: at most 2 TTY fds + 1 answer fd):

```c
static void
cmdPollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  size_t n = 0;

  if (ctx->app_io)
  {
    /* timeout=0 only if there's already work to do; otherwise let the
       host's poll() block on its own fds until it has bytes to push us. */
    *timeout_ms = (ttyBufRHasData(ctx) || ctx->step_cmdbuf.eol) ? 0 : -1;
  }
  else
  {
    appendTtyPollfds(ctx, fds, &n,
                     ttyBufWReady(ctx) ? POLLIN : 0,
                     ttyBufWHasData(ctx) ? POLLOUT : 0);
    *timeout_ms = -1;
  }

  if (ctx->answer_fd != -1 && !ctx->sock.alive)
  {
    if (ctx->atcmd.s[1] == 0)
    {
      /* No ring cycle yet — wake when a caller arrives. */
      fds[n].fd = ctx->answer_fd;
      fds[n].events = POLLIN;
      fds[n].revents = 0;
      n++;
    }
    else
    {
      /* Ring cycle in progress: the listener stays readable (level-
         triggered), so wake on the next-RING deadline instead. */
      struct timeval now, remaining;
      gettimeofday(&now, NULL);
      int ms = 0;
      if (timevalCmp(&now, &ctx->ring_next) < 0)
      {
        remaining = ctx->ring_next;
        timevalSub(&remaining, &now);
        long l = remaining.tv_sec * 1000L + remaining.tv_usec / 1000L;
        ms = l > 0 ? (int) l : 1;
      }
      if (*timeout_ms < 0 || ms < *timeout_ms)
        *timeout_ms = ms;
    }
  }
  *nfds_inout = n;
}
```

3. Add `cmdRingCheck` just above `m2k_get_pollfds` (it needs `answerPending`/`answerAccept` from Task 3, which sit earlier in the file):

```c
/* RING/S0 bookkeeping for CMD state. Returns 1 when S0 auto-answer
   accepted a caller (caller transitions to ONLINE), else 0. */
static int
cmdRingCheck(m2k_t *ctx)
{
  if (ctx->answer_fd == -1 || ctx->sock.alive)
    return 0;
  if (!answerPending(ctx))
  {
    ctx->atcmd.s[1] = 0; /* caller gone (or none): ring cycle over */
    return 0;
  }
  struct timeval now;
  gettimeofday(&now, NULL);
  if (ctx->atcmd.s[1] != 0 && timevalCmp(&now, &ctx->ring_next) < 0)
    return 0; /* between rings */
  putTtyCmdstat(ctx, CMDST_RING);
  if (ctx->atcmd.s[1] < 255)
    ctx->atcmd.s[1]++;
  ctx->ring_next = now;
  ctx->ring_next.tv_sec += RING_INTERVAL_SEC;
  if (ctx->atcmd.s[0] > 0 && ctx->atcmd.s[1] >= ctx->atcmd.s[0])
    return answerAccept(ctx) == 0;
  return 0;
}
```

4. `m2k_step` — at the top of the `M2K_STATE_CMD` case, before `cmdIter` runs:

```c
  case M2K_STATE_CMD:
  {
    if (cmdRingCheck(ctx))
    {
      stepEnterOnline(ctx);
      return M2K_OK;
    }
    Cmdstat s = cmdIter(ctx, fds, nfds);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `ninja -C _build-debug && meson test -C _build-debug test_answer -v`
Expected: PASS. Full suite: `meson test -C _build-debug` — all pass (`test_app_io` exercises app_io CMD pollfds; watch it).

- [ ] **Step 5: Commit**

```bash
git add src/m2k_api.c tests/test_answer.c
git commit -m "feat(api): RING cadence and S0 auto-answer while a caller waits

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: CLI `-a/--answer`, man page, ChangeLog

**Files:**
- Modify: `src/cmdarg.h` (struct field), `src/cmdarg.c` (getopt + usage), `src/main.c` (setup call)
- Modify: `modemu2k.1` (SYNOPSIS + option entry)
- Modify: `tests/test_options.sh` (help-text check)
- Modify: `ChangeLog` (one line)

**Interfaces:**
- Consumes: `m2k_setup_answer` (Task 1).
- Produces: `cmdarg.answer_port` (`const char *`, NULL when absent). Nothing downstream.

- [ ] **Step 1: Write the failing test**

In `tests/test_options.sh`, next to the existing `check_info` lines, add:

```sh
check_info "help"     -h "--answer"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ninja -C _build-debug && meson test -C _build-debug test_options -v`
Expected: FAIL — help output has no `--answer`.

- [ ] **Step 3: Implement**

`src/cmdarg.h` — add to `struct st_cmdarg` after `listen_port`:

```c
  const char *answer_port;
```

`src/cmdarg.c`:

1. In `cmdargParse`, short options string becomes:

```c
  const char *const short_options = "Va:c:d:e:hl:svw";
```

2. Long options — add first in the array:

```c
    {"answer", 1, NULL, 'a'},
```

3. Field init — after `x->listen_port = NULL;`:

```c
  x->answer_port = NULL;
```

4. Switch case — before `case 'c':` (NOT a `SET_MODE` flag; it composes with all modes):

```c
    case 'a': /* -a <port> */
      x->answer_port = optarg;
      break;
```

5. `showUsage` — add before the `-c` line (description column aligns at column 42 like the others):

```c
  printf("  %s-a, --answer=%s<port>                     answer incoming TCP \"calls\" on [port] (RING, ATA, S0)\n", opt, r);
```

and extend the Note paragraph's text — replace the existing sentence block with:

```c
  printf("%sNote:%s The -c, -d, -l, and -s options are mutually exclusive; passing\n"
         "more than one exits with an error. The -a option can be combined\n"
         "with any of them. With no mode option given but -e present (e.g.\n"
         "'modemu2k -e \"ATZ\"'), modemu2k reads AT commands from stdin/stdout.\n"
         "Invoked with no arguments at all, it prints this help.\n",
         sec, r);
```

`src/main.c` — after the `switch (cmdarg.ttymode)` block closes (line ~132), before the `force_verbose` call:

```c
  if (cmdarg.answer_port != NULL && m2k_setup_answer(ctx, cmdarg.answer_port) != M2K_OK)
  {
    fprintf(stderr, "modemu2k: failed to bind answer port %s\n", cmdarg.answer_port);
    m2k_free(ctx);
    return EXIT_FAILURE;
  }
```

(`-a` alone still prints usage and exits 0: the existing `ttymode == CA_STDINOUT && atcmd == NULL` check runs earlier and `-a` sets neither.)

`modemu2k.1`:

1. SYNOPSIS (line ~13) — add `-a` first:

```troff
[\fB-a \fIport\fR] [\fB-c \fIcomm_prog\fR] [\fB-d \fIpty_master\fR] [\fB-e \fIatcmds\fR]
```

2. New option entry before the `-c` entry (match the `.\" ===== -x =====` comment style):

```troff
.\" ===== -a =====
.TP
.BI -a " port"
Answer incoming TCP connections (``calls'') on
.IR port .
A waiting caller makes
.B modemu2k
print RING in command mode; the ATA command, or S0 auto-answer
(e.g.\& ATS0=1), accepts the call and goes online. The listener stays
bound after hangup, so the next caller can be answered without a
restart. Can be combined with any of the -c, -d, -e, -l, and -s
options. Dual-stack IPv4/IPv6.
```

3. The Note paragraph near line 137 — after "the -c, -d, -l, and -s options are mutually exclusive" sentence, add:

```troff
The -a option can be combined with any of them.
```

`ChangeLog` — add one bullet at the top of the `(in-progress)` v0.2.3 stanza:

```
  * Add -a/--answer and ATA/RING/S0 answer-side modem semantics, so a
    BBS on the PTY can receive incoming TCP calls (new m2k_setup_answer
    / m2k_get_answer_fd API)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `ninja -C _build-debug && meson test -C _build-debug test_options -v`, then the full suite `meson test -C _build-debug`.
Expected: all PASS.

Manual smoke check (optional but recommended): `./_build-debug/modemu2k -a 2030 -e "ATZ"` in one terminal, `nc localhost 2030` in another → `RING` appears; type `ATA` → `CONNECT`; bytes flow; Ctrl-C twice to quit.

- [ ] **Step 5: Commit**

```bash
git add src/cmdarg.h src/cmdarg.c src/main.c modemu2k.1 tests/test_options.sh ChangeLog
git commit -m "feat(cli): add -a/--answer for answer-side (ATA/RING/S0) operation

Fixes #67

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```
