#include "test.h"
#include <signal.h>
#include <stdio.h>

/* Separate port from test_listen.c to avoid conflicts when both run. */
#define TEST_PORT "19877"

static pid_t connector_pid = -1;

static void
stop_connector(void)
{
  if (connector_pid > 0)
    kill(connector_pid, SIGTERM);
}

/* Check that dual-stack actually works: m2k_sockListen tries AF_INET6 with
   IPV6_V6ONLY=0 and falls back to IPv4 if the OS refuses (e.g. OpenBSD).
   Replicate that probe here so we skip rather than time out. */
static int
has_dualstack(void)
{
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return 0;
#ifdef IPV6_V6ONLY
  int zero = 0;
  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
  int v6only = 1;
  socklen_t vlen = sizeof v6only;
  getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &vlen);
  if (v6only != 0)
  {
    close(fd);
    return 0;
  }
#endif
  close(fd);
  return 1;
}

/* Fork a child that connects to the listen port via IPv6 loopback. */
static void
start_connector(void)
{
  connector_pid = fork();
  if (connector_pid == 0)
  {
    usleep(100000);   /* 100ms — enough for parent to reach bind/listen */
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0)
      _exit(1);
    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_port   = htons(atoi(TEST_PORT));
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);
    connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    usleep(100000);
    close(fd);
    _exit(0);
  }
  atexit(stop_connector);
}

static void
test_setup_listen_ipv6(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  assert(m2k_setup_listen(ctx, TEST_PORT) == M2K_OK);
  assert(m2k_listen_accept(ctx) == M2K_OK);
  m2k_free(ctx);
}

int
main(void)
{
  if (!has_dualstack())
    return 77;

  start_connector();
  if (connector_pid < 0)
    return 77;

  test_setup_listen_ipv6();
  return 0;
}
