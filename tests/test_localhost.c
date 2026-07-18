/* NOTE: uses the deprecated synchronous m2k_dial(). When 0.3.0 removes
   it, port this to the step API — the getaddrinfo multi-address
   fallback under test must survive the removal. */

#include "test.h"
#include "test_helpers.h"
#include <stdio.h>

static m2k_t *ctx;
static int server_port = -1;

static void
setup(void)
{
  m2k_atcmd(ctx, "ATS7=5");
  m2k_atcmd(ctx, "AT%D1");
}

static void
test_localhost_fallback(void)
{
  char dialport[8];
  snprintf(dialport, sizeof(dialport), "%d", server_port);
  assert(m2k_dial(ctx, "localhost", dialport) == M2K_OK);
  assert(m2k_hangup(ctx) == M2K_OK);
}

int
main(void)
{
  /* Listen on 127.0.0.1 only — not ::1 — so connecting via "localhost"
     (which resolves to ::1 first on dual-stack systems) must fall through
     to the IPv4 address to succeed. */
  if (start_loopback_listener(AF_INET, &server_port) < 0)
    return 77;
  ctx = m2k_new();
  assert(ctx != NULL);
  setup();
  test_localhost_fallback();
  m2k_free(ctx);
  return 0;
}
