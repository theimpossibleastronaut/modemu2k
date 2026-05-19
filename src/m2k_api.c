
#include <arpa/telnet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include "modemu2k.h"
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
