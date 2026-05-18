#include "test.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
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

/* Listen on 127.0.0.1 only — not ::1 — so connecting via "localhost"
   (which resolves to ::1 first on dual-stack systems) must fall through
   to the IPv4 address to succeed. */
static void
start_ipv4_only_server(void)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = 0;
  if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0 ||
      listen(fd, 5) != 0)
  {
    close(fd);
    return;
  }
  socklen_t len = sizeof(addr);
  getsockname(fd, (struct sockaddr *) &addr, &len);
  server_port = ntohs(addr.sin_port);
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

static void
setup(void)
{
  ctx->atcmd.s[7] = 5;
  ctx->atcmd.pd = 1;
}

static void
test_localhost_fallback(void)
{
  char dialstr[32];
  snprintf(dialstr, sizeof(dialstr), "localhost %d", server_port);
  st_sock sock;
  telOptReset(ctx);
  m2k_atcmdD(ctx, dialstr, ATDA_STR, ATDP_NUM);
  assert(m2k_sockDial(ctx, &sock) == 0);
  assert(sockShutdown(&sock) == 0);
}

int
main(void)
{
  start_ipv4_only_server();
  if (server_port < 0)
    return 77;
  setup();
  test_localhost_fallback();
  return 0;
}
