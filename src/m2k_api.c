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


/* ── public API ──────────────────────────────────────────────────── */

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
