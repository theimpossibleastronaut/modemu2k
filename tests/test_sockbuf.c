#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include <string.h>

static m2k_t ctx_storage;
static m2k_t *ctx = &ctx_storage;

/* --- read buffer --- */

static void
test_sockBufR_empty(void)
{
  sockBufRReset(ctx);
  assert(getSock1(ctx) == -1);
  assert(getSock1(ctx) == -1);
}

static void
test_sockBufR_read(void)
{
  sockBufRReset(ctx);
  /* manually load the read buffer as sockBufRead() would */
  ctx->sock.bufR.buf[0] = 'A';
  ctx->sock.bufR.buf[1] = 'T';
  ctx->sock.bufR.buf[2] = 'Z';
  ctx->sock.bufR.ptr = ctx->sock.bufR.buf;
  ctx->sock.bufR.end = ctx->sock.bufR.buf + 3;

  assert(getSock1(ctx) == 'A');
  assert(getSock1(ctx) == 'T');
  assert(getSock1(ctx) == 'Z');
  assert(getSock1(ctx) == -1);
}

static void
test_sockBufR_reset_clears(void)
{
  /* fill then reset — should be empty again */
  ctx->sock.bufR.ptr = ctx->sock.bufR.buf;
  ctx->sock.bufR.end = ctx->sock.bufR.buf + 10;
  sockBufRReset(ctx);
  assert(getSock1(ctx) == -1);
}

/* --- write buffer --- */

static void
test_sockBufW_initial_state(void)
{
  sockBufWReset(ctx);
  assert(!sockBufWHasData(ctx));
  assert(sockBufWReady(ctx));
}

static void
test_putSock1(void)
{
  sockBufWReset(ctx);
  putSock1(ctx, 'X');
  assert(sockBufWHasData(ctx));
  assert(sockBufWReady(ctx));
  assert(ctx->sock.bufW.buf[0] == 'X');
}

static void
test_putSockN(void)
{
  const unsigned char data[] = "hello";
  sockBufWReset(ctx);
  putSockN(ctx, data, 5);
  assert(sockBufWHasData(ctx));
  assert(memcmp(ctx->sock.bufW.buf, "hello", 5) == 0);
}

static void
test_sockBufW_reset_clears(void)
{
  sockBufWReset(ctx);
  putSock1(ctx, 'Q');
  assert(sockBufWHasData(ctx));
  sockBufWReset(ctx);
  assert(!sockBufWHasData(ctx));
  assert(sockBufWReady(ctx));
}

static void
test_putSock1_flow_control(void)
{
  sockBufWReset(ctx);
  /* fill up to the soft limit */
  for (int i = 0; i < SOCKBUFW_SIZE; i++)
    putSock1(ctx, 'X');
  /* soft limit triggers flow control on the next byte */
  putSock1(ctx, 'Y');
  assert(!sockBufWReady(ctx));
  /* hard limit: SOCKBUFW_SIZE_A - 1 bytes in total written so far;
     one more write should hit the guard and be dropped */
  for (int i = SOCKBUFW_SIZE + 1; i < SOCKBUFW_SIZE_A; i++)
    putSock1(ctx, 'Z');
  /* this one hits the hard limit and must be silently dropped:
     the write pointer must not advance past the buffer */
  uchar *before = ctx->sock.bufW.ptr;
  putSock1(ctx, '!');
  assert(ctx->sock.bufW.ptr == before);
}

int
main(void)
{
  test_sockBufR_empty();
  test_sockBufR_read();
  test_sockBufR_reset_clears();
  test_sockBufW_initial_state();
  test_putSock1();
  test_putSockN();
  test_sockBufW_reset_clears();
  test_putSock1_flow_control();
  return 0;
}
