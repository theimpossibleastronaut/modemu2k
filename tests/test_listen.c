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

/* Fork a child that waits briefly then connects to localhost:TEST_PORT.
   The connection queues in the listen backlog so the parent's accept()
   finds it regardless of exact ordering. */
static void
start_connector(void)
{
  connector_pid = fork();
  if (connector_pid == 0)
  {
    usleep(100000);   /* 100ms — enough for parent to reach bind/listen */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
      _exit(1);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(atoi(TEST_PORT));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(fd, (struct sockaddr *)&addr, sizeof(addr));
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
