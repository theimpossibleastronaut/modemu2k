#include "test.h"
#include <signal.h>
#include <stdio.h>

static m2k_t *ctx;
static pid_t server_pid = -1;
static int server_port = -1;

static void
stop_server(void)
{
  if (server_pid > 0)
    kill(server_pid, SIGTERM);
}

static void
start_server(void)
{
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in6 addr = {0};
  addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &addr.sin6_addr);
  addr.sin6_port = 0;
  if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0 ||
      listen(fd, 5) != 0)
  {
    close(fd);
    return;
  }
  socklen_t len = sizeof(addr);
  getsockname(fd, (struct sockaddr *) &addr, &len);
  server_port = ntohs(addr.sin6_port);
  server_pid = fork();
  if (server_pid == 0)
  {
    while (1)
    {
      int conn = accept(fd, NULL, NULL);
      if (conn >= 0)
        close(conn);
    }
    _exit(0);
  }
  close(fd);
  atexit(stop_server);
}

static int
has_ipv6(void)
{
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return 0;
  close(fd);
  return 1;
}

static void
setup(void)
{
  m2k_atcmd(ctx, "ATS7=20");
  m2k_atcmd(ctx, "AT%D1");
}

static void
test_ipv4_numeric(void)
{
  assert(m2k_dial(ctx, "140.82.113.3", "80") == M2K_OK);
  sleep(1);
  assert(m2k_hangup(ctx) == M2K_OK);
}

static void
test_ipv6_loopback(void)
{
  char dialhost[32];
  char dialport[8];
  snprintf(dialhost, sizeof(dialhost), "::1");
  snprintf(dialport, sizeof(dialport), "%d", server_port);
  assert(m2k_dial(ctx, dialhost, dialport) == M2K_OK);
  sleep(1);
  assert(m2k_hangup(ctx) == M2K_OK);
}

int
main(void)
{
  if (!has_ipv6())
    return 77;

  start_server();
  if (server_port < 0)
    return 77;

  ctx = m2k_new();
  assert(ctx != NULL);
  setup();
  test_ipv4_numeric();
  test_ipv6_loopback();
  m2k_free(ctx);
  return 0;
}
