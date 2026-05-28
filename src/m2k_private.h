/*
 *
 * Copyright 2018 Andy Alt <arch_stanton5995@protonmail.com>
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

#pragma once

/* private internal header — not installed, not part of the public API */

/* Pull in the public types (m2k_t, m2k_err_t, m2k_log_fn, public functions) */
#include "config.h"
#include "modemu2k.h"
#include "modemu2k_version.h"

#include <stdio.h>  /*stderr,(sscanf,sprintf) */
#include <string.h> /*(strncpy) */
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/telnet.h> /* NTELOPTS for per-ctx telopt state */

#ifdef TERMNET
#include <termnet.h>
#endif

#ifdef USE_FILIO_H
#include <sys/filio.h>
#else
#include <sys/ioctl.h>
#endif

#include "cmdarg.h"

/* socket read buffer size */
#define SOCKBUFR_SIZE 1024

/* tty read buffer size */
#define TTYBUFR_SIZE 1024

/* line (!char) mode line-length */
#define LINEBUF_SIZE 256

/* command mode line-length (w/o null) */
#define CMDBUF_MAX 255

typedef unsigned char uchar;

/* +++ escape detection — per-ctx so two coexisting m2k_t instances
   don't share/corrupt each other's online-mode escape timing. */
typedef enum
{
  ESH_NORM,
  ESH_P1,
  ESH_P2,
  ESH_P3
} m2k_esh_state;
struct m2k_escseq
{
  m2k_esh_state state;
  struct timeval plus1T;
  int checkSilence;
  struct timeval expireT;
};

/* line buffer for non-SGA telnet mode — also per-ctx. */
struct m2k_linebuf
{
  uchar buf[LINEBUF_SIZE];
  uchar *ptr;
};

/* Command-mode input line buffer. Defined here (rather than as a
   m2k_api.c private struct) so the m2k_t context can embed one for
   the steppable m2k_step() API. */
struct m2k_cmdbuf
{
  uchar buf[CMDBUF_MAX + 1];
  uchar *ptr;
  int eol;
};

/* Top-level state of the steppable event loop. */
typedef enum
{
  M2K_STATE_CMD,    /* command mode — reading AT commands from the TTY */
  M2K_STATE_DIAL,   /* non-blocking dial in progress — sock.fd is mid-connect */
  M2K_STATE_ONLINE, /* online mode — relaying between TTY and socket */
  M2K_STATE_DONE    /* PTY closed; m2k_run_done() returns true */
} m2k_step_state;

#define DEFAULT_PORT 23

// sock

typedef struct st_sock
{
  /** file descriptor returned by socket(3P) **/
  int fd;

  /** is the socket still alive */
  int alive;

  /** pointer to the addrinfo struct returned by getaddrinfo(3) */
  struct addrinfo *rp;
} st_sock;

void sockInit(struct st_sock *sock);
int sockClose(st_sock *sock);
int sockShutdown(st_sock *sock);


// atcmd

typedef enum
{
  ATDA_NUM,
  ATDA_STR
} AtdAType;

typedef enum
{
  ATDP_NUL,
  ATDP_NUM,
  ATDP_STR
} AtdPType;

#define ADDR_MAX 255 /* _POSIX_HOST_NAME_MAX */
#define PORT_MAX 63
#define PT_MAX 40
#define SREG_MAX 12

typedef struct
{
  struct
  {
    struct
    {
      char str[ADDR_MAX + 1];
      AtdAType type;
    } addr;
    struct
    {
      char str[PORT_MAX + 1];
      AtdPType type;
    } port;
  } d;
  int f;
  unsigned char s[SREG_MAX + 1];
  int pb[2];
  int pd;
  int pl;
  int pr;
  struct
  {
    char str[PT_MAX + 1];
    int len;
    int wont;
  } pt;
  int pv;
} Atcmd;

/* m2k_err_t, m2k_log_fn, and m2k_t are defined in the public header above */

#define CHAR_ESC(ctx) ((ctx)->atcmd.s[2])
#define CHAR_CR(ctx) ((ctx)->atcmd.s[3])
#define CHAR_LF(ctx) ((ctx)->atcmd.s[4])
#define CHAR_BS(ctx) ((ctx)->atcmd.s[5])

void atcmdInit(m2k_t *ctx, struct st_cmdarg *cmdarg, st_sock *sock);


void m2k_atcmdD(m2k_t *ctx, const char *s, AtdAType at, AtdPType pt);


int atcmdFake(m2k_t *ctx, const char *s, const char *vals);
int atcmdH(m2k_t *ctx, const char *s, st_sock *sock);
int atcmdI(m2k_t *ctx, const char *s);
int atcmdSQuery(m2k_t *ctx, const char *s);
int atcmdSSet(m2k_t *ctx, const char *s);
void atcmdZ(m2k_t *ctx, st_sock *sock);
void atcmdAW(m2k_t *ctx);
int atcmdPB(m2k_t *ctx, const char *s);
int atcmdPD(m2k_t *ctx, const char *s);
int atcmdPL(m2k_t *ctx, const char *s);
void atcmdPQ(m2k_t *ctx, st_sock *sock);
int atcmdPR(m2k_t *ctx, const char *s);
int atcmdPT(m2k_t *ctx, const char *s);
int atcmdPTSet(m2k_t *ctx, const char *s);
int atcmdPV(m2k_t *ctx, const char *s);

// comm_program
m2k_err_t commProgramForkExec(m2k_t *ctx, const char *cmd, char *ptyslave);

// sockbuf


/* reading socket */

struct st_sockBufR
{
  uchar buf[SOCKBUFR_SIZE];
  uchar *ptr;
  uchar *end;
};


void sockBufRReset(m2k_t *ctx);

int getSock1(m2k_t *ctx);

void sockBufRead(m2k_t *ctx, st_sock *sock);


/* writing socket */

#define SOCKBUFW_SIZE (2 * TTYBUFR_SIZE)               /* this seems to be any number */
#define SOCKBUFW_SIZE_A (SOCKBUFW_SIZE + TTYBUFR_SIZE) /* important */

struct st_sockBufW
{
  uchar buf[SOCKBUFW_SIZE_A];
  uchar *top;
  uchar *ptr;
  int stop;
};

void sockBufWReset(m2k_t *ctx);

bool sockBufWHasData(m2k_t *ctx);

bool sockBufWReady(m2k_t *ctx);

void sockBufWrite(m2k_t *ctx, st_sock *sock);
void putSock1(m2k_t *ctx, uchar c);
void putSockN(m2k_t *ctx, const uchar *cp, int n);

// stty

void setTty(void);

// telopt

/* requirements for a telnet option */
typedef enum
{
  TOR_MUSTNOT,   /* disable the opt or disconnect */
  TOR_BETTERNOT, /* disable the opt or enable unwillingly */
  TOR_NEUTRAL,   /* modemu doesn't initiate any action for the opt.
                                   {en,dis}able the opt as the peer requests. */
  TOR_BETTER,    /* enable the opt or disable unwillingly */
  TOR_MUST       /* enable the opt or disconnect */
} TelOptReq;

typedef struct
{
  TelOptReq req; /* requirement for the opt */
  int state;     /* current state (enabled:1 or disabled:0) */
  int pending;   /* state is pending (requested but no reply yet) */
} TelOptState;

typedef struct
{
  int opt;            /* an telnet option. TELOPT_XXX */
  TelOptState local;  /* local state of the option */
  TelOptState remote; /* remote state of the option */
} TelOptStates;

/* 4 negotiated options (BINARY, ECHO, SGA, TTYPE) + 1 sentinel/default */
#define M2K_TELOPT_ENTRIES 5

struct st_telOpt
{
  int binsend;  /* local binary opt is enabled */
  int binrecv;  /* remote binary opt is enabled */
  int sgasend;  /* local SGA opt is enabled (char-at-a-time mode) */
  int sentReqs; /* have sent option requests to the peer
                                   or skip sending them */
  /* Per-ctx telnet-option state. Was file-scope globals in telopt.c
     until per-ctx separation. stTabMaster holds the live (mutated by
     telOptReset) per-option negotiation state; stTab is the opt-id
     lookup table; the last entry of stTabMaster is the default/unknown
     fallback. */
  TelOptStates stTabMaster[M2K_TELOPT_ENTRIES];
  TelOptStates *stTab[NTELOPTS];
};

#define putOptCmd(ctx, s, c) \
  {                          \
    putSock1((ctx), IAC);    \
    putSock1((ctx), (s));    \
    putSock1((ctx), (c));    \
  }

void telOptReset(m2k_t *ctx);
void telOptInit(m2k_t *ctx);
void telOptPrintCmd(m2k_t *ctx, const char *str, int cmd);
void telOptSendReqs(m2k_t *ctx);
int telOptHandle(m2k_t *ctx, int cmd, int opt);
int telOptSBHandle(m2k_t *ctx, int opt);

// timeval

void timevalSet10ms(struct timeval *ap, int b);
void timevalAdd(struct timeval *ap, const struct timeval *bp);
void timevalSub(struct timeval *ap, const struct timeval *bp);
int timevalCmp(const struct timeval *ap, const struct timeval *bp);

// ttybuf

struct st_tty
{
  int rfd;
  int wfd;
};


/* reading tty */

struct st_ttyBufR
{
  unsigned char buf[TTYBUFR_SIZE];
  unsigned char *ptr;
  unsigned char *end;
  struct timeval newT;
  struct timeval prevT;
};

void ttyBufRReset(m2k_t *ctx);
bool ttyBufRHasData(m2k_t *ctx);

int getTty1(m2k_t *ctx);

m2k_err_t ttyBufRead(m2k_t *ctx, st_sock *sock);


/* writing tty */

#define TTYBUFW_SIZE (2 * SOCKBUFR_SIZE)              /* this seems to be any number */
#define TTYBUFW_SIZE_A (TTYBUFW_SIZE + SOCKBUFR_SIZE) /* important */

struct st_ttyBufW
{
  uchar buf[TTYBUFW_SIZE_A];
  uchar *top;
  uchar *ptr;
  int stop;
};

void ttyBufWReset(m2k_t *ctx);
bool ttyBufWHasData(m2k_t *ctx);
bool ttyBufWReady(m2k_t *ctx);
#define putTtyStr(ctx, s) putTtyN((ctx), (s), sizeof(s) - 1)

m2k_err_t ttyBufWrite(m2k_t *ctx, st_sock *sock);
void putTty1(m2k_t *ctx, unsigned char c);
void putTtyN(m2k_t *ctx, const char *cp, int n);

// utils

/* malloc/calloc with logged failure. On OOM, logs the failing call
   site (via __func__/__LINE__ captured by the macro) and returns NULL.
   Caller still must null-check and propagate M2K_ERR_NOMEM — a library
   does not unilaterally exit. ctx may be NULL (logs to stderr). */
void *m2k_alloc_at(m2k_t *ctx, size_t size, const char *func, int line);
void *m2k_calloc_at(m2k_t *ctx, size_t n, size_t size,
                    const char *func, int line);

#define m2k_alloc(ctx, size) \
  m2k_alloc_at((ctx), (size), __func__, __LINE__)
#define m2k_calloc(ctx, n, size) \
  m2k_calloc_at((ctx), (n), (size), __func__, __LINE__)

// logging / verbose

void m2k_log(m2k_t *ctx, const char *fmt, ...);
void m2k_err_set(m2k_t *ctx, const char *fmt, ...);

#define VERB_MISC 1
#define VERB_TELOPT 2

void verboseOut(m2k_t *ctx, int mask, const char *format, ...);
void verbosePerror(m2k_t *ctx, int mask, const char *s);

// cmdlex

typedef enum
{
  CMDST_OK,
  CMDST_ERROR,
  CMDST_CONNECT,
  CMDST_NOCARRIER,
  CMDST_NOAT,
  CMDST_ATD,
  CMDST_ATO,
  CMDST_PTY_CLOSED
} Cmdstat;

Cmdstat cmdLex(m2k_t *ctx, const char *ptr, st_sock *sock);
void putTtyCmdstat(m2k_t *ctx, Cmdstat s);

int m2k_sockDial(m2k_t *ctx, st_sock *sock);
int m2k_sockDialStart(m2k_t *ctx, st_sock *sock);
int m2k_sockDialProgress(m2k_t *ctx, st_sock *sock);
void m2k_sockDialAbort(m2k_t *ctx, st_sock *sock);
int m2k_sockListen(m2k_t *ctx, const char *port);
int m2k_sockAccept(m2k_t *ctx, int server_fd);
