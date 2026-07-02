#include "test.h"
#include <signal.h>
#include <stdio.h>

#define TEST_PORT "19876"

static pid_t connector_pid = -1;

static void
stop_connector(void)
{
  if (connector_pid > 0)
    kill(connector_pid, SIGTERM);
}

/* Fork a child that connects to localhost:TEST_PORT, retrying until the
   parent is listening. A fixed delay would race the parent's bind/listen
   under load (e.g. the ASan build), leaving the parent to block in accept()
   with nobody connecting. */
static void
start_connector(void)
{
  connector_pid = fork();
  if (connector_pid == 0)
  {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(atoi(TEST_PORT));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int fd = -1;
    for (int i = 0; i < 100; i++)   /* up to ~5s */
    {
      fd = socket(AF_INET, SOCK_STREAM, 0);
      if (fd < 0)
        _exit(1);
      if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
        break;
      close(fd);
      fd = -1;
      usleep(50000);
    }
    if (fd < 0)
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
  assert(m2k_setup_listen(ctx, TEST_PORT) == M2K_OK);
  assert(m2k_listen_accept(ctx) == M2K_OK);
  m2k_free(ctx);
}

int
main(void)
{
  start_connector();
  if (connector_pid < 0)
    return 77;
  test_setup_listen();
  return 0;
}
