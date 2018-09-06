#include <stdio.h>	/*stderr*/
#include <signal.h>	/*SIGCHLD*/
#include <unistd.h>	/*(fork,execv)*/
#include <string.h>	/*(strdup)*/
#include <sys/wait.h>	/*WNOHANG*/
#include "commx.h"	/*(commxForkExec)*/
#include "verbose.h"	/*VERB_MISC*/

static int commxPid;

static void
sigchld(int dummy)
{
    int s;

    /* wait() for commx exit() only */
    /* waitpid(-1,,) messes SOCKS up */
    if (waitpid(commxPid, &s, WNOHANG) > 0) {
	fprintf(stderr, "Comm program exited.\r\n");
	verboseOut(VERB_MISC, "Child returns status %d.\r\n", WEXITSTATUS(s));
	exit(0);
    }
}

static void
forkExec(char *s)
{
    static char *argv[4] = {"sh", "-c", "", NULL};

    signal(SIGCHLD, sigchld);
    commxPid = fork();
    switch (commxPid) {
    case -1: /*error*/
	perror("fork()");
	exit(1);
    case 0: /*child*/
	signal(SIGCHLD, SIG_DFL);
	argv[2] = s;
	execv("/bin/sh", argv);
	fprintf(stderr, "Could not exec sh.\n");
	_exit(127);
    default: /*parent*/
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
    }
}

void
commxForkExec(const char *cmd, char c10, char c01)
{
    char c[3];
    char *s;

    c[0] = c10; c[1] = c01; c[2] = 0;
    s = strdup(cmd);
    sprintf(s, cmd, c); /*'%s' -> 'p1' or sth*/
    forkExec(s);
}
