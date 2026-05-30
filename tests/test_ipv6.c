#include "test.h"
#include "test_helpers.h"
#include <stdio.h>

static int
has_ipv6(void)
{
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

int
main(void)
{
  if (!has_ipv6())
    return 77;

  int port;
  if (start_loopback_listener(AF_INET6, &port) < 0)
    return 77;

  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  m2k_atcmd(ctx, "ATS7=20");
  m2k_atcmd(ctx, "AT%D1");

  char port_s[8];
  snprintf(port_s, sizeof(port_s), "%d", port);
  assert(m2k_dial(ctx, "::1", port_s) == M2K_OK);
  sleep(1);
  assert(m2k_hangup(ctx) == M2K_OK);

  m2k_free(ctx);
  return 0;
}
