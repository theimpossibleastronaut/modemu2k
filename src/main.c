/*
 * main.c
 *      Modemu2k adds Telnet capability to a comm program.
 *      It can redirect Telnet I/O to a pty so that a comm program
 *      can handle the pty as a tty with a real modem.
 *
 * Copyright 2018-2020 Andy Alt <arch_stanton5995@protonmail.com>
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

#include <arpa/telnet.h>        /*TELOPT_xxx */
#include <sys/time.h>   /*->ttybuf.h (timeval)*/
#include <unistd.h>
#include <stdlib.h>             /*(getenv) */
#include <sys/stat.h>
#include <ctype.h>              /*isprint */
#include <fcntl.h>              /*O_RDWR */
#include <errno.h> /*EINTR*/
#include <string.h>
#ifdef __GLIBC__
#include <pty.h>
#endif
#include "cmdarg.h"             /*cmdarg */
#include "config.h"
#include "modemu2k.h"
#include "m2k_ctx.h"

/* command mode input processing loop */

/*#define CMDBUF_MAX 255 =>defs.h*/

struct cmdBuf
{
  uchar buf[CMDBUF_MAX + 1];
  uchar *ptr;
  int eol;
};

static void
cmdBufReset(struct cmdBuf *x)
{
  x->ptr = x->buf;
  x->eol = 0;
}

static void
putCmd1(const int c, struct cmdBuf *cmdBuf)
{
  if (cmdBuf->ptr < cmdBuf->buf + CMDBUF_MAX)
    *cmdBuf->ptr++ = c;
}

static void
cmdBufBS(struct cmdBuf *cmdBuf)
{
  if (cmdBuf->ptr > cmdBuf->buf)
    cmdBuf->ptr--;
}

static void
cmdReadLoop(m2k_t *ctx, struct cmdBuf *cmdBuf)
{
  int c;

  while ((c = getTty1(ctx)) >= 0)
  {
    putTty1(ctx, c);
    if (c == CHAR_CR(ctx))
    {
      cmdBuf->eol = 1;
      *cmdBuf->ptr = '\0';
      return;                   /* may discard some chars in ttyBufR */
    }
    else if (c == CHAR_BS(ctx))
    {
      cmdBufBS(cmdBuf);
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
      putCmd1(c, cmdBuf);
    }
  }
}


static Cmdstat
cmdMode(m2k_t *ctx, struct cmdBuf *cmdBuf, st_sock *sock)
{
  fd_set rfds, wfds;
  Cmdstat stat;

  cmdBufReset(cmdBuf);
  ttyBufRReset(ctx);
  /*ttyBufWReset(ctx); */

  for (;;)
  {
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    if (ttyBufWReady(ctx))
      FD_SET(ctx->tty.rfd, &rfds);   /*flow control */
    if (ttyBufWHasData(ctx))
      FD_SET(ctx->tty.wfd, &wfds);

    if (select(ctx->tty.wfd + 1, &rfds, &wfds, NULL, NULL) < 0)
    {
      if (errno != EINTR)
        m2k_log(ctx, "select(): %s\n", strerror(errno));
      continue;
    }

    if (FD_ISSET(ctx->tty.wfd, &wfds))
    {
      if (ttyBufWrite(ctx, sock) != M2K_OK)
        return CMDST_PTY_CLOSED;
      if (cmdBuf->eol)
      {
        stat = cmdLex(ctx, (char *) cmdBuf->buf, sock);
        cmdBufReset(cmdBuf);
        switch (stat)
        {
        case CMDST_ATD:
        case CMDST_ATO:
          return stat;
        case CMDST_OK:
        case CMDST_ERROR:
          putTtyCmdstat(ctx, stat);
          break;
        default:;              /*CMDST_NOCMD */
        }
      }
    }
    if (FD_ISSET(ctx->tty.rfd, &rfds))
    {
      if (ttyBufRead(ctx, sock) != M2K_OK)
        return CMDST_PTY_CLOSED;
      cmdReadLoop(ctx, cmdBuf);
    }
  }
}


/* open a pty */
static int
openPtyMaster(m2k_t *ctx, const char *dev)
{
  int fd;

  fd = open(dev, O_RDWR);
  if (fd < 0)
  {
    m2k_log(ctx, "Pty open error.\n");
    return -1;
  }
  return fd;
}

#ifdef HAVE_GRANTPT

static int
getPtyMaster(m2k_t *ctx, char **line_return)
{
  int rc;
  char name[12], *temp_line, *line = NULL;
  int pty = -1;
  char *name1 = "pqrstuvwxyzPQRST", *name2 = "0123456789abcdef";
  char *p1, *p2;

#ifdef HAVE_GRANTPT
  pty = open("/dev/ptmx", O_RDWR);
  if (pty < 0)
    goto bsd;

  rc = grantpt(pty);
  if (rc < 0)
  {
    close(pty);
    goto bsd;
  }

  rc = unlockpt(pty);
  if (rc < 0)
  {
    close(pty);
    goto bsd;
  }

  temp_line = ptsname(pty);
  if (!temp_line)
  {
    close(pty);
    goto bsd;
  }
  line = malloc(strlen(temp_line) + 1);
  if (line == NULL)
  {
    close(pty);
    return -1;
  }
  strcpy(line, temp_line);

  *line_return = line;
  return pty;
#endif /* HAVE_GRANTPT */

bsd:

  strcpy(name, "/dev/pty??");
  for (p1 = name1; *p1; p1++)
  {
    name[8] = *p1;
    for (p2 = name2; *p2; p2++)
    {
      name[9] = *p2;
      pty = open(name, O_RDWR);
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
  line = malloc(strlen(name) + 1);
  if (line == NULL)
  {
    close(pty);
    return -1;
  }
  strcpy(line, name);
  line[5] = 't';
  rc = chown(line, getuid(), getgid());
  if (rc < 0)
  {
    /* TRANSLATORS: do not translate "tty" or "pty" */
    m2k_log(ctx, "Warning: could not change ownership of tty -- pty is insecure!\n");
  }
  rc = chmod(line, S_IRUSR | S_IWUSR | S_IWGRP);
  if (rc < 0)
  {
    /* TRANSLATORS: do not translate "tty" or "pty" */
    m2k_log(ctx, "Warning: could not change permissions of tty -- pty is insecure!\n");
  }
  *line_return = line;
  return pty;

bail:
  if (pty >= 0)
    close(pty);

  return -1;
}

#else

#define PTY00 "/dev/ptyXX"
#define PTY10 "pqrs"
#define PTY01 "0123456789abcdef"

static int
getPtyMaster(m2k_t *ctx, char *tty10, char *tty01)
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
      fd = open(dev, O_RDWR);
      if (fd >= 0)
      {
        *tty10 = *p10;
        *tty01 = *p01;
        return fd;
      }
    }
  }
  m2k_log(ctx, "No more pty devices available.\n");
  return -1;
}
#endif


int
main(int argc, char *const argv[])
{
#ifdef SOCKS
  SOCKSinit(argv[0]);
#endif

  m2k_t *ctx = m2k_new();
  if (ctx == NULL)
    return EXIT_FAILURE;

  struct st_cmdarg cmdarg;
  cmdargParse(argc, argv, &cmdarg);
  fputs(PACKAGE_STRING " " VERSION "\n", stdout);
  fputs("Enter 'at%q' to quit\n\n", stdout);

  switch (cmdarg.ttymode)
  {
#ifdef HAVE_GRANTPT
    char *ptyslave;
  case CA_SHOWDEV:
    ctx->tty.rfd = ctx->tty.wfd = getPtyMaster(ctx, &ptyslave);
    if (ctx->tty.rfd < 0)
      return EXIT_FAILURE;
    puts(ptyslave);
    return 0;
  case CA_COMMX:
    ctx->tty.rfd = ctx->tty.wfd = getPtyMaster(ctx, &ptyslave);
    if (ctx->tty.rfd < 0 ||
        commxForkExec(ctx, cmdarg.commx, ptyslave) != M2K_OK)
      return EXIT_FAILURE;
    break;
#else
    char c10, c01;
  case CA_SHOWDEV:
    ctx->tty.rfd = ctx->tty.wfd = getPtyMaster(ctx, &c10, &c01);
    if (ctx->tty.rfd < 0)
      return EXIT_FAILURE;
    printf("%c%c\n", c10, c01);
    return 0;
  case CA_COMMX:
    ctx->tty.rfd = ctx->tty.wfd = getPtyMaster(ctx, &c10, &c01);
    if (ctx->tty.rfd < 0 ||
        commxForkExec(ctx, cmdarg.commx, c10, c01) != M2K_OK)
      return EXIT_FAILURE;
    break;
#endif
  case CA_STDINOUT:
    ctx->tty.rfd = 0;
    ctx->tty.wfd = 1;
    setTty();
    break;
  case CA_DEVGIVEN:
    ctx->tty.rfd = ctx->tty.wfd = openPtyMaster(ctx, cmdarg.dev);
    if (ctx->tty.rfd < 0)
      return EXIT_FAILURE;
    break;
  }

  /* m2k_new() ran INITSTR + MODEMU2k env; apply user's -a option if given */
  {
    Cmdstat s = cmdLex(ctx, cmdarg.atcmd, &ctx->sock);
    if (s != CMDST_OK && s != CMDST_NOAT)
    {
      m2k_log(ctx, "Error in initialization commands.\r\n");
      CHAR_CR(ctx) = '\r';
      CHAR_LF(ctx) = '\n';
    }
  }

  struct cmdBuf cmdBuf;

CMDMODE:
  switch (cmdMode(ctx, &cmdBuf, &ctx->sock))
  {
  case CMDST_ATD:
    if (ctx->sock.alive)
    {
      putTtyCmdstat(ctx, CMDST_ERROR);
      goto CMDMODE;
    }
    goto DIAL;
  case CMDST_ATO:
    if (!ctx->sock.alive)
    {
      putTtyCmdstat(ctx, CMDST_NOCARRIER);
      goto CMDMODE;
    }
    goto ONLINE;
  case CMDST_PTY_CLOSED:
    m2k_free(ctx);
    return 0;
  default:;
  }

  m2k_free(ctx);
  return 1;

DIAL:
  telOptReset(ctx);
  switch (m2k_sockDial(ctx, &ctx->sock))
  {
  case 0:                      /* connect */
    goto ONLINE;
  case 1:                      /* error */
    putTtyCmdstat(ctx, CMDST_NOCARRIER);
    goto CMDMODE;
  default:;
  }

  m2k_free(ctx);
  return 1;

ONLINE:
  switch (m2k_online(ctx))
  {
  case M2K_OK:          /* connection lost */
  case M2K_ERR_CANCELED:  /* +++ escaped */
    goto CMDMODE;
  default:;
  }

  m2k_free(ctx);
  return 1;
}
