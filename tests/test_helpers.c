#include "test.h"
#include "test_helpers.h"
#include <errno.h>
#include <signal.h>
#include <poll.h>

#define MAX_TEST_CHILDREN 4
static pid_t child_pids[MAX_TEST_CHILDREN];
static int   child_pid_count = 0;
static int   atexit_installed = 0;

static void
stop_test_children(void)
{
  for (int i = 0; i < child_pid_count; i++)
    if (child_pids[i] > 0)
      kill(child_pids[i], SIGTERM);
}

/* Track a forked test child so it's SIGTERM'd at process exit. */
static void
register_child(pid_t pid)
{
  if (child_pid_count < MAX_TEST_CHILDREN)
    child_pids[child_pid_count++] = pid;
  if (!atexit_installed)
  {
    atexit(stop_test_children);
    atexit_installed = 1;
  }
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

  /* Parent-death guard. The child watches the read end of this pipe
     and exits when the parent's write end closes, whatever killed the
     parent. stop_test_children() cannot cover that case: it runs from
     atexit(), and a failed assert() calls abort(), which skips atexit
     handlers. The orphan then holds the test's stdout open, so the run
     reports as a 30s TIMEOUT instead of a failure -- and one survives
     every direct run of a failing test binary. */
  int guard[2];
  if (pipe(guard) != 0)
  {
    close(fd);
    return -1;
  }

  pid_t pid = fork();
  if (pid == 0)
  {
    close(guard[1]);
    struct pollfd p[2];
    p[0].fd = fd;
    p[0].events = POLLIN;
    p[1].fd = guard[0];
    p[1].events = 0; /* POLLHUP is reported whether or not it is asked for */
    while (1)
    {
      if (poll(p, 2, -1) < 0)
      {
        if (errno == EINTR)
          continue;
        break;
      }
      if (p[1].revents)
        break;
      if (p[0].revents & POLLIN)
      {
        int conn = accept(fd, NULL, NULL);
        if (conn >= 0)
          close(conn);
      }
    }
    _exit(0);
  }
  close(fd);
  close(guard[0]);
  if (pid < 0)
  {
    close(guard[1]);
    return -1;
  }
  /* guard[1] stays open for the life of the process on purpose: its
     closing is the signal the child waits for. */

  register_child(pid);
  *port_out = port;
  return 0;
}

int
start_connector(int family, int port)
{
  pid_t pid = fork();
  if (pid == 0)
  {
    struct sockaddr_storage ss = {0};
    socklen_t slen;
    if (family == AF_INET6)
    {
      struct sockaddr_in6 *a = (struct sockaddr_in6 *) &ss;
      a->sin6_family = AF_INET6;
      a->sin6_port = htons((unsigned short) port);
      inet_pton(AF_INET6, "::1", &a->sin6_addr);
      slen = sizeof *a;
    }
    else
    {
      struct sockaddr_in *a = (struct sockaddr_in *) &ss;
      a->sin_family = AF_INET;
      a->sin_port = htons((unsigned short) port);
      inet_pton(AF_INET, "127.0.0.1", &a->sin_addr);
      slen = sizeof *a;
    }
    int fd = socket(family, SOCK_STREAM, 0);
    if (fd < 0)
      _exit(1);
    if (connect(fd, (struct sockaddr *) &ss, slen) != 0)
      _exit(1);
    usleep(100000); /* stay connected until parent's accept() returns */
    close(fd);
    _exit(0);
  }
  if (pid > 0)
    register_child(pid);
  return pid;
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
test_connect_slow_peer(int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  /* SO_RCVBUF has to be set before connect() for the advertised window
     to shrink. */
  int sz = 2048;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = htons((unsigned short) port);
  assert(connect(fd, (struct sockaddr *) &addr, sizeof addr) == 0);
  return fd;
}

void
test_step_nowait(m2k_t *ctx)
{
  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int timeout_ms;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
  if (nfds > 0)
    poll(fds, nfds, 0);
  assert(m2k_step(ctx, fds, nfds) == M2K_OK);
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
