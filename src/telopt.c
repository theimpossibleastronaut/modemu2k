
#include <stdio.h> /*stderr,(fprintf) */
#include <stdlib.h>
#include <sys/time.h> /*->ttybuf.h (timeval)*/
#define TELCMDS       /*to use strings defined in telnet.h */
#define TELOPTS
#include <arpa/telnet.h> /*IAC,DO,DONT,... */

#include "m2k_private.h" /*->sockbuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/
#include "m2k_ctx.h"

/* telnet option negotiation module — per-ctx state lives in
   ctx->telOpt.stTabMaster[] and ctx->telOpt.stTab[]. The initial
   requirement template below is copied into each ctx by telOptInit. */

static const TelOptStates stTabTemplate[M2K_TELOPT_ENTRIES] = {
  /*[opt]             [local]               [remote] */
  {TELOPT_BINARY, {TOR_BETTER, 0, 0}, {TOR_BETTER, 0, 0}},  /*0 */
  {TELOPT_ECHO, {TOR_MUSTNOT, 0, 0}, {TOR_BETTER, 0, 0}},   /*1 */
  {TELOPT_SGA, {TOR_BETTER, 0, 0}, {TOR_MUST, 0, 0}},       /*3 */
  {TELOPT_TTYPE, {TOR_NEUTRAL, 0, 0}, {TOR_MUSTNOT, 0, 0}}, /*24 */
  {-1, {TOR_MUSTNOT, 0, 0}, {TOR_MUSTNOT, 0, 0}}            /* default state */
};

/* The default/unknown-option entry is the last slot of stTabMaster. */
#define DEFAULT_ST(ctx) (&(ctx)->telOpt.stTabMaster[M2K_TELOPT_ENTRIES - 1])


/* must call before each telnet session begins */
void
telOptReset(m2k_t *ctx)
{
  TelOptStates *tosp;

  for (tosp = ctx->telOpt.stTabMaster; tosp->opt >= 0; tosp++)
  {
    tosp->local.state = tosp->remote.state = 0; /* all options are disabled initially */
    tosp->local.pending = tosp->remote.pending = 0;
  }
  ctx->telOpt.binsend = ctx->telOpt.binrecv = ctx->telOpt.sgasend = 0;
  ctx->telOpt.sentReqs = 0;
}


/* must call once before using this ctx for telnet negotiation */
void
telOptInit(m2k_t *ctx)
{
  TelOptStates *tosp;
  TelOptStates *master = ctx->telOpt.stTabMaster;
  int i;

  /* Copy the per-process template into per-ctx mutable storage. */
  memcpy(master, stTabTemplate, sizeof(stTabTemplate));

  /* Build the opt-id lookup: every slot points at the sentinel first,
     then per-option slots get pointed at their stTabMaster entry. */
  for (tosp = master; tosp->opt >= 0; tosp++)
    ;
  TelOptStates *sentinel = tosp;
  for (i = 0; i < NTELOPTS; i++)
    ctx->telOpt.stTab[i] = sentinel;
  for (tosp--; tosp >= master; tosp--)
    ctx->telOpt.stTab[tosp->opt] = tosp;
}


static const char *
telcmdStr(int cmd)
{
  static char str[16];

#ifndef TELCMD_FIRST /*is this rule correct for all telnet.h? */
#define TELCMD_FIRST (256 - sizeof(telcmds) / sizeof(telcmds[0]))
#endif
  if (cmd >= TELCMD_FIRST)
  {
    return telcmds[cmd - TELCMD_FIRST];
  }
  else
  {
    sprintf(str, "?(%d)", cmd);
    return str;
  }
}


static const char *
teloptStr(int opt)
{
  static char str[16];

  if (opt < NTELOPTS)
  {
    return telopts[opt];
  }
  else
  {
    sprintf(str, "?(%d)", opt);
    return str;
  }
}


void
telOptPrintCmd(m2k_t *ctx, const char *str, int cmd)
{
  verboseOut(ctx, VERB_TELOPT, "%s IAC %s\r\n", str, telcmdStr(cmd));
}


static void
printCmdOpt(m2k_t *ctx, const char *str, int cmd, int opt)
{
  verboseOut(ctx, VERB_TELOPT, "%s %s %s\r\n", str, telcmdStr(cmd),
             teloptStr(opt));
}


static void
setReqs(m2k_t *ctx)
{
  static TelOptReq tabP[] = {TOR_BETTERNOT, TOR_BETTER, TOR_MUSTNOT, TOR_MUST};
  static TelOptReq tabN[] = {TOR_BETTER, TOR_BETTERNOT, TOR_MUST, TOR_MUSTNOT};

  TelOptStates **st = ctx->telOpt.stTab;
  /* %Bn=m (binary mode control) */
  st[TELOPT_BINARY]->local.req = tabP[ctx->atcmd.pb[1]];
  st[TELOPT_BINARY]->remote.req = tabP[ctx->atcmd.pb[0]];
  /* %Ln (linemode control) */
  st[TELOPT_SGA]->remote.req = tabN[ctx->atcmd.pl];
  st[TELOPT_ECHO]->remote.req = tabN[ctx->atcmd.pl];
  /* %Tn (terminal-type response control) */
  st[TELOPT_TTYPE]->local.req = ctx->atcmd.pt.wont ? TOR_MUSTNOT : TOR_NEUTRAL;
}


/* tell the peer my option-state-to-be requests */
void
telOptSendReqs(m2k_t *ctx)
{
  TelOptStates *tosp;

  setReqs(ctx);

  for (tosp = ctx->telOpt.stTabMaster; tosp->opt >= 0; tosp++)
  {
    switch (tosp->local.req)
    {
    case TOR_MUSTNOT:
    case TOR_BETTERNOT:
      if (tosp->local.state == 1)
      {
        putOptCmd(ctx, WONT, tosp->opt);
        printCmdOpt(ctx, ">", WONT, tosp->opt);
        tosp->local.pending = 1;
      }
      break;
    case TOR_BETTER:
    case TOR_MUST:
      if (tosp->local.state == 0)
      {
        putOptCmd(ctx, WILL, tosp->opt);
        printCmdOpt(ctx, ">", WILL, tosp->opt);
        tosp->local.pending = 1;
      }
      break;
    default:;
    }
    switch (tosp->remote.req)
    {
    case TOR_MUSTNOT:
    case TOR_BETTERNOT:
      if (tosp->remote.state == 1)
      {
        putOptCmd(ctx, DONT, tosp->opt);
        printCmdOpt(ctx, ">", DONT, tosp->opt);
        tosp->remote.pending = 1;
      }
      break;
    case TOR_BETTER:
    case TOR_MUST:
      if (tosp->remote.state == 0)
      {
        putOptCmd(ctx, DO, tosp->opt);
        printCmdOpt(ctx, ">", DO, tosp->opt);
        tosp->remote.pending = 1;
      }
      break;
    default:;
    }
  }
  ctx->telOpt.sentReqs = 1;
}


/* summarize option states into flags */
static void
telOptSummarize(m2k_t *ctx)
{
  ctx->telOpt.binsend = ctx->telOpt.stTab[TELOPT_BINARY]->local.state;
  ctx->telOpt.binrecv = ctx->telOpt.stTab[TELOPT_BINARY]->remote.state;
  ctx->telOpt.sgasend = ctx->telOpt.stTab[TELOPT_SGA]->remote.state;
}


/* telnet option request/response handling */
int
telOptHandle(m2k_t *ctx, int cmd, int opt)
{
  TelOptState *tostp;
  TelOptStates *tosp;
  int reqState;           /* cmd's requiring state */
  int posiResCmd;         /* positive response command for cmd */
  int negaResCmd;         /* negative response command for cmd */
  TelOptReq mustNegate;   /* must negate if req is this */
  TelOptReq betterNegate; /* better negate if req is this */
  //  TelOptReq betterAssert;       /* better assert if req is this */
  //  TelOptReq mustAssert;         /* must assert if req is this */

  printCmdOpt(ctx, "<", cmd, opt);

  tosp = (opt < NTELOPTS) ? ctx->telOpt.stTab[opt] : DEFAULT_ST(ctx);

  switch (cmd)
  {
  case WILL:
    tostp = &tosp->remote;
    reqState = 1;
    mustNegate = TOR_MUSTNOT;
    betterNegate = TOR_BETTERNOT;
    // betterAssert = TOR_BETTER;
    // mustAssert = TOR_MUST;
    posiResCmd = DO;
    negaResCmd = DONT;
    break;
  case WONT:
    tostp = &tosp->remote;
    reqState = 0;
    mustNegate = TOR_MUST;
    betterNegate = TOR_BETTER;
    // betterAssert = TOR_BETTERNOT;
    // mustAssert = TOR_MUSTNOT;
    posiResCmd = DONT;
    negaResCmd = DO;
    break;
  case DO:
    tostp = &tosp->local;
    reqState = 1;
    mustNegate = TOR_MUSTNOT;
    betterNegate = TOR_BETTERNOT;
    //  betterAssert = TOR_BETTER;
    //  mustAssert = TOR_MUST;
    posiResCmd = WILL;
    negaResCmd = WONT;
    break;
  case DONT:
    tostp = &tosp->local;
    reqState = 0;
    mustNegate = TOR_MUST;
    betterNegate = TOR_BETTER;
    // betterAssert = TOR_BETTERNOT;
    // mustAssert = TOR_MUSTNOT;
    posiResCmd = WONT;
    negaResCmd = WILL;
    break;
  default:
    abort();
  }

  if (tostp->req == mustNegate || tostp->req == betterNegate)
  {
    if (tostp->pending)
    {
      tostp->pending = 0;
      if (tostp->req == mustNegate)
        return 1; /* requirment didn't meet */
      if (tostp->state == !reqState)
      { /* this may not happen */
        tostp->state = reqState;
        putOptCmd(ctx, posiResCmd, opt); /* positive response */
        printCmdOpt(ctx, ">", posiResCmd, opt);
      }
    }
    else
    {
      putOptCmd(ctx, negaResCmd, opt); /* negative response */
      printCmdOpt(ctx, ">", negaResCmd, opt);
    }
  }
  else /*if (tostp->req == betterAssert or mustAssert or TOR_NEUTRAL) */
  {
    if (tostp->pending)
    {
      tostp->pending = 0;
      /* don't response because cmd is the response of my request */
    }
    else
    {
      if (tostp->state == !reqState)
      {                                  /* this may not happen */
        putOptCmd(ctx, posiResCmd, opt); /* positive response */
        printCmdOpt(ctx, ">", posiResCmd, opt);
      }
    }
    tostp->state = reqState; /* {en,dis}able option as requested */
  }

  telOptSummarize(ctx);
  return 0;
}

/* send term-type subnego param */
static void
ttypeSBHandle(m2k_t *ctx)
{
  putSock1(ctx, IAC);
  putSock1(ctx, SB);
  putSock1(ctx, TELOPT_TTYPE);
  putSock1(ctx, TELQUAL_IS);
  putSockN(ctx, (uchar *) ctx->atcmd.pt.str, ctx->atcmd.pt.len);
  putSock1(ctx, IAC);
  putSock1(ctx, SE);
  verboseOut(ctx, VERB_TELOPT, "> SB %s IS %s SE\r\n",
             telopts[TELOPT_TTYPE], ctx->atcmd.pt.str);
}

/* telnet option subnegotiation request handling */
int
telOptSBHandle(m2k_t *ctx, int opt)
{
  verboseOut(ctx, VERB_TELOPT, "< SB %s SEND SE.\r\n", telopts[opt]);

  switch (opt)
  {
  case TELOPT_TTYPE:
    ttypeSBHandle(ctx);
    break;
  default:
    return 1;
  }
  return 0;
}
