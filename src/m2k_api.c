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

/* socket input processing loop — IAC stream parser state lives in
   ctx->srl so coexisting contexts (and successive connections) don't
   share or inherit it. */
enum
{
  SRL_NORM,
  SRL_IAC,
  SRL_CMD,
  SRL_SB,
  SRL_SBC,
  SRL_SBS,
  SRL_SBI
};

static void
sockReadLoop(m2k_t *ctx, st_sock *sock)
{
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
      switch (ctx->srl.state)
      {
      case SRL_IAC:
        switch (c)
        {
        case WILL:
        case WONT:
        case DO:
        case DONT:
          ctx->srl.cmd = c;
          ctx->srl.state = SRL_CMD;
          break;
        case IAC:
          putTty1(ctx, c);
          ctx->srl.state = SRL_NORM;
          break;
        case SB:
          ctx->srl.state = SRL_SB;
          break;
        default:
          ctx->srl.state = SRL_NORM;
          telOptPrintCmd(ctx, "<", c);
        }
        break;
      case SRL_CMD:
        if (telOptHandle(ctx, ctx->srl.cmd, c))
          sock->alive = 0;
        ctx->srl.state = SRL_NORM;
        break;
      case SRL_SB:
        ctx->srl.opt = c;
        ctx->srl.state = SRL_SBC;
        break;
      case SRL_SBC:
        ctx->srl.state = (c == TELQUAL_SEND) ? SRL_SBS : SRL_NORM;
        break;
      case SRL_SBS:
        ctx->srl.state = (c == IAC) ? SRL_SBI : SRL_NORM;
        break;
      case SRL_SBI:
        telOptSBHandle(ctx, ctx->srl.opt);
        ctx->srl.state = SRL_NORM;
        break;
      default:
        if (c == IAC)
          ctx->srl.state = SRL_IAC;
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
    if (c == CHAR_ESC(ctx) && !s12timePassed(ctx, &ctx->tty.bufR.newT, &es->plus1T))
      es->state = ESH_P2;
    else
      es->state = ESH_NORM;
    break;
  case ESH_P2:
    if (c == CHAR_ESC(ctx) && !s12timePassed(ctx, &ctx->tty.bufR.newT, &es->plus1T))
    {
      es->checkSilence = 1;
      timevalSet10ms(&es->expireT, ctx->atcmd.s[12] * 2);
      timevalAdd(&es->expireT, &ctx->tty.bufR.newT);
      es->state = ESH_P3;
      verboseOut(ctx, VERB_BYTES, "+++ seen; escape arms after guard silence\r\n");
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
    if (c == CHAR_ESC(ctx) && s12timePassed(ctx, &ctx->tty.bufR.newT, &ctx->tty.bufR.prevT))
    {
      es->plus1T = ctx->tty.bufR.newT;
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
  ctx->srl.state = SRL_NORM; /* don't inherit mid-IAC state from a
                                 previous connection */

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
        m2k_log(ctx, M2K_LOG_ERROR, "select(): %s\n", strerror(errno));
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
    "",     /* CMDST_ATA — never printed */
    "RING",
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
    putTtyN(ctx, msg_escape_seq, sizeof msg_escape_seq - 1);

    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    putTtyN(ctx, msg_return_online, sizeof msg_return_online - 1);
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
  ctx->sock.listen_fd = -1;
  ctx->answer.fd = -1;
  ctx->log.level = M2K_LOG_INFO;
  ctx->step.state = M2K_STATE_CMD;
  ctx->ctrl.dtr = true;
  ctx->ctrl.rts = true;
  cmdBufReset(&ctx->step.cmdbuf);
  sockInit(&ctx->sock.conn);
  ttyBufRReset(ctx);
  ttyBufWReset(ctx);
  telOptInit(ctx);
  atcmdInit(ctx, NULL, &ctx->sock.conn);
  return ctx;
}

void
m2k_free(m2k_t *ctx)
{
  if (ctx == NULL)
    return;
  if (ctx->sock.listen_fd != -1)
    close(ctx->sock.listen_fd);
  if (ctx->answer.fd != -1)
    close(ctx->answer.fd);
  /* Close the TTY fd only when the library opened it (PTY master, dev,
     accepted client). stdin mode (rfd/wfd = 0/1) and app-IO are
     caller-owned. rfd == wfd in every owned case, so one close suffices. */
  if (ctx->tty.owned && ctx->tty.rfd >= 0)
    close(ctx->tty.rfd);
  sockShutdown(&ctx->sock.conn);
  free(ctx);
}

void
m2k_set_log_fn(m2k_t *ctx, m2k_log_fn fn, void *userdata)
{
  ctx->log.fn = fn;
  ctx->log.userdata = userdata;
}

void
m2k_set_error_buffer(m2k_t *ctx, char *buf, size_t size)
{
  ctx->log.err_buf = buf;
  ctx->log.err_buf_size = buf ? size : 0;
  if (buf && size)
    buf[0] = '\0';
}

m2k_err_t
m2k_atcmd(m2k_t *ctx, const char *cmd)
{
  Cmdstat r = cmdLex(ctx, cmd, &ctx->sock.conn);
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
  case CMDST_ATA:
    m2k_err_set(ctx, "ATA is not actionable from m2k_atcmd; use the step loop\n");
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
  return m2k_sockDial(ctx, &ctx->sock.conn) == 0 ? M2K_OK : M2K_ERR_SOCKET;
}

m2k_err_t
m2k_online(m2k_t *ctx)
{
  putTtyCmdstat(ctx, CMDST_CONNECT);
  int r = onlineMode(ctx, &ctx->sock.conn);
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
  if (ctx->step.state == M2K_STATE_ONLINE)
    ctx->step.escape_req = true;
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
    if (c == 0x03) /* Ctrl-C (ETX) */
    {
      if (ctx->step.intr_armed)
      {
        ctx->step.quit_req = true;
        ctx->step.intr_armed = false;
        return;
      }
      ctx->step.intr_armed = true;
      cmdBufReset(cmdBuf);
      continue;
    }
    ctx->step.intr_armed = false;
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
  ctx->tty.owned = false;
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
  ctx->tty.owned = true;
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
  ctx->tty.owned = true;
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
  ctx->tty.owned = true;
  return M2K_OK;
}

m2k_err_t
m2k_setup_listen(m2k_t *ctx, const char *port)
{
  int fd = m2k_sockListen(ctx, port);
  if (fd == -1)
    return M2K_ERR_SOCKET;
  ctx->sock.listen_fd = fd;
  return M2K_OK;
}

m2k_err_t
m2k_listen_accept(m2k_t *ctx)
{
  if (ctx->sock.listen_fd == -1)
  {
    m2k_err_set(ctx, "m2k_listen_accept: no listener (call m2k_setup_listen first)\n");
    return M2K_ERR_SOCKET;
  }
  int client_fd = m2k_sockAccept(ctx, ctx->sock.listen_fd);
  ctx->sock.listen_fd = -1;
  if (client_fd == -1)
    return M2K_ERR_SOCKET;
  ctx->tty.rfd = ctx->tty.wfd = client_fd;
  ctx->tty.owned = true;
  return M2K_OK;
}

m2k_err_t
m2k_setup_answer(m2k_t *ctx, const char *port)
{
  if (ctx->answer.fd != -1)
  {
    m2k_err_set(ctx, "m2k_setup_answer: answer listener already bound\n");
    return M2K_ERR_SOCKET;
  }
  int fd = m2k_sockListen(ctx, port);
  if (fd == -1)
    return M2K_ERR_SOCKET;
  /* Non-blocking: Linux silently removes RST-aborted callers from the
     accept queue, so a blocking accept() after a positive poll could
     hang m2k_step indefinitely. */
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  ctx->answer.fd = fd;
  return M2K_OK;
}

int
m2k_get_answer_fd(const m2k_t *ctx)
{
  return ctx->answer.fd;
}

/* ── App-IO (embed) mode ──────────────────────────────────────────────
   m2k_write_from_app appends to ttyBufR (replacing read() from tty.rfd);
   m2k_read_to_app drains ttyBufW (replacing write() to tty.wfd). The
   socket side still uses real fds. */

m2k_err_t
m2k_setup_app_io(m2k_t *ctx)
{
  ctx->step.app_io = true;
  ctx->tty.rfd = ctx->tty.wfd = -1;
  ctx->tty.owned = false;
  return M2K_OK;
}

m2k_err_t
m2k_write_from_app(m2k_t *ctx, const void *buf, size_t len, size_t *consumed)
{
  *consumed = 0;
  if (!ctx->step.app_io)
  {
    m2k_err_set(ctx, "m2k_write_from_app: not in app-I/O mode (call m2k_setup_app_io first)\n");
    return M2K_ERR_PTY;
  }
  if (len == 0)
    return M2K_OK;

  size_t residue = ctx->tty.bufR.end - ctx->tty.bufR.ptr;
  size_t cap = sizeof(ctx->tty.bufR.buf);
  if (residue >= cap)
  {
    m2k_err_set(ctx, "m2k_write_from_app: TTY read buffer full (%zu bytes pending)\n",
                residue);
    return M2K_ERR_WOULDBLOCK;
  }
  size_t room = cap - residue;
  size_t take = len < room ? len : room;

  if (residue && ctx->tty.bufR.ptr != ctx->tty.bufR.buf)
    memmove(ctx->tty.bufR.buf, ctx->tty.bufR.ptr, residue);
  memcpy(ctx->tty.bufR.buf + residue, buf, take);
  ctx->tty.bufR.ptr = ctx->tty.bufR.buf;
  ctx->tty.bufR.end = ctx->tty.bufR.buf + residue + take;
  ctx->tty.bufR.prevT = ctx->tty.bufR.newT;
  gettimeofday(&ctx->tty.bufR.newT, NULL);
  *consumed = take;
  return M2K_OK;
}

m2k_err_t
m2k_read_to_app(m2k_t *ctx, void *buf, size_t max, size_t *len_out)
{
  if (!ctx->step.app_io)
  {
    *len_out = 0;
    return M2K_ERR_PTY;
  }
  size_t available = ctx->tty.bufW.ptr - ctx->tty.bufW.top;
  size_t n = available < max ? available : max;
  if (n)
    memcpy(buf, ctx->tty.bufW.top, n);
  ctx->tty.bufW.top += n;
  if (ctx->tty.bufW.top >= ctx->tty.bufW.ptr)
  {
    /* Buffer fully drained — rewind to make room for new output. */
    ctx->tty.bufW.ptr = ctx->tty.bufW.top = ctx->tty.bufW.buf;
    ctx->tty.bufW.stop = 0;
  }
  *len_out = n;
  return M2K_OK;
}

int
m2k_has_pending_output(const m2k_t *ctx)
{
  if (!ctx || !ctx->step.app_io)
    return 0;
  return ctx->tty.bufW.ptr > ctx->tty.bufW.top;
}

/* ── Steppable event-loop API ────────────────────────────────────────
   m2k_run() is a poll()-driven wrapper over m2k_get_pollfds + m2k_step.

       CMD    ── ATD (alive=0) ──▶ (dial inline) ──▶ ONLINE on success
                                                  ──▶ CMD on dial fail
       CMD    ── ATO (alive=1) ──▶ ONLINE
       CMD    ── ATA (caller)   ──▶ ONLINE (CONNECT)
       CMD    ── ATA (none)     ──▶ ANSWER (wait ≤ S7)
       ANSWER ── caller arrives ──▶ ONLINE (CONNECT)
       ANSWER ── S7 / keypress  ──▶ CMD (NO CARRIER)
       CMD    ── PTY closed    ──▶ DONE
       ONLINE ── +++ escape    ──▶ CMD
       ONLINE ── sock dead     ──▶ CMD
       ONLINE ── PTY closed    ──▶ DONE                                 */

static const char *
stepStateName(m2k_step_state s)
{
  switch (s)
  {
  case M2K_STATE_CMD:
    return "CMD";
  case M2K_STATE_DIAL:
    return "DIAL";
  case M2K_STATE_ANSWER:
    return "ANSWER";
  case M2K_STATE_ONLINE:
    return "ONLINE";
  case M2K_STATE_DONE:
    return "DONE";
  }
  return "?";
}

static void
stepEnterCmd(m2k_t *ctx)
{
  verboseOut(ctx, VERB_MISC, "state: %s -> CMD\r\n", stepStateName(ctx->step.state));
  cmdBufReset(&ctx->step.cmdbuf);
  ttyBufRReset(ctx);
  ctx->step.state = M2K_STATE_CMD;
}

static void
stepEnterOnline(m2k_t *ctx)
{
  verboseOut(ctx, VERB_MISC, "state: %s -> ONLINE\r\n", stepStateName(ctx->step.state));
  sockBufRReset(ctx);
  sockBufWReset(ctx);
  ttyBufRReset(ctx);
  lineBufReset(ctx);
  escSeqReset(ctx);
  ctx->srl.state = SRL_NORM; /* don't inherit mid-IAC state from a
                                 previous connection */
  if (!ctx->telOpt.sentReqs && !ctx->atcmd.pr)
    telOptSendReqs(ctx);
  putTtyCmdstat(ctx, CMDST_CONNECT);
  ctx->step.state = M2K_STATE_ONLINE;
}

static void
stepEnterDial(m2k_t *ctx)
{
  verboseOut(ctx, VERB_MISC, "state: %s -> DIAL\r\n", stepStateName(ctx->step.state));
  /* Drop any pending TTY input — semantics of going off-hook. */
  ttyBufRReset(ctx);
  cmdBufReset(&ctx->step.cmdbuf);
  ctx->step.state = M2K_STATE_DIAL;
}

static void
stepEnterAnswer(m2k_t *ctx)
{
  verboseOut(ctx, VERB_MISC, "state: %s -> ANSWER\r\n", stepStateName(ctx->step.state));
  /* Drop any pending TTY input — semantics of going off-hook. */
  ttyBufRReset(ctx);
  cmdBufReset(&ctx->step.cmdbuf);
  gettimeofday(&ctx->answer.deadline, NULL);
  ctx->answer.deadline.tv_sec += ctx->atcmd.s[7];
  ctx->step.state = M2K_STATE_ANSWER;
}

m2k_err_t
m2k_hangup(m2k_t *ctx)
{
  /* Abort a dial-in-progress so the host can recover the context
     without waiting out the S7 timeout. */
  if (ctx->step.state == M2K_STATE_DIAL)
  {
    m2k_sockDialAbort(ctx, &ctx->sock.conn);
    putTtyCmdstat(ctx, CMDST_NOCARRIER);
    stepEnterCmd(ctx);
    return M2K_OK;
  }
  if (ctx->step.state == M2K_STATE_ANSWER)
  {
    putTtyCmdstat(ctx, CMDST_NOCARRIER);
    stepEnterCmd(ctx);
    return M2K_OK;
  }
  sockShutdown(&ctx->sock.conn);
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

/* Seconds between RING emissions while a caller waits (US ring cadence). */
#define RING_INTERVAL_SEC 6

/* One entry if rfd==wfd; else one per fd, each only when its mask is set. */
static void
appendTtyPollfds(m2k_t *ctx, struct pollfd *fds, size_t *n,
                 short read_ev, short write_ev)
{
  if (ctx->step.app_io)
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

  if (ctx->step.app_io)
  {
    /* timeout=0 only if there's already work to do; otherwise let the
       host's poll() block on its own fds until it has bytes to push us. */
    *timeout_ms = (ttyBufRHasData(ctx) || ctx->step.cmdbuf.eol) ? 0 : -1;
  }
  else
  {
    appendTtyPollfds(ctx, fds, &n,
                     ttyBufWReady(ctx) ? POLLIN : 0,
                     ttyBufWHasData(ctx) ? POLLOUT : 0);
    *timeout_ms = -1;
  }

  if (ctx->answer.fd != -1 && !ctx->sock.conn.alive)
  {
    if (ctx->atcmd.s[1] == 0)
    {
      /* No ring cycle yet — wake when a caller arrives. */
      fds[n].fd = ctx->answer.fd;
      fds[n].events = POLLIN;
      fds[n].revents = 0;
      n++;
    }
    else
    {
      /* Ring cycle in progress: the listener stays readable (level-
         triggered), so wake on the next-RING deadline instead. */
      struct timeval now, remaining;
      gettimeofday(&now, NULL);
      int ms = 0;
      if (timevalCmp(&now, &ctx->answer.ring_next) < 0)
      {
        remaining = ctx->answer.ring_next;
        timevalSub(&remaining, &now);
        long l = remaining.tv_sec * 1000L + remaining.tv_usec / 1000L;
        ms = l > 0 ? (int) l : 1;
      }
      if (*timeout_ms < 0 || ms < *timeout_ms)
        *timeout_ms = ms;
    }
  }
  *nfds_inout = n;
}

static Cmdstat
cmdDispatchIfReady(m2k_t *ctx, struct m2k_cmdbuf *cmdBuf, st_sock *sock)
{
  if (!cmdBuf->eol)
    return CMDST_OK;
  verboseOut(ctx, VERB_MISC, "dispatch: %s\r\n", (char *) cmdBuf->buf);
  Cmdstat stat = cmdLex(ctx, (char *) cmdBuf->buf, sock);
  cmdBufReset(cmdBuf);
  switch (stat)
  {
  case CMDST_ATD:
  case CMDST_ATO:
  case CMDST_ATA:
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
  st_sock *sock = &ctx->sock.conn;
  struct m2k_cmdbuf *cmdBuf = &ctx->step.cmdbuf;

  if (ctx->step.app_io)
  {
    for (;;)
    {
      Cmdstat s = cmdDispatchIfReady(ctx, cmdBuf, sock);
      if (s != CMDST_OK)
        return s;
      if (ctx->step.quit_req && !ttyBufWHasData(ctx))
        return CMDST_PTY_CLOSED;
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
  if (ctx->step.quit_req && !ttyBufWHasData(ctx))
    return CMDST_PTY_CLOSED;
  return CMDST_OK;
}

/* Dial mode: wait on the in-progress connect socket (POLLOUT) plus the
   TTY rfd (so a keypress can cancel the dial). Timeout is the remaining
   time until the S7 deadline. */
static void
dialPollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  size_t n = 0;

  fds[n].fd = ctx->sock.conn.fd;
  fds[n].events = POLLOUT;
  fds[n].revents = 0;
  n++;

  /* Allow tty read so the user can cancel by typing. No write side here
     — we have nothing to emit until the dial resolves. */
  appendTtyPollfds(ctx, fds, &n, POLLIN, 0);
  *nfds_inout = n;

  struct timeval now, remaining;
  gettimeofday(&now, NULL);
  if (timevalCmp(&now, &ctx->dial.deadline) >= 0)
  {
    *timeout_ms = 0;
    return;
  }
  remaining = ctx->dial.deadline;
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
  if (!ctx->step.app_io)
  {
    struct pollfd *p = findPollfd(fds, nfds, ctx->tty.rfd);
    if (p && (p->revents & READ_EV))
    {
      m2k_sockDialAbort(ctx, &ctx->sock.conn);
      return -1;
    }
  }
  /* Whether the socket fired or the deadline elapsed, ask Progress. */
  return m2k_sockDialProgress(ctx, &ctx->sock.conn);
}

/* Non-blocking "is a caller waiting in the accept queue?" check. */
static int
answerPending(m2k_t *ctx)
{
  if (ctx->answer.fd == -1 || ctx->sock.conn.alive)
    return 0;
  struct pollfd p = {.fd = ctx->answer.fd, .events = POLLIN, .revents = 0};
  return poll(&p, 1, 0) == 1 && (p.revents & POLLIN);
}

/* Accept the pending caller and adopt it as the connection. Returns 0
   on success, -1 on accept failure (the listener stays bound). */
static int
answerAccept(m2k_t *ctx)
{
  int fd = m2k_sockAcceptKeep(ctx, ctx->answer.fd);
  if (fd == -1)
    return -1;
  telOptReset(ctx);
  sockInit(&ctx->sock.conn);
  ctx->sock.conn.fd = fd;
  ctx->sock.conn.alive = 1;
  ctx->atcmd.s[1] = 0; /* call answered — ring counter rests */
  return 0;
}

/* Answer mode: wait for the listener to become readable, plus the TTY
   rfd (a keypress cancels, like dial cancel). Timeout runs to the S7
   deadline. */
static void
answerPollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  size_t n = 0;

  fds[n].fd = ctx->answer.fd;
  fds[n].events = POLLIN;
  fds[n].revents = 0;
  n++;

  /* No TTY write side here (same as dialPollfds): nothing is emitted
     until the answer resolves, so output buffered at ANSWER entry
     stalls for up to S7 seconds. */
  appendTtyPollfds(ctx, fds, &n, POLLIN, 0);
  *nfds_inout = n;

  struct timeval now, remaining;
  gettimeofday(&now, NULL);
  if (timevalCmp(&now, &ctx->answer.deadline) >= 0)
  {
    *timeout_ms = 0;
    return;
  }
  remaining = ctx->answer.deadline;
  timevalSub(&remaining, &now);
  long ms = remaining.tv_sec * 1000L + remaining.tv_usec / 1000L;
  if (ms <= 0) ms = 1;
  *timeout_ms = (int) ms;
}

/* Returns 1 (accepted — caller transitions to ONLINE), 0 (continue
   polling), -1 (canceled or timed out — caller emits NOCARRIER + CMD). */
static int
answerIter(m2k_t *ctx, struct pollfd *fds, size_t nfds)
{
  if (!ctx->step.app_io)
  {
    struct pollfd *p = findPollfd(fds, nfds, ctx->tty.rfd);
    if (p && (p->revents & READ_EV))
      return -1;
  }
  struct pollfd *p = findPollfd(fds, nfds, ctx->answer.fd);
  if (p && (p->revents & READ_EV))
    return answerAccept(ctx) == 0 ? 1 : -1;

  struct timeval now;
  gettimeofday(&now, NULL);
  if (timevalCmp(&now, &ctx->answer.deadline) >= 0)
    return -1;
  return 0;
}

/* timeout_ms is driven by the +++ escape silence guard; expiry is
   detected inside onlineIter after poll returns. */
static void
onlinePollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  st_sock *sock = &ctx->sock.conn;
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
  st_sock *sock = &ctx->sock.conn;
  struct pollfd *p;

  if (ctx->step.escape_req)
  {
    ctx->step.escape_req = false;
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

  if (ctx->step.app_io)
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

  if (!ctx->step.app_io)
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

/* RING/S0 bookkeeping for CMD state. Returns 1 when S0 auto-answer
   accepted a caller (caller transitions to ONLINE), else 0. */
static int
cmdRingCheck(m2k_t *ctx)
{
  if (ctx->answer.fd == -1 || ctx->sock.conn.alive)
    return 0;
  if (!answerPending(ctx))
  {
    ctx->atcmd.s[1] = 0; /* caller gone (or none): ring cycle over */
    return 0;
  }
  struct timeval now;
  gettimeofday(&now, NULL);
  if (ctx->atcmd.s[1] != 0 && timevalCmp(&now, &ctx->answer.ring_next) < 0)
    return 0; /* between rings */
  putTtyCmdstat(ctx, CMDST_RING);
  if (ctx->atcmd.s[1] < 255)
    ctx->atcmd.s[1]++;
  ctx->answer.ring_next = now;
  ctx->answer.ring_next.tv_sec += RING_INTERVAL_SEC;
  if (ctx->atcmd.s[0] > 0 && ctx->atcmd.s[1] >= ctx->atcmd.s[0])
    return answerAccept(ctx) == 0;
  return 0;
}

m2k_err_t
m2k_get_pollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  if (*nfds_inout < M2K_MAX_POLLFDS)
    return M2K_ERR_BUG;

  switch (ctx->step.state)
  {
  case M2K_STATE_CMD:
    cmdPollfds(ctx, fds, nfds_inout, timeout_ms);
    return M2K_OK;
  case M2K_STATE_DIAL:
    dialPollfds(ctx, fds, nfds_inout, timeout_ms);
    return M2K_OK;
  case M2K_STATE_ANSWER:
    answerPollfds(ctx, fds, nfds_inout, timeout_ms);
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
  switch (ctx->step.state)
  {
  case M2K_STATE_CMD:
  {
    if (cmdRingCheck(ctx))
    {
      stepEnterOnline(ctx);
      return M2K_OK;
    }
    Cmdstat s = cmdIter(ctx, fds, nfds);
    switch (s)
    {
    case CMDST_ATD:
      if (ctx->sock.conn.alive)
      {
        putTtyCmdstat(ctx, CMDST_ERROR);
        return M2K_OK;
      }
      telOptReset(ctx);
      {
        int r = m2k_sockDialStart(ctx, &ctx->sock.conn);
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
      if (!ctx->sock.conn.alive)
      {
        putTtyCmdstat(ctx, CMDST_NOCARRIER);
        return M2K_OK;
      }
      stepEnterOnline(ctx);
      return M2K_OK;
    case CMDST_ATA:
      if (ctx->sock.conn.alive || ctx->answer.fd == -1)
      {
        putTtyCmdstat(ctx, CMDST_ERROR);
        return M2K_OK;
      }
      if (answerPending(ctx))
      {
        if (answerAccept(ctx) == 0)
          stepEnterOnline(ctx);
        else
          putTtyCmdstat(ctx, CMDST_NOCARRIER);
      }
      else if (ctx->atcmd.s[7] == 0)
      {
        putTtyCmdstat(ctx, CMDST_NOCARRIER);
      }
      else
      {
        stepEnterAnswer(ctx);
      }
      return M2K_OK;
    case CMDST_PTY_CLOSED:
      verboseOut(ctx, VERB_MISC, "state: %s -> DONE\r\n", stepStateName(ctx->step.state));
      ctx->step.state = M2K_STATE_DONE;
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
  case M2K_STATE_ANSWER:
  {
    int r = answerIter(ctx, fds, nfds);
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
      sockShutdown(&ctx->sock.conn);
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
  return ctx->step.state == M2K_STATE_DONE;
}

int
m2k_is_online(const m2k_t *ctx)
{
  return ctx->step.state == M2K_STATE_ONLINE;
}

int
m2k_describe_state(const m2k_t *ctx, char *buf, size_t cap)
{
  char tmp[512];
  int n = snprintf(tmp, sizeof tmp,
                   "state=%s carrier=%s app_io=%s\n"
                   "tty: rfd=%d wfd=%d owned=%s bufR=%zu/%zu bufW=%zu/%zu\n"
                   "sock: fd=%d alive=%s bufR=%zu/%zu bufW=%zu/%zu listen_fd=%d\n"
                   "line: answer_fd=%d rings(S1)=%u dial=%s dtr=%d rts=%d "
                   "verbose=0x%02x force=%s",
                   stepStateName(ctx->step.state),
                   ctx->sock.conn.alive ? "yes" : "no",
                   ctx->step.app_io ? "yes" : "no",
                   ctx->tty.rfd, ctx->tty.wfd,
                   ctx->tty.owned ? "yes" : "no",
                   (size_t) (ctx->tty.bufR.end - ctx->tty.bufR.ptr),
                   sizeof ctx->tty.bufR.buf,
                   (size_t) (ctx->tty.bufW.ptr - ctx->tty.bufW.top),
                   sizeof ctx->tty.bufW.buf,
                   /* sockInit's "no socket" sentinel is 0, not -1; present
                      it as -1 so it can't be read as stdin. */
                   ctx->sock.conn.fd > 0 ? ctx->sock.conn.fd : -1,
                   ctx->sock.conn.alive ? "yes" : "no",
                   (size_t) (ctx->sock.bufR.end - ctx->sock.bufR.ptr),
                   sizeof ctx->sock.bufR.buf,
                   (size_t) (ctx->sock.bufW.ptr - ctx->sock.bufW.top),
                   sizeof ctx->sock.bufW.buf,
                   ctx->sock.listen_fd,
                   ctx->answer.fd, (unsigned) ctx->atcmd.s[1],
                   ctx->dial.result ? "in-progress" : "idle",
                   ctx->ctrl.dtr, ctx->ctrl.rts,
                   (unsigned) ctx->atcmd.pv,
                   ctx->log.force_verbose ? "yes" : "no");
  if (cap > 0 && buf != NULL)
  {
    size_t copy = (size_t) n < cap - 1 ? (size_t) n : cap - 1;
    if (copy > sizeof tmp - 1) /* unreachable while the format's worst
                                  case (~450 bytes) fits tmp; guards a
                                  future fifth line. */
      copy = sizeof tmp - 1;
    memcpy(buf, tmp, copy);
    buf[copy] = '\0';
  }
  return n;
}

int
m2k_has_carrier(const m2k_t *ctx)
{
  return ctx->sock.conn.alive;
}

int
m2k_get_listen_fd(const m2k_t *ctx)
{
  return ctx->sock.listen_fd;
}

void
m2k_set_dtr(m2k_t *ctx, int on)
{
  bool new_state = !!on;
  bool was_asserted = ctx->ctrl.dtr;
  ctx->ctrl.dtr = new_state;
  /* &D2-equivalent: 1→0 transition while a connection is live hangs up. */
  if (was_asserted && !new_state && ctx->sock.conn.alive)
    m2k_hangup(ctx);
}

void
m2k_set_rts(m2k_t *ctx, int on)
{
  ctx->ctrl.rts = !!on;
}

int
m2k_get_dtr(const m2k_t *ctx)
{
  return ctx->ctrl.dtr;
}

void
m2k_set_force_verbose(m2k_t *ctx, int on)
{
  ctx->log.force_verbose = !!on;
}

int
m2k_get_force_verbose(const m2k_t *ctx)
{
  return ctx->log.force_verbose;
}

void
m2k_set_log_level(m2k_t *ctx, m2k_log_level_t level)
{
  ctx->log.level = level;
}

m2k_log_level_t
m2k_get_log_level(const m2k_t *ctx)
{
  return ctx->log.level;
}

int
m2k_get_rts(const m2k_t *ctx)
{
  return ctx->ctrl.rts;
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
          m2k_log(ctx, M2K_LOG_ERROR, "poll(): %s\n", strerror(errno));
        continue;
      }
    }
    err = m2k_step(ctx, fds, nfds);
    if (err != M2K_OK)
      return err;
  }
  return M2K_OK;
}
