/* Back-pressure in telnet line mode.

   ttyReadLoop() has three branches: raw (AT%R1), telnet binary, and
   line mode. The first two stop pulling from the DTE-side buffer while
   sockBufWReady() is false, which is what makes m2k_write_from_app()
   report M2K_ERR_WOULDBLOCK to the host. Line mode is the default when
   the peer negotiates nothing, and it is also the branch that can
   double its input: every CR emits CRLF, so a full 1024-byte DTE
   buffer becomes 2048 socket bytes -- twice the headroom putSock1()
   keeps between the soft limit and the hard one. */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include "test_helpers.h"
#include <poll.h>
#include <string.h>

static char logbuf[8192];
static size_t loglen;

static void
log_cb(const char *msg, void *userdata)
{
  (void) userdata;
  size_t n = strlen(msg);
  if (loglen + n >= sizeof logbuf)
    return;
  memcpy(logbuf + loglen, msg, n);
  loglen += n;
  logbuf[loglen] = '\0';
}

/* A peer that accepts the call and then never reads, with a small
   receive buffer so the socket saturates in a few KB instead of the
   hundreds the kernel would otherwise absorb. SO_RCVBUF has to be set
   before connect() for the advertised window to shrink. */
static int
connect_deaf_peer(int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  int sz = 2048;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = htons((unsigned short) port);
  assert(connect(fd, (struct sockaddr *) &addr, sizeof addr) == 0);
  return fd;
}

/* test_step() waits up to 100ms per call. Once the socket is stopped
   nothing becomes readable, so that wait would dominate the loop
   below; poll with no timeout instead. */
static void
step_fast(m2k_t *ctx)
{
  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int timeout_ms;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
  if (nfds > 0)
    poll(fds, nfds, 0);
  assert(m2k_step(ctx, fds, nfds) == M2K_OK);
}

static void
test_line_mode_stops_draining_dte(void)
{
  char junk[1024];
  size_t got;

  loglen = 0;
  logbuf[0] = '\0';

  m2k_t *ctx = m2k_new();
  assert(ctx);
  m2k_set_log_fn(ctx, log_cb, NULL);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  assert(m2k_atcmd(ctx, "ATS0=1") == M2K_OK); /* answer on the first ring */
  assert(m2k_setup_answer(ctx, "0") == M2K_OK);

  int peer = connect_deaf_peer(test_local_port(m2k_get_answer_fd(ctx)));

  for (int i = 0; i < 200 && !m2k_is_online(ctx); i++)
  {
    test_step(ctx);
    m2k_read_to_app(ctx, junk, sizeof junk, &got);
  }
  assert(m2k_is_online(ctx));
  /* The peer answered no telnet negotiation, so this is the line-mode
     branch rather than binary. */
  assert(!ctx->telOpt.sgasend);
  assert(!ctx->atcmd.pr);

  int sz = 2048;
  setsockopt(ctx->sock.conn.fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);

  char line[256];
  for (size_t i = 0; i < sizeof line; i += 2)
  {
    line[i] = 'x';
    line[i + 1] = '\r';
  }

  int blocked = 0;
  for (int i = 0; i < 400 && !blocked; i++)
  {
    size_t consumed = 0;
    if (m2k_write_from_app(ctx, line, sizeof line, &consumed) == M2K_ERR_WOULDBLOCK)
      blocked = 1;
    step_fast(ctx);
    m2k_read_to_app(ctx, junk, sizeof junk, &got);
  }

  /* 400 * 256 bytes is 100KB into a socket nobody reads. Without
     back-pressure the host is never told to stop, and the excess is
     dropped inside putSock1() instead. */
  assert(blocked);
  assert(strstr(logbuf, "overrun") == NULL);

  close(peer);
  m2k_free(ctx);
}

int
main(void)
{
  test_line_mode_stops_draining_dte();
  return 0;
}
