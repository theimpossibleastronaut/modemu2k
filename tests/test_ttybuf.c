#include "test.h"
#include "modemu2k.h"

static void
test_ttyBufRHasData_empty(void)
{
  ttyBufRReset();
  assert(!ttyBufRHasData());
}

static void
test_ttyBufRHasData_with_data(void)
{
  ttyBufRReset();
  ttyBufR.buf[0] = 'A';
  ttyBufR.buf[1] = 'B';
  ttyBufR.ptr = ttyBufR.buf;
  ttyBufR.end = ttyBufR.buf + 2;
  assert(ttyBufRHasData());
  getTty1();
  assert(ttyBufRHasData());
  getTty1();
  assert(!ttyBufRHasData());
}

int
main(void)
{
  test_ttyBufRHasData_empty();
  test_ttyBufRHasData_with_data();
  return 0;
}
