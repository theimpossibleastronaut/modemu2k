/* Drives the telnet option negotiation machinery in src/telopt.c.

   The functions under test (telOptInit, telOptReset, telOptHandle,
   telOptSBHandle, telOptSendReqs) read and write into the sock-write
   buffer via putSock1/putOptCmd. We initialize a ctx with all the
   buffers it needs and never actually flush them — just verify state
   transitions and that bytes were emitted on outbound paths. */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include <arpa/telnet.h>
#include <string.h>

static m2k_t ctx_storage;
static m2k_t *ctx = &ctx_storage;

static void
init_ctx(void)
{
  memset(ctx, 0, sizeof *ctx);
  ctx->atcmd.s[2] = 43;
  ctx->atcmd.s[3] = 13;
  ctx->atcmd.s[4] = 10;
  ctx->atcmd.s[5] = 8;
  ctx->atcmd.s[7] = 20;
  ctx->atcmd.s[12] = 50;
  telOptInit(ctx);
  telOptReset(ctx);
  sockBufWReset(ctx);
}

static void
test_init_and_reset(void)
{
  init_ctx();
  /* All summarized flags must be zero after reset. */
  assert(ctx->telOpt.binsend == 0);
  assert(ctx->telOpt.binrecv == 0);
  assert(ctx->telOpt.sgasend == 0);
  assert(ctx->telOpt.sentReqs == 0);
  assert(ctx->telOpt.stTab != NULL);
  /* Known options must have entries; unknown ones get the default. */
  assert(ctx->telOpt.stTab[TELOPT_BINARY] != NULL);
  assert(ctx->telOpt.stTab[TELOPT_ECHO] != NULL);
  assert(ctx->telOpt.stTab[TELOPT_SGA] != NULL);
  assert(ctx->telOpt.stTab[TELOPT_TTYPE] != NULL);
}

static void
test_handle_will_better(void)
{
  init_ctx();
  /* TELOPT_BINARY remote starts as TOR_BETTER. Receiving WILL BINARY
     from peer should enable remote state and send DO back (positive
     response to a previously un-requested state). */
  assert(telOptHandle(ctx, WILL, TELOPT_BINARY) == 0);
  assert(ctx->telOpt.stTab[TELOPT_BINARY]->remote.state == 1);
  assert(ctx->telOpt.binrecv == 1);
  /* A DO must have been put in the sock-write buffer (3 bytes:
     IAC DO BINARY). */
  assert(sockBufWHasData(ctx));
}

static void
test_handle_will_mustnot(void)
{
  init_ctx();
  /* TELOPT_ECHO local is TOR_MUSTNOT but for remote it's TOR_BETTER —
     change requirement so WILL is rejected. */
  ctx->telOpt.stTab[TELOPT_ECHO]->remote.req = TOR_MUSTNOT;
  assert(telOptHandle(ctx, WILL, TELOPT_ECHO) == 0);
  /* Negative response sent (DONT). State stays 0. */
  assert(ctx->telOpt.stTab[TELOPT_ECHO]->remote.state == 0);
  assert(sockBufWHasData(ctx));
}

static void
test_handle_do_better(void)
{
  init_ctx();
  /* TELOPT_BINARY local is TOR_BETTER. Receiving DO BINARY enables
     local. */
  assert(telOptHandle(ctx, DO, TELOPT_BINARY) == 0);
  assert(ctx->telOpt.stTab[TELOPT_BINARY]->local.state == 1);
  assert(ctx->telOpt.binsend == 1);
}

static void
test_handle_dont_response_to_pending(void)
{
  init_ctx();
  /* Simulate that we previously sent WILL BINARY (so local.pending=1)
     and peer responds DONT. With TOR_BETTER, library accepts. */
  ctx->telOpt.stTab[TELOPT_BINARY]->local.pending = 1;
  ctx->telOpt.stTab[TELOPT_BINARY]->local.state = 1;
  /* DONT to a TOR_BETTER requirement: mustNegate=TOR_MUST,
     betterNegate=TOR_BETTER. With pending=1 and req=TOR_BETTER, this
     hits the betterNegate branch — pending cleared, returns 0. */
  ctx->telOpt.stTab[TELOPT_BINARY]->local.req = TOR_BETTER;
  assert(telOptHandle(ctx, DONT, TELOPT_BINARY) == 0);
  assert(ctx->telOpt.stTab[TELOPT_BINARY]->local.pending == 0);
}

static void
test_handle_wont_must_returns_error(void)
{
  init_ctx();
  ctx->telOpt.stTab[TELOPT_SGA]->remote.req = TOR_MUST;
  ctx->telOpt.stTab[TELOPT_SGA]->remote.pending = 1;
  /* WONT with pending=1 and req==TOR_MUST should fail (return 1). */
  assert(telOptHandle(ctx, WONT, TELOPT_SGA) == 1);
}

static void
test_handle_unknown_opt(void)
{
  init_ctx();
  /* An opt beyond NTELOPTS goes through the default state entry; the
     default is TOR_MUSTNOT on both sides, so WILL <unknown> from peer
     yields a DONT response without enabling anything. */
  int weird = NTELOPTS + 5;
  assert(telOptHandle(ctx, WILL, weird) == 0);
  assert(sockBufWHasData(ctx));
}

static void
test_send_reqs(void)
{
  init_ctx();
  /* %B0=1, %B1=1 means BINARY local/remote BETTER. With state=0 and
     req=BETTER, telOptSendReqs should emit WILL+DO for BINARY. */
  ctx->atcmd.pb[0] = 1;
  ctx->atcmd.pb[1] = 1;
  ctx->atcmd.pl = 1;            /* %L1 -> line mode: SGA/ECHO BETTERNOT */
  ctx->atcmd.pt.wont = 0;

  telOptSendReqs(ctx);
  assert(ctx->telOpt.sentReqs == 1);
  assert(sockBufWHasData(ctx));
  /* BINARY local should be pending after we sent WILL. */
  assert(ctx->telOpt.stTab[TELOPT_BINARY]->local.pending == 1);
  assert(ctx->telOpt.stTab[TELOPT_BINARY]->remote.pending == 1);
}

static void
test_sb_handle_ttype(void)
{
  init_ctx();
  strcpy(ctx->atcmd.pt.str, "vt100");
  ctx->atcmd.pt.len = (int) strlen("vt100");
  assert(telOptSBHandle(ctx, TELOPT_TTYPE) == 0);
  assert(sockBufWHasData(ctx));
}

static void
test_sb_handle_unknown(void)
{
  init_ctx();
  /* Unknown SB option returns 1. */
  assert(telOptSBHandle(ctx, TELOPT_NAWS) == 1);
}

static void
test_print_cmd(void)
{
  init_ctx();
  /* telOptPrintCmd writes via verboseOut. With pv=0 (default), nothing
     emits anyway — but the call path is covered. */
  ctx->atcmd.pv = 0xFF;          /* enable all verbose categories */
  telOptPrintCmd(ctx, ">", DO);
}

/* Two coexisting m2k_t contexts must not share telnet option state.
   This used to be a real bug: stTabMaster and stTab were file-scope
   globals, so negotiation on one ctx would visibly mutate the other. */
static void
test_multi_ctx_isolation(void)
{
  m2k_t *a = m2k_new();
  m2k_t *b = m2k_new();
  assert(a && b);
  /* sockBufW is reset on online entry by stepEnterOnline; for this
     unit test we drive telOptHandle directly so initialize manually. */
  sockBufWReset(a);
  sockBufWReset(b);

  /* Drive negotiation only on ctx A. */
  assert(telOptHandle(a, WILL, TELOPT_BINARY) == 0);
  assert(a->telOpt.stTab[TELOPT_BINARY]->remote.state == 1);
  assert(a->telOpt.binrecv == 1);

  /* Ctx B must remain untouched. */
  assert(b->telOpt.stTab[TELOPT_BINARY]->remote.state == 0);
  assert(b->telOpt.binrecv == 0);

  /* The per-ctx storage really is separate memory. */
  assert(a->telOpt.stTabMaster != b->telOpt.stTabMaster);
  assert(a->telOpt.stTab[TELOPT_BINARY] != b->telOpt.stTab[TELOPT_BINARY]);

  m2k_free(a);
  m2k_free(b);
}

int
main(void)
{
  test_init_and_reset();
  test_handle_will_better();
  test_handle_will_mustnot();
  test_handle_do_better();
  test_handle_dont_response_to_pending();
  test_handle_wont_must_returns_error();
  test_handle_unknown_opt();
  test_send_reqs();
  test_sb_handle_ttype();
  test_sb_handle_unknown();
  test_print_cmd();
  test_multi_ctx_isolation();
  return 0;
}
