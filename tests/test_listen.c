#include "test.h"
#include "test_helpers.h"
#include <signal.h>
#include <stdio.h>

static pid_t connector_pid = -1;

static void
stop_connector(void)
{
  if (connector_pid > 0)
    kill(connector_pid, SIGTERM);
}

/* Fork a child that connects to the already-bound listener. The parent
   binds first (kernel-assigned port 0, discovered via getsockname on
   m2k_get_listen_fd), so a single connect attempt suffices — no retry
   loop and no fixed-port collision with other tests or build dirs. */
static void
start_connector(int port)
{
  connector_pid = fork();
  if (connector_pid == 0)
  {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short) port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      _exit(1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
      _exit(1);
    usleep(100000);   /* stay connected until parent's accept() returns */
    close(fd);
    _exit(0);
  }
  atexit(stop_connector);
}

static void
test_setup_listen(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  assert(m2k_setup_listen(ctx, "0") == M2K_OK);
  start_connector(test_local_port(m2k_get_listen_fd(ctx)));
  if (connector_pid < 0)
    exit(77);
  assert(m2k_listen_accept(ctx) == M2K_OK);
  m2k_free(ctx);
}

int
main(void)
{
  test_setup_listen();
  return 0;
}
