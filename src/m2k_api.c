/* m2k_api.c — implements the documented API symbols declared in modemu2k.h */

#include <arpa/telnet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "m2k_private.h"
#include "m2k_ctx.h"


/* ── relay helpers (moved from main.c) ───────────────────────────── */

/* socket input processing loop */
static void
sockReadLoop(m2k_t *ctx, st_sock *sock)
{
  static enum
  {
    SRL_NORM, SRL_IAC, SRL_CMD,
    SRL_SB, SRL_SBC, SRL_SBS, SRL_SBI
  } state;
  static int cmd;
  static int opt;
  int c;

  if (ctx->atcmd.pr)
  {
    while ((c = getSock1(ctx)) >= 0)
      putTty1(ctx, c);
  }
  else
  {
    while ((c = getSock1(ctx)) >= 0)
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
          putTty1(ctx, c);
          state = SRL_NORM;
          break;
        case SB:
          state = SRL_SB;
          break;
        default:
          state = SRL_NORM;
          telOptPrintCmd(ctx, "<", c);
        }
        break;
      case SRL_CMD:
        if (telOptHandle(ctx, cmd, c))
          sock->alive = 0;
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
        telOptSBHandle(ctx, opt);
        state = SRL_NORM;
        break;
      default:
        if (c == IAC)
          state = SRL_IAC;
        else
          putTty1(ctx, c);
      }
    }
  }
}


/* escape-sequence (+++) detection */

static struct
{
  enum
  { ESH_NORM, ESH_P1, ESH_P2, ESH_P3 } state;
  struct timeval plus1T;
  int checkSilence;
  struct timeval expireT;
} escSeq;

void
escSeqReset(void)
{
  escSeq.state = ESH_NORM;
}

static int
s12timePassed(m2k_t *ctx, const struct timeval *t1p, const struct timeval *t2p)
{
  struct timeval t;

  timevalSet10ms(&t, ctx->atcmd.s[12] * 2);
  timevalAdd(&t, t2p);
  return (timevalCmp(t1p, &t) > 0);
}

static void
escSeqHandle(m2k_t *ctx, int c)
{
  switch (escSeq.state)
  {
  case ESH_P1:
    if (c == CHAR_ESC(ctx) && !s12timePassed(ctx, &ctx->ttyBufR.newT, &escSeq.plus1T))
      escSeq.state = ESH_P2;
    else
      escSeq.state = ESH_NORM;
    break;
  case ESH_P2:
    if (c == CHAR_ESC(ctx) && !s12timePassed(ctx, &ctx->ttyBufR.newT, &escSeq.plus1T))
    {
      escSeq.checkSilence = 1;
      timevalSet10ms(&escSeq.expireT, ctx->atcmd.s[12] * 2);
      timevalAdd(&escSeq.expireT, &ctx->ttyBufR.newT);
      escSeq.state = ESH_P3;
    }
    else
      escSeq.state = ESH_NORM;
    break;
  case ESH_P3:
    escSeq.checkSilence = 0;
    escSeq.state = ESH_NORM;
    /* fall through — same as ESH_NORM first-char logic */
    /* FALLTHROUGH */
  case ESH_NORM:
    if (c == CHAR_ESC(ctx) && s12timePassed(ctx, &ctx->ttyBufR.newT, &ctx->ttyBufR.prevT))
    {
      escSeq.plus1T = ctx->ttyBufR.newT;
      escSeq.state = ESH_P1;
    }
  }
}


/* line buffer (for non-SGA mode) */

static struct
{
  uchar buf[LINEBUF_SIZE];
  uchar *ptr;
} lineBuf;

#define lineBufReset() { lineBuf.ptr = lineBuf.buf; }
#define putLine1(c) \
{ \
    if (lineBuf.ptr < lineBuf.buf + LINEBUF_SIZE) *lineBuf.ptr++ = (c); \
}
#define lineBufBS() \
{ \
    if (lineBuf.ptr > lineBuf.buf) lineBuf.ptr--; \
}


/* TTY input processing loop */
static void
ttyReadLoop(m2k_t *ctx)
{
  int c;

  if (ctx->atcmd.pr)
  {
    while (sockBufWReady(ctx) && (c = getTty1(ctx)) >= 0)
    {
      putSock1(ctx, c);
      escSeqHandle(ctx, c);
    }
  }
  else if (ctx->telOpt.sgasend)
  {
    while (sockBufWReady(ctx) && (c = getTty1(ctx)) >= 0)
    {
      if (c == IAC)
        putSock1(ctx, IAC);
      putSock1(ctx, c);
      escSeqHandle(ctx, c);
    }
  }
  else
  {
    while ((c = getTty1(ctx)) >= 0)
    {
      putTty1(ctx, c);
      if (c == CHAR_CR(ctx))
      {
        putTty1(ctx, CHAR_LF(ctx));
        putSockN(ctx, lineBuf.buf, lineBuf.ptr - lineBuf.buf);
        putSock1(ctx, '\r');
        putSock1(ctx, '\n');
        lineBufReset();
      }
      else if (c == CHAR_LF(ctx))
      {
        /* ignore LFs */
      }
      else if (c == CHAR_BS(ctx))
      {
        lineBufBS();
      }
      else
      {
        if (c == IAC)
          putLine1(IAC);
        putLine1(c);
      }
      escSeqHandle(ctx, c);
    }
  }
}


/* online mode relay loop — returns 0=connection lost, 1=+++ escape */
static int
onlineMode(m2k_t *ctx, st_sock *sock)
{
  fd_set rfds, wfds;
  struct timeval t;

  sockBufRReset(ctx);
  sockBufWReset(ctx);
  ttyBufRReset(ctx);
  lineBufReset();
  escSeqReset();

  if (!ctx->telOpt.sentReqs && !ctx->atcmd.pr)
    telOptSendReqs(ctx);

  t.tv_sec = 0;
  while (sock->alive)
  {
    struct timeval *tp;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    if (ttyBufWReady(ctx))
      FD_SET(sock->fd, &rfds);
    if (sockBufWHasData(ctx))
      FD_SET(sock->fd, &wfds);
    if (sockBufWReady(ctx) && !ttyBufRHasData(ctx))
      FD_SET(ctx->tty.rfd, &rfds);
    if (ttyBufWHasData(ctx))
      FD_SET(ctx->tty.wfd, &wfds);

    if (escSeq.checkSilence)
    {
      struct timeval tt;
      gettimeofday(&tt, NULL);
      if (timevalCmp(&tt, &escSeq.expireT) >= 0)
      {
        escSeq.checkSilence = 0;
        return 1;
      }
      t = escSeq.expireT;
      timevalSub(&t, &tt);
      tp = &t;
    }
    else
    {
      tp = NULL;
    }

    if (select(sock->fd + 1, &rfds, &wfds, NULL, tp) < 0)
    {
      if (errno != EINTR)
        m2k_log(ctx, "select(): %s\n", strerror(errno));
      continue;
    }

    if (FD_ISSET(sock->fd, &wfds))
    {
      sockBufWrite(ctx, sock);
      if (sock->alive && ttyBufRHasData(ctx) && sockBufWReady(ctx))
        ttyReadLoop(ctx);
    }
    if (FD_ISSET(ctx->tty.wfd, &wfds))
    {
      if (ttyBufWrite(ctx, sock) != M2K_OK)
        break;
    }
    if (FD_ISSET(sock->fd, &rfds))
    {
      sockBufRead(ctx, sock);
      sockReadLoop(ctx, sock);
    }
    if (FD_ISSET(ctx->tty.rfd, &rfds))
    {
      if (ttyBufRead(ctx, sock) != M2K_OK)
        break;
      ttyReadLoop(ctx);
    }
  }
  sockShutdown(sock);
  return 0;
}


/* output modem result code to tty */
void
putTtyCmdstat(m2k_t *ctx, Cmdstat s)
{
  static const char *cmdstatStr[] = {
    "OK",
    "ERROR",
    "CONNECT",
    "NO CARRIER",
    "",
    "",
    "",
    "",
  };

  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTtyN(ctx, cmdstatStr[s], strlen(cmdstatStr[s]));

  if (s == CMDST_CONNECT)
  {
    static const char msg_escape_seq[] = "To escape to command mode, use '+++'.";
    static const char msg_return_online[] = "Use ATO to return to online mode.";

    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    putTtyN(ctx, msg_escape_seq, sizeof msg_escape_seq);

    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    putTtyN(ctx, msg_return_online, sizeof msg_return_online);
  }

  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
}


/* ── public API ──────────────────────────────────────────────────── */

m2k_t *
m2k_new(void)
{
  m2k_t *ctx = calloc(1, sizeof(m2k_t));
  if (ctx == NULL)
    return NULL;
  sockInit(&ctx->sock);
  ttyBufWReset(ctx);
  telOptInit(ctx);
  atcmdInit(ctx, NULL, &ctx->sock);
  return ctx;
}

void
m2k_free(m2k_t *ctx)
{
  if (ctx == NULL)
    return;
  sockShutdown(&ctx->sock);
  free(ctx);
}

void
m2k_set_log_fn(m2k_t *ctx, m2k_log_fn fn, void *userdata)
{
  ctx->log_fn = fn;
  ctx->log_userdata = userdata;
}

m2k_err_t
m2k_atcmd(m2k_t *ctx, const char *cmd)
{
  Cmdstat r = cmdLex(ctx, cmd, &ctx->sock);
  return (r == CMDST_OK || r == CMDST_NOAT) ? M2K_OK : M2K_ERR_BUG;
}

m2k_err_t
m2k_dial(m2k_t *ctx, const char *host, const char *port)
{
  strncpy(ctx->atcmd.d.addr.str, host, ADDR_MAX);
  ctx->atcmd.d.addr.str[ADDR_MAX] = '\0';
  ctx->atcmd.d.addr.type = ATDA_STR;

  if (port != NULL)
  {
    strncpy(ctx->atcmd.d.port.str, port, PORT_MAX);
    ctx->atcmd.d.port.str[PORT_MAX] = '\0';
    ctx->atcmd.d.port.type = ATDP_STR;
  }
  else
  {
    ctx->atcmd.d.port.str[0] = '\0';
    ctx->atcmd.d.port.type = ATDP_NUL;
  }

  telOptReset(ctx);
  return m2k_sockDial(ctx, &ctx->sock) == 0 ? M2K_OK : M2K_ERR_SOCKET;
}

m2k_err_t
m2k_online(m2k_t *ctx)
{
  putTtyCmdstat(ctx, CMDST_CONNECT);
  int r = onlineMode(ctx, &ctx->sock);
  if (r == 1)
  {
    putTtyCmdstat(ctx, CMDST_OK);
    return M2K_ERR_CANCELED;
  }
  putTtyCmdstat(ctx, CMDST_NOCARRIER);
  return M2K_OK;
}

m2k_err_t
m2k_hangup(m2k_t *ctx)
{
  sockShutdown(&ctx->sock);
  return M2K_OK;
}

const char *
m2k_strerror(m2k_err_t err)
{
  static const char *strs[] = {
    "Success",
    "Out of memory",
    "PTY error",
    "Socket error",
    "Connection timed out",
    "Operation canceled",
    "Internal bug",
  };
  if ((unsigned) err < sizeof(strs) / sizeof(strs[0]))
    return strs[err];
  return "Unknown error";
}


/* ── PTY allocation (moved from main.c) ─────────────────────────── */

#ifdef HAVE_GRANTPT
static int
getPtyMaster(m2k_t *ctx, char **line_return)
{
  int rc;
  char name[12], *temp_line, *line = NULL;
  int pty = -1;
  char *name1 = "pqrstuvwxyzPQRST", *name2 = "0123456789abcdef";
  char *p1, *p2;

  pty = open("/dev/ptmx", O_RDWR);
  if (pty < 0)
    goto bsd;

  rc = grantpt(pty);
  if (rc < 0) { close(pty); goto bsd; }

  rc = unlockpt(pty);
  if (rc < 0) { close(pty); goto bsd; }

  temp_line = ptsname(pty);
  if (!temp_line) { close(pty); goto bsd; }

  line = malloc(strlen(temp_line) + 1);
  if (line == NULL) { close(pty); return -1; }
  strcpy(line, temp_line);
  *line_return = line;
  return pty;

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
    }
  }
  goto bail;

found:
  line = malloc(strlen(name) + 1);
  if (line == NULL) { close(pty); return -1; }
  strcpy(line, name);
  line[5] = 't';
  rc = chown(line, getuid(), getgid());
  if (rc < 0)
    m2k_log(ctx, "Warning: could not change ownership of tty -- pty is insecure!\n");
  rc = chmod(line, S_IRUSR | S_IWUSR | S_IWGRP);
  if (rc < 0)
    m2k_log(ctx, "Warning: could not change permissions of tty -- pty is insecure!\n");
  *line_return = line;
  return pty;

bail:
  if (pty >= 0)
    close(pty);
  return -1;
}

#else  /* !HAVE_GRANTPT */

static int
getPtyMaster(m2k_t *ctx, char *tty10, char *tty01)
{
  (void)ctx;
  static const char *name1 = "pqrs";
  static const char *name2 = "0123456789abcdef";
  static char dev[] = "/dev/ptyXX";
  const char *p10, *p01;
  int fd;

  for (p10 = name1; *p10 != '\0'; p10++)
  {
    dev[8] = *p10;
    for (p01 = name2; *p01 != '\0'; p01++)
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
#endif  /* HAVE_GRANTPT */


/* ── Command mode (moved from main.c) ───────────────────────────── */

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
      return;
    }
    else if (c == CHAR_BS(ctx))
      cmdBufBS(cmdBuf);
    else if (c < ' ' || c == 127)
      ; /* ignore */
    else
      putCmd1(c, cmdBuf);
  }
}

static Cmdstat
cmdMode(m2k_t *ctx, struct cmdBuf *cmdBuf)
{
  st_sock *sock = &ctx->sock;
  fd_set rfds, wfds;
  Cmdstat stat;

  cmdBufReset(cmdBuf);
  ttyBufRReset(ctx);

  for (;;)
  {
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    if (ttyBufWReady(ctx))
      FD_SET(ctx->tty.rfd, &rfds);
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
        default:;
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


/* ── Setup API ───────────────────────────────────────────────────── */

m2k_err_t
m2k_setup_stdin(m2k_t *ctx)
{
  ctx->tty.rfd = 0;
  ctx->tty.wfd = 1;
  setTty();
  return M2K_OK;
}

m2k_err_t
m2k_setup_pty(m2k_t *ctx, char **slave_out)
{
#ifdef HAVE_GRANTPT
  int fd = getPtyMaster(ctx, slave_out);
  if (fd < 0)
    return M2K_ERR_PTY;
  ctx->tty.rfd = ctx->tty.wfd = fd;
  return M2K_OK;
#else
  char c10, c01;
  int fd = getPtyMaster(ctx, &c10, &c01);
  if (fd < 0)
    return M2K_ERR_PTY;
  ctx->tty.rfd = ctx->tty.wfd = fd;
  char *slave = malloc(3);
  if (!slave)
    return M2K_ERR_NOMEM;
  slave[0] = c10;
  slave[1] = c01;
  slave[2] = '\0';
  *slave_out = slave;
  return M2K_OK;
#endif
}

m2k_err_t
m2k_setup_commx(m2k_t *ctx, const char *cmd)
{
#ifdef HAVE_GRANTPT
  char *slave;
  int fd = getPtyMaster(ctx, &slave);
  if (fd < 0)
    return M2K_ERR_PTY;
  ctx->tty.rfd = ctx->tty.wfd = fd;
  m2k_err_t err = commxForkExec(ctx, cmd, slave);
  free(slave);
  return err;
#else
  char c10, c01;
  int fd = getPtyMaster(ctx, &c10, &c01);
  if (fd < 0)
    return M2K_ERR_PTY;
  ctx->tty.rfd = ctx->tty.wfd = fd;
  return commxForkExec(ctx, cmd, c10, c01);
#endif
}

m2k_err_t
m2k_setup_dev(m2k_t *ctx, const char *dev)
{
  int fd = open(dev, O_RDWR);
  if (fd < 0)
  {
    m2k_log(ctx, "Pty open error.\n");
    return M2K_ERR_PTY;
  }
  ctx->tty.rfd = ctx->tty.wfd = fd;
  return M2K_OK;
}

m2k_err_t
m2k_setup_listen(m2k_t *ctx, const char *port)
{
  int fd = m2k_sockListen(ctx, port);
  if (fd == -1)
    return M2K_ERR_SOCKET;
  ctx->tty.rfd = ctx->tty.wfd = fd;
  return M2K_OK;
}

m2k_err_t
m2k_run(m2k_t *ctx)
{
  struct cmdBuf cmdBuf;

CMDMODE:
  switch (cmdMode(ctx, &cmdBuf))
  {
  case CMDST_ATD:
    if (ctx->sock.alive) { putTtyCmdstat(ctx, CMDST_ERROR); goto CMDMODE; }
    goto DIAL;
  case CMDST_ATO:
    if (!ctx->sock.alive) { putTtyCmdstat(ctx, CMDST_NOCARRIER); goto CMDMODE; }
    goto ONLINE;
  case CMDST_PTY_CLOSED:
    return M2K_OK;
  default:
    return M2K_ERR_BUG;
  }

DIAL:
  telOptReset(ctx);
  if (m2k_sockDial(ctx, &ctx->sock) != 0)
  {
    putTtyCmdstat(ctx, CMDST_NOCARRIER);
    goto CMDMODE;
  }
  goto ONLINE;

ONLINE:
  switch (m2k_online(ctx))
  {
  case M2K_OK:
  case M2K_ERR_CANCELED:
    goto CMDMODE;
  default:
    return M2K_ERR_BUG;
  }
}
