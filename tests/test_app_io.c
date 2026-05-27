#include "test.h"
#include <poll.h>
#include <stddef.h>
#include <string.h>

static int
contains(const char *hay, size_t haylen, const char *needle)
{
  size_t nlen = strlen(needle);
  if (nlen > haylen) return 0;
  for (size_t i = 0; i + nlen <= haylen; i++)
    if (memcmp(hay + i, needle, nlen) == 0)
      return 1;
  return 0;
}

static void
test_at_round_trip(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);

  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, "AT\r", 3, &consumed) == M2K_OK);
  assert(consumed == 3);

  char drained[512];
  size_t total = 0;
  for (int i = 0; i < 32; i++)
  {
    struct pollfd fds[M2K_MAX_POLLFDS];
    size_t nfds = M2K_MAX_POLLFDS;
    int timeout_ms;
    assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
    assert(m2k_step(ctx, fds, nfds) == M2K_OK);

    size_t n = 0;
    assert(m2k_read_to_app(ctx, drained + total, sizeof(drained) - total, &n) == M2K_OK);
    total += n;
    if (contains(drained, total, "OK"))
      break;
  }
  assert(contains(drained, total, "OK"));
  m2k_free(ctx);
}

static void
test_multi_line_in_one_write(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);

  const char *batch = "ATZ\rAT\r";
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, batch, strlen(batch), &consumed) == M2K_OK);
  assert(consumed == strlen(batch));

  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int timeout_ms;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
  assert(m2k_step(ctx, fds, nfds) == M2K_OK);

  char drained[512];
  size_t total = 0;
  for (int i = 0; i < 8; i++)
  {
    size_t n = 0;
    assert(m2k_read_to_app(ctx, drained + total, sizeof(drained) - total, &n) == M2K_OK);
    total += n;
    nfds = M2K_MAX_POLLFDS;
    assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
    assert(m2k_step(ctx, fds, nfds) == M2K_OK);
  }
  size_t hits = 0;
  for (size_t i = 0; i + 1 < total; i++)
    if (drained[i] == 'O' && drained[i+1] == 'K')
      hits++;
  assert(hits >= 2);
  m2k_free(ctx);
}

static void
test_full_buffer_returns_full(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);

  char big[8192];
  memset(big, 'x', sizeof(big));

  for (int i = 0; i < 16; i++)
  {
    size_t consumed = 0;
    m2k_err_t r = m2k_write_from_app(ctx, big, sizeof(big), &consumed);
    if (r == M2K_ERR_WOULDBLOCK)
    {
      assert(consumed == 0);
      m2k_free(ctx);
      return;
    }
    assert(r == M2K_OK);
  }
  assert(0 && "expected M2K_ERR_WOULDBLOCK before 128KB consumed");
}

/* The drain-pattern test for m2k_has_pending_output(). Without the
   predicate, a host doing a tiny-buffer drain has no precise way to
   know whether more bytes are queued — only a heuristic ("did I get
   back as many bytes as I asked for"). This test pins the predicate's
   contract: true iff a subsequent read_to_app would return data. */
static void
test_has_pending_output(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);

  /* Off by default — context is fresh, no output queued. */
  assert(m2k_has_pending_output(ctx) == 0);

  /* Push an AT command. The lexer's local echo (default ATE1) and the
     OK response together produce TTY-bound bytes after a step. */
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, "ATZ\r", 4, &consumed) == M2K_OK);
  assert(consumed == 4);

  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int t;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &t) == M2K_OK);
  assert(m2k_step(ctx, fds, nfds) == M2K_OK);

  /* After the step, output is queued. */
  assert(m2k_has_pending_output(ctx) != 0);

  /* Drain in tiny chunks and verify the predicate stays true until the
     final read empties the buffer. */
  char buf[4];
  for (;;) {
    size_t got = 0;
    assert(m2k_read_to_app(ctx, buf, sizeof buf, &got) == M2K_OK);
    if (got == 0)
      break;
    /* If we got fewer than requested, that's the last chunk — the
       predicate must now report empty. */
    if (got < sizeof buf) {
      assert(m2k_has_pending_output(ctx) == 0);
      break;
    }
    /* Full chunk: more *might* be pending. Predicate tells the truth. */
    int pending = m2k_has_pending_output(ctx);
    /* If predicate says no pending, next read returns 0. If yes, > 0. */
    size_t peek = 0;
    char tmp[4];
    assert(m2k_read_to_app(ctx, tmp, sizeof tmp, &peek) == M2K_OK);
    if (pending) {
      assert(peek > 0);
      /* Put nothing back — m2k_read_to_app is destructive, but we've
         verified the predicate agreed with reality for this iteration. */
    } else {
      assert(peek == 0);
      break;
    }
  }

  /* Now empty. */
  assert(m2k_has_pending_output(ctx) == 0);

  m2k_free(ctx);
}

/* Outside app-I/O mode, has_pending_output must return 0 unconditionally
   (the function isn't meaningful there). */
static void
test_has_pending_output_wrong_mode(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  /* No m2k_setup_app_io call. */
  assert(m2k_has_pending_output(ctx) == 0);
  m2k_free(ctx);
}

/* Verify m2k_escape() is a no-op outside online mode. The full
   online→cmd escape path is exercised indirectly by test_connect; we
   just confirm the API doesn't crash or misbehave in the easy case. */
static void
test_escape_in_cmd_mode(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);

  /* Starts in cmd mode — escape should be a no-op, return OK. */
  assert(m2k_escape(ctx) == M2K_OK);

  /* Cmd mode still works afterwards. */
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, "AT\r", 3, &consumed) == M2K_OK);
  assert(consumed == 3);

  m2k_free(ctx);
}

int
main(void)
{
  test_at_round_trip();
  test_multi_line_in_one_write();
  test_full_buffer_returns_full();
  test_has_pending_output();
  test_has_pending_output_wrong_mode();
  test_escape_in_cmd_mode();
  return 0;
}
