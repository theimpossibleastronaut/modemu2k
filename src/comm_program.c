
#include <errno.h>
#include <signal.h> /*SIGCHLD*/
#include <string.h>
#include <sys/wait.h> /*WNOHANG*/
#include <unistd.h>
#include <stdlib.h> /*(getenv) */
#include "m2k_private.h"
#include "m2k_ctx.h"

static int commProgramPid;

static void
sigchld(int dummy)
{
  (void) dummy;
  int s;

  /* wait() for comm program exit() only */
  /* waitpid(-1,,) messes SOCKS up */
  if (waitpid(commProgramPid, &s, WNOHANG) > 0)
  {
    fputs("Comm program exited.\r\n", stderr);
    /* Note: we don't have ctx here in the signal handler */
    _exit(0);
  }
}

static int
forkExec(m2k_t *ctx, char *s)
{
  static char *argv[4] = {"sh", "-c", "", NULL};

  signal(SIGCHLD, sigchld);
  commProgramPid = fork();
  switch (commProgramPid)
  {
  case -1: /*error */
    m2k_log(ctx, "fork(): %s\n", strerror(errno));
    return -1;
  case 0: /*child */
    signal(SIGCHLD, SIG_DFL);
    argv[2] = s;
    execv("/bin/sh", argv);
    fputs("Could not run shell.\n", stderr);
    _exit(127);
  default: /*parent */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    return 0;
  }
}

#ifdef HAVE_GRANTPT
m2k_err_t
commProgramForkExec(m2k_t *ctx, const char *cmd, char *ptyslave)
{
  char *s = m2k_alloc(ctx, strlen(cmd) + strlen(ptyslave) + 1);
  if (!s)
    return M2K_ERR_NOMEM;
  sprintf(s, cmd, ptyslave);
  int rc = forkExec(ctx, s);
  free(s);
  return rc < 0 ? M2K_ERR_PTY : M2K_OK;
}
#else
m2k_err_t
commProgramForkExec(m2k_t *ctx, const char *cmd, char c10, char c01)
{
  char c[16];
  strcpy(c, "tty");
  c[3] = c10;
  c[4] = c01;
  c[5] = 0;
  char *s = m2k_alloc(ctx, strlen(cmd) + strlen(c) + 1);
  if (!s)
    return M2K_ERR_NOMEM;
  sprintf(s, cmd, c); /*'%s' -> 'p1' or sth */
  int rc = forkExec(ctx, s);
  free(s);
  return rc < 0 ? M2K_ERR_PTY : M2K_OK;
}
#endif
