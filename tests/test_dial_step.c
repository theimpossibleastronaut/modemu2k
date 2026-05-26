/* Exercises the step-mode (non-blocking) dial path:
   m2k_step on CMDST_ATD must not block on connect(); it should either
   complete immediately (local loopback usually does) or transition to
   the DIAL state and resolve via subsequent m2k_step calls.

   Also verifies m2k_hangup can abort a dial-in-progress without
   waiting for the S7 timeout. */

#include "test.h"
#include "../modemu2k.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int
bind_listener(int *port_out)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = 0;
  assert(bind(fd, (struct sockaddr *) &addr, sizeof addr) == 0);
  assert(listen(fd, 5) == 0);
  socklen_t len = sizeof addr;
  getsockname(fd, (struct sockaddr *) &addr, &len);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static void
push_atd(m2k_t *ctx, const char *host, int port)
{
  char line[128];
  int n = snprintf(line, sizeof line, "ATD\"%s %d\"\r", host, port);
  assert(n > 0 && (size_t) n < sizeof line);
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, line, (size_t) n, &consumed) == M2K_OK);
  assert(consumed == (size_t) n);
}

static int
step_once(m2k_t *ctx)
{
  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int timeout_ms;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
  /* Cap timeout to make the test snappy. */
  if (timeout_ms < 0 || timeout_ms > 100) timeout_ms = 100;
  if (nfds > 0)
    poll(fds, nfds, timeout_ms);
  assert(m2k_step(ctx, fds, nfds) == M2K_OK);
  return 0;
}

static void
test_step_dial_reaches_online(void)
{
  int port;
  int srv = bind_listener(&port);

  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  m2k_atcmd(ctx, "ATS7=5");
  m2k_atcmd(ctx, "AT%D1");        /* disable +++/keypress dial cancel */

  push_atd(ctx, "127.0.0.1", port);

  /* Drive m2k_step until ONLINE. Each step must return quickly. */
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);

  assert(m2k_is_online(ctx));
  assert(m2k_has_carrier(ctx));

  /* Server-side accept proves connect actually landed. */
  int conn = accept(srv, NULL, NULL);
  assert(conn >= 0);
  close(conn);

  m2k_hangup(ctx);
  m2k_free(ctx);
  close(srv);
}

static void
test_step_dial_no_blocking(void)
{
  /* Dial an address on a port nobody answers on a routable but firewalled
     subnet. RFC 5737 TEST-NET-1 (192.0.2.0/24) is documentation-only and
     usually drops/rejects, so connect() goes EINPROGRESS without ever
     completing — m2k_step must come back immediately, not stall. */
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  m2k_atcmd(ctx, "ATS7=30");
  m2k_atcmd(ctx, "AT%D1");

  push_atd(ctx, "192.0.2.1", 1);

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  step_once(ctx);
  clock_gettime(CLOCK_MONOTONIC, &t1);

  double elapsed = (t1.tv_sec - t0.tv_sec) +
                   (t1.tv_nsec - t0.tv_nsec) / 1e9;
  /* The step_once helper caps timeout_ms at 100; total must be well
     under the S7=30 second synchronous blocking window. */
  assert(elapsed < 1.0);

  /* Aborting a dial-in-progress should be immediate. */
  assert(m2k_hangup(ctx) == M2K_OK);
  assert(!m2k_is_online(ctx));
  assert(!m2k_has_carrier(ctx));

  m2k_free(ctx);
}

int
main(void)
{
  test_step_dial_reaches_online();
  test_step_dial_no_blocking();
  return 0;
}
