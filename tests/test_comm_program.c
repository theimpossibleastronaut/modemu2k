/* Exercises m2k_setup_comm_program: PTY allocation, fork+exec of a
   shell pipeline.

   The library installs a SIGCHLD handler that calls _exit(0) when the
   spawned program exits, which would race with the test runner's
   normal exit path. We block SIGCHLD around setup, then override the
   handler with SIG_IGN (which auto-reaps zombies on Linux) and unblock
   — any pending SIGCHLD is then discarded by the POSIX rules. */

#include "test.h"
#include "../modemu2k.h"
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

  m2k_free(ctx);
}

static void
test_setup_comm_program_with_format(void)
{
  /* Verify the %s slave-path expansion path: command contains %s and
     should be substituted with the PTY slave path. We can't directly
     observe what got sprintf'd, but a malformed format wouldn't crash
     this either; what we're really checking is that a cmd with %s
     reaches the same OK status as one without. */
  sigset_t set, oldset;
  sigemptyset(&set);
  sigaddset(&set, SIGCHLD);
  sigprocmask(SIG_BLOCK, &set, &oldset);

  m2k_t *ctx = m2k_new();
  assert(ctx);
  /* This shell line runs `: <slavepath>` — a no-op that ignores its
     argument — so it exits with status 0 regardless of the path. */
  assert(m2k_setup_comm_program(ctx, ": %s") == M2K_OK);

  signal(SIGCHLD, SIG_IGN);
  sigprocmask(SIG_SETMASK, &oldset, NULL);

  m2k_free(ctx);
}

int
main(void)
{
  test_setup_comm_program_basic();
  test_setup_comm_program_with_format();
  return 0;
}
