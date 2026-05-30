#include "test.h"
#include <signal.h>
#include <stdio.h>

static pid_t server_pid = -1;
static int server_port = -1;

static void
stop_server(void)
{
  if (server_pid > 0)
    kill(server_pid, SIGTERM);
}

/* Bind 127.0.0.1 to an ephemeral port and fork an accept-and-close loop
   so dial attempts complete without needing the public internet. */
static void
start_server(void)
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
test_connect(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  m2k_atcmd(ctx, "ATS7=20");
  m2k_atcmd(ctx, "AT%D1");  /* disable dial canceling — stdin is not a tty */

  char port_s[8];
  snprintf(port_s, sizeof(port_s), "%d", server_port);

  /* Hostname path: "localhost" resolves via /etc/hosts / nsswitch — no
     public DNS lookup needed, so this works inside sandboxed builds. */
  assert(m2k_dial(ctx, "localhost", port_s) == M2K_OK);
  sleep(1);
  assert(m2k_hangup(ctx) == M2K_OK);

  assert(m2k_dial(ctx, "127.0.0.1", port_s) == M2K_OK);
  sleep(1);
  assert(m2k_hangup(ctx) == M2K_OK);

  m2k_free(ctx);
}

int
main(void)
{
  start_server();
  if (server_port < 0)
    return 77;
  test_connect();
  return 0;
}
