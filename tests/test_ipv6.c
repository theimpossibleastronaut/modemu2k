#include "test.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "modemu2k.h"
#include "m2k_ctx.h"

static m2k_t ctx_storage;
static m2k_t *ctx = &ctx_storage;

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
  ctx->atcmd.s[7] = 20;
  ctx->atcmd.pd = 1;
}

static void
test_ipv4_numeric(void)
{
  st_sock sock;
  telOptReset(ctx);
  m2k_atcmdD(ctx, "140.82.113.3 80", ATDA_NUM, ATDP_NUM);
  assert(m2k_sockDial(ctx, &sock) == 0);
  sleep(1);
  assert(sockShutdown(&sock) == 0);
}

static void
test_ipv6_loopback(void)
{
  char dialstr[32];
  snprintf(dialstr, sizeof(dialstr), "::1 %d", server_port);
  st_sock sock;
  telOptReset(ctx);
  m2k_atcmdD(ctx, dialstr, ATDA_NUM, ATDP_NUM);
  assert(m2k_sockDial(ctx, &sock) == 0);
  sleep(1);
  assert(sockShutdown(&sock) == 0);
}

int
main(void)
{
  if (!has_ipv6())
    return 77;

  start_server();
  if (server_port < 0)
    return 77;

  setup();
  test_ipv4_numeric();
  test_ipv6_loopback();
  return 0;
}
