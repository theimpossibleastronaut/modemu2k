#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include <string.h>

static m2k_t ctx_storage;
static m2k_t *ctx = &ctx_storage;

/* Install telOpt state tables so the atcmd functions that read them
   (atcmdI 6, telOptSendReqs via %B/%L) don't dereference NULL. */
static void
telopt_setup(void)
{
  static int inited = 0;
  if (inited) return;
  telOptInit(ctx);
  inited = 1;
}

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

/* --- atcmdH: hangup --- */

static void
test_atcmdH(void)
{
  st_sock sock;
  sockInit(&sock);

  /* No live socket: H0 succeeds, just a no-op. */
  assert(atcmdH(ctx, "H0", &sock) == 0);
  assert(atcmdH(ctx, "H", &sock) == 0);

  /* Non-zero arg rejected. */
  assert(atcmdH(ctx, "H1", &sock) == 1);

  /* Simulate a live socket; H0 must call sockClose, which is wired to
     close() on fd. Use a real pipe fd so close is harmless. */
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  sock.fd = pipefd[0];
  sock.alive = 1;
  assert(atcmdH(ctx, "H0", &sock) == 0);
  assert(sock.alive == 0);
  close(pipefd[1]);
}

/* --- atcmdZ: reset to NVRAM + disconnect --- */

static void
test_atcmdZ(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  /* Stash a known NV value then mutate working copy. */
  ctx->atcmd.s[7] = 99;
  atcmdAW(ctx);                       /* NV = working */
  ctx->atcmd.s[7] = 1;                /* diverge */

  atcmdZ(ctx, &sock);
  assert(ctx->atcmd.s[7] == 99);      /* restored from NV */

  /* With alive sock, atcmdZ closes it. */
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  sock.fd = pipefd[0];
  sock.alive = 1;
  atcmdZ(ctx, &sock);
  assert(sock.alive == 0);
  close(pipefd[1]);
}

/* --- atcmdI: info pages (drives prPercent / prSreg / prOption / prVersion) --- */

static void
test_atcmdI(void)
{
  telopt_setup();
  setup();
  ttyBufWReset(ctx);

  /* Valid indices print to ttyBufW; we just check return code. */
  assert(atcmdI(ctx, "I4") == 0);   /* prPercent + prSreg */
  assert(atcmdI(ctx, "I5") == 0);   /* same but for NVRAM copy */
  assert(atcmdI(ctx, "I6") == 0);   /* prOption */
  assert(atcmdI(ctx, "I7") == 0);   /* prVersion */

  /* Invalid index returns 1. */
  assert(atcmdI(ctx, "I8") == 1);
  assert(atcmdI(ctx, "I0") == 1);

  /* The output buffer should now hold at least the version banner. */
  assert(ttyBufWHasData(ctx));
}

/* --- atcmdPL, atcmdPT, atcmdPTSet --- */

static void
test_atcmdPL(void)
{
  assert(atcmdPL(ctx, "L0") == 0);
  assert(ctx->atcmd.pl == 0);
  assert(atcmdPL(ctx, "L3") == 0);
  assert(ctx->atcmd.pl == 3);
  assert(atcmdPL(ctx, "L4") == 1);
}

static void
test_atcmdPT(void)
{
  assert(atcmdPT(ctx, "T0") == 0);
  assert(ctx->atcmd.pt.wont == 1);

  setenv("TERM", "xterm-test", 1);
  assert(atcmdPT(ctx, "T1") == 0);
  assert(ctx->atcmd.pt.wont == 0);
  assert(strcmp(ctx->atcmd.pt.str, "xterm-test") == 0);
  assert(ctx->atcmd.pt.len == (int) strlen("xterm-test"));

  /* T1 with TERM unset falls through to empty string. */
  unsetenv("TERM");
  assert(atcmdPT(ctx, "T1") == 0);
  assert(ctx->atcmd.pt.str[0] == '\0');

  assert(atcmdPT(ctx, "T2") == 1);
}

static void
test_atcmdPTSet(void)
{
  assert(atcmdPTSet(ctx, "%T=\"vt100\"") == 0);
  assert(strcmp(ctx->atcmd.pt.str, "vt100") == 0);
  assert(ctx->atcmd.pt.wont == 0);
}

/* --- atcmdPQ: just exercises sockShutdown (the dirty quit path) --- */

static void
test_atcmdPQ(void)
{
  st_sock sock;
  sockInit(&sock);
  /* No alive socket → shutdown is a no-op. */
  atcmdPQ(ctx, &sock);
  assert(sock.alive == 0);
}

/* --- cmdLex coverage for verbs hit only by extended tests --- */

static void
test_cmdlex_verbs(void)
{
  st_sock sock;
  sockInit(&sock);
  telopt_setup();
  setup();
  ttyBufWReset(ctx);

  /* H0 — hangup; Z — reset; &W — save; I4 — info. */
  assert(cmdLex(ctx, "ATH0", &sock) == CMDST_OK);
  assert(cmdLex(ctx, "ATZ", &sock) == CMDST_OK);
  assert(cmdLex(ctx, "AT&W", &sock) == CMDST_OK);
  assert(cmdLex(ctx, "ATI4", &sock) == CMDST_OK);
  assert(cmdLex(ctx, "ATI7", &sock) == CMDST_OK);
  assert(cmdLex(ctx, "ATI8", &sock) == CMDST_ERROR);
}

static void
test_cmdlex_percent(void)
{
  st_sock sock;
  sockInit(&sock);
  telopt_setup();
  setup();

  /* %B (binary mode); the side-effect is that sentReqs is cleared. */
  ctx->telOpt.sentReqs = 1;
  assert(cmdLex(ctx, "AT%B0=1", &sock) == CMDST_OK);
  assert(ctx->atcmd.pb[0] == 1);
  assert(ctx->telOpt.sentReqs == 0);

  /* %L (linemode) */
  assert(cmdLex(ctx, "AT%L2", &sock) == CMDST_OK);
  assert(ctx->atcmd.pl == 2);

  /* %T0 / %T1 */
  assert(cmdLex(ctx, "AT%T0", &sock) == CMDST_OK);
  assert(ctx->atcmd.pt.wont == 1);
  setenv("TERM", "vt220", 1);
  assert(cmdLex(ctx, "AT%T1", &sock) == CMDST_OK);
  assert(ctx->atcmd.pt.wont == 0);

  /* %T="…" */
  assert(cmdLex(ctx, "AT%T=\"ansi\"", &sock) == CMDST_OK);
  assert(strcmp(ctx->atcmd.pt.str, "ansi") == 0);
}

static void
test_cmdlex_sreg_query(void)
{
  st_sock sock;
  sockInit(&sock);
  telopt_setup();
  setup();
  ttyBufWReset(ctx);

  assert(cmdLex(ctx, "ATS7?", &sock) == CMDST_OK);
  assert(ttyBufWHasData(ctx));
  assert(cmdLex(ctx, "ATS13?", &sock) == CMDST_ERROR);
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
  test_atcmdH();
  test_atcmdZ();
  test_atcmdI();
  test_atcmdPL();
  test_atcmdPT();
  test_atcmdPTSet();
  test_atcmdPQ();
  test_cmdlex_verbs();
  test_cmdlex_percent();
  test_cmdlex_sreg_query();
  return 0;
}
