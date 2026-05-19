
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <stdbool.h>
#include "modemu2k.h"       /*->sockbuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/
#include "m2k_ctx.h"

/* reading socket */
#define MSG_CONNECTION_CLOSED_BY_PEER "Connection closed by peer.\r\n"

void
sockBufRReset(m2k_t *ctx)
{
  ctx->sockBufR.ptr = ctx->sockBufR.end = ctx->sockBufR.buf;
}

int
getSock1(m2k_t *ctx)
{
  return ((ctx->sockBufR.ptr >= ctx->sockBufR.end) ? -1 : *ctx->sockBufR.ptr++);
}

void
sockBufRead(m2k_t *ctx, st_sock *sock)
{
  int l;

  l = recv(sock->fd, ctx->sockBufR.buf, sizeof(ctx->sockBufR.buf), 0);
  if (l <= 0)
  {
    sock->alive = 0;
    if (l == 0)
      verboseOut(ctx, VERB_MISC, MSG_CONNECTION_CLOSED_BY_PEER);
    else
      /* PPP link down or something to reach here. */
      /* v0.0 exited, which comm progs don't expect. */
      /* now just NO CARRIERs. Thanks >> Rod May */
      m2k_log(ctx, "recv(): %s\n", strerror(errno));
    return;
  }
  ctx->sockBufR.ptr = ctx->sockBufR.buf;
  ctx->sockBufR.end = ctx->sockBufR.buf + l;
}

/* writing socket */

void
sockBufWReset(m2k_t *ctx)
{
  ctx->sockBufW.ptr = ctx->sockBufW.top = ctx->sockBufW.buf;
  ctx->sockBufW.stop = 0;
}

bool
sockBufWHasData(m2k_t *ctx)
{
  return (ctx->sockBufW.ptr > ctx->sockBufW.buf);
}

bool
sockBufWReady(m2k_t *ctx)
{
  return !ctx->sockBufW.stop;
}

void
sockBufWrite(m2k_t *ctx, st_sock *sock)
{
  int wl, l;

  wl = ctx->sockBufW.ptr - ctx->sockBufW.top;
  if (wl == 0)
    return;
  l = send(sock->fd, ctx->sockBufW.top, wl, 0);
  if (l <= 0)
  {
    sock->alive = 0;
    if (l == 0)
      verboseOut(ctx, VERB_MISC, MSG_CONNECTION_CLOSED_BY_PEER);
    else
      m2k_log(ctx, "send(): %s\n", strerror(errno));
    return;
  }
  else if (l < wl)
  {
    ctx->sockBufW.top += l;
    /*return 1; *//* needs retry */
    return;
  }
  ctx->sockBufW.ptr = ctx->sockBufW.top = ctx->sockBufW.buf;
  ctx->sockBufW.stop = 0;
  return;
}

void
putSock1(m2k_t *ctx, uchar c)
{
  if (ctx->sockBufW.ptr >= ctx->sockBufW.buf + SOCKBUFW_SIZE)
  {                             /* limit */
    if (ctx->sockBufW.ptr >= ctx->sockBufW.buf + SOCKBUFW_SIZE_A)
    {                           /*actual limit */
      m2k_log(ctx, "\asockBufW overrun.\n");
      return;
    }
    else
      ctx->sockBufW.stop = 1;        /* flow control */
  }
  *ctx->sockBufW.ptr++ = c;
}

void
putSockN(m2k_t *ctx, const uchar *cp, int n)
{
  for (; n > 0; n--, cp++)
    putSock1(ctx, *cp);
}
