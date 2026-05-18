#include "test.h"
#include <string.h>
#include "modemu2k.h"
#include "m2k_ctx.h"

static m2k_t ctx_storage;
static m2k_t *ctx = &ctx_storage;

static void
setup(void)
{
  ttyBufWReset(ctx);
  ctx->atcmd.s[2] = 43;   /* escape char '+' */
  ctx->atcmd.s[3] = 13;   /* CR */
  ctx->atcmd.s[4] = 10;   /* LF */
  ctx->atcmd.s[5] = 8;    /* BS */
  ctx->atcmd.s[7] = 20;   /* connection timeout (S7) */
  ctx->atcmd.s[12] = 50;  /* escape guard time (S12) */
}

/* --- m2k_atcmdD address parsing --- */

static void
test_atcmdD_str_port(void)
{
  m2k_atcmdD(ctx, "\"github.com 80\"", ATDA_STR, ATDP_NUM);
  assert(strcmp(ctx->atcmd.d.addr.str, "github.com") == 0);
  assert(strcmp(ctx->atcmd.d.port.str, "80") == 0);
  assert(ctx->atcmd.d.addr.type == ATDA_STR);
  assert(ctx->atcmd.d.port.type == ATDP_NUM);
}

static void
test_atcmdD_num_port(void)
{
  m2k_atcmdD(ctx, "\"140.82.113.3 80\"", ATDA_NUM, ATDP_NUM);
  assert(strcmp(ctx->atcmd.d.addr.str, "140.82.113.3") == 0);
  assert(strcmp(ctx->atcmd.d.port.str, "80") == 0);
  assert(ctx->atcmd.d.addr.type == ATDA_NUM);
}

static void
test_atcmdD_no_port(void)
{
  m2k_atcmdD(ctx, "\"github.com\"", ATDA_STR, ATDP_NUL);
  assert(strcmp(ctx->atcmd.d.addr.str, "github.com") == 0);
  assert(ctx->atcmd.d.port.type == ATDP_NUL);
}

static void
test_atcmdD_no_quote(void)
{
  /* lexer passes yytext without quote for bare numeric addresses */
  m2k_atcmdD(ctx, "192.168.1.1", ATDA_NUM, ATDP_NUL);
  assert(strcmp(ctx->atcmd.d.addr.str, "192.168.1.1") == 0);
}

/* --- S-register operations --- */

static void
test_atcmdSSet(void)
{
  setup();
  assert(atcmdSSet(ctx, "S7=30") == 0);
  assert(ctx->atcmd.s[7] == 30);

  assert(atcmdSSet(ctx, "S12=100") == 0);
  assert(ctx->atcmd.s[12] == 100);

  assert(atcmdSSet(ctx, "S0=255") == 0);
  assert(ctx->atcmd.s[0] == 255);

  /* boundary: SREG_MAX is 12 */
  assert(atcmdSSet(ctx, "S12=1") == 0);
  assert(atcmdSSet(ctx, "S13=0") == 1);   /* out of range index */
  assert(atcmdSSet(ctx, "S7=256") == 1);  /* out of range value */
}

static void
test_atcmdSQuery(void)
{
  setup();
  ctx->atcmd.s[7] = 20;
  assert(atcmdSQuery(ctx, "S7?") == 0);   /* valid */
  assert(atcmdSQuery(ctx, "S0?") == 0);
  assert(atcmdSQuery(ctx, "S12?") == 0);  /* max valid */
  assert(atcmdSQuery(ctx, "S13?") == 1);  /* out of range */
}

/* --- atcmdFake range checks --- */

static void
test_atcmdFake(void)
{
  /* arg 0 is in "01" */
  assert(atcmdFake(ctx, "E0", "01") == 0);
  assert(atcmdFake(ctx, "E1", "01") == 0);
  assert(atcmdFake(ctx, "E2", "01") == 1);   /* 2 not in "01" */
  assert(atcmdFake(ctx, "E",  "01") == 0);   /* default 0 is valid */

  /* "0" only accepts 0 */
  assert(atcmdFake(ctx, "Q0", "0") == 0);
  assert(atcmdFake(ctx, "Q1", "0") == 1);
}

/* --- flag setters --- */

static void
test_atcmdPD(void)
{
  assert(atcmdPD(ctx, "D0") == 0);
  assert(ctx->atcmd.pd == 0);
  assert(atcmdPD(ctx, "D1") == 0);
  assert(ctx->atcmd.pd == 1);
  assert(atcmdPD(ctx, "D2") == 1);   /* out of range */
}

static void
test_atcmdPR(void)
{
  assert(atcmdPR(ctx, "R0") == 0);
  assert(ctx->atcmd.pr == 0);
  assert(atcmdPR(ctx, "R1") == 0);
  assert(ctx->atcmd.pr == 1);
  assert(atcmdPR(ctx, "R2") == 1);
}

static void
test_atcmdPV(void)
{
  assert(atcmdPV(ctx, "V0") == 0);
  assert(ctx->atcmd.pv == 0);
  assert(atcmdPV(ctx, "V255") == 0);
  assert(ctx->atcmd.pv == 255);
  assert(atcmdPV(ctx, "V256") == 1);
}

static void
test_atcmdPB(void)
{
  /* atcmdPB receives yytext which includes the %B prefix */
  assert(atcmdPB(ctx, "%B0=0") == 0);
  assert(ctx->atcmd.pb[0] == 0);
  assert(atcmdPB(ctx, "%B1=3") == 0);
  assert(ctx->atcmd.pb[1] == 3);
  assert(atcmdPB(ctx, "%B2=0") == 1);   /* index out of range */
  assert(atcmdPB(ctx, "%B0=4") == 1);   /* value out of range */
}

static void
test_atcmdAW(void)
{
  setup();
  ctx->atcmd.s[7] = 42;
  ctx->atcmd.pv = 3;
  atcmdAW(ctx);
  assert(ctx->atcmdNV.s[7] == 42);
  assert(ctx->atcmdNV.pv == 3);
}

/* --- cmdLex return values --- */

static void
test_cmdlex_basic(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex(ctx, "AT", &sock) == CMDST_OK);
  assert(cmdLex(ctx, "at", &sock) == CMDST_OK);
  assert(cmdLex(ctx, "AT ", &sock) == CMDST_OK);
  assert(cmdLex(ctx, NULL, &sock) == CMDST_NOAT);
  assert(cmdLex(ctx, "# comment", &sock) == CMDST_NOAT);
  assert(cmdLex(ctx, "garbage", &sock) == CMDST_NOAT);
  assert(cmdLex(ctx, "AT!", &sock) == CMDST_ERROR);
}

static void
test_cmdlex_sreg(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex(ctx, "ATS7=30", &sock) == CMDST_OK);
  assert(ctx->atcmd.s[7] == 30);

  assert(cmdLex(ctx, "ATS12=50", &sock) == CMDST_OK);
  assert(ctx->atcmd.s[12] == 50);

  assert(cmdLex(ctx, "ATS13=0", &sock) == CMDST_ERROR);  /* bad index */
  assert(cmdLex(ctx, "ATS7=256", &sock) == CMDST_ERROR); /* bad value */
}

static void
test_cmdlex_dial(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex(ctx, "ATD\"github.com 80\"", &sock) == CMDST_ATD);
  assert(strcmp(ctx->atcmd.d.addr.str, "github.com") == 0);
  assert(strcmp(ctx->atcmd.d.port.str, "80") == 0);

  assert(cmdLex(ctx, "ATD\"192.168.1.1 8080\"", &sock) == CMDST_ATD);
  assert(strcmp(ctx->atcmd.d.addr.str, "192.168.1.1") == 0);
  assert(strcmp(ctx->atcmd.d.port.str, "8080") == 0);
}

static void
test_cmdlex_ato(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex(ctx, "ATO", &sock) == CMDST_ATO);
  assert(cmdLex(ctx, "ATO0", &sock) == CMDST_ATO);
  assert(cmdLex(ctx, "ATO1", &sock) == CMDST_ERROR);  /* only 0 is valid */
}

static void
test_cmdlex_flags(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex(ctx, "AT%D1", &sock) == CMDST_OK);
  assert(ctx->atcmd.pd == 1);

  assert(cmdLex(ctx, "AT%R0", &sock) == CMDST_OK);
  assert(ctx->atcmd.pr == 0);

  assert(cmdLex(ctx, "AT%V3", &sock) == CMDST_OK);
  assert(ctx->atcmd.pv == 3);
}

int
main(void)
{
  test_atcmdD_str_port();
  test_atcmdD_num_port();
  test_atcmdD_no_port();
  test_atcmdD_no_quote();
  test_atcmdSSet();
  test_atcmdSQuery();
  test_atcmdFake();
  test_atcmdPD();
  test_atcmdPR();
  test_atcmdPV();
  test_atcmdPB();
  test_atcmdAW();
  test_cmdlex_basic();
  test_cmdlex_sreg();
  test_cmdlex_dial();
  test_cmdlex_ato();
  test_cmdlex_flags();
  return 0;
}
