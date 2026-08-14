/* Socket output queued in ONLINE must still reach the peer after the
   state machine leaves ONLINE with the connection alive (issue #85).

   A +++ escape does not hang up: the call is still there, and ATO
   resumes it. Anything the DTE handed over before the escape is owed
   to the peer. Only ONLINE ever drained the socket write buffer, so
   CMD has to keep draining it while the carrier is up. */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include "test_helpers.h"
#include <string.h>


/* Drain whatever the peer has, without blocking. */
static size_t
drain_peer(int fd)
{
  char buf[4096];
  size_t total = 0;
  for (;;)
  {
    ssize_t n = recv(fd, buf, sizeof buf, MSG_DONTWAIT);
    if (n <= 0)
      return total;
    total += (size_t) n;
  }
}

/* Step until the modem's queue is empty and the peer has seen every
   byte. m2k_pending_to_line() reaching zero says the bytes left
   modemu2k, not that they arrived -- the rest of the trip belongs to
   the kernel.

   Wait on the byte count rather than polling a fixed number of times.
   An earlier version polled 50 times, which outlasted loopback
   delivery on Linux and not on FreeBSD or OpenBSD. Measuring showed
   one poll landing after the queue empties on Linux too, so the
   assumption was wrong everywhere and only Linux was fast enough to
   hide it. */
static size_t
flush_to_peer(m2k_t *ctx, int peer, size_t want)
{
  size_t got = 0;

  for (int i = 0; i < 4000 && (m2k_pending_to_line(ctx) > 0 || got < want); i++)
  {
    got += drain_peer(peer);
    test_step_nowait(ctx);
    if (got < want)
      usleep(500);
  }
  assert(m2k_pending_to_line(ctx) == 0);
  return got;
}

static m2k_t *
online_ctx(int *peer_out)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  assert(m2k_atcmd(ctx, "AT%R1") == M2K_OK); /* raw: byte counts stay exact */
  assert(m2k_atcmd(ctx, "ATS0=1") == M2K_OK);
  assert(m2k_setup_answer(ctx, "0") == M2K_OK);

  int peer = test_connect_slow_peer(test_local_port(m2k_get_answer_fd(ctx)));
  char junk[512];
  size_t got;
  for (int i = 0; i < 200 && !m2k_is_online(ctx); i++)
  {
    test_step(ctx);
    m2k_read_to_app(ctx, junk, sizeof junk, &got);
  }
  assert(m2k_is_online(ctx));

  int sz = 2048;
  setsockopt(ctx->sock.conn.fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
  *peer_out = peer;
  return ctx;
}

/* Push until the socket backs up, so bytes are demonstrably still
   queued when the escape happens rather than already sent. Returns how
   many the modem accepted, which is what the peer must eventually
   receive. */
static size_t
fill_queue(m2k_t *ctx, char fill)
{
  char block[256];
  memset(block, fill, sizeof block);

  size_t written = 0;
  char junk[512];
  size_t got;
  for (int i = 0; i < 400 && m2k_pending_to_line(ctx) < 2048; i++)
  {
    size_t consumed = 0;
    if (m2k_write_from_app(ctx, block, sizeof block, &consumed) == M2K_OK)
      written += consumed;
    test_step_nowait(ctx);
    m2k_read_to_app(ctx, junk, sizeof junk, &got);
  }
  assert(m2k_pending_to_line(ctx) > 0);
  return written;
}

static void
test_escape_still_sends_queued_bytes(void)
{
  int peer;
  m2k_t *ctx = online_ctx(&peer);

  size_t written = fill_queue(ctx, 'x');

  /* Escape to CMD. The call is still up. */
  assert(m2k_escape(ctx) == M2K_OK);
  for (int i = 0; i < 20 && m2k_is_online(ctx); i++)
    test_step_nowait(ctx);
  assert(!m2k_is_online(ctx));
  assert(m2k_has_carrier(ctx));         /* +++ does not hang up */
  assert(m2k_pending_to_line(ctx) > 0); /* owed, not thrown away */

  /* The peer starts reading. CMD must keep flushing until the count
     reaches zero by sending. */
  assert(flush_to_peer(ctx, peer, written) == written);

  close(peer);
  m2k_free(ctx);
}

/* ATO returns to the same call, so the queue it left behind is still
   owed to the peer. Resetting the socket buffers on every entry to
   ONLINE was right for a fresh connection and wrong for this one. */
static void
test_ato_resume_keeps_queued_bytes(void)
{
  int peer;
  m2k_t *ctx = online_ctx(&peer);

  size_t written = fill_queue(ctx, 'y');

  assert(m2k_escape(ctx) == M2K_OK);
  for (int i = 0; i < 20 && m2k_is_online(ctx); i++)
    test_step_nowait(ctx);
  assert(!m2k_is_online(ctx));

  size_t consumed = 0;
  char junk[512];
  size_t got;
  assert(m2k_write_from_app(ctx, "ATO\r", 4, &consumed) == M2K_OK);
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
  {
    test_step_nowait(ctx);
    m2k_read_to_app(ctx, junk, sizeof junk, &got);
  }
  assert(m2k_is_online(ctx));

  assert(flush_to_peer(ctx, peer, written) == written);

  close(peer);
  m2k_free(ctx);
}

/* ATH and ATZ close the connection from inside CMD, with no state
   transition to hang the cleanup on. The queue has nowhere to go then,
   so the count must still fall to zero or a host watching it for a
   transmit-empty bit waits forever. */
static void
test_hangup_in_cmd_clears_the_queue(void)
{
  int peer;
  m2k_t *ctx = online_ctx(&peer);

  fill_queue(ctx, 'z');
  assert(m2k_escape(ctx) == M2K_OK);
  for (int i = 0; i < 20 && m2k_is_online(ctx); i++)
    test_step_nowait(ctx);
  assert(m2k_pending_to_line(ctx) > 0);

  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, "ATH\r", 4, &consumed) == M2K_OK);
  for (int i = 0; i < 50 && m2k_has_carrier(ctx); i++)
    test_step_nowait(ctx);
  assert(!m2k_has_carrier(ctx));
  /* Zero in the same step that closed the connection, with no extra
     step needed to sweep up after it. */
  assert(m2k_pending_to_line(ctx) == 0);

  close(peer);
  m2k_free(ctx);
}

int
main(void)
{
  test_escape_still_sends_queued_bytes();
  test_ato_resume_keeps_queued_bytes();
  test_hangup_in_cmd_clears_the_queue();
  return 0;
}
