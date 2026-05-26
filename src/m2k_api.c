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

static void cmdBufReset(struct m2k_cmdbuf *x);

m2k_t *
m2k_new(void)
{
  m2k_t *ctx = calloc(1, sizeof(m2k_t));
  if (ctx == NULL)
    return NULL;
  ctx->listen_fd = -1;
  ctx->step_state = M2K_STATE_CMD;
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

/* struct m2k_cmdbuf is defined in m2k_private.h so the m2k_t context
   can embed one for the steppable m2k_step() API. */

static void
cmdBufReset(struct m2k_cmdbuf *x)
{
  x->ptr = x->buf;
  x->eol = 0;
}

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
m2k_setup_comm_program(m2k_t *ctx, const char *cmd)
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
  ctx->listen_fd = fd;
  return M2K_OK;
}

m2k_err_t
m2k_listen_accept(m2k_t *ctx)
{
  if (ctx->listen_fd == -1)
  {
    m2k_log(ctx, "m2k_listen_accept: no listener (call m2k_setup_listen first)\n");
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
  ctx->app_io = 1;
  ctx->tty.rfd = ctx->tty.wfd = -1;
  return M2K_OK;
}

m2k_err_t
m2k_write_from_app(m2k_t *ctx, const void *buf, size_t len)
{
  if (!ctx->app_io)
    return M2K_ERR_PTY;
  if (len == 0)
    return M2K_OK;

  /* Shift any residue down to the start of the buffer, then append. */
  size_t residue = ctx->ttyBufR.end - ctx->ttyBufR.ptr;
  size_t cap     = sizeof(ctx->ttyBufR.buf);
  if (residue + len > cap)
    return M2K_ERR_BUG;  /* caller wrote more than the read buffer can hold;
                            split into smaller writes */
  if (residue && ctx->ttyBufR.ptr != ctx->ttyBufR.buf)
    memmove(ctx->ttyBufR.buf, ctx->ttyBufR.ptr, residue);
  memcpy(ctx->ttyBufR.buf + residue, buf, len);
  ctx->ttyBufR.ptr = ctx->ttyBufR.buf;
  ctx->ttyBufR.end = ctx->ttyBufR.buf + residue + len;
  ctx->ttyBufR.prevT = ctx->ttyBufR.newT;
  gettimeofday(&ctx->ttyBufR.newT, NULL);
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
  lineBufReset();
  escSeqReset();
  if (!ctx->telOpt.sentReqs && !ctx->atcmd.pr)
    telOptSendReqs(ctx);
  putTtyCmdstat(ctx, CMDST_CONNECT);
  ctx->step_state = M2K_STATE_ONLINE;
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
#define READ_EV  (POLLIN  | POLLHUP | POLLERR)
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
                   ttyBufWReady(ctx)   ? POLLIN  : 0,
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
    Cmdstat s = cmdDispatchIfReady(ctx, cmdBuf, sock);
    if (s != CMDST_OK)
      return s;
    if (ttyBufRHasData(ctx))
    {
      cmdReadLoop(ctx, cmdBuf);
      s = cmdDispatchIfReady(ctx, cmdBuf, sock);
      if (s != CMDST_OK)
        return s;
    }
    return CMDST_OK;
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

/* timeout_ms is driven by the +++ escape silence guard; expiry is
   detected inside onlineIter after poll returns. */
static void
onlinePollfds(m2k_t *ctx, struct pollfd *fds, size_t *nfds_inout, int *timeout_ms)
{
  st_sock *sock = &ctx->sock;
  size_t n = 0;

  short sock_ev = 0;
  if (ttyBufWReady(ctx))     sock_ev |= POLLIN;
  if (sockBufWHasData(ctx))  sock_ev |= POLLOUT;
  fds[n].fd = sock->fd;
  fds[n].events = sock_ev;
  fds[n].revents = 0;
  n++;

  appendTtyPollfds(ctx, fds, &n,
                   (sockBufWReady(ctx) && !ttyBufRHasData(ctx)) ? POLLIN  : 0,
                   ttyBufWHasData(ctx) ? POLLOUT : 0);
  *nfds_inout = n;

  /* Compute timeout for the +++ silence guard. */
  if (escSeq.checkSilence)
  {
    struct timeval now, remaining;
    gettimeofday(&now, NULL);
    if (timevalCmp(&now, &escSeq.expireT) >= 0)
    {
      *timeout_ms = 0;
    }
    else
    {
      remaining = escSeq.expireT;
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

/* Returns 0 (continue), 1 (+++ escape elapsed), -1 (sock or PTY closed). */
static int
onlineIter(m2k_t *ctx, struct pollfd *fds, size_t nfds)
{
  st_sock *sock = &ctx->sock;
  struct pollfd *p;

  if (escSeq.checkSilence)
  {
    struct timeval now;
    gettimeofday(&now, NULL);
    if (timevalCmp(&now, &escSeq.expireT) >= 0)
    {
      escSeq.checkSilence = 0;
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
  case M2K_STATE_ONLINE:
    onlinePollfds(ctx, fds, nfds_inout, timeout_ms);
    return M2K_OK;
  case M2K_STATE_DONE:
  default:
    *nfds_inout = 0;
    *timeout_ms = 0;
    return M2K_OK;
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
      /* Synchronous dial — connect() briefly blocks the caller's loop. */
      telOptReset(ctx);
      if (m2k_sockDial(ctx, &ctx->sock) != 0)
      {
        putTtyCmdstat(ctx, CMDST_NOCARRIER);
        return M2K_OK;
      }
      stepEnterOnline(ctx);
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
  default:
    return M2K_OK;
  }
}

int
m2k_run_done(const m2k_t *ctx)
{
  return ctx->step_state == M2K_STATE_DONE;
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
