
#include <sys/time.h> /*->ttybuf.h (timeval)*/
#include <unistd.h>
#include "m2k_private.h" /*->ttybuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/
#include "m2k_ctx.h"

void
ttyBufRReset(m2k_t *ctx)
{
  ctx->tty.bufR.ptr = ctx->tty.bufR.end = ctx->tty.bufR.buf;
  ctx->tty.bufR.prevT.tv_sec = ctx->tty.bufR.prevT.tv_usec = 0;
}

bool
ttyBufRHasData(m2k_t *ctx)
{
  return ctx->tty.bufR.ptr < ctx->tty.bufR.end;
}

/* reading tty */

int
getTty1(m2k_t *ctx)
{
  return ((ctx->tty.bufR.ptr >= ctx->tty.bufR.end) ? -1 : *ctx->tty.bufR.ptr++);
}

m2k_err_t
ttyBufRead(m2k_t *ctx, st_sock *sock)
{
  int l;

  l = read(ctx->tty.rfd, ctx->tty.bufR.buf, sizeof(ctx->tty.bufR.buf));
  if (l <= 0)
  {
    sockClose(sock);
    verboseOut(ctx, VERB_MISC, "Pty closed. (read() returned %d)\r\n", l);
    if (l < 0)
      verbosePerror(ctx, VERB_MISC, "read()");
    return M2K_ERR_PTY;
  }
  ctx->tty.bufR.prevT = ctx->tty.bufR.newT;
  gettimeofday(&ctx->tty.bufR.newT, NULL);
  ctx->tty.bufR.ptr = ctx->tty.bufR.buf;
  ctx->tty.bufR.end = ctx->tty.bufR.buf + l;
  verboseOut(ctx, VERB_BYTES, "tty: read %d bytes\r\n", l);
  return M2K_OK;
}


/* writing tty */

void
ttyBufWReset(m2k_t *ctx)
{
  ctx->tty.bufW.ptr = ctx->tty.bufW.top = ctx->tty.bufW.buf;
  ctx->tty.bufW.stop = 0;
}

bool
ttyBufWHasData(m2k_t *ctx)
{
  return (ctx->tty.bufW.ptr > ctx->tty.bufW.buf);
}

bool
ttyBufWReady(m2k_t *ctx)
{
  return !ctx->tty.bufW.stop;
}

m2k_err_t
ttyBufWrite(m2k_t *ctx, st_sock *sock)
{
  int wl, l;

  wl = ctx->tty.bufW.ptr - ctx->tty.bufW.top;
  if (wl == 0)
    return M2K_OK;
  l = write(ctx->tty.wfd, ctx->tty.bufW.top, wl);
  if (l <= 0)
  {
    sockClose(sock);
    verboseOut(ctx, VERB_MISC, "Pty closed. (write() returned %d)\r\n", l);
    if (l < 0)
      verbosePerror(ctx, VERB_MISC, "write()");
    return M2K_ERR_PTY;
  }
  verboseOut(ctx, VERB_BYTES, "tty: wrote %d/%d bytes\r\n", l, wl);
  if (l < wl)
  {
    ctx->tty.bufW.top += l;
    return M2K_OK;
  }
  ctx->tty.bufW.ptr = ctx->tty.bufW.top = ctx->tty.bufW.buf;
  ctx->tty.bufW.stop = 0;
  return M2K_OK;
}

void
putTty1(m2k_t *ctx, uchar c)
{
  if (ctx->tty.bufW.ptr >= ctx->tty.bufW.buf + TTYBUFW_SIZE)
  { /* limit */
    if (ctx->tty.bufW.ptr >= ctx->tty.bufW.buf + TTYBUFW_SIZE_A)
    { /*actual limit */
      m2k_log(ctx, M2K_LOG_WARN, "\attyBufW overrun.\n");
      return;
    }
    else
      ctx->tty.bufW.stop = 1; /* flow control */
  }
  *ctx->tty.bufW.ptr++ = c;
}

void
putTtyN(m2k_t *ctx, const char *cp, int n)
{
  for (; n > 0; n--, cp++)
    putTty1(ctx, *cp);
}
