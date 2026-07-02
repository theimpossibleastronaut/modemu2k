/* Exercises m2k_setup_comm_program: PTY allocation, fork+exec of a
   shell pipeline.

   The library installs a SIGCHLD handler that calls _exit(0) when the
   spawned program exits, which would race with the test runner's
   normal exit path. We block SIGCHLD around setup, then override the
   handler with SIG_IGN (which auto-reaps zombies on Linux) and unblock
   — any pending SIGCHLD is then discarded by the POSIX rules. */

#include "test.h"
#include "../modemu2k.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void
test_setup_comm_program_basic(void)
{
  sigset_t set, oldset;
  sigemptyset(&set);
  sigaddset(&set, SIGCHLD);
  sigprocmask(SIG_BLOCK, &set, &oldset);

  m2k_t *ctx = m2k_new();
  assert(ctx);

  /* "true" exits immediately. The fork+exec path runs; we only care
     that setup returned OK and that a PTY fd was opened. */
  assert(m2k_setup_comm_program(ctx, "true") == M2K_OK);

  /* Replace the library's SIGCHLD handler so the imminent child-exit
     doesn't _exit(0) our test. SIG_IGN on SIGCHLD also auto-reaps. */
  signal(SIGCHLD, SIG_IGN);
  sigprocmask(SIG_SETMASK, &oldset, NULL);

  /* Validate that a PTY master fd is actually open and accessible. */
  struct stat st;
  /* m2k_t is opaque — we can only inspect via public surface. The
     poll-fd contract is that get_pollfds will return a tty fd in CMD
     state; verify that. */
  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int timeout_ms;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
  assert(nfds >= 1);
  assert(fds[0].fd > 2);
  assert(fstat(fds[0].fd, &st) == 0);

  int ptyfd = fds[0].fd;
  m2k_free(ctx);
  /* m2k_free must close the library-owned PTY fd (regression guard for
     the fd leak fixed in b573d19). */
  assert(fstat(ptyfd, &st) == -1 && errno == EBADF);
}

static void
test_setup_comm_program_with_format(void)
{
  /* Verify %s is actually substituted with the PTY slave path — not just
     that a cmd containing %s returns OK. The spawned shell echoes its
     argument to a tmpfile; the file then holds a /dev/... node only if
     the substitution ran (a broken expansion would write "%s" or empty). */
  char tmpl[] = "/tmp/m2k_comm_XXXXXX";
  int tfd = mkstemp(tmpl);
  assert(tfd >= 0);
  close(tfd);

  char cmd[128];
  /* -> "echo %s > /tmp/m2k_comm_XXXXXX"; the first %s is the slave-path
     placeholder that commProgramForkExec substitutes. */
  snprintf(cmd, sizeof cmd, "echo %%s > %s", tmpl);

  sigset_t set, oldset;
  sigemptyset(&set);
  sigaddset(&set, SIGCHLD);
  sigprocmask(SIG_BLOCK, &set, &oldset);

  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_comm_program(ctx, cmd) == M2K_OK);

  signal(SIGCHLD, SIG_IGN);
  sigprocmask(SIG_SETMASK, &oldset, NULL);

  /* The child exits right after echo; poll the file until it has data. */
  char buf[128];
  ssize_t n = 0;
  for (int i = 0; i < 200; i++)   /* up to ~2s */
  {
    int rfd = open(tmpl, O_RDONLY);
    if (rfd >= 0)
    {
      n = read(rfd, buf, sizeof buf - 1);
      close(rfd);
      if (n > 0)
        break;
    }
    usleep(10000);
  }
  assert(n > 0);
  buf[n] = '\0';
  assert(strstr(buf, "/dev/") != NULL);

  m2k_free(ctx);
  unlink(tmpl);
}

int
main(void)
{
  test_setup_comm_program_basic();
  test_setup_comm_program_with_format();
  return 0;
}
