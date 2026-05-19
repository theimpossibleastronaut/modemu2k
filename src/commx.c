
#include <errno.h>
#include <signal.h> /*SIGCHLD*/
#include <string.h>
#include <sys/wait.h> /*WNOHANG*/
#include <unistd.h>
#include <stdlib.h>             /*(getenv) */
#include "m2k_private.h"
#include "m2k_ctx.h"

static int commxPid;

static void
sigchld(int dummy)
{
  (void)dummy;
  int s;

  /* wait() for commx exit() only */
  /* waitpid(-1,,) messes SOCKS up */
  if (waitpid(commxPid, &s, WNOHANG) > 0)
  {
    fputs("Comm program exited.\r\n", stderr);
    /* Note: we don't have ctx here in the signal handler */
    _exit(0);
  }
}

static int
forkExec(m2k_t *ctx, char *s)
{
  static char *argv[4] = { "sh", "-c", "", NULL };

  signal(SIGCHLD, sigchld);
  commxPid = fork();
  switch (commxPid)
  {
  case -1:                     /*error */
    m2k_log(ctx, "fork(): %s\n", strerror(errno));
    return -1;
  case 0:                      /*child */
    signal(SIGCHLD, SIG_DFL);
    argv[2] = s;
    execv("/bin/sh", argv);
    fputs("Could not run shell.\n", stderr);
    _exit(127);
  default:                     /*parent */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    return 0;
  }
}

#ifdef HAVE_GRANTPT
m2k_err_t
commxForkExec(m2k_t *ctx, const char *cmd, char *ptyslave)
{
  char *s;
  s = malloc(strlen(cmd) + strlen(ptyslave) + 1);
  chk_alloc(ctx, s);
  if (s == NULL)
    return M2K_ERR_NOMEM;
  if (strcmp("/dev/", ptyslave) == 0)
    ptyslave += 5;
  sprintf(s, cmd, ptyslave);
  return forkExec(ctx, s) < 0 ? M2K_ERR_PTY : M2K_OK;
}
#else
m2k_err_t
commxForkExec(m2k_t *ctx, const char *cmd, char c10, char c01)
{
  char c[16];
  char *s;

  strcpy(c, "tty");
  c[3] = c10;
  c[4] = c01;
  c[5] = 0;
  s = malloc(strlen(cmd) + strlen(c) + 1);
  chk_alloc(ctx, s);
  if (s == NULL)
    return M2K_ERR_NOMEM;
  sprintf(s, cmd, c);           /*'%s' -> 'p1' or sth */
  return forkExec(ctx, s) < 0 ? M2K_ERR_PTY : M2K_OK;
}
#endif
