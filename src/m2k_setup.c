/* m2k_setup.c — the m2k_setup_* family: how a context acquires its
   TTY side (stdin, PTY, comm program, device, TCP listener) and its
   line-side answer listener. Split out of m2k_api.c (issue #71). */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "m2k_private.h"
#include "m2k_ctx.h"

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
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    m2k_err_set(ctx, "m2k_setup_answer: fcntl(O_NONBLOCK): %s\n",
                strerror(errno));
    close(fd);
    return M2K_ERR_SOCKET;
  }
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

int
m2k_get_listen_fd(const m2k_t *ctx)
{
  return ctx->sock.listen_fd;
}
