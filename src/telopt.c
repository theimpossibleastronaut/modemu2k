
#include <stdio.h>              /*stderr,(fprintf) */
#include <stdlib.h>
#include <sys/time.h>   /*->ttybuf.h (timeval)*/
#define TELCMDS                 /*to use strings defined in telnet.h */
#define TELOPTS
#include <arpa/telnet.h>        /*IAC,DO,DONT,... */

#include "m2k_private.h"       /*->sockbuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/
#include "m2k_ctx.h"

/* telnet option negotiation module */

static TelOptStates stTabMaster[] = {
  /*[opt]             [local]               [remote] */
  {TELOPT_BINARY, {TOR_BETTER, 0, 0}, {TOR_BETTER, 0, 0}},      /*0 */
  {TELOPT_ECHO, {TOR_MUSTNOT, 0, 0}, {TOR_BETTER, 0, 0}},       /*1 */
  {TELOPT_SGA, {TOR_BETTER, 0, 0}, {TOR_MUST, 0, 0}},   /*3 */
  {TELOPT_TTYPE, {TOR_NEUTRAL, 0, 0}, {TOR_MUSTNOT, 0, 0}},     /*24 */
  {-1, {TOR_MUSTNOT, 0, 0}, {TOR_MUSTNOT, 0, 0}}        /* default state */
};

TelOptStates *stTab[NTELOPTS];  /* telOptInit() makes it usable */

static /*const */ TelOptStates *defaultSt;      /* used when unknown options come */


/* must call before each telnet session begins */
void
telOptReset(m2k_t *ctx)
{
  TelOptStates *tosp;

  for (tosp = stTabMaster; tosp->opt >= 0; tosp++)
  {
    tosp->local.state = tosp->remote.state = 0; /* all options are disabled initially */
    tosp->local.pending = tosp->remote.pending = 0;
  }
  ctx->telOpt.binsend = ctx->telOpt.binrecv = ctx->telOpt.sgasend = 0;
  ctx->telOpt.sentReqs = 0;
}


/* must call once before using this module */
void
telOptInit(m2k_t *ctx)
{
  TelOptStates *tosp;
  int i;

  for (tosp = stTabMaster; tosp->opt >= 0; tosp++);
  for (i = 0; i < NTELOPTS; i++)
    stTab[i] = tosp;            /* default entry */
  defaultSt = tosp;
  for (tosp--; tosp >= stTabMaster; tosp--)
  {
    stTab[tosp->opt] = tosp;
  }
  ctx->telOpt.stTab = stTab;
}


static const char *
telcmdStr(int cmd)
{
  static char str[16];

#ifndef TELCMD_FIRST            /*is this rule correct for all telnet.h? */
#define TELCMD_FIRST (256 - sizeof(telcmds)/sizeof(telcmds[0]))
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
  static TelOptReq tabP[]
    = { TOR_BETTERNOT, TOR_BETTER, TOR_MUSTNOT, TOR_MUST };
  static TelOptReq tabN[]
    = { TOR_BETTER, TOR_BETTERNOT, TOR_MUST, TOR_MUSTNOT };

  /* %Bn=m (binary mode control) */
  stTab[TELOPT_BINARY]->local.req = tabP[ctx->atcmd.pb[1]];
  stTab[TELOPT_BINARY]->remote.req = tabP[ctx->atcmd.pb[0]];
  /* %Ln (linemode control) */
  stTab[TELOPT_SGA]->remote.req = tabN[ctx->atcmd.pl];
  stTab[TELOPT_ECHO]->remote.req = tabN[ctx->atcmd.pl];
  /* %Tn (terminal-type response control) */
  stTab[TELOPT_TTYPE]->local.req = ctx->atcmd.pt.wont ? TOR_MUSTNOT : TOR_NEUTRAL;
}


/* tell the peer my option-state-to-be requests */
void
telOptSendReqs(m2k_t *ctx)
{
  TelOptStates *tosp;

  setReqs(ctx);

  for (tosp = stTabMaster; tosp->opt >= 0; tosp++)
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
  ctx->telOpt.binsend = stTab[TELOPT_BINARY]->local.state;
  ctx->telOpt.binrecv = stTab[TELOPT_BINARY]->remote.state;
  ctx->telOpt.sgasend = stTab[TELOPT_SGA]->remote.state;
}


/* telnet option request/response handling */
int
telOptHandle(m2k_t *ctx, int cmd, int opt)
{
  TelOptState *tostp;
  TelOptStates *tosp;
  int reqState;                 /* cmd's requiring state */
  int posiResCmd;               /* positive response command for cmd */
  int negaResCmd;               /* negative response command for cmd */
  TelOptReq mustNegate;         /* must negate if req is this */
  TelOptReq betterNegate;       /* better negate if req is this */
  //  TelOptReq betterAssert;       /* better assert if req is this */
  //  TelOptReq mustAssert;         /* must assert if req is this */

  printCmdOpt(ctx, "<", cmd, opt);

  tosp = (opt < NTELOPTS) ? stTab[opt] : defaultSt;

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
        return 1;               /* requirment didn't meet */
      if (tostp->state == !reqState)
      {                         /* this may not happen */
        tostp->state = reqState;
        putOptCmd(ctx, posiResCmd, opt);     /* positive response */
        printCmdOpt(ctx, ">", posiResCmd, opt);
      }
    }
    else
    {
      putOptCmd(ctx, negaResCmd, opt);       /* negative response */
      printCmdOpt(ctx, ">", negaResCmd, opt);
    }
  }
  else                          /*if (tostp->req == betterAssert or mustAssert or TOR_NEUTRAL) */
  {
    if (tostp->pending)
    {
      tostp->pending = 0;
      /* don't response because cmd is the response of my request */
    }
    else
    {
      if (tostp->state == !reqState)
      {                         /* this may not happen */
        putOptCmd(ctx, posiResCmd, opt);     /* positive response */
        printCmdOpt(ctx, ">", posiResCmd, opt);
      }
    }
    tostp->state = reqState;    /* {en,dis}able option as requested */
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
