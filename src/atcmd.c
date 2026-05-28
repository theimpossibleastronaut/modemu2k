/*
 *
 * Copyright 2018-2021 Andy Alt <arch_stanton5995@protonmail.com>
 *
 * modemu2k is a fork of modemu
 * Originally developed by Toru Egashira
 * Copyright (c) 1995, 1996
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 *
 */

#include <stdlib.h>      /*(getenv) */
#include <arpa/telnet.h> /*TELOPT_xxx */
#include "cmdarg.h"
#include "m2k_private.h"
#include "m2k_ctx.h"

#ifdef BINMODE_AS_DEFAULT
#define BINCMD "%B0=1%B1=1"
#else
#define BINCMD
#endif

#define INITSTR "AT"                                              \
                "S2=43"  /* escape char = '+' */                  \
                "S3=13"  /* CR */                                 \
                "S4=10"  /* LF */                                 \
                "S5=8"   /* BS */                                 \
                "S7=20"  /* timelimit for non-blocking connect */ \
                "S12=50" /* escape sequence guard time */         \
  BINCMD                 /* binary mode */                        \
                "%T1"    /* terminal-type = $TERM */              \
                "&W"     /* write to NVRAM */

void
atcmdInit(m2k_t *ctx, struct st_cmdarg *cmdarg, st_sock *sock)
{
  Cmdstat s;

  /*memset(atcmd, 0, sizeof(atcmd)); */
  if (cmdLex(ctx, INITSTR, sock) != CMDST_OK || ((s = cmdLex(ctx, getenv("MODEMU2k"), sock)) != CMDST_OK && s != CMDST_NOAT) || (cmdarg != NULL && (s = cmdLex(ctx, cmdarg->atcmd, sock)) != CMDST_OK && s != CMDST_NOAT))
  {
    m2k_log(ctx, "Error in initialization commands.\r\n");
    CHAR_CR(ctx) = '\r'; /* force normal settings */
    CHAR_LF(ctx) = '\n';
  }
}

/* LIT(A) -> "10" */
#define LIT_(s) #s
#define LIT(s) LIT_(s)

#define PORT_DELIMITER ' '

/* D */
/* dial command */
void
m2k_atcmdD(m2k_t *ctx, const char *s, AtdAType at, AtdPType pt)
{
  // fprintf(stderr,"DEBUG: <%s>,%d,%d\r\n",s,at,pt);
  if (*s == '"')
    s++;
  /* "%[^:\"]:%[^\"]" */
  sscanf(s, "%" LIT(ADDR_MAX) "[^ \"] %" LIT(PORT_MAX) "[^\"]",
         ctx->atcmd.d.addr.str, ctx->atcmd.d.port.str);
  ctx->atcmd.d.addr.type = at;
  ctx->atcmd.d.port.type = pt;
  // fprintf(stderr,"DEBUG: <%s>:<%s>\r\n",ctx->atcmd.d.addr.str, ctx->atcmd.d.port.str);
}

/* "x0" or "x" -> 0, "x1" -> 1, ... */
static int
getNumArg(const char *s)
{
#define isdigit(c) ('0' <= (c) && (c) <= '9')
  for (; *s != '\0'; s++)
    if (isdigit(*s))
      return atoi(s);
  return 0;
}

/* fake command */
/* ("x1","012") -> 0, ("y3","012") -> 1 (just a range check) */
int
atcmdFake(m2k_t *ctx, const char *s, const char *vals)
{
  (void) ctx;
  int i;

  i = getNumArg(s) + '0';
  for (; *vals != '\0'; vals++)
    if (i == *vals)
      return 0;
  return 1;
}

/* Hn */
/* n: 0(disconnect) */
int
atcmdH(m2k_t *ctx, const char *s, st_sock *sock)
{
  if (getNumArg(s) != 0)
    return 1;
  if (sock->alive)
  {
    sockClose(sock);
    verboseOut(ctx, VERB_MISC, "Connection closed with ATH.\r\n");
  }
  return 0;
}


/* In */
/* n: 4(show current settings), 5(show '&W'ed settings) or */
/*    6(show telnet option states) */

static void
prPercent(m2k_t *ctx, Atcmd *atcmdp)
{
  char buf[64];

  snprintf(buf, sizeof buf, "%c%c%%B0=%d  %%B1=%d  %%D%d  %%L%d  %%R%d",
           CHAR_CR(ctx), CHAR_LF(ctx),
           atcmdp->pb[0], atcmdp->pb[1], atcmdp->pd, atcmdp->pl, atcmdp->pr);
  putTtyN(ctx, buf, strlen(buf));
  if (atcmdp->pt.wont)
  {
    putTtyStr(ctx, "  %T0");
  }
  else
  {
    sprintf(buf, "  %%T=\"%s\"", atcmdp->pt.str);
    putTtyN(ctx, buf, strlen(buf));
  }
  sprintf(buf, "  %%V%d", atcmdp->pv);
  putTtyN(ctx, buf, strlen(buf));
}

static void
prSreg(m2k_t *ctx, uchar *s)
{
  int i;
  char buf[8];

  for (i = 0; i <= SREG_MAX; i++, s++)
  {
    if (i % 8 == 0)
    {
      putTty1(ctx, CHAR_CR(ctx));
      putTty1(ctx, CHAR_LF(ctx));
    }
    else
      putTtyStr(ctx, "  ");
    snprintf(buf, sizeof buf, "S%02d=%03d", i, *s);
    putTtyN(ctx, buf, 7);
  }
}

static void
prOption(m2k_t *ctx)
{
  static char *onoff[] = {"off", "on "};
  char buf[64];

  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTtyStr(ctx, "modemu2k telnet option states:");
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTtyStr(ctx, "OPTION  LOCAL  REMOTE");
  sprintf(buf, "Binary   %s    %s",
          onoff[ctx->telOpt.stTab[TELOPT_BINARY]->local.state],
          onoff[ctx->telOpt.stTab[TELOPT_BINARY]->remote.state]);
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTtyN(ctx, buf, strlen(buf));
  sprintf(buf, "Echo     %s    %s",
          onoff[ctx->telOpt.stTab[TELOPT_ECHO]->local.state],
          onoff[ctx->telOpt.stTab[TELOPT_ECHO]->remote.state]);
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTtyN(ctx, buf, strlen(buf));
  sprintf(buf, "SGA      %s    %s",
          onoff[ctx->telOpt.stTab[TELOPT_SGA]->local.state],
          onoff[ctx->telOpt.stTab[TELOPT_SGA]->remote.state]);
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTtyN(ctx, buf, strlen(buf));
  sprintf(buf, "TType    %s    %s",
          onoff[ctx->telOpt.stTab[TELOPT_TTYPE]->local.state],
          onoff[ctx->telOpt.stTab[TELOPT_TTYPE]->remote.state]);
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTtyN(ctx, buf, strlen(buf));
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
}

static void
prVersion(m2k_t *ctx)
{
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  putTtyStr(ctx, "modemu2k version " M2K_VERSION);
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
}

int
atcmdI(m2k_t *ctx, const char *s)
{
  int idx;

  idx = getNumArg(s);
  switch (idx)
  {
  case 4:
    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    putTtyStr(ctx, "modemu2k current settings:");
    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    prPercent(ctx, &ctx->atcmd);
    prSreg(ctx, ctx->atcmd.s);
    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    break;
  case 5:
    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    putTtyStr(ctx, "modemu2k '&W'ed settings:");
    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    prPercent(ctx, &ctx->atcmdNV);
    prSreg(ctx, ctx->atcmdNV.s);
    putTty1(ctx, CHAR_CR(ctx));
    putTty1(ctx, CHAR_LF(ctx));
    break;
  case 6:
    prOption(ctx);
    break;
  case 7:
    prVersion(ctx);
    break;
  default:
    return 1;
  }
  return 0;
}


/* Sn? */
/* n: S register number */
int
atcmdSQuery(m2k_t *ctx, const char *s)
{
  int idx;
  char buf[4];

  idx = getNumArg(s);
  if (idx > SREG_MAX)
    return 1;
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx));
  sprintf(buf, "%03u", ctx->atcmd.s[idx]);
  putTtyN(ctx, buf, 3);
  putTty1(ctx, CHAR_CR(ctx));
  putTty1(ctx, CHAR_LF(ctx)); /*at least Courier does */
  return 0;
}

/* "1=2" -> (1,2), "1=" -> (1,0), "=2" -> (0,2), "=" -> (0,0) */
static void
getNumArg2(const char *s, int *n1p, int *n2p)
{
  char *s2;

  *n1p = strtol(s, &s2, 10);
  *n2p = strtol(s2 + 1, NULL, 10);
}

/* Sn=m */
/* n: S register number */
/* m: value 0-255 */
int
atcmdSSet(m2k_t *ctx, const char *s)
{
  int idx, val;

  getNumArg2(s + 1, &idx, &val);
  if (idx > SREG_MAX || val > 255)
    return 1;
  ctx->atcmd.s[idx] = val;
  return 0;
}

/* Z */
/* recover &Wed settings and disconnect */
void
atcmdZ(m2k_t *ctx, st_sock *sock)
{
  ctx->atcmd = ctx->atcmdNV;
  if (sock->alive)
  {
    sockClose(sock);
    verboseOut(ctx, VERB_MISC, "Connection closed with ATZ.\r\n");
  }
}

/* &W */
/* save current settings */
void
atcmdAW(m2k_t *ctx)
{
  ctx->atcmdNV = ctx->atcmd;
}

/* %Bn=m */
/* n: 0(local) or 1(remote) */
/* m: 0(better non-binary), 1(better binary), */
/*    2(must non-binary) or 3(must binary) */
int
atcmdPB(m2k_t *ctx, const char *s)
{
  int idx, val;

  getNumArg2(s + 2, &idx, &val);
  if (idx > 1 || val > 3)
    return 1;
  ctx->atcmd.pb[idx] = val;
  ctx->telOpt.sentReqs = 0; /* renegotiate when returning online */
  return 0;
}

/* %Dn */
/* n: 0(enable dial canceling) or 1(disable dial canceling) */
int
atcmdPD(m2k_t *ctx, const char *s)
{
  int i;

  i = getNumArg(s);
  if (i > 1)
    return 1;
  ctx->atcmd.pd = i;
  return 0;
}

/* %Ln */
/* n: 0(better char mode), 1(better line mode), */
/*    2(must char mode) or 3(must line mode) */
int
atcmdPL(m2k_t *ctx, const char *s)
{
  int i;

  i = getNumArg(s);
  if (i > 3)
    return 1;
  ctx->atcmd.pl = i;
  ctx->telOpt.sentReqs = 0; /* renegotiate when returning online */
  return 0;
}

/* %Q */
/* quit modemu */
void
atcmdPQ(m2k_t *ctx, st_sock *sock)
{
  (void) ctx;
  sockShutdown(sock); /* may discard unsent chars in kernel,
                                   or do ATH before quitting */
}

/* %Rn */
/* n: 0(cooked?? mode) or 1(raw mode: 8bit thru, no IAC handling) */
/* overrides %B and %L settings */
int
atcmdPR(m2k_t *ctx, const char *s)
{
  int i;

  i = getNumArg(s);
  if (i > 1)
    return 1;
  ctx->atcmd.pr = i;
  return 0;
}

/* %Tn */
/* n: 0(dont support telnet term-type option) or */
/*    1(send $TERM for term-type option request) */
int
atcmdPT(m2k_t *ctx, const char *s)
{
  int i;

  i = getNumArg(s);
  switch (i)
  {
  case 0:
    ctx->atcmd.pt.wont = 1;
    break;
  case 1:
  {
    const char *term = getenv("TERM");
    strncpy(ctx->atcmd.pt.str, term ? term : "", PT_MAX);
    ctx->atcmd.pt.len = strlen(ctx->atcmd.pt.str);
    ctx->atcmd.pt.wont = 0;
    break;
  }
  default:
    return 1;
  }
  return 0;
}

/* %T="xxxx" */
/* send xxxx for term-type option request */
int
atcmdPTSet(m2k_t *ctx, const char *s)
{
  sscanf(s + 4, "%" LIT(PT_MAX) "[^\"]", ctx->atcmd.pt.str);
  /*strncpy(ctx->atcmd.pt.str, s+3, PT_MAX); */
  ctx->atcmd.pt.len = strlen(ctx->atcmd.pt.str);
  /*ctx->telOpt.sentReqs = 0; renegotiation will be of no effect */
  ctx->atcmd.pt.wont = 0;
  return 0;
}

/* %Vn */
/* n: 0-255 */
/*    bit0: output misc info to make up for less descriptive ATX0 indication */
/*    bit1: output telnet option negotioation */
int
atcmdPV(m2k_t *ctx, const char *s)
{
  int i;

  i = getNumArg(s);
  if (i > 255)
    return 1;
  ctx->atcmd.pv = i;
  return 0;
}
