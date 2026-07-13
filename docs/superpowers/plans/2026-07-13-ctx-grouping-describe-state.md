# Context Grouping + m2k_describe_state Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Group `struct m2k_s`'s flat fields into named subsystem substructs (#69), then add the public `m2k_describe_state()` snapshot helper on top (#70).

**Architecture:** Seven mechanical rename passes (one commit per group: tty, sock, dial, answer, step, ctrl, log), each rewriting the struct in `src/m2k_ctx.h` and all access sites via word-boundary regex replacement; the untouched test suite is the behavior oracle. Then one TDD task adds `m2k_describe_state()` walking the new groups.

**Tech Stack:** C, meson/ninja, python3 for the batch renames, existing white-box test pattern.

**Spec:** `docs/superpowers/specs/2026-07-13-ctx-grouping-describe-state-design.md` — the approved struct layout lives there; every rename below implements it verbatim.

## Global Constraints

- Branch: `ctx-grouping` off master. Never push.
- Build dir `_build-debug` (ASan/UBSan, `-Werror`). Full `meson test -C _build-debug` must be green **after every commit**.
- Rename scope: `src/*.c`, `src/*.h`, `src/cmdlex.l`, `tests/*.c`. Public `modemu2k.h` contains none of these identifiers (verify with grep before assuming).
- Replacements use `->` anchored word-boundary regexes so any pointer name (not just `ctx`) is covered; all renamed member names are unique in the codebase.
- No behavior change in Tasks 1–4; no new tests there; no ChangeLog entries for them. Task 4's final commit body: `Fixes #69`. Task 5 commit body: `Fixes #70` + ChangeLog line.
- Commit messages: one-line subject; `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` trailer (all these diffs exceed 10 lines); never a `Claude-Session:` trailer.
- Match existing brace/indent style; comments rare and short.

## Rename helper (used by Tasks 1–4)

Each rename step runs this pattern (fill `RULES`):

```bash
python3 - <<'EOF'
import re, subprocess
files = subprocess.run(["git", "ls-files", "src/*.c", "src/*.h", "src/*.l", "tests/*.c"],
                       capture_output=True, text=True).stdout.split()
RULES = [ (r"->ttyBufR\b", "->tty.bufR") ]   # per-step list
for f in files:
    s = open(f).read()
    orig = s
    for pat, rep in RULES:
        s = re.sub(pat, rep, s)
    if s != orig:
        open(f, "w").write(s)
        print("rewrote", f)
EOF
```

---

### Task 1: tty group

**Files:**
- Modify: `src/m2k_ctx.h` (struct layout), all of `src/*.c`, `src/m2k_private.h`, `tests/*.c` via rename script

**Interfaces:**
- Produces: `ctx->tty.rfd`, `ctx->tty.wfd` (unchanged spelling), `ctx->tty.owned`, `ctx->tty.bufR`, `ctx->tty.bufW`. Task 5 reads these.

- [ ] **Step 1: Create branch**

```bash
git checkout -b ctx-grouping
```

- [ ] **Step 2: Rewrite the struct members in `src/m2k_ctx.h`**

Replace the members `struct st_tty tty;`, `struct st_ttyBufR ttyBufR;`, `struct st_ttyBufW ttyBufW;`, and `bool tty_owned;` (keep the existing comment content where it still applies) with:

```c
  struct
  {
    int rfd, wfd;             /* absorbed st_tty */
    bool owned;               /* true when rfd/wfd is a library-opened fd
                                   (PTY master, dev, accepted client) that
                                   m2k_free() must close; false for stdin
                                   and app-IO. rfd == wfd in every owned
                                   case, so one close suffices. */
    struct st_ttyBufR bufR;
    struct st_ttyBufW bufW;
  } tty;
```

- [ ] **Step 3: Run the rename rules**

RULES for this task:

```python
RULES = [
  (r"->ttyBufR\b",  "->tty.bufR"),
  (r"->ttyBufW\b",  "->tty.bufW"),
  (r"->tty_owned\b", "->tty.owned"),
]
```

`ctx->tty.rfd` / `ctx->tty.wfd` sites need no change (absorption). Check leftovers and the st_tty type:

```bash
grep -rn "ttyBufR\|ttyBufW\|tty_owned" src/ tests/ | grep -v "st_ttyBuf\|BufRReset\|BufWReset\|BufRHasData\|BufWHasData\|BufWReady"
grep -rn "st_tty\b" src/ tests/
```

The first grep must return nothing (type names `st_ttyBufR/W` and function names like `ttyBufRReset` legitimately keep the substring — the grep above excludes them; inspect anything else by hand). If `struct st_tty` is now referenced only by its definition in `src/m2k_private.h`, delete that definition.

- [ ] **Step 4: Build and test**

Run: `nice -n 19 ionice -c 3 ninja -C _build-debug && meson test -C _build-debug`
Expected: all tests pass (21+).

- [ ] **Step 5: Commit**

```bash
git add -A src tests
git commit -m "refactor(ctx): group TTY fds, ownership flag, and buffers under ctx->tty

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: sock group

**Files:**
- Modify: `src/m2k_ctx.h`, rename across `src/*.c`, `src/cmdlex.l`, `tests/*.c`

**Interfaces:**
- Produces: `ctx->sock.conn` (an `st_sock`; functions taking `st_sock *` receive `&ctx->sock.conn`), `ctx->sock.bufR`, `ctx->sock.bufW`, `ctx->sock.listen_fd`. Task 5 reads these.

- [ ] **Step 1: Rewrite the struct members in `src/m2k_ctx.h`**

Replace `st_sock sock;`, `struct st_sockBufR sockBufR;`, `struct st_sockBufW sockBufW;`, and `int listen_fd;` with:

```c
  struct
  {
    st_sock conn;
    struct st_sockBufR bufR;
    struct st_sockBufW bufW;
    int listen_fd;            /* Bound listener from m2k_setup_listen,
                                   consumed by m2k_listen_accept; -1 when
                                   no listener is open. */
  } sock;
```

- [ ] **Step 2: Run the rename rules**

Ordering matters: rewrite the longer names first, then the bare `->sock`:

```python
RULES = [
  (r"->sockBufR\b",  "->sock.bufR"),
  (r"->sockBufW\b",  "->sock.bufW"),
  (r"->listen_fd\b", "->sock.listen_fd"),
  (r"->sock\b",      "->sock.conn"),
]
```

The `\b` after `sock` stops `->sockBufR` (already rewritten) from double-matching, and turns `ctx->sock.fd` → `ctx->sock.conn.fd`, `&ctx->sock` → `&ctx->sock.conn` correctly. Leftover check:

```bash
grep -rn -- "->sockBufR\|->sockBufW\|->listen_fd" src/ tests/
grep -rn -- "->sock\b" src/ tests/ | grep -v -- "->sock\.conn\|->sock\.bufR\|->sock\.bufW\|->sock\.listen_fd"
```

Both must return nothing.

- [ ] **Step 3: Build and test**

Run: `nice -n 19 ionice -c 3 ninja -C _build-debug && meson test -C _build-debug`
Expected: all pass.

- [ ] **Step 4: Commit**

```bash
git add -A src tests
git commit -m "refactor(ctx): group connection, socket buffers, and TTY-side listener under ctx->sock

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: dial + answer + ctrl groups (three commits)

**Files:**
- Modify: `src/m2k_ctx.h`, rename across `src/*.c`, `tests/*.c`

**Interfaces:**
- Produces: `ctx->dial.result`, `ctx->dial.deadline`; `ctx->answer.fd`, `ctx->answer.deadline`, `ctx->answer.ring_next`; `ctx->ctrl.dtr`, `ctx->ctrl.rts`. Task 5 reads these.

- [ ] **Step 1: dial — struct edit + rename + test + commit**

Struct: replace `struct addrinfo *dial_result;` and `struct timeval dial_deadline;` with

```c
  struct
  {
    struct addrinfo *result;  /* getaddrinfo() head during non-blocking
                                   dial (M2K_STATE_DIAL); freed when the
                                   dial resolves or is aborted. */
    struct timeval deadline;
  } dial;
```

```python
RULES = [
  (r"->dial_result\b",   "->dial.result"),
  (r"->dial_deadline\b", "->dial.deadline"),
]
```

Verify no leftovers (`grep -rn -- "->dial_result\|->dial_deadline" src/ tests/` → empty), build + full test, then:

```bash
git add -A src tests
git commit -m "refactor(ctx): group non-blocking dial state under ctx->dial

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 2: answer — struct edit + rename + test + commit**

Struct: replace `int answer_fd;`, `struct timeval answer_deadline;`, `struct timeval ring_next;` with

```c
  struct
  {
    int fd;                   /* Line-side listener from m2k_setup_answer
                                   (ATA/RING answer path); -1 when unbound.
                                   Survives hangup; closed by m2k_free(). */
    struct timeval deadline;  /* S7 wait window for M2K_STATE_ANSWER. */
    struct timeval ring_next; /* Next RING emission while a caller waits. */
  } answer;
```

```python
RULES = [
  (r"->answer_fd\b",       "->answer.fd"),
  (r"->answer_deadline\b", "->answer.deadline"),
  (r"->ring_next\b",       "->answer.ring_next"),
]
```

Verify (`grep -rn -- "->answer_fd\|->answer_deadline\|->ring_next" src/ tests/` → empty), build + full test, then:

```bash
git add -A src tests
git commit -m "refactor(ctx): group answer-side listener state under ctx->answer

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 3: ctrl — struct edit + rename + test + commit**

Struct: replace `bool dtr;` and `bool rts;` with

```c
  struct
  {
    bool dtr, rts;            /* m2k_set_dtr()/m2k_set_rts() — DTE control
                                   lines. */
  } ctrl;
```

```python
RULES = [
  (r"->dtr\b", "->ctrl.dtr"),
  (r"->rts\b", "->ctrl.rts"),
]
```

Verify (`grep -rn -- "->dtr\b\|->rts\b" src/ tests/ | grep -v ctrl` → empty), build + full test, then:

```bash
git add -A src tests
git commit -m "refactor(ctx): group DTE control lines under ctx->ctrl

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: step + log groups (two commits; second closes #69)

**Files:**
- Modify: `src/m2k_ctx.h`, rename across `src/*.c`, `tests/*.c`

**Interfaces:**
- Produces: `ctx->step.state`, `ctx->step.cmdbuf`, `ctx->step.escape_req`, `ctx->step.quit_req`, `ctx->step.intr_armed`, `ctx->step.app_io`; `ctx->log.fn`, `ctx->log.userdata`, `ctx->log.err_buf`, `ctx->log.err_buf_size`, `ctx->log.force_verbose`. Task 5 reads these.

- [ ] **Step 1: step — struct edit + rename + test + commit**

Struct: replace `m2k_step_state step_state;`, `struct m2k_cmdbuf step_cmdbuf;`, `bool app_io;`, `bool escape_req;`, `bool quit_req;`, `bool intr_armed;` with

```c
  struct
  {
    m2k_step_state state;     /* Steppable event-loop state machine. */
    struct m2k_cmdbuf cmdbuf; /* Per-step persistent cmd-mode line buffer. */
    bool escape_req;          /* m2k_escape() — drives one online→cmd
                                   transition on the next m2k_step(). */
    bool quit_req;            /* atcmdPQ (at%q) — transition to DONE once
                                   ttyBufW drains. */
    bool intr_armed;          /* Double-Ctrl-C quit arming in CMD mode. */
    bool app_io;              /* m2k_setup_app_io() — host supplies/drains
                                   TTY bytes via m2k_{write_from,read_to}_app
                                   instead of read()/write() on tty fds. */
  } step;
```

```python
RULES = [
  (r"->step_state\b",  "->step.state"),
  (r"->step_cmdbuf\b", "->step.cmdbuf"),
  (r"->escape_req\b",  "->step.escape_req"),
  (r"->quit_req\b",    "->step.quit_req"),
  (r"->intr_armed\b",  "->step.intr_armed"),
  (r"->app_io\b",      "->step.app_io"),
]
```

Verify (`grep -rn -- "->step_state\|->step_cmdbuf\|->escape_req\|->quit_req\|->intr_armed\|->app_io" src/ tests/` → empty), build + full test, then:

```bash
git add -A src tests
git commit -m "refactor(ctx): group step-loop state and request flags under ctx->step

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 2: log — struct edit + rename + test + commit (closes #69)**

Struct: replace `m2k_log_fn log_fn;`, `void *log_userdata;`, `char *err_buf;`, `size_t err_buf_size;`, `bool force_verbose;` with

```c
  struct
  {
    m2k_log_fn fn;
    void *userdata;
    char *err_buf;            /* m2k_set_error_buffer() — caller-owned. */
    size_t err_buf_size;
    bool force_verbose;       /* m2k_set_force_verbose() — bypasses the
                                   AT%V mask in verboseOut/verbosePerror. */
  } log;
```

```python
RULES = [
  (r"->log_fn\b",        "->log.fn"),
  (r"->log_userdata\b",  "->log.userdata"),
  (r"->err_buf\b",       "->log.err_buf"),
  (r"->err_buf_size\b",  "->log.err_buf_size"),
  (r"->force_verbose\b", "->log.force_verbose"),
]
```

Verify (`grep -rn -- "->log_fn\|->log_userdata\|->err_buf\|->force_verbose" src/ tests/ | grep -v "log\."` → empty), build + full test, then:

```bash
git add -A src tests
git commit -m "refactor(ctx): group log callback, error buffer, and verbose override under ctx->log

Fixes #69

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: m2k_describe_state() (#70)

**Files:**
- Create: `tests/test_describe.c`, `examples/m2k_describe_state.c`
- Modify: `modemu2k.h` (prototype + docblock near `m2k_is_online`), `src/m2k_api.c` (implementation near the introspection predicates), `tests/meson.build` (+`'describe'`), `examples/meson.build` (+`'m2k_describe_state'`), `ChangeLog` (one line)

**Interfaces:**
- Consumes: the grouped fields from Tasks 1–4 (exact spellings in their Produces blocks) and the file-static `stepStateName()` in `src/m2k_api.c`.
- Produces: `M2K_API int m2k_describe_state(const m2k_t *ctx, char *buf, size_t cap)` — snprintf contract.

- [ ] **Step 1: Write the failing test**

Create `tests/test_describe.c`:

```c
/* m2k_describe_state() — content in known states + snprintf contract. */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>

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
}

static void
test_cmd_state_content(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char buf[512];
  int n = m2k_describe_state(ctx, buf, sizeof buf);
  assert(n > 0 && (size_t) n < sizeof buf);
  assert((size_t) n == strlen(buf));
  assert(strstr(buf, "state=CMD") != NULL);
  assert(strstr(buf, "carrier=no") != NULL);
  assert(strstr(buf, "answer_fd=-1") != NULL);
  assert(strstr(buf, "dial=idle") != NULL);
  /* four lines: exactly three newlines, none trailing */
  int nl = 0;
  for (char *p = buf; *p; p++) nl += (*p == '\n');
  assert(nl == 3);
  assert(buf[n - 1] != '\n');
  m2k_free(ctx);
}

static void
test_online_state_content(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  assert(m2k_setup_answer(ctx, "0") == M2K_OK);
  int client = socket(AF_INET, SOCK_STREAM, 0);
  assert(client >= 0);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = htons((unsigned short) answer_port(ctx));
  assert(connect(client, (struct sockaddr *) &addr, sizeof addr) == 0);

  char line[] = "ATA\r";
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, line, sizeof line - 1, &consumed) == M2K_OK);
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));

  char buf[512];
  int n = m2k_describe_state(ctx, buf, sizeof buf);
  assert(n > 0);
  assert(strstr(buf, "state=ONLINE") != NULL);
  assert(strstr(buf, "carrier=yes") != NULL);
  assert(strstr(buf, "alive=yes") != NULL);
  assert(strstr(buf, "app_io=yes") != NULL);

  close(client);
  m2k_free(ctx);
}

static void
test_snprintf_contract(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char full[512];
  int n = m2k_describe_state(ctx, full, sizeof full);
  assert(n > 0);
  /* size probe: cap == 0, buf NULL */
  assert(m2k_describe_state(ctx, NULL, 0) == n);
  /* truncation: NUL-terminated, same return */
  char tiny[8];
  memset(tiny, 'x', sizeof tiny);
  assert(m2k_describe_state(ctx, tiny, sizeof tiny) == n);
  assert(tiny[7] == '\0');
  assert(strncmp(tiny, full, 7) == 0);
  m2k_free(ctx);
}

int
main(void)
{
  test_cmd_state_content();
  test_online_state_content();
  test_snprintf_contract();
  return 0;
}
```

Register in `tests/meson.build` `test_cases` (alphabetical): add `'describe',` after `'connect',`.

- [ ] **Step 2: Run test to verify it fails**

Run: `nice -n 19 ionice -c 3 ninja -C _build-debug`
Expected: FAIL — `m2k_describe_state` undeclared.

- [ ] **Step 3: Implement**

`modemu2k.h`, after the `m2k_is_online` prototype's docblock block (near `m2k_has_carrier`):

```c
/**
 * @brief Write a human-readable snapshot of the modem's state into @p buf.
 *
 * Four newline-separated lines covering the step-machine state, carrier,
 * TTY fds and buffer fill, socket and buffer fill, listeners, ring count,
 * dial progress, control lines, and verbose mask. Intended for bug
 * reports and host-side debug dumps; the exact format may evolve between
 * versions and is not a parsing API.
 *
 * snprintf contract: returns the length the full description requires
 * (excluding the terminating NUL) regardless of @p cap; writes at most
 * @p cap - 1 bytes plus a NUL. @p cap == 0 (with @p buf NULL or not) is
 * a legal size probe.
 *
 * @param ctx Modem context.
 * @param buf Destination buffer (may be NULL when @p cap is 0).
 * @param cap Capacity of @p buf in bytes.
 * @return Length of the full description, excluding the NUL.
 *
 * @snippet examples/m2k_describe_state.c describe_state
 */
M2K_API int         m2k_describe_state(const m2k_t *ctx, char *buf, size_t cap);
```

`src/m2k_api.c`, after `m2k_is_online()`:

```c
int
m2k_describe_state(const m2k_t *ctx, char *buf, size_t cap)
{
  char tmp[512];
  int n = snprintf(tmp, sizeof tmp,
                   "state=%s carrier=%s app_io=%s\n"
                   "tty: rfd=%d wfd=%d owned=%s bufR=%zu/%zu bufW=%zu/%zu\n"
                   "sock: fd=%d alive=%s bufR=%zu/%zu bufW=%zu/%zu listen_fd=%d\n"
                   "line: answer_fd=%d rings(S1)=%u dial=%s dtr=%d rts=%d "
                   "verbose=0x%02x force=%s",
                   stepStateName(ctx->step.state),
                   ctx->sock.conn.alive ? "yes" : "no",
                   ctx->step.app_io ? "yes" : "no",
                   ctx->tty.rfd, ctx->tty.wfd,
                   ctx->tty.owned ? "yes" : "no",
                   (size_t) (ctx->tty.bufR.end - ctx->tty.bufR.ptr),
                   sizeof ctx->tty.bufR.buf,
                   (size_t) (ctx->tty.bufW.ptr - ctx->tty.bufW.top),
                   sizeof ctx->tty.bufW.buf,
                   ctx->sock.conn.fd,
                   ctx->sock.conn.alive ? "yes" : "no",
                   (size_t) (ctx->sock.bufR.end - ctx->sock.bufR.ptr),
                   sizeof ctx->sock.bufR.buf,
                   (size_t) (ctx->sock.bufW.ptr - ctx->sock.bufW.top),
                   sizeof ctx->sock.bufW.buf,
                   ctx->sock.listen_fd,
                   ctx->answer.fd, (unsigned) ctx->atcmd.s[1],
                   ctx->dial.result ? "in-progress" : "idle",
                   ctx->ctrl.dtr, ctx->ctrl.rts,
                   (unsigned) ctx->atcmd.pv,
                   ctx->log.force_verbose ? "yes" : "no");
  if (cap > 0 && buf != NULL)
  {
    size_t copy = (size_t) n < cap - 1 ? (size_t) n : cap - 1;
    memcpy(buf, tmp, copy);
    buf[copy] = '\0';
  }
  return n;
}
```

(`stepStateName` is file-static earlier in `m2k_api.c`; buffer member names `ptr`/`end`/`top`/`buf` match `m2k_write_from_app`/`m2k_read_to_app` usage. If a buffer-struct member differs, read `src/m2k_private.h` and use the actual names — the used/capacity math must mirror what `m2k_write_from_app` (residue) and `m2k_read_to_app` (available) compute.)

Create `examples/m2k_describe_state.c`:

```c
/* Compile-checked example for the m2k_describe_state() docblock. */
#include <stdio.h>
#include <modemu2k.h>

int
main(void)
{
  m2k_t *ctx = m2k_new();
  if (ctx == NULL)
    return 1;
  //! [describe_state]
  char state[512];
  m2k_describe_state(ctx, state, sizeof state);
  fprintf(stderr, "modem state:\n%s\n", state);   /* paste into bug reports */
  //! [describe_state]
  m2k_free(ctx);
  return 0;
}
```

Add `'m2k_describe_state',` to `example_cases` in `examples/meson.build` (alphabetical: after `'m2k_dial',`).

`ChangeLog`, new bullet at the top of the `(in-progress)` stanza:

```
  * Add m2k_describe_state() — one-call state snapshot for bug reports
    and host-side debugging
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `nice -n 19 ionice -c 3 ninja -C _build-debug && meson test -C _build-debug`
Expected: all pass, including the new `test_describe`.

- [ ] **Step 5: Commit**

```bash
git add modemu2k.h src/m2k_api.c tests/test_describe.c tests/meson.build examples/m2k_describe_state.c examples/meson.build ChangeLog
git commit -m "feat(api): add m2k_describe_state() introspection snapshot

Fixes #70

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```
