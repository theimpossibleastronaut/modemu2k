#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"

static m2k_t ctx_storage;
static m2k_t *ctx = &ctx_storage;

static void
test_ttyBufRHasData_empty(void)
{
  ttyBufRReset(ctx);
  assert(!ttyBufRHasData(ctx));
}

static void
test_ttyBufRHasData_with_data(void)
{
  ttyBufRReset(ctx);
  ctx->tty.bufR.buf[0] = 'A';
  ctx->tty.bufR.buf[1] = 'B';
  ctx->tty.bufR.ptr = ctx->tty.bufR.buf;
  ctx->tty.bufR.end = ctx->tty.bufR.buf + 2;
  assert(ttyBufRHasData(ctx));
  getTty1(ctx);
  assert(ttyBufRHasData(ctx));
  getTty1(ctx);
  assert(!ttyBufRHasData(ctx));
}

int
main(void)
{
  test_ttyBufRHasData_empty();
  test_ttyBufRHasData_with_data();
  return 0;
}
