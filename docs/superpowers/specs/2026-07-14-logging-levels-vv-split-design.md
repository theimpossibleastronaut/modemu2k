# Logging levels, repeatable -v, and the m2k_api.c split — design

Issues: #72 and #71 (sub-issues of tracking parent #73), #64.
Date: 2026-07-14
Status: executed under /goal; -vvv explicitly excluded by Andy.

## Order and rationale

#72 → #64 → #71, per the cross-reference comments on #64/#72: the
level-vs-mask design decision (#72) had to precede #64 so `-vv` maps to
a level instead of minting stopgap mask bits; #71 ran last so the file
split moves final code.

## #72 — severity levels (`m2k_set_log_level`)

Two-knob model, both preserved:

- **Severity threshold** (`ctx->log.level`, new): ERROR < WARN < INFO <
  DEBUG < TRACE. Default INFO. Public `m2k_set_log_level()` /
  `m2k_get_log_level()`. Lives outside the Hayes-visible registers, so
  ATZ cannot reset it.
- **`AT%V` category mask** (existing, unchanged): can still open
  individual DEBUG/TRACE categories (MISC=1, TELOPT=2, new BYTES=4)
  when the threshold would hide them. `m2k_set_force_verbose()` remains
  as the mask bypass, unchanged.

Delivery rules:

- Tagged messages (`m2k_log`, now `m2k_log(ctx, level, fmt, ...)`,
  internal-only symbol): deliver when `level <= ctx->log.level`.
  Site classification: syscall/hard failures ERROR; peer-behavior
  conditions (recv/send failures mid-session) and buffer overruns WARN;
  "Listening on port" INFO.
- Category messages (`verboseOut`/`verbosePerror`): deliver when
  `(pv & mask) || force_verbose || level >= (BYTES ? TRACE : DEBUG)`.
- `m2k_err_set`: always delivers (error path by construction).

**No breaking changes**: additions are API-additive; every previously
unconditional message is tagged ≤ INFO = still emitted at the default
threshold; the verboseOut gate is a strict superset of the old one.

## #64 — repeatable -v (no -vvv)

- `cmdarg.verbose` is a counter. `-v` → `M2K_LOG_DEBUG`; `-vv` (or
  more) → `M2K_LOG_TRACE`. `-v` output is identical to the previous
  behavior; `-vv` is additive.
- New `VERB_BYTES` (AT%V4) narration sites: sockbuf recv/send byte
  counts, ttybuf read/write byte counts, `+++` escape-detector arming.
- `-vvv`/hexdump from the original issue: **dropped** per Andy — rare
  need, not warranted for this program.
- Docs: usage text and man page `-v` stanza updated; test_verbose.sh
  gains a `-vv` shows / `-v` hides byte-narration pair.

## #71 — split m2k_api.c (closes #73)

Pure internal reorganization, no API/ABI/behavior change:

- `src/m2k_step.c` — relay loops (sockReadLoop/ttyReadLoop/escSeq/
  lineBuf), command-mode input, app-IO data path, poll-set builders,
  per-state iterators, `m2k_get_pollfds`/`m2k_step`/`m2k_run`,
  `m2k_hangup`/`m2k_escape`, predicates, `m2k_describe_state`,
  `putTtyCmdstat`, and the deprecated-path `onlineMode` (non-static
  now; `m2k_online` in m2k_api.c still calls it).
- `src/m2k_setup.c` — `getPtyMaster` + the `m2k_setup_*` family +
  `m2k_get_listen_fd`/`m2k_get_answer_fd`.
- `src/m2k_api.c` — lifecycle (`m2k_new`/`m2k_free`), log/error-buffer
  setters, `m2k_atcmd`, deprecated `m2k_dial`/`m2k_online`,
  `m2k_version`/`m2k_strerror`, control-line and logging getters/
  setters.
- Cross-file internals declared in `m2k_private.h`: `onlineMode`,
  `cmdBufReset`.

## Verification

- New `tests/test_log_level.c`: delivery matrix (default INFO; ERROR
  threshold hides INFO but never m2k_err_set; DEBUG opens MISC/TELOPT
  not BYTES; TRACE opens BYTES; AT%V mask opens categories below
  threshold; force_verbose bypasses mask).
- `tests/test_verbose.sh`: `-vv` emits "tty: read" byte narration; `-v`
  must not. Existing ATZ-survival checks unchanged and passing.
- Full ASan/UBSan suite green after each of the three commits;
  unit suite green under --repeat 3 after the split.
