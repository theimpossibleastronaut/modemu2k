/* m2k_api.c — implements the documented API symbols declared in modemu2k.h */

#include <arpa/telnet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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
  static enum {
    SRL_NORM,
    SRL_IAC,
    SRL_CMD,
    SRL_SB,
    SRL_SBC,
    SRL_SBS,
    SRL_SBI
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


/* escape-sequence (+++) detection — state lives in ctx->escSeq so two
   coexisting m2k_t instances don't share/corrupt timing. */

void
escSeqReset(m2k_t *ctx)
{
  ctx->escSeq.state = ESH_NORM;
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
  struct m2k_escseq *es = &ctx->escSeq;
  switch (es->state)
  {
  case ESH_P1:
    if (c == CHAR_ESC(ctx) && !s12timePassed(ctx, &ctx->ttyBufR.newT, &es->plus1T))
      es->state = ESH_P2;
    else
      es->state = ESH_NORM;
    break;
  case ESH_P2:
    if (c == CHAR_ESC(ctx) && !s12timePassed(ctx, &ctx->ttyBufR.newT, &es->plus1T))
    {
      es->checkSilence = 1;
      timevalSet10ms(&es->expireT, ctx->atcmd.s[12] * 2);
      timevalAdd(&es->expireT, &ctx->ttyBufR.newT);
      es->state = ESH_P3;
    }
    else
      es->state = ESH_NORM;
    break;
  case ESH_P3:
    es->checkSilence = 0;
    es->state = ESH_NORM;
    /* fall through — same as ESH_NORM first-char logic */
    /* FALLTHROUGH */
  case ESH_NORM:
    if (c == CHAR_ESC(ctx) && s12timePassed(ctx, &ctx->ttyBufR.newT, &ctx->ttyBufR.prevT))
    {
      es->plus1T = ctx->ttyBufR.newT;
      es->state = ESH_P1;
    }
  }
}


/* line buffer (for non-SGA mode) — state lives in ctx->lineBuf. */

#define lineBufReset(ctx) ((ctx)->lineBuf.ptr = (ctx)->lineBuf.buf)
#define putLine1(ctx, c)                                       \
  do                                                           \
  {                                                            \
    struct m2k_linebuf *_lb = &(ctx)->lineBuf;                 \
    if (_lb->ptr < _lb->buf + LINEBUF_SIZE) *_lb->ptr++ = (c); \
  } while (0)
#define lineBufBS(ctx)                         \
  do                                           \
  {                                            \
    struct m2k_linebuf *_lb = &(ctx)->lineBuf; \
    if (_lb->ptr > _lb->buf) _lb->ptr--;       \
  } while (0)


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
        putSockN(ctx, ctx->lineBuf.buf, ctx->lineBuf.ptr - ctx->lineBuf.buf);
        putSock1(ctx, '\r');
        putSock1(ctx, '\n');
        lineBufReset(ctx);
      }
      else if (c == CHAR_LF(ctx))
      {
        /* ignore LFs */
      }
      else if (c == CHAR_BS(ctx))
      {
        lineBufBS(ctx);
      }
      else
      {
        if (c == IAC)
          putLine1(ctx, IAC);
        putLine1(ctx, c);
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
  lineBufReset(ctx);
  escSeqReset(ctx);

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

    if (ctx->escSeq.checkSilence)
    {
      struct timeval tt;
      gettimeofday(&tt, NULL);
      if (timevalCmp(&tt, &ctx->escSeq.expireT) >= 0)
      {
        ctx->escSeq.checkSilence = 0;
        return 1;
      }
      t = ctx->escSeq.expireT;
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

static void
cmdBufReset(struct m2k_cmdbuf *x)
{
  x->ptr = x->buf;
  x->eol = 0;
}

m2k_t *
m2k_new(void)
{
  /* ctx==NULL here: m2k_calloc falls back to stderr for the failure log. */
  m2k_t *ctx = m2k_calloc(NULL, 1, sizeof(m2k_t));
  if (!ctx)
    return NULL;
  ctx->listen_fd = -1;
  ctx->step_state = M2K_STATE_CMD;
  ctx->dtr = true;
  ctx->rts = true;
  cmdBufReset(&ctx->step_cmdbuf);
  sockInit(&ctx->sock);
  ttyBufRReset(ctx);
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
  if (ctx->listen_fd != -1)
    close(ctx->listen_fd);
  sockShutdown(&ctx->sock);
  free(ctx);
}

void
m2k_set_log_fn(m2k_t *ctx, m2k_log_fn fn, void *userdata)
{
  ctx->log_fn = fn;
  ctx->log_userdata = userdata;
}

void
m2k_set_error_buffer(m2k_t *ctx, char *buf, size_t size)
{
  ctx->err_buf = buf;
  ctx->err_buf_size = buf ? size : 0;
  if (buf && size)
    buf[0] = '\0';
}

m2k_err_t
m2k_atcmd(m2k_t *ctx, const char *cmd)
{
  Cmdstat r = cmdLex(ctx, cmd, &ctx->sock);
  switch (r)
  {
  case CMDST_OK:
  case CMDST_NOAT:
    return M2K_OK;
  case CMDST_ERROR:
    m2k_err_set(ctx, "AT command rejected: \"%s\"\n", cmd);
    return M2K_ERR_AT;
  case CMDST_ATD:
    m2k_err_set(ctx, "ATD is not actionable from m2k_atcmd; use m2k_dial()\n");
    return M2K_ERR_AT;
  case CMDST_ATO:
    m2k_err_set(ctx, "ATO is not actionable from m2k_atcmd; use m2k_online()\n");
    return M2K_ERR_AT;
  default:
    m2k_err_set(ctx, "m2k_atcmd: unexpected lexer status %d for \"%s\"\n", r, cmd);
    return M2K_ERR_BUG;
  }
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
m2k_escape(m2k_t *ctx)
{
  if (ctx->step_state == M2K_STATE_ONLINE)
    ctx->escape_req = true;
  return M2K_OK;
}

const char *
m2k_version(void)
{
  return M2K_VERSION;
}

const char *
m2k_strerror(m2k_err_t err)
{
  static const char *strs[] = {
    "Success",                            /* M2K_OK */
    "Out of memory",                      /* M2K_ERR_NOMEM */
    "PTY error",                          /* M2K_ERR_PTY */
    "Socket error",                       /* M2K_ERR_SOCKET */
    "Connection timed out",               /* M2K_ERR_TIMEOUT */
    "Operation canceled",                 /* M2K_ERR_CANCELED */
    "Internal bug",                       /* M2K_ERR_BUG */
    "Operation would block; retry later", /* M2K_ERR_WOULDBLOCK */
    "AT command rejected",                /* M2K_ERR_AT */
  };
  if ((unsigned) err < sizeof(strs) / sizeof(strs[0]))
    return strs[err];
  return "Unknown error";
}


/* ── PTY allocation (moved from main.c) ─────────────────────────── */

static int
getPtyMaster(m2k_t *ctx)
{
  /* posix_openpt is portable across Linux/macOS/FreeBSD; FreeBSD in
     particular doesn't reliably expose /dev/ptmx as a devnode.
     Meson refuses to configure without it. */
  int pty = posix_openpt(O_RDWR | O_NOCTTY);
  if (pty < 0)
    return -1;

  if (grantpt(pty) < 0 || unlockpt(pty) < 0)
  {
    close(pty);
    return -1;
  }

  const char *slave = ptsname(pty);
  if (!slave || strlen(slave) >= sizeof(ctx->slave_path))
  {
    close(pty);
    return -1;
  }
  strcpy(ctx->slave_path, slave);
  return pty;
}


/* ── Command mode (moved from main.c) ───────────────────────────── */

/* struct m2k_cmdbuf is defined in m2k_private.h so the m2k_t context
   can embed one for the steppable m2k_step() API. */

static void
putCmd1(const int c, struct m2k_cmdbuf *cmdBuf)
{
  if (cmdBuf->ptr < cmdBuf->buf + CMDBUF_MAX)
    *cmdBuf->ptr++ = c;
}

static void
cmdBufBS(struct m2k_cmdbuf *cmdBuf)
{
  if (cmdBuf->ptr > cmdBuf->buf)
    cmdBuf->ptr--;
}

static void
cmdReadLoop(m2k_t *ctx, struct m2k_cmdbuf *cmdBuf)
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

/* cmdMode and onlineMode are now expressed via the cmdPollfds/cmdIter
   and onlinePollfds/onlineIter primitives further below (used by
   m2k_step). m2k_online() still calls the legacy onlineMode wrapper
   because the public m2k_dial/m2k_online surface is synchronous. */


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
m2k_setup_pty(m2k_t *ctx, const char **slave_out)
{
  int fd = getPtyMaster(ctx);
  if (fd < 0)
    return M2K_ERR_PTY;
  ctx->tty.rfd = ctx->tty.wfd = fd;
  *slave_out = ctx->slave_path;
  return M2K_OK;
}

m2k_err_t
m2k_setup_comm_program(m2k_t *ctx, const char *cmd)
{
  int fd = getPtyMaster(ctx);
  if (fd < 0)
    return M2K_ERR_PTY;
  ctx->tty.rfd = ctx->tty.wfd = fd;
  return commProgramForkExec(ctx, cmd, ctx->slave_path);
}

m2k_err_t
m2k_setup_dev(m2k_t *ctx, const char *dev)
{
  int fd = open(dev, O_RDWR);
  if (fd < 0)
  {
    m2k_err_set(ctx, "open(%s): %s\n", dev, strerror(errno));
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
  ctx->listen_fd = fd;
  return M2K_OK;
}

m2k_err_t
m2k_listen_accept(m2k_t *ctx)
{
  if (ctx->listen_fd == -1)
  {
    m2k_err_set(ctx, "m2k_listen_accept: no listener (call m2k_setup_listen first)\n");
    return M2K_ERR_SOCKET;
  }
  int client_fd = m2k_sockAccept(ctx, ctx->listen_fd);
  ctx->listen_fd = -1;
  if (client_fd == -1)
    return M2K_ERR_SOCKET;
  ctx->tty.rfd = ctx->tty.wfd = client_fd;
  return M2K_OK;
}

/* ── App-IO (embed) mode ──────────────────────────────────────────────
   m2k_write_from_app appends to ttyBufR (replacing read() from tty.rfd);
   m2k_read_to_app drains ttyBufW (replacing write() to tty.wfd). The
   socket side still uses real fds. */

m2k_err_t
m2k_setup_app_io(m2k_t *ctx)
{
  ctx->app_io = true;
  ctx->tty.rfd = ctx->tty.wfd = -1;
  return M2K_OK;
}

m2k_err_t
m2k_write_from_app(m2k_t *ctx, const void *buf, size_t len, size_t *consumed)
{
  *consumed = 0;
  if (!ctx->app_io)
  {
    m2k_err_set(ctx, "m2k_write_from_app: not in app-I/O mode (call m2k_setup_app_io first)\n");
    return M2K_ERR_PTY;
  }
  if (len == 0)
    return M2K_OK;

  size_t residue = ctx->ttyBufR.end - ctx->ttyBufR.ptr;
  size_t cap = sizeof(ctx->ttyBufR.buf);
  if (residue >= cap)
  {
    m2k_err_set(ctx, "m2k_write_from_app: TTY read buffer full (%zu bytes pending)\n",
                residue);
    return M2K_ERR_WOULDBLOCK;
  }
  size_t room = cap - residue;
  size_t take = len < room ? len : room;

  if (residue && ctx->ttyBufR.ptr != ctx->ttyBufR.buf)
    memmove(ctx->ttyBufR.buf, ctx->ttyBufR.ptr, residue);
  memcpy(ctx->ttyBufR.buf + residue, buf, take);
  ctx->ttyBufR.ptr = ctx->ttyBufR.buf;
  ctx->ttyBufR.end = ctx->ttyBufR.buf + residue + take;
  ctx->ttyBufR.prevT = ctx->ttyBufR.newT;
  gettimeofday(&ctx->ttyBufR.newT, NULL);
  *consumed = take;
  return M2K_OK;
}

m2k_err_t
m2k_read_to_app(m2k_t *ctx, void *buf, size_t max, size_t *len_out)
{
  if (!ctx->app_io)
  {
    *len_out = 0;
    return M2K_ERR_PTY;
  }
  size_t available = ctx->ttyBufW.ptr - ctx->ttyBufW.top;
  size_t n = available < max ? available : max;
  if (n)
    memcpy(buf, ctx->ttyBufW.top, n);
  ctx->ttyBufW.top += n;
  if (ctx->ttyBufW.top >= ctx->ttyBufW.ptr)
  {
    /* Buffer fully drained — rewind to make room for new output. */
    ctx->ttyBufW.ptr = ctx->ttyBufW.top = ctx->ttyBufW.buf;
    ctx->ttyBufW.stop = 0;
  }
  *len_out = n;
  return M2K_OK;
}

int
m2k_has_pending_output(const m2k_t *ctx)
{
  if (!ctx || !ctx->app_io)
    return 0;
  return ctx->ttyBufW.ptr > ctx->ttyBufW.top;
}

/* ── Steppable event-loop API ────────────────────────────────────────
   m2k_run() is a poll()-driven wrapper over m2k_get_pollfds + m2k_step.

       CMD    ── ATD (alive=0) ──▶ (dial inline) ──▶ ONLINE on success
                                                  ──▶ CMD on dial fail
       CMD    ── ATO (alive=1) ──▶ ONLINE
       CMD    ── PTY closed    ──▶ DONE
       ONLINE ── +++ escape    ──▶ CMD
       ONLINE ── sock dead     ──▶ CMD
       ONLINE ── PTY closed    ──▶ DONE                                 */

static void
stepEnterCmd(m2k_t *ctx)
{
  cmdBufReset(&ctx->step_cmdbuf);
  ttyBufRReset(ctx);
  ctx->step_state = M2K_STATE_CMD;
}

static void
stepEnterOnline(m2k_t *ctx)
{
  sockBufRReset(ctx);
  sockBufWReset(ctx);
  ttyBufRReset(ctx);
  lineBufReset(ctx);
  escSeqReset(ctx);
  if (!ctx->telOpt.sentReqs && !ctx->atcmd.pr)
    telOptSendReqs(ctx);
  putTtyCmdstat(ctx, CMDST_CONNECT);
  ctx->step_state = M2K_STATE_ONLINE;
}

static void
stepEnterDial(m2k_t *ctx)
{
  /* Drop any pending TTY input — semantics of going off-hook. */
  ttyBufRReset(ctx);
  cmdBufReset(&ctx->step_cmdbuf);
  ctx->step_state = M2K_STATE_DIAL;
}

m2k_err_t
m2k_hangup(m2k_t *ctx)
{
  /* Abort a dial-in-progress so the host can recover the context
     without waiting out the S7 timeout. */
  if (ctx->step_state == M2K_STATE_DIAL)
  {
    m2k_sockDialAbort(ctx, &ctx->sock);
    putTtyCmdstat(ctx, CMDST_NOCARRIER);
    stepEnterCmd(ctx);
    return M2K_OK;
  }
  sockShutdown(&ctx->sock);
  return M2K_OK;
}

static struct pollfd *
findPollfd(struct pollfd *fds, size_t nfds, int fd)
{
  for (size_t i = 0; i < nfds; i++)
    if (fds[i].fd == fd)
      return &fds[i];
  return NULL;
}

/* POLLHUP/POLLERR fire regardless of requested events — fold them into
   the read trigger so EOF closure flows through the read path. */
#define READ_EV (POLLIN | POLLHUP | POLLERR)
#define WRITE_EV (POLLOUT | POLLERR)

/* One entry if rfd==wfd; else one per fd, each only when its mask is set. */
static void
appendTtyPollfds(m2k_t *ctx, struct pollfd *fds, size_t *n,
                 short read_ev, short write_ev)
{
  if (ctx->app_io)
    return;
  if (ctx->tty.rfd == ctx->tty.wfd)
  {
    fds[*n].fd = ctx->tty.rfd;
    fds[*n].events = read_ev | write_ev;
    fds[*n].revents = 0;
    (*n)++;
  }
  else
  {
    if (read_ev)
    {
      fds[*n].fd = ctx->tty.rfd;
      fds[*n].events = read_ev;
      fds[*n].revents = 0;
      (*n)++;
    }
    if (write_ev)
    {
      fds[*n].fd = ctx->tty.wfd;
      fds[*n].events = write_ev;
      fds[*n].revents = 0;
      (*n)++;
    }
  }
}

static void
cmdPollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  size_t n = 0;

  if (ctx->app_io)
  {
    *nfds_inout = 0;
    /* timeout=0 only if there's already work to do; otherwise let the
       host's poll() block on its own fds until it has bytes to push us. */
    *timeout_ms = (ttyBufRHasData(ctx) || ctx->step_cmdbuf.eol) ? 0 : -1;
    return;
  }

  appendTtyPollfds(ctx, fds, &n,
                   ttyBufWReady(ctx) ? POLLIN : 0,
                   ttyBufWHasData(ctx) ? POLLOUT : 0);
  *nfds_inout = n;
  *timeout_ms = -1;
}

static Cmdstat
cmdDispatchIfReady(m2k_t *ctx, struct m2k_cmdbuf *cmdBuf, st_sock *sock)
{
  if (!cmdBuf->eol)
    return CMDST_OK;
  Cmdstat stat = cmdLex(ctx, (char *) cmdBuf->buf, sock);
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
  return CMDST_OK;
}

static Cmdstat
cmdIter(m2k_t *ctx, struct pollfd *fds, size_t nfds)
{
  st_sock *sock = &ctx->sock;
  struct m2k_cmdbuf *cmdBuf = &ctx->step_cmdbuf;

  if (ctx->app_io)
  {
    for (;;)
    {
      Cmdstat s = cmdDispatchIfReady(ctx, cmdBuf, sock);
      if (s != CMDST_OK)
        return s;
      if (!ttyBufRHasData(ctx))
        return CMDST_OK;
      cmdReadLoop(ctx, cmdBuf);
      if (!cmdBuf->eol)
        return CMDST_OK;
    }
  }

  struct pollfd *p;
  p = findPollfd(fds, nfds, ctx->tty.wfd);
  if (p && (p->revents & WRITE_EV))
  {
    if (ttyBufWrite(ctx, sock) != M2K_OK)
      return CMDST_PTY_CLOSED;
    Cmdstat s = cmdDispatchIfReady(ctx, cmdBuf, sock);
    if (s != CMDST_OK)
      return s;
  }
  p = findPollfd(fds, nfds, ctx->tty.rfd);
  if (p && (p->revents & READ_EV))
  {
    if (ttyBufRead(ctx, sock) != M2K_OK)
      return CMDST_PTY_CLOSED;
    cmdReadLoop(ctx, cmdBuf);
  }
  return CMDST_OK;
}

/* Dial mode: wait on the in-progress connect socket (POLLOUT) plus the
   TTY rfd (so a keypress can cancel the dial). Timeout is the remaining
   time until the S7 deadline. */
static void
dialPollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  size_t n = 0;

  fds[n].fd = ctx->sock.fd;
  fds[n].events = POLLOUT;
  fds[n].revents = 0;
  n++;

  /* Allow tty read so the user can cancel by typing. No write side here
     — we have nothing to emit until the dial resolves. */
  appendTtyPollfds(ctx, fds, &n, POLLIN, 0);
  *nfds_inout = n;

  struct timeval now, remaining;
  gettimeofday(&now, NULL);
  if (timevalCmp(&now, &ctx->dial_deadline) >= 0)
  {
    *timeout_ms = 0;
    return;
  }
  remaining = ctx->dial_deadline;
  timevalSub(&remaining, &now);
  long ms = remaining.tv_sec * 1000L + remaining.tv_usec / 1000L;
  if (ms <= 0) ms = 1;
  *timeout_ms = (int) ms;
}

/* Returns 1 (connected — caller transitions to ONLINE), 0 (continue
   polling), -1 (dial failed — caller emits NOCARRIER + back to CMD). */
static int
dialIter(m2k_t *ctx, struct pollfd *fds, size_t nfds)
{
  /* User-cancel via TTY input. Only meaningful outside app_io; in
     app_io the host can call m2k_hangup() to abort. */
  if (!ctx->app_io)
  {
    struct pollfd *p = findPollfd(fds, nfds, ctx->tty.rfd);
    if (p && (p->revents & READ_EV))
    {
      m2k_sockDialAbort(ctx, &ctx->sock);
      return -1;
    }
  }
  /* Whether the socket fired or the deadline elapsed, ask Progress. */
  return m2k_sockDialProgress(ctx, &ctx->sock);
}

/* timeout_ms is driven by the +++ escape silence guard; expiry is
   detected inside onlineIter after poll returns. */
static void
onlinePollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  st_sock *sock = &ctx->sock;
  size_t n = 0;

  short sock_ev = 0;
  if (ttyBufWReady(ctx)) sock_ev |= POLLIN;
  if (sockBufWHasData(ctx)) sock_ev |= POLLOUT;
  fds[n].fd = sock->fd;
  fds[n].events = sock_ev;
  fds[n].revents = 0;
  n++;

  appendTtyPollfds(ctx, fds, &n,
                   (sockBufWReady(ctx) && !ttyBufRHasData(ctx)) ? POLLIN : 0,
                   ttyBufWHasData(ctx) ? POLLOUT : 0);
  *nfds_inout = n;

  /* Compute timeout for the +++ silence guard. */
  if (ctx->escSeq.checkSilence)
  {
    struct timeval now, remaining;
    gettimeofday(&now, NULL);
    if (timevalCmp(&now, &ctx->escSeq.expireT) >= 0)
    {
      *timeout_ms = 0;
    }
    else
    {
      remaining = ctx->escSeq.expireT;
      timevalSub(&remaining, &now);
      long ms = remaining.tv_sec * 1000L + remaining.tv_usec / 1000L;
      if (ms <= 0) ms = 1;
      *timeout_ms = (int) ms;
    }
  }
  else
  {
    *timeout_ms = -1;
  }
}

/* Returns 0 (continue), 1 (+++ escape elapsed or m2k_escape requested),
   -1 (sock or PTY closed). */
static int
onlineIter(m2k_t *ctx, struct pollfd *fds, size_t nfds)
{
  st_sock *sock = &ctx->sock;
  struct pollfd *p;

  if (ctx->escape_req)
  {
    ctx->escape_req = false;
    ctx->escSeq.checkSilence = 0;
    return 1;
  }

  if (ctx->escSeq.checkSilence)
  {
    struct timeval now;
    gettimeofday(&now, NULL);
    if (timevalCmp(&now, &ctx->escSeq.expireT) >= 0)
    {
      ctx->escSeq.checkSilence = 0;
      return 1;
    }
  }

  p = findPollfd(fds, nfds, sock->fd);
  if (p && (p->revents & WRITE_EV))
  {
    sockBufWrite(ctx, sock);
    if (sock->alive && ttyBufRHasData(ctx) && sockBufWReady(ctx))
      ttyReadLoop(ctx);
  }

  if (ctx->app_io)
  {
    if (ttyBufRHasData(ctx))
      ttyReadLoop(ctx);
  }
  else
  {
    p = findPollfd(fds, nfds, ctx->tty.wfd);
    if (p && (p->revents & WRITE_EV))
    {
      if (ttyBufWrite(ctx, sock) != M2K_OK)
        return -1;
    }
  }

  p = findPollfd(fds, nfds, sock->fd);
  if (p && (p->revents & READ_EV))
  {
    sockBufRead(ctx, sock);
    sockReadLoop(ctx, sock);
  }

  if (!ctx->app_io)
  {
    p = findPollfd(fds, nfds, ctx->tty.rfd);
    if (p && (p->revents & READ_EV))
    {
      if (ttyBufRead(ctx, sock) != M2K_OK)
        return -1;
      ttyReadLoop(ctx);
    }
  }
  if (!sock->alive)
    return -1;
  return 0;
}

m2k_err_t
m2k_get_pollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  if (*nfds_inout < M2K_MAX_POLLFDS)
    return M2K_ERR_BUG;

  switch (ctx->step_state)
  {
  case M2K_STATE_CMD:
    cmdPollfds(ctx, fds, nfds_inout, timeout_ms);
    return M2K_OK;
  case M2K_STATE_DIAL:
    dialPollfds(ctx, fds, nfds_inout, timeout_ms);
    return M2K_OK;
  case M2K_STATE_ONLINE:
    onlinePollfds(ctx, fds, nfds_inout, timeout_ms);
    return M2K_OK;
  case M2K_STATE_DONE:
    *nfds_inout = 0;
    *timeout_ms = 0;
    return M2K_OK;
  default:
    return M2K_ERR_BUG;
  }
}

m2k_err_t
m2k_step(m2k_t *ctx, struct pollfd *fds, size_t nfds)
{
  switch (ctx->step_state)
  {
  case M2K_STATE_CMD:
  {
    Cmdstat s = cmdIter(ctx, fds, nfds);
    switch (s)
    {
    case CMDST_ATD:
      if (ctx->sock.alive)
      {
        putTtyCmdstat(ctx, CMDST_ERROR);
        return M2K_OK;
      }
      telOptReset(ctx);
      {
        int r = m2k_sockDialStart(ctx, &ctx->sock);
        if (r == 1)
        {
          stepEnterOnline(ctx);
        }
        else if (r == 0)
        {
          stepEnterDial(ctx);
        }
        else
        {
          putTtyCmdstat(ctx, CMDST_NOCARRIER);
        }
      }
      return M2K_OK;
    case CMDST_ATO:
      if (!ctx->sock.alive)
      {
        putTtyCmdstat(ctx, CMDST_NOCARRIER);
        return M2K_OK;
      }
      stepEnterOnline(ctx);
      return M2K_OK;
    case CMDST_PTY_CLOSED:
      ctx->step_state = M2K_STATE_DONE;
      return M2K_OK;
    default:
      return M2K_OK;
    }
  }
  case M2K_STATE_DIAL:
  {
    int r = dialIter(ctx, fds, nfds);
    if (r == 1)
    {
      stepEnterOnline(ctx);
    }
    else if (r == -1)
    {
      putTtyCmdstat(ctx, CMDST_NOCARRIER);
      stepEnterCmd(ctx);
    }
    return M2K_OK;
  }
  case M2K_STATE_ONLINE:
  {
    int r = onlineIter(ctx, fds, nfds);
    if (r == 1)
    {
      /* +++ escape: stay connected, drop back to cmd mode with OK. */
      putTtyCmdstat(ctx, CMDST_OK);
      stepEnterCmd(ctx);
    }
    else if (r == -1)
    {
      /* Connection or PTY ended. Tear down the socket and report. */
      sockShutdown(&ctx->sock);
      putTtyCmdstat(ctx, CMDST_NOCARRIER);
      stepEnterCmd(ctx);
    }
    return M2K_OK;
  }
  case M2K_STATE_DONE:
    return M2K_OK;
  default:
    return M2K_ERR_BUG;
  }
}

int
m2k_run_done(const m2k_t *ctx)
{
  return ctx->step_state == M2K_STATE_DONE;
}

int
m2k_is_online(const m2k_t *ctx)
{
  return ctx->step_state == M2K_STATE_ONLINE;
}

int
m2k_has_carrier(const m2k_t *ctx)
{
  return ctx->sock.alive;
}

int
m2k_get_listen_fd(const m2k_t *ctx)
{
  return ctx->listen_fd;
}

void
m2k_set_dtr(m2k_t *ctx, int on)
{
  bool new_state = !!on;
  bool was_asserted = ctx->dtr;
  ctx->dtr = new_state;
  /* &D2-equivalent: 1→0 transition while a connection is live hangs up. */
  if (was_asserted && !new_state && ctx->sock.alive)
    m2k_hangup(ctx);
}

void
m2k_set_rts(m2k_t *ctx, int on)
{
  ctx->rts = !!on;
}

int
m2k_get_dtr(const m2k_t *ctx)
{
  return ctx->dtr;
}

int
m2k_get_rts(const m2k_t *ctx)
{
  return ctx->rts;
}

m2k_err_t
m2k_run(m2k_t *ctx)
{
  while (!m2k_run_done(ctx))
  {
    struct pollfd fds[M2K_MAX_POLLFDS];
    size_t nfds = M2K_MAX_POLLFDS;
    int timeout_ms;
    m2k_err_t err = m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms);
    if (err != M2K_OK)
      return err;
    if (nfds > 0)
    {
      int pr = poll(fds, nfds, timeout_ms);
      if (pr < 0)
      {
        if (errno != EINTR)
          m2k_log(ctx, "poll(): %s\n", strerror(errno));
        continue;
      }
    }
    err = m2k_step(ctx, fds, nfds);
    if (err != M2K_OK)
      return err;
  }
  return M2K_OK;
}
