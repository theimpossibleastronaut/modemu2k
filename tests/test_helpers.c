#include "test.h"
#include "test_helpers.h"
#include <signal.h>
#include <poll.h>

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

void
test_step(m2k_t *ctx)
{
  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int timeout_ms;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
  if (timeout_ms < 0 || timeout_ms > 100)
    timeout_ms = 100;
  if (nfds > 0)
    poll(fds, nfds, timeout_ms);
  assert(m2k_step(ctx, fds, nfds) == M2K_OK);
}

void
test_step_drain(m2k_t *ctx, char *buf, size_t cap, size_t *len)
{
  test_step(ctx);
  size_t got = 0;
  m2k_read_to_app(ctx, buf + *len, cap - *len - 1, &got);
  *len += got;
  buf[*len] = '\0';
}

int
test_connect_client(int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = htons((unsigned short) port);
  assert(connect(fd, (struct sockaddr *) &addr, sizeof addr) == 0);
  return fd;
}

int
test_local_port(int fd)
{
  struct sockaddr_storage ss;
  socklen_t slen = sizeof ss;
  assert(getsockname(fd, (struct sockaddr *) &ss, &slen) == 0);
  if (ss.ss_family == AF_INET6)
    return ntohs(((struct sockaddr_in6 *) &ss)->sin6_port);
  return ntohs(((struct sockaddr_in *) &ss)->sin_port);
}
