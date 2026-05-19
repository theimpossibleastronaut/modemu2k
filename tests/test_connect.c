#include "test.h"

static void
test_connect(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  m2k_atcmd(ctx, "ATS7=20");
  m2k_atcmd(ctx, "AT%D1");  /* disable dial canceling — stdin is not a tty */

  assert(m2k_dial(ctx, "github.com", "80") == M2K_OK);
  sleep(2);
  assert(m2k_hangup(ctx) == M2K_OK);

  assert(m2k_dial(ctx, "140.82.113.3", "80") == M2K_OK);
  sleep(2);
  assert(m2k_hangup(ctx) == M2K_OK);

  m2k_free(ctx);
}

int
main(void)
{
  test_connect();
  return 0;
}
