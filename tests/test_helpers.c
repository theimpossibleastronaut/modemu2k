#include "test.h"
#include "test_helpers.h"
#include <signal.h>

#define MAX_LOOPBACK_SERVERS 4
static pid_t loopback_pids[MAX_LOOPBACK_SERVERS];
static int   loopback_pid_count = 0;
static int   atexit_installed = 0;

static void
stop_loopback_servers(void)
{
  for (int i = 0; i < loopback_pid_count; i++)
    if (loopback_pids[i] > 0)
      kill(loopback_pids[i], SIGTERM);
}

int
start_loopback_listener(int family, int *port_out)
{
  int fd = socket(family, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  int port;
  if (family == AF_INET6)
  {
    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &addr.sin6_addr);
    addr.sin6_port = 0;
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0 ||
        listen(fd, 5) != 0)
    {
      close(fd);
      return -1;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr *) &addr, &len);
    port = ntohs(addr.sin6_port);
  }
  else if (family == AF_INET)
  {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0 ||
        listen(fd, 5) != 0)
    {
      close(fd);
      return -1;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, (struct sockaddr *) &addr, &len);
    port = ntohs(addr.sin_port);
  }
  else
  {
    close(fd);
    return -1;
  }

  pid_t pid = fork();
  if (pid == 0)
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
  if (pid < 0)
    return -1;

  if (loopback_pid_count < MAX_LOOPBACK_SERVERS)
    loopback_pids[loopback_pid_count++] = pid;
  if (!atexit_installed)
  {
    atexit(stop_loopback_servers);
    atexit_installed = 1;
  }
  *port_out = port;
  return 0;
}
