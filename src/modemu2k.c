/*
 * modemu2k.c
 *      Modemu2k adds Telnet capability to a comm program.
 *      It can redirect Telnet I/O to a pty so that a comm program
 *      can handle the pty as a tty with a real modem.
 *
 * Copyright 2018 Andy Alt <andy400-dev@yahoo.com>
 *
 * modemu2k is a fork of modemu
 * Originally developed by Toru Egashira
 * Copyright (c) 1995, 1996
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 *
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>              /*(printf,fprintf) */
#include <ctype.h>              /*isprint */
#include <arpa/telnet.h>        /*IAC,DO,DONT,... */
#include <sys/time.h>           /*fd_set,FD_ZERO */
#include <fcntl.h>              /*O_RDWR */
#include <errno.h> /*EINTR*/
#include <string.h>
#ifdef __GLIBC__
#include <pty.h>
#endif
#include "defs.h"               /*uchar */
#include "sock.h"               /*sock */
#include "sockbuf.h"            /*sockBufR,sockBufW */
#include "ttybuf.h"             /*tty */
#include "stty.h"               /*(setTty) */
#include "cmdlex.h"             /*Cmdstat */
#include "telopt.h"             /*telOptSummary */
#include "atcmd.h"              /*CHAR_CR */
#include "timeval.h"            /*(timeval...) */
#include "commx.h"              /*(commxForkExec) */
#include "cmdarg.h"             /*cmdarg */
/* socket input processing loop */
  static void
sockReadLoop (void)
{
  static enum
  {
    SRL_NORM, SRL_IAC, SRL_CMD,
    SRL_SB, SRL_SBC, SRL_SBS, SRL_SBI
  } state /*= SRL_NORM*/ ;
  static int cmd;
  static int opt;
  int c;

  if (atcmd.pr)
  {
    while ((c = getSock1 ()) >= 0)
      putTty1 (c);
  }
  else
  {
    while ((c = getSock1 ()) >= 0)
    {
      switch (state)
      {
      case SRL_IAC:
        switch (c)
        {
        case WILL:
        case WONT:
        case DO:
        case DONT:
          cmd = c;
          state = SRL_CMD;
          break;
        case IAC:
          /*if (telOpt.binrecv) */
          {
            putTty1 (c);
            state = SRL_NORM;
          }
          break;
        case SB:
          state = SRL_SB;
          break;
        default:
          state = SRL_NORM;
          telOptPrintCmd ("<", c);
        }
        break;
      case SRL_CMD:
        if (telOptHandle (cmd, c))
          sock.alive = 0;
        state = SRL_NORM;
        break;
      case SRL_SB:
        opt = c;
        state = SRL_SBC;
        break;
      case SRL_SBC:
        state = (c == TELQUAL_SEND) ? SRL_SBS : SRL_NORM;
        break;
      case SRL_SBS:
        state = (c == IAC) ? SRL_SBI : SRL_NORM;
        break;
      case SRL_SBI:
        telOptSBHandle (opt);
        state = SRL_NORM;
        break;
      default:
        if (c == IAC)
        {
          state = SRL_IAC;
        }
        else
        {
          /*putTty1(telOpt.binrecv? c : (c & 0x7f)); */
          putTty1 (c);
        }
      }
    }
  }
}


/* TTY input processing loop */

static struct
{
  enum
  { ESH_NORM, ESH_P1, ESH_P2, ESH_P3 } state;
  struct timeval plus1T;        /* the time 1st '+' input */
  int checkSilence;             /* Recognized silence,"+++" sequence.
                                   Now prepare for the 2nd silence.. */
  struct timeval expireT;       /* keep silence until the time */
} escSeq;

#define escSeqReset() { escSeq.state = ESH_NORM; }
#define checkTtySilence() (escSeq.checkSilence)

/* t1 - t2 > S12? */
static int
s12timePassed (const struct timeval *t1p, const struct timeval *t2p)
{
  struct timeval t;

  timevalSet10ms (&t, atcmd.s[12] * 2);
  timevalAdd (&t, t2p);
  return (timevalCmp (t1p, &t) > 0);
}

static void
escSeqHandle (int c)
{
  switch (escSeq.state)
  {
  case ESH_P1:
    if (c == CHAR_ESC && !s12timePassed (&ttyBufR.newT, &escSeq.plus1T))
    {
      escSeq.state = ESH_P2;
    }
    else
      escSeq.state = ESH_NORM;
    break;
  case ESH_P2:
    if (c == CHAR_ESC && !s12timePassed (&ttyBufR.newT, &escSeq.plus1T))
    {
      escSeq.checkSilence = 1;
      timevalSet10ms (&escSeq.expireT, atcmd.s[12] * 2);
      timevalAdd (&escSeq.expireT, &ttyBufR.newT);
      escSeq.state = ESH_P3;
    }
    else
      escSeq.state = ESH_NORM;
    break;
  case ESH_P3:
    escSeq.checkSilence = 0;
    escSeq.state = ESH_NORM;
    /*break; */
  case ESH_NORM:
    if (c == CHAR_ESC && s12timePassed (&ttyBufR.newT, &ttyBufR.prevT))
    {
      escSeq.plus1T = ttyBufR.newT;
      escSeq.state = ESH_P1;
    }
  }
}


/*#define LINEBUF_SIZE 256 =>defs.h*/

static struct
{
  uchar buf[LINEBUF_SIZE];
  uchar *ptr;
  /*int eol; */
} lineBuf;

#define lineBufReset() { lineBuf.ptr = lineBuf.buf; /*lineBuf.eol = 0;*/ }
#define putLine1(c) \
{ \
    if (lineBuf.ptr < lineBuf.buf + LINEBUF_SIZE) *lineBuf.ptr++ = (c); \
}
#define lineBufBS() \
{ \
    if (lineBuf.ptr > lineBuf.buf) lineBuf.ptr--; \
}

static void
ttyReadLoop (void)
{
  int c;

  if (atcmd.pr)
  {
    while ((c = getTty1 ()) >= 0)
    {
      putSock1 (c);
      escSeqHandle (c);
    }
  }
  else if (telOpt.sgasend)
  {
    while ((c = getTty1 ()) >= 0)
    {
      /*if (telOpt.binsend) */
      {
        if (c == IAC)
          putSock1 (IAC);
        putSock1 (c);
      }                         /*else putSock1(c & 0x7f); */
      escSeqHandle (c);
    }
  }
  else
  {
    /* !sgasend == local echo mode, which cannot be true binmode */
    while ((c = getTty1 ()) >= 0)
    {
      putTty1 (c);
      if (c == CHAR_CR)
      {
        putTty1 (CHAR_LF);
        putSockN (lineBuf.buf, lineBuf.ptr - lineBuf.buf);
        putSock1 ('\r');        /* EOL = CRLF */
        putSock1 ('\n');
        lineBufReset ();
      }
      else if (c == CHAR_LF)
      {
        /* ignore LFs. CR is the EOL char for modems */
      }
      else if (c == CHAR_BS)
      {
        lineBufBS ();
      }
      else
      {
        /*if (telOpt.binsend) */
        {
          if (c == IAC)
            putLine1 (IAC);
          putLine1 (c);
        }                       /*else putLine1(c & 0x7f); */
      }
      escSeqHandle (c);
    }
  }
}


/* online mode main loop */
#ifndef USE_AS_TEST_LIB
static
#endif
  int
onlineMode (void)
{
  fd_set rfds, wfds;
  struct timeval t;

  sockBufRReset ();
  sockBufWReset ();
  ttyBufRReset ();
  /*ttyBufWReset(); */
  lineBufReset ();
  escSeqReset ();

  if (!telOpt.sentReqs && !atcmd.pr)
    telOptSendReqs ();

  t.tv_sec = 0;
  while (sockIsAlive ())
  {
    struct timeval *tp;

    FD_ZERO (&rfds);
    FD_ZERO (&wfds);

    if (ttyBufWReady ())
      FD_SET (sock.fd, &rfds);  /*flow control */
    if (sockBufWHasData ())
      FD_SET (sock.fd, &wfds);
    if (sockBufWReady ())
      FD_SET (tty.rfd, &rfds);  /*flow control */
    if (ttyBufWHasData ())
      FD_SET (tty.wfd, &wfds);

    if (escSeq.checkSilence)
    {
      struct timeval tt;
      gettimeofday (&tt, NULL);
      if (timevalCmp (&tt, &escSeq.expireT) >= 0)
      {
        escSeq.checkSilence = 0;
        return 1;
      }
      t = escSeq.expireT;
      timevalSub (&t, &tt);
      tp = &t;
    }
    else
    {
      tp = NULL;                /* infinite */
    }

    if (select (sock.fd + 1, &rfds, &wfds, NULL, tp) < 0)
    {
      if (errno != EINTR)
        perror ("select()");
      continue;
    }

    if (FD_ISSET (sock.fd, &wfds))
    {
      sockBufWrite ();
    }
    if (FD_ISSET (tty.wfd, &wfds))
    {
      ttyBufWrite ();
    }
    if (FD_ISSET (sock.fd, &rfds))
    {
      sockBufRead ();
      sockReadLoop ();
    }
    if (FD_ISSET (tty.rfd, &rfds))
    {
      ttyBufRead ();
      ttyReadLoop ();
    }
  }
  sockShutdown ();
  return 0;
}


/* command mode input processing loop */

/*#define CMDBUF_MAX 255 =>defs.h*/

static struct
{
  uchar buf[CMDBUF_MAX + 1];
  uchar *ptr;
  int eol;
} cmdBuf;

#define cmdBufReset() { cmdBuf.ptr = cmdBuf.buf; cmdBuf.eol = 0; }
#define putCmd1(c) \
{ \
    if (cmdBuf.ptr < cmdBuf.buf + CMDBUF_MAX) *cmdBuf.ptr++ = (c); \
}
#define cmdBufBS() \
{ \
    if (cmdBuf.ptr > cmdBuf.buf) cmdBuf.ptr--; \
}

static void
cmdReadLoop (void)
{
  int c;

  while ((c = getTty1 ()) >= 0)
  {
    putTty1 (c);
    if (c == CHAR_CR)
    {
      cmdBuf.eol = 1;
      *cmdBuf.ptr = '\0';
      return;                   /* may discard some chars in ttyBufR */
    }
    else if (c == CHAR_BS)
    {
      cmdBufBS ();
#if 0
    }
    else if (c <= ' ' || c == 127)
    {
      /* side effect: "a  t" is recognized as "at" */
#else
    }
    else if (c < ' ' || c == 127)
    {
#endif
      /* just ignore them */
    }
    else
    {
      putCmd1 (c);
    }
  }
}


/* command mode main loop */

static void
putTtyCmdstat (Cmdstat s)
{
  static const char *cmdstatStr[] = {
    "OK",
    "ERROR",
    "CONNECT",
    "NO CARRIER",
    "",
    "",
    "",
  };

  putTty1 (CHAR_CR);
  putTty1 (CHAR_LF);
  putTtyN (cmdstatStr[s], strlen (cmdstatStr[s]));

  if (s == CMDST_CONNECT)
  {
    static const char msg_escape_seq[] = gettext_noop("To escape to command mode, use '+++'.");
    static const char msg_return_online[] = gettext_noop("Use ATO to return to online mode.");

    putTty1 (CHAR_CR);
    putTty1 (CHAR_LF);
    putTtyN (msg_escape_seq, sizeof msg_escape_seq);

    putTty1 (CHAR_CR);
    putTty1 (CHAR_LF);
    putTtyN (msg_return_online, sizeof msg_return_online);
  }

  putTty1 (CHAR_CR);
  putTty1 (CHAR_LF);
}

#ifndef USE_AS_TEST_LIB
static
#endif
  Cmdstat
cmdMode (void)
{
  fd_set rfds, wfds;
  Cmdstat stat;

  cmdBufReset ();
  ttyBufRReset ();
  /*ttyBufWReset(); */

  for (;;)
  {
    FD_ZERO (&rfds);
    FD_ZERO (&wfds);

    if (ttyBufWReady ())
      FD_SET (tty.rfd, &rfds);  /*flow control */
    if (ttyBufWHasData ())
      FD_SET (tty.wfd, &wfds);

    if (select (tty.wfd + 1, &rfds, &wfds, NULL, NULL) < 0)
    {
      if (errno != EINTR)
        perror ("select()");
      continue;
    }

    if (FD_ISSET (tty.wfd, &wfds))
    {
      ttyBufWrite ();           /* put CR before dialup */
      if (cmdBuf.eol)
      {
        stat = cmdLex ((char *) cmdBuf.buf);
        cmdBufReset ();
        switch (stat)
        {
        case CMDST_ATD:
        case CMDST_ATO:
          return stat;
        case CMDST_OK:
        case CMDST_ERROR:
          putTtyCmdstat (stat);
          break;
        default:;              /*CMDST_NOCMD */
        }
      }
    }
    if (FD_ISSET (tty.rfd, &rfds))
    {
      ttyBufRead ();
      cmdReadLoop ();
    }
  }
}


/* open a pty */
#ifndef USE_AS_TEST_LIB
static
#endif
  int
openPtyMaster (const char *dev)
{
  int fd;

  fd = open (dev, O_RDWR);
  if (fd < 0)
  {
    fputs (_("Pty open error.\n"), stderr);
    exit (1);
  }
  return fd;
}

#ifdef HAVE_GRANTPT

#ifndef USE_AS_TEST_LIB
static
#endif
  int
getPtyMaster (char **line_return)
{
  int rc;
  char name[12], *temp_line, *line = NULL;
  int pty = -1;
  char *name1 = "pqrstuvwxyzPQRST", *name2 = "0123456789abcdef";
  char *p1, *p2;

#ifdef HAVE_GRANTPT
  pty = open ("/dev/ptmx", O_RDWR);
  if (pty < 0)
    goto bsd;

  rc = grantpt (pty);
  if (rc < 0)
  {
    close (pty);
    goto bsd;
  }

  rc = unlockpt (pty);
  if (rc < 0)
  {
    close (pty);
    goto bsd;
  }

  temp_line = ptsname (pty);
  if (!temp_line)
  {
    close (pty);
    goto bsd;
  }
  line = malloc (strlen (temp_line) + 1);
  if (!line)
  {
    close (pty);
    return -1;
  }
  strcpy (line, temp_line);

  *line_return = line;
  return pty;
#endif /* HAVE_GRANTPT */

bsd:

  strcpy (name, "/dev/pty??");
  for (p1 = name1; *p1; p1++)
  {
    name[8] = *p1;
    for (p2 = name2; *p2; p2++)
    {
      name[9] = *p2;
      pty = open (name, O_RDWR);
      if (pty >= 0)
        goto found;
      if (errno == ENOENT)
        goto bail;
      else
        continue;
    }
  }

  goto bail;

found:
  line = malloc (strlen (name) + 1);
  if (line)
  {
    strcpy (line, name);
    line[5] = 't';
    rc = chown (line, getuid (), getgid ());
    if (rc < 0)
    {
      /* TRANSLATORS: do not translate "tty" or "pty" */
      fputs (_("\
Warning: could not change ownership of tty -- pty is insecure!\n"), stderr);
    }
    rc = chmod (line, S_IRUSR | S_IWUSR | S_IWGRP);
    if (rc < 0)
    {
      /* TRANSLATORS: do not translate "tty" or "pty" */
      fputs (_
             ("Warning: could not change permissions of tty -- pty is insecure!\n"),
             stderr);
    }

    *line_return = line;
    return pty;
  }
  fputs ("malloc(): error allocating memory -- exiting.", stderr);
  exit (EXIT_FAILURE);

bail:
  if (pty >= 0)
    close (pty);

  return -1;
}

#else

#define PTY00 "/dev/ptyXX"
#define PTY10 "pqrs"
#define PTY01 "0123456789abcdef"

static int
getPtyMaster (char *tty10, char *tty01)
{
  char *p10;
  char *p01;
  static char dev[] = PTY00;
  int fd;

  for (p10 = PTY10; *p10 != '\0'; p10++)
  {
    dev[8] = *p10;
    for (p01 = PTY01; *p01 != '\0'; p01++)
    {
      dev[9] = *p01;
      fd = open (dev, O_RDWR);
      if (fd >= 0)
      {
        *tty10 = *p10;
        *tty01 = *p01;
        return fd;
      }
    }
  }
  fputs (_("No more pty devices available.\n"), stderr);
  exit (1);
  return fd;
}
#endif

#ifndef USE_AS_TEST_LIB

int
main (int argc, char *const argv[])
{
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

#ifdef SOCKS
  SOCKSinit (argv[0]);
#endif
  cmdargParse (argc, argv);
  fputs (PACKAGE_NAME " " VERSION "\n", stdout);
  /* TRANSLATORS: do not translate `at%%q` */
  fputs (_("Enter 'at%q' to quit\n\n"), stdout);
  switch (cmdarg.ttymode)
  {
#ifdef HAVE_GRANTPT
    char *ptyslave;
  case CA_SHOWDEV:
    tty.rfd = tty.wfd = getPtyMaster (&ptyslave);
    puts (ptyslave);
    return 0;
  case CA_COMMX:
    tty.rfd = tty.wfd = getPtyMaster (&ptyslave);
    commxForkExec (cmdarg.commx, ptyslave);
    break;
#else
    char c10, c01;
  case CA_SHOWDEV:
    tty.rfd = tty.wfd = getPtyMaster (&c10, &c01);
    printf ("%c%c\n", c10, c01);
    return 0;
  case CA_COMMX:
    tty.rfd = tty.wfd = getPtyMaster (&c10, &c01);
    commxForkExec (cmdarg.commx, c10, c01);
    break;
#endif
  case CA_STDINOUT:
    tty.rfd = 0;
    tty.wfd = 1;
    setTty ();
    break;
  case CA_DEVGIVEN:
    tty.rfd = tty.wfd = openPtyMaster (cmdarg.dev);
    break;
  }

  ttyBufWReset ();
  telOptInit ();
  atcmdInit ();                 /* initialize atcmd */

CMDMODE:
  switch (cmdMode ())
  {
  case CMDST_ATD:
    if (sockIsAlive ())
    {
      putTtyCmdstat (CMDST_ERROR);
      goto CMDMODE;
    }
    goto DIAL;
  case CMDST_ATO:
    if (!sockIsAlive ())
    {
      putTtyCmdstat (CMDST_NOCARRIER);
      goto CMDMODE;
    }
    goto ONLINE;
  default:;
  }

  return 1;

DIAL:
  telOptReset ();               /* before sockDial(), which may change telOpt.xx */
  switch (sockDial ())
  {
  case 0:                      /* connect */
    goto ONLINE;
  case 1:                      /* error */
    putTtyCmdstat (CMDST_NOCARRIER);
    goto CMDMODE;
  default:;
  }

  return 1;

ONLINE:
  putTtyCmdstat (CMDST_CONNECT);
  switch (onlineMode ())
  {
  case 0:                      /* connection lost */
    putTtyCmdstat (CMDST_NOCARRIER);
    goto CMDMODE;
  case 1:                      /* +++ */
    putTtyCmdstat (CMDST_OK);
    goto CMDMODE;
  default:;
  }

  return 1;
}

#endif
