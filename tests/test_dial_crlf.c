/* Regression: line terminators trailing the ATD command must not abort
   the dial. DOS-style DTEs (e.g. `echo atd... > com1` under dosemu2)
   send the command line as CR LF; the command dispatches on the CR and
   the LF arrives while the state machine is in M2K_STATE_DIAL, where it
   used to be treated as a user-cancel keypress (V.250 abort-on-input).
   Real modem firmware ignores CR/LF/NUL there; only a substantive byte
   cancels the dial.

   Uses a real PTY (not app_io): the abort-on-input path is skipped
   entirely in app_io mode, so this can only be exercised through
   tty.rfd. */

#include "test.h"
#include "test_helpers.h"
#include <fcntl.h>
#include <stdio.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>

/* Listener that never accepts: the kernel backlog still completes the
   TCP handshake, so a non-blocking dial resolves without an acceptor
   process racing us to close the connection. */
static int
bind_quiet_listener(int *port_out)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  assert(bind(fd, (struct sockaddr *) &addr, sizeof addr) == 0);
  assert(listen(fd, 5) == 0);
  socklen_t len = sizeof addr;
  assert(getsockname(fd, (struct sockaddr *) &addr, &len) == 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static m2k_t *
make_pty_ctx(int *slave_fd_out)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  const char *slave = NULL;
  assert(m2k_setup_pty(ctx, &slave) == M2K_OK);
  int sfd = open(slave, O_RDWR | O_NOCTTY | O_NONBLOCK);
  assert(sfd >= 0);
  struct termios t;
  assert(tcgetattr(sfd, &t) == 0);
  cfmakeraw(&t);
  assert(tcsetattr(sfd, TCSANOW, &t) == 0);
  *slave_fd_out = sfd;
  return ctx;
}

/* Pump until the predicate holds or ~5s elapse. */
#define PUMP_UNTIL(ctx, cond)                    \
  do                                             \
  {                                              \
    for (int i_ = 0; i_ < 50 && !(cond); i_++)  \
      test_step(ctx);                            \
  } while (0)

static void
dial_via_slave(int sfd, int port, const char *terminator, m2k_t *ctx)
{
  char cmd[64];
  snprintf(cmd, sizeof cmd, "atd\"127.0.0.1 %d\"", port);
  assert(write(sfd, cmd, strlen(cmd)) == (ssize_t) strlen(cmd));
  /* Dispatch on CR with exactly ONE step: it consumes the command,
     enters DIAL and fires the non-blocking connect(), which resolves
     only on a later step. The terminator then lands while the machine
     is provably in DIAL — the UART-paced worst case. */
  assert(write(sfd, "\r", 1) == 1);
  test_step(ctx);
  assert(write(sfd, terminator, strlen(terminator)) ==
         (ssize_t) strlen(terminator));
}

static void
drain_slave(int sfd, char *buf, size_t cap)
{
  size_t len = 0;
  struct timespec ts = {0, 20 * 1000 * 1000};
  for (int i = 0; i < 10 && len < cap - 1; i++)
  {
    ssize_t n = read(sfd, buf + len, cap - 1 - len);
    if (n > 0)
      len += (size_t) n;
    nanosleep(&ts, NULL);
  }
  buf[len] = '\0';
}

/* The regression proper: LF trailing the dial command. */
static void
test_dial_survives_trailing_lf(void)
{
  int port, sfd;
  int srv = bind_quiet_listener(&port);
  m2k_t *ctx = make_pty_ctx(&sfd);

  dial_via_slave(sfd, port, "\n", ctx);
  PUMP_UNTIL(ctx, m2k_is_online(ctx));
  assert(m2k_is_online(ctx));
  /* a few more steps so the CONNECT result code flushes to the tty */
  for (int i = 0; i < 5; i++)
    test_step(ctx);

  char out[256];
  drain_slave(sfd, out, sizeof out);
  assert(strstr(out, "CONNECT") != NULL);

  close(sfd);
  close(srv);
  m2k_free(ctx);
}

/* Same, with the whole CRLF pair plus a stray NUL arriving late. */
static void
test_dial_survives_crlf_nul(void)
{
  int port, sfd;
  int srv = bind_quiet_listener(&port);
  m2k_t *ctx = make_pty_ctx(&sfd);

  dial_via_slave(sfd, port, "\r\n", ctx);
  assert(write(sfd, "\0", 1) == 1);
  PUMP_UNTIL(ctx, m2k_is_online(ctx));
  assert(m2k_is_online(ctx));

  close(sfd);
  close(srv);
  m2k_free(ctx);
}

/* A substantive keypress during DIAL must still cancel (V.250). Dial a
   blackhole address so the DIAL state persists long enough to abort;
   if the environment refuses the route instantly the dial fails on its
   own and the observable result is the same NO CARRIER. */
static void
test_dial_keypress_still_cancels(void)
{
  int sfd;
  m2k_t *ctx = make_pty_ctx(&sfd);

  const char *cmd = "atd\"192.0.2.1 1\"\r";
  assert(write(sfd, cmd, strlen(cmd)) == (ssize_t) strlen(cmd));
  test_step(ctx);
  assert(write(sfd, "x", 1) == 1);
  for (int i = 0; i < 10; i++)
    test_step(ctx);
  assert(!m2k_is_online(ctx));

  char out[256];
  drain_slave(sfd, out, sizeof out);
  assert(strstr(out, "NO CARRIER") != NULL);

  close(sfd);
  m2k_free(ctx);
}

int
main(void)
{
  test_dial_survives_trailing_lf();
  test_dial_survives_crlf_nul();
  test_dial_keypress_still_cancels();
  return 0;
}
