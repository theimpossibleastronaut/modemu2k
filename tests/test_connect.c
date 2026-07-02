/* Covers the deprecated synchronous m2k_dial(). Keep while m2k_dial ships;
   remove (or port to the m2k_step path) when it is removed in 0.3.0. */
#include "test.h"
#include "test_helpers.h"
#include <stdio.h>

static void
test_connect(void)
{
  int port;
  assert(start_loopback_listener(AF_INET, &port) == 0);

  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  m2k_atcmd(ctx, "ATS7=20");
  m2k_atcmd(ctx, "AT%D1");  /* disable dial canceling — stdin is not a tty */

  char port_s[8];
  snprintf(port_s, sizeof(port_s), "%d", port);

  /* m2k_dial is blocking: it returns M2K_OK only once connected, so no
     wait is needed before hanging up. "localhost" resolves via
     /etc/hosts / nsswitch — no public DNS lookup, so this works in
     sandboxed builds. */
  assert(m2k_dial(ctx, "localhost", port_s) == M2K_OK);
  assert(m2k_hangup(ctx) == M2K_OK);

  assert(m2k_dial(ctx, "127.0.0.1", port_s) == M2K_OK);
  assert(m2k_hangup(ctx) == M2K_OK);

  m2k_free(ctx);
}

int
main(void)
{
  test_connect();
  return 0;
}
