#include "test.h"

#include <unistd.h>
#include "modemu2k.h"
#include "m2k_ctx.h"

static m2k_t ctx_storage;
static m2k_t *ctx = &ctx_storage;

static void test_connect (void)
{
  st_sock sock;
  ctx->atcmd.s[7] = 20;  /* S7: connection timeout in seconds */
  ctx->atcmd.pd = 1;    /* disable dial canceling; stdin is not a tty in tests */

  // should connect
  telOptReset (ctx);
  m2k_atcmdD (ctx, "github.com 80", ATDA_STR, ATDP_NUM);
  assert (m2k_sockDial(ctx, &sock) == 0);
  sleep (2);
  assert (sockShutdown (&sock) == 0);

  // should connect
  telOptReset (ctx);
  m2k_atcmdD (ctx, "140.82.113.3 80", ATDA_NUM, ATDP_NUM);
  assert (m2k_sockDial(ctx, &sock) == 0);
  sleep (2);
  assert (sockShutdown (&sock) == 0);

  return;
}

int main (void)
{
  test_connect ();
  return 0;
}
