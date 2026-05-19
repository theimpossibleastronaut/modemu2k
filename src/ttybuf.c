
#include <sys/time.h>   /*->ttybuf.h (timeval)*/
#include <unistd.h>
#include "modemu2k.h"       /*->ttybuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/
#include "m2k_ctx.h"

void
ttyBufRReset(m2k_t *ctx)
{
  ctx->ttyBufR.ptr = ctx->ttyBufR.end = ctx->ttyBufR.buf;
  ctx->ttyBufR.prevT.tv_sec = ctx->ttyBufR.prevT.tv_usec = 0;
}

bool
ttyBufRHasData(m2k_t *ctx)
{
  return ctx->ttyBufR.ptr < ctx->ttyBufR.end;
}

/* reading tty */

int
getTty1(m2k_t *ctx)
{
  return ((ctx->ttyBufR.ptr >= ctx->ttyBufR.end) ? -1 : *ctx->ttyBufR.ptr++);
}

m2k_err_t
ttyBufRead(m2k_t *ctx, st_sock *sock)
{
  int l;

  l = read(ctx->tty.rfd, ctx->ttyBufR.buf, sizeof(ctx->ttyBufR.buf));
  if (l <= 0)
  {
    sockClose(sock);
    verboseOut(ctx, VERB_MISC, "Pty closed. (read() returned %d)\r\n", l);
    if (l < 0)
      verbosePerror(ctx, VERB_MISC, "read()");
    return M2K_ERR_PTY;
  }
  ctx->ttyBufR.prevT = ctx->ttyBufR.newT;
  gettimeofday(&ctx->ttyBufR.newT, NULL);
  ctx->ttyBufR.ptr = ctx->ttyBufR.buf;
  ctx->ttyBufR.end = ctx->ttyBufR.buf + l;
  return M2K_OK;
}


/* writing tty */

void
ttyBufWReset(m2k_t *ctx)
{
  ctx->ttyBufW.ptr = ctx->ttyBufW.top = ctx->ttyBufW.buf;
  ctx->ttyBufW.stop = 0;
}

bool
ttyBufWHasData(m2k_t *ctx)
{
  return (ctx->ttyBufW.ptr > ctx->ttyBufW.buf);
}

bool
ttyBufWReady(m2k_t *ctx)
{
  return !ctx->ttyBufW.stop;
}

m2k_err_t
ttyBufWrite(m2k_t *ctx, st_sock *sock)
{
  int wl, l;

  wl = ctx->ttyBufW.ptr - ctx->ttyBufW.top;
  if (wl == 0)
    return M2K_OK;
  l = write(ctx->tty.wfd, ctx->ttyBufW.top, wl);
  if (l <= 0)
  {
    sockClose(sock);
    verboseOut(ctx, VERB_MISC, "Pty closed. (write() returned %d)\r\n", l);
    if (l < 0)
      verbosePerror(ctx, VERB_MISC, "write()");
    return M2K_ERR_PTY;
  }
  else if (l < wl)
  {
    ctx->ttyBufW.top += l;
    return M2K_OK;
  }
  ctx->ttyBufW.ptr = ctx->ttyBufW.top = ctx->ttyBufW.buf;
  ctx->ttyBufW.stop = 0;
  return M2K_OK;
}

void
putTty1(m2k_t *ctx, uchar c)
{
  if (ctx->ttyBufW.ptr >= ctx->ttyBufW.buf + TTYBUFW_SIZE)
  {                             /* limit */
    if (ctx->ttyBufW.ptr >= ctx->ttyBufW.buf + TTYBUFW_SIZE_A)
    {                           /*actual limit */
      m2k_log(ctx, "\attyBufW overrun.\n");
      return;
    }
    else
      ctx->ttyBufW.stop = 1;         /* flow control */
  }
  *ctx->ttyBufW.ptr++ = c;
}

void
putTtyN(m2k_t *ctx, const char *cp, int n)
{
  for (; n > 0; n--, cp++)
    putTty1(ctx, *cp);
}
