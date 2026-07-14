
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <stdbool.h>
#include "m2k_private.h" /*->sockbuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/
#include "m2k_ctx.h"

/* reading socket */
#define MSG_CONNECTION_CLOSED_BY_PEER "Connection closed by peer.\r\n"

void
sockBufRReset(m2k_t *ctx)
{
  ctx->sock.bufR.ptr = ctx->sock.bufR.end = ctx->sock.bufR.buf;
}

int
getSock1(m2k_t *ctx)
{
  return ((ctx->sock.bufR.ptr >= ctx->sock.bufR.end) ? -1 : *ctx->sock.bufR.ptr++);
}

void
sockBufRead(m2k_t *ctx, st_sock *sock)
{
  int l;

  l = recv(sock->fd, ctx->sock.bufR.buf, sizeof(ctx->sock.bufR.buf), 0);
  if (l <= 0)
  {
    sock->alive = 0;
    if (l == 0)
      verboseOut(ctx, VERB_MISC, MSG_CONNECTION_CLOSED_BY_PEER);
    else
      /* PPP link down or something to reach here. */
      /* v0.0 exited, which comm progs don't expect. */
      /* now just NO CARRIERs. Thanks >> Rod May */
      m2k_log(ctx, M2K_LOG_WARN, "recv(): %s\n", strerror(errno));
    return;
  }
  ctx->sock.bufR.ptr = ctx->sock.bufR.buf;
  ctx->sock.bufR.end = ctx->sock.bufR.buf + l;
  verboseOut(ctx, VERB_BYTES, "sock: recv %d bytes\r\n", l);
}

/* writing socket */

void
sockBufWReset(m2k_t *ctx)
{
  ctx->sock.bufW.ptr = ctx->sock.bufW.top = ctx->sock.bufW.buf;
  ctx->sock.bufW.stop = 0;
}

bool
sockBufWHasData(m2k_t *ctx)
{
  return (ctx->sock.bufW.ptr > ctx->sock.bufW.buf);
}

bool
sockBufWReady(m2k_t *ctx)
{
  return !ctx->sock.bufW.stop;
}

void
sockBufWrite(m2k_t *ctx, st_sock *sock)
{
  int wl, l;

  wl = ctx->sock.bufW.ptr - ctx->sock.bufW.top;
  if (wl == 0)
    return;
  l = send(sock->fd, ctx->sock.bufW.top, wl, 0);
  if (l <= 0)
  {
    sock->alive = 0;
    if (l == 0)
      verboseOut(ctx, VERB_MISC, MSG_CONNECTION_CLOSED_BY_PEER);
    else
      m2k_log(ctx, M2K_LOG_WARN, "send(): %s\n", strerror(errno));
    return;
  }
  verboseOut(ctx, VERB_BYTES, "sock: sent %d/%d bytes\r\n", l, wl);
  if (l < wl)
  {
    ctx->sock.bufW.top += l;
    /*return 1; */ /* needs retry */
    return;
  }
  ctx->sock.bufW.ptr = ctx->sock.bufW.top = ctx->sock.bufW.buf;
  ctx->sock.bufW.stop = 0;
  return;
}

void
putSock1(m2k_t *ctx, uchar c)
{
  if (ctx->sock.bufW.ptr >= ctx->sock.bufW.buf + SOCKBUFW_SIZE)
  { /* limit */
    if (ctx->sock.bufW.ptr >= ctx->sock.bufW.buf + SOCKBUFW_SIZE_A)
    { /*actual limit */
      m2k_log(ctx, M2K_LOG_WARN, "\asockBufW overrun.\n");
      return;
    }
    else
      ctx->sock.bufW.stop = 1; /* flow control */
  }
  *ctx->sock.bufW.ptr++ = c;
}

void
putSockN(m2k_t *ctx, const uchar *cp, int n)
{
  for (; n > 0; n--, cp++)
    putSock1(ctx, *cp);
}
