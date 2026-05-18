#include "test.h"
#include "modemu2k.h"
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
  ctx->ttyBufR.buf[0] = 'A';
  ctx->ttyBufR.buf[1] = 'B';
  ctx->ttyBufR.ptr = ctx->ttyBufR.buf;
  ctx->ttyBufR.end = ctx->ttyBufR.buf + 2;
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
