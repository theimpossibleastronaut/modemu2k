/* NOTE: uses the deprecated synchronous m2k_dial(). When 0.3.0 removes
   it, port this to the step API — the getaddrinfo multi-address
   fallback under test must survive the removal. */

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
  start_ipv4_only_server();
  if (server_port < 0)
    return 77;
  ctx = m2k_new();
  assert(ctx != NULL);
  setup();
  test_localhost_fallback();
  m2k_free(ctx);
  return 0;
}
