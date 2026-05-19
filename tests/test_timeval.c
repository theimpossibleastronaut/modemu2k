#include "test.h"
#include "m2k_private.h"
#include <sys/time.h>

static void
test_set10ms(void)
{
  struct timeval t;

  timevalSet10ms(&t, 0);
  assert(t.tv_sec == 0 && t.tv_usec == 0);

  timevalSet10ms(&t, 100);      /* 100 × 10ms = 1s */
  assert(t.tv_sec == 1 && t.tv_usec == 0);

  timevalSet10ms(&t, 150);      /* 150 × 10ms = 1.5s */
  assert(t.tv_sec == 1 && t.tv_usec == 500000);

  timevalSet10ms(&t, 2000);     /* S7=20 default: 20s */
  assert(t.tv_sec == 20 && t.tv_usec == 0);

  timevalSet10ms(&t, 1);        /* 1 × 10ms = 10ms */
  assert(t.tv_sec == 0 && t.tv_usec == 10000);
}

static void
test_cmp(void)
{
  struct timeval a = {1, 0};
  struct timeval b = {2, 0};
  struct timeval c = {1, 0};
  struct timeval d = {1, 500000};
  struct timeval e = {0, 999999};

  assert(timevalCmp(&a, &b) < 0);    /* sec: a < b */
  assert(timevalCmp(&b, &a) > 0);    /* sec: b > a */
  assert(timevalCmp(&a, &c) == 0);   /* equal */
  assert(timevalCmp(&a, &d) < 0);    /* same sec, usec: a < d */
  assert(timevalCmp(&d, &a) > 0);    /* same sec, usec: d > a */
  assert(timevalCmp(&e, &a) < 0);    /* smaller sec wins */
}

static void
test_add(void)
{
  struct timeval a;
  struct timeval b;

  /* no carry */
  a.tv_sec = 1; a.tv_usec = 200000;
  b.tv_sec = 2; b.tv_usec = 300000;
  timevalAdd(&a, &b);
  assert(a.tv_sec == 3 && a.tv_usec == 500000);

  /* usec carry */
  a.tv_sec = 1; a.tv_usec = 500000;
  b.tv_sec = 1; b.tv_usec = 600000;
  timevalAdd(&a, &b);                /* 1.5 + 1.6 = 3.1 */
  assert(a.tv_sec == 3 && a.tv_usec == 100000);

  /* zero + zero */
  a.tv_sec = 0; a.tv_usec = 0;
  b.tv_sec = 0; b.tv_usec = 0;
  timevalAdd(&a, &b);
  assert(a.tv_sec == 0 && a.tv_usec == 0);
}

static void
test_sub(void)
{
  struct timeval a;
  struct timeval b;

  /* no borrow */
  a.tv_sec = 5; a.tv_usec = 800000;
  b.tv_sec = 2; b.tv_usec = 300000;
  timevalSub(&a, &b);
  assert(a.tv_sec == 3 && a.tv_usec == 500000);

  /* usec borrow */
  a.tv_sec = 3; a.tv_usec = 100000;
  b.tv_sec = 1; b.tv_usec = 600000;
  timevalSub(&a, &b);                /* 3.1 - 1.6 = 1.5 */
  assert(a.tv_sec == 1 && a.tv_usec == 500000);

  /* subtract self */
  a.tv_sec = 7; a.tv_usec = 123456;
  b = a;
  timevalSub(&a, &b);
  assert(a.tv_sec == 0 && a.tv_usec == 0);
}

int
main(void)
{
  test_set10ms();
  test_cmp();
  test_add();
  test_sub();
  return 0;
}
