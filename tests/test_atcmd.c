#include "test.h"
#include <string.h>
#include "modemu2k.h"

static void
setup(void)
{
  ttyBufWReset();
  atcmd.s[2] = 43;   /* escape char '+' */
  atcmd.s[3] = 13;   /* CR */
  atcmd.s[4] = 10;   /* LF */
  atcmd.s[5] = 8;    /* BS */
  atcmd.s[7] = 20;   /* connection timeout (S7) */
  atcmd.s[12] = 50;  /* escape guard time (S12) */
}

/* --- m2k_atcmdD address parsing --- */

static void
test_atcmdD_str_port(void)
{
  m2k_atcmdD("\"github.com 80\"", ATDA_STR, ATDP_NUM);
  assert(strcmp(atcmd.d.addr.str, "github.com") == 0);
  assert(strcmp(atcmd.d.port.str, "80") == 0);
  assert(atcmd.d.addr.type == ATDA_STR);
  assert(atcmd.d.port.type == ATDP_NUM);
}

static void
test_atcmdD_num_port(void)
{
  m2k_atcmdD("\"140.82.113.3 80\"", ATDA_NUM, ATDP_NUM);
  assert(strcmp(atcmd.d.addr.str, "140.82.113.3") == 0);
  assert(strcmp(atcmd.d.port.str, "80") == 0);
  assert(atcmd.d.addr.type == ATDA_NUM);
}

static void
test_atcmdD_no_port(void)
{
  m2k_atcmdD("\"github.com\"", ATDA_STR, ATDP_NUL);
  assert(strcmp(atcmd.d.addr.str, "github.com") == 0);
  assert(atcmd.d.port.type == ATDP_NUL);
}

static void
test_atcmdD_no_quote(void)
{
  /* lexer passes yytext without quote for bare numeric addresses */
  m2k_atcmdD("192.168.1.1", ATDA_NUM, ATDP_NUL);
  assert(strcmp(atcmd.d.addr.str, "192.168.1.1") == 0);
}

/* --- S-register operations --- */

static void
test_atcmdSSet(void)
{
  setup();
  assert(atcmdSSet("S7=30") == 0);
  assert(atcmd.s[7] == 30);

  assert(atcmdSSet("S12=100") == 0);
  assert(atcmd.s[12] == 100);

  assert(atcmdSSet("S0=255") == 0);
  assert(atcmd.s[0] == 255);

  /* boundary: SREG_MAX is 12 */
  assert(atcmdSSet("S12=1") == 0);
  assert(atcmdSSet("S13=0") == 1);   /* out of range index */
  assert(atcmdSSet("S7=256") == 1);  /* out of range value */
}

static void
test_atcmdSQuery(void)
{
  setup();
  atcmd.s[7] = 20;
  assert(atcmdSQuery("S7?") == 0);   /* valid */
  assert(atcmdSQuery("S0?") == 0);
  assert(atcmdSQuery("S12?") == 0);  /* max valid */
  assert(atcmdSQuery("S13?") == 1);  /* out of range */
}

/* --- atcmdFake range checks --- */

static void
test_atcmdFake(void)
{
  /* arg 0 is in "01" */
  assert(atcmdFake("E0", "01") == 0);
  assert(atcmdFake("E1", "01") == 0);
  assert(atcmdFake("E2", "01") == 1);   /* 2 not in "01" */
  assert(atcmdFake("E",  "01") == 0);   /* default 0 is valid */

  /* "0" only accepts 0 */
  assert(atcmdFake("Q0", "0") == 0);
  assert(atcmdFake("Q1", "0") == 1);
}

/* --- flag setters --- */

static void
test_atcmdPD(void)
{
  assert(atcmdPD("D0") == 0);
  assert(atcmd.pd == 0);
  assert(atcmdPD("D1") == 0);
  assert(atcmd.pd == 1);
  assert(atcmdPD("D2") == 1);   /* out of range */
}

static void
test_atcmdPR(void)
{
  assert(atcmdPR("R0") == 0);
  assert(atcmd.pr == 0);
  assert(atcmdPR("R1") == 0);
  assert(atcmd.pr == 1);
  assert(atcmdPR("R2") == 1);
}

static void
test_atcmdPV(void)
{
  assert(atcmdPV("V0") == 0);
  assert(atcmd.pv == 0);
  assert(atcmdPV("V255") == 0);
  assert(atcmd.pv == 255);
  assert(atcmdPV("V256") == 1);
}

static void
test_atcmdPB(void)
{
  /* atcmdPB receives yytext which includes the %B prefix */
  assert(atcmdPB("%B0=0") == 0);
  assert(atcmd.pb[0] == 0);
  assert(atcmdPB("%B1=3") == 0);
  assert(atcmd.pb[1] == 3);
  assert(atcmdPB("%B2=0") == 1);   /* index out of range */
  assert(atcmdPB("%B0=4") == 1);   /* value out of range */
}

static void
test_atcmdAW(void)
{
  setup();
  atcmd.s[7] = 42;
  atcmd.pv = 3;
  atcmdAW();
  assert(atcmdNV.s[7] == 42);
  assert(atcmdNV.pv == 3);
}

/* --- cmdLex return values --- */

static void
test_cmdlex_basic(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex("AT", &sock) == CMDST_OK);
  assert(cmdLex("at", &sock) == CMDST_OK);
  assert(cmdLex("AT ", &sock) == CMDST_OK);
  assert(cmdLex(NULL, &sock) == CMDST_NOAT);
  assert(cmdLex("# comment", &sock) == CMDST_NOAT);
  assert(cmdLex("garbage", &sock) == CMDST_NOAT);
  assert(cmdLex("AT!", &sock) == CMDST_ERROR);
}

static void
test_cmdlex_sreg(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex("ATS7=30", &sock) == CMDST_OK);
  assert(atcmd.s[7] == 30);

  assert(cmdLex("ATS12=50", &sock) == CMDST_OK);
  assert(atcmd.s[12] == 50);

  assert(cmdLex("ATS13=0", &sock) == CMDST_ERROR);  /* bad index */
  assert(cmdLex("ATS7=256", &sock) == CMDST_ERROR); /* bad value */
}

static void
test_cmdlex_dial(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex("ATD\"github.com 80\"", &sock) == CMDST_ATD);
  assert(strcmp(atcmd.d.addr.str, "github.com") == 0);
  assert(strcmp(atcmd.d.port.str, "80") == 0);

  assert(cmdLex("ATD\"192.168.1.1 8080\"", &sock) == CMDST_ATD);
  assert(strcmp(atcmd.d.addr.str, "192.168.1.1") == 0);
  assert(strcmp(atcmd.d.port.str, "8080") == 0);
}

static void
test_cmdlex_ato(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex("ATO", &sock) == CMDST_ATO);
  assert(cmdLex("ATO0", &sock) == CMDST_ATO);
  assert(cmdLex("ATO1", &sock) == CMDST_ERROR);  /* only 0 is valid */
}

static void
test_cmdlex_flags(void)
{
  st_sock sock;
  sockInit(&sock);
  setup();

  assert(cmdLex("AT%D1", &sock) == CMDST_OK);
  assert(atcmd.pd == 1);

  assert(cmdLex("AT%R0", &sock) == CMDST_OK);
  assert(atcmd.pr == 0);

  assert(cmdLex("AT%V3", &sock) == CMDST_OK);
  assert(atcmd.pv == 3);
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
