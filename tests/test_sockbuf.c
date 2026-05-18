#include "test.h"
#include <string.h>
#include "modemu2k.h"

/* --- read buffer --- */

static void
test_sockBufR_empty(void)
{
  sockBufRReset();
  assert(getSock1() == -1);
  assert(getSock1() == -1);
}

static void
test_sockBufR_read(void)
{
  sockBufRReset();
  /* manually load the read buffer as sockBufRead() would */
  sockBufR.buf[0] = 'A';
  sockBufR.buf[1] = 'T';
  sockBufR.buf[2] = 'Z';
  sockBufR.ptr = sockBufR.buf;
  sockBufR.end = sockBufR.buf + 3;

  assert(getSock1() == 'A');
  assert(getSock1() == 'T');
  assert(getSock1() == 'Z');
  assert(getSock1() == -1);
}

static void
test_sockBufR_reset_clears(void)
{
  /* fill then reset — should be empty again */
  sockBufR.ptr = sockBufR.buf;
  sockBufR.end = sockBufR.buf + 10;
  sockBufRReset();
  assert(getSock1() == -1);
}

/* --- write buffer --- */

static void
test_sockBufW_initial_state(void)
{
  sockBufWReset();
  assert(!sockBufWHasData());
  assert(sockBufWReady());
}

static void
test_putSock1(void)
{
  sockBufWReset();
  putSock1('X');
  assert(sockBufWHasData());
  assert(sockBufWReady());
  assert(sockBufW.buf[0] == 'X');
}

static void
test_putSockN(void)
{
  const unsigned char data[] = "hello";
  sockBufWReset();
  putSockN(data, 5);
  assert(sockBufWHasData());
  assert(memcmp(sockBufW.buf, "hello", 5) == 0);
}

static void
test_sockBufW_reset_clears(void)
{
  sockBufWReset();
  putSock1('Q');
  assert(sockBufWHasData());
  sockBufWReset();
  assert(!sockBufWHasData());
  assert(sockBufWReady());
}

static void
test_putSock1_flow_control(void)
{
  sockBufWReset();
  /* fill up to the soft limit */
  for (int i = 0; i < SOCKBUFW_SIZE; i++)
    putSock1('X');
  /* soft limit triggers flow control on the next byte */
  putSock1('Y');
  assert(!sockBufWReady());
  /* hard limit: SOCKBUFW_SIZE_A - 1 bytes in total written so far;
     one more write should hit the guard and be dropped */
  for (int i = SOCKBUFW_SIZE + 1; i < SOCKBUFW_SIZE_A; i++)
    putSock1('Z');
  /* this one hits the hard limit and is silently dropped */
  putSock1('!');
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
