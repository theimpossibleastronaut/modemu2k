
#include <sys/time.h>   /*->ttybuf.h (timeval)*/
#include <stdlib.h>             /*(getenv) */
#include <unistd.h>
#include "modemu2k.h"       /*->ttybuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/

struct st_tty tty;
struct st_ttyBufR ttyBufR;
struct st_ttyBufW ttyBufW;

void
ttyBufRReset(void)
{
  ttyBufR.ptr = ttyBufR.end = ttyBufR.buf;
  ttyBufR.prevT.tv_sec = ttyBufR.prevT.tv_usec = 0;
}

/* reading tty */

int
getTty1(void)
{
  return ((ttyBufR.ptr >= ttyBufR.end)? -1 : *ttyBufR.ptr++);
}

void
ttyBufRead (st_sock *sock)
{
  int l;

  l = read (tty.rfd, ttyBufR.buf, sizeof (ttyBufR.buf));
  if (l <= 0)
  {
    sockClose (sock);
    /* TRANSLATORS: do not translate "pty" or "read" */
    verboseOut (VERB_MISC, _("Pty closed. (read() returned %d)\r\n"), l);
    if (l < 0)
      verbosePerror (VERB_MISC, "read()");
    exit (0);
  }
  ttyBufR.prevT = ttyBufR.newT;
  gettimeofday (&ttyBufR.newT, NULL);
  ttyBufR.ptr = ttyBufR.buf;
  ttyBufR.end = ttyBufR.buf + l;
}


/* writing tty */

void
ttyBufWrite (st_sock *sock)
{
  int wl, l;

  wl = ttyBufW.ptr - ttyBufW.top;
  if (wl == 0)
    return;
  l = write (tty.wfd, ttyBufW.top, wl);
  if (l <= 0)
  {
    sockClose (sock);
    /* TRANSLATORS do not translate "pty" or "write" */
    verboseOut (VERB_MISC, _("Pty closed. (write() returned %d)\r\n"), l);
    if (l < 0)
      verbosePerror (VERB_MISC, "write()");
    exit (0);
  }
  else if (l < wl)
  {
    ttyBufW.top += l;
    /*return 1; *//* needs retry */
    return;
  }
  ttyBufW.ptr = ttyBufW.top = ttyBufW.buf;
  ttyBufW.stop = 0;
  return;
}

void
putTty1 (uchar c)
{
  if (ttyBufW.ptr >= ttyBufW.buf + TTYBUFW_SIZE)
  {                             /* limit */
    if (ttyBufW.ptr >= ttyBufW.buf + TTYBUFW_SIZE_A)
    {                           /*actual limit */
      /* TRANSLATORS: do not translate ttyBufW */
      fputs (_("\attyBufW overrun.\n"), stderr);
      return;
    }
    else
      ttyBufW.stop = 1;         /* flow control */
  }
  *ttyBufW.ptr++ = c;
}

void
putTtyN (const char *cp, int n)
{
  for (; n > 0; n--, cp++)
    putTty1 (*cp);
}
