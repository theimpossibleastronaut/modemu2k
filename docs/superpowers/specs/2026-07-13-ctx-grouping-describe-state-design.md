# Context-struct grouping + m2k_describe_state() — design

Issues: #69 (grouping), #70 (introspection helper); sub-issues of tracking parent #73.
Date: 2026-07-13
Status: negotiables approved by Andy (grouping shape, API contract); executed under /goal

## Scope

Two changes, sequenced, one branch (`ctx-grouping`):

1. **#69** — group the ~30 flat fields of `struct m2k_s` (`src/m2k_ctx.h`,
   private) into named subsystem substructs. Pure internal refactor: no
   behavior change, no ABI change (consumers only see the opaque `m2k_t`).
2. **#70** — new public `m2k_describe_state()` returning a multi-line
   state snapshot, implemented by walking the grouped fields.

Deferred (per tracking-parent discussion): #71 (file split — wait for the
0.3.0 deprecation sweep), #72 (logging levels — needs its own design
pass, interacts with #64).

## #69 — approved struct layout

One-level groups; `st_tty`'s members are absorbed into the `tty` group so
the hottest access pattern (`ctx->tty.rfd`, 34 sites) does not change.
`ctx->sock` becomes a group whose `conn` member stays an `st_sock`, so
the many functions taking `st_sock *` keep working via `&ctx->sock.conn`.

```c
struct m2k_s {
  Atcmd atcmd, atcmdNV;      /* flat: lexer touches directly */
  struct {                    /* ctx->tty.* */
    int rfd, wfd;             /* absorbed st_tty: ctx->tty.rfd unchanged */
    bool owned;               /* was tty_owned */
    struct st_ttyBufR bufR;   /* was ttyBufR */
    struct st_ttyBufW bufW;   /* was ttyBufW */
  } tty;
  struct {                    /* ctx->sock.* */
    st_sock conn;             /* was sock */
    struct st_sockBufR bufR;  /* was sockBufR */
    struct st_sockBufW bufW;  /* was sockBufW */
    int listen_fd;            /* was listen_fd (TTY-side listener) */
  } sock;
  struct st_telOpt telOpt;    /* unchanged */
  struct {                    /* ctx->dial.* */
    struct addrinfo *result;  /* was dial_result */
    struct timeval deadline;  /* was dial_deadline */
  } dial;
  struct {                    /* ctx->answer.* */
    int fd;                   /* was answer_fd */
    struct timeval deadline;  /* was answer_deadline */
    struct timeval ring_next; /* was ring_next */
  } answer;
  struct {                    /* ctx->step.* */
    m2k_step_state state;     /* was step_state */
    struct m2k_cmdbuf cmdbuf; /* was step_cmdbuf */
    bool escape_req, quit_req, intr_armed;
    bool app_io;
  } step;
  struct { bool dtr, rts; } ctrl;
  struct {                    /* ctx->log.* */
    m2k_log_fn fn;            /* was log_fn */
    void *userdata;           /* was log_userdata */
    char *err_buf;            /* was err_buf */
    size_t err_buf_size;      /* was err_buf_size */
    bool force_verbose;       /* was force_verbose */
  } log;
  char slave_path[64];        /* unchanged */
  struct m2k_escseq escSeq;   /* unchanged */
  struct m2k_linebuf lineBuf; /* unchanged */
};
```

Notes:

- `struct st_tty` is removed if nothing else uses it after absorption
  (verify at implementation time; a `grep -rn "st_tty"` decides).
- Wide mechanical rename across `src/*.c`, `src/*.h`, `src/cmdlex.l`,
  and the white-box tests (`tests/test_*.c` touch `ctx->sock.alive`,
  `ctx->ttyBufW`, `ctx->atcmd.s[...]` — the last is unchanged).
- Executed as **one commit per group** (tty, sock, dial, answer, step,
  ctrl, log) so each diff is a single mechanical rename; the full test
  suite must pass after every commit.
- No ChangeLog entry (pure refactor). Final commit body: `Fixes #69`.

## #70 — approved API contract

```c
M2K_API int m2k_describe_state(const m2k_t *ctx, char *buf, size_t cap);
```

- **snprintf semantics**: returns the length the full description needs
  (excluding the terminating NUL) regardless of `cap`; writes at most
  `cap - 1` bytes plus NUL; `buf` is always NUL-terminated when
  `cap > 0`; `cap == 0` (with any `buf`, including NULL) is a legal
  size probe. No error return — a valid `ctx` always describes.
- **Output**: four `\n`-separated lines (no trailing newline), walking
  the grouped fields:

```
state=ONLINE carrier=yes app_io=no
tty: rfd=5 wfd=5 owned=yes bufR=42/1024 bufW=0/4096
sock: fd=7 alive=yes bufR=0/2048 bufW=0/2048 listen_fd=-1
line: answer_fd=6 rings(S1)=2 dial=idle dtr=1 rts=1 verbose=0x03 force=no
```

  Field notes: `state` uses the existing `stepStateName()` strings;
  buffer numbers are used/capacity in bytes; `dial` shows `idle` or
  `in-progress`; `verbose` is the `AT%V` mask (`ctx->atcmd.pv`) in hex;
  `force` is the force_verbose flag. Exact widths/order may be tuned in
  implementation, but every field listed above must appear and the
  line count is fixed at four (stable enough to grep, not a parse API —
  the docblock says the format may evolve between versions).
- **Placement**: implemented in `src/m2k_api.c` next to the other
  introspection predicates; declared in `modemu2k.h` near
  `m2k_is_online()` with a full docblock and `@snippet`.
- **Example**: `examples/m2k_describe_state.c` (house compile-check
  pattern; one line added to `example_cases`).
- **Tests** (`tests/test_answer.c` is the wrong home; new
  `tests/test_describe.c` + meson registration):
  - CMD-state fresh ctx: contains `state=CMD`, `carrier=no`.
  - ONLINE via the answer path (reuse the app_io + loopback pattern):
    contains `state=ONLINE`, `carrier=yes`, `alive=yes`.
  - Contract: return value equals `strlen` of the full string when
    `cap` is ample; same value returned with `cap == 0`; truncated call
    (`cap == 8`) still NUL-terminates and returns the full length.
- **ChangeLog**: one line (new API). Commit body: `Fixes #70`.

## Sequencing and verification

1. `git checkout -b ctx-grouping` from current master.
2. #69 group-by-group commits; `meson test -C _build-debug` green after
   each; final commit closes #69.
3. #70 TDD (failing test → implement → pass); closes #70.
4. Full suite + the `-a`/minicom smoke path must behave identically
   (spot-check `test_answer` exercises the renamed fields heavily).
