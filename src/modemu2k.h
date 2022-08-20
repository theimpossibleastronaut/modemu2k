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

/**
 * @file
 * @brief core modemu2k API
 */

#include <stdio.h>              /*stderr,(sscanf,sprintf) */
#include <string.h>             /*(strncpy) */
#include <stdbool.h>
#include <sys/types.h>
#include <sys/time.h>

#ifdef TERMNET
#include <termnet.h>
#endif

#ifdef USE_FILIO_H
#include <sys/filio.h>
#else
#include <sys/ioctl.h>
#endif

#include "cmdarg.h"

/* TODO: the API shouldn't be localized and really shouldn't contain any strings;
 * but rather the functions should return values to the application can print
 * any messages to stdout or stderr
 */
#define ENABLE_NLS 1
#include <locale.h>
#include "gettext.h"
#define _(String) gettext (String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)

/* socket read buffer size */
#define SOCKBUFR_SIZE	1024

/* tty read buffer size */
#define TTYBUFR_SIZE	1024

/* line (!char) mode line-length */
#define LINEBUF_SIZE	256

/* command mode line-length (w/o null) */
#define CMDBUF_MAX	255

typedef unsigned char uchar;

#if defined(__GLIBC__) || defined(SVR4)
#define HAVE_GRANTPT
#endif

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

//extern st_sock sock;

void sockInit(struct st_sock *sock);
int sockClose(st_sock * sock);
int sockShutdown(st_sock * sock);


/**
 * Initiate connection
 *
 * @param sock
 * @returns 0 if completed successfully, 1 otherwise
 * @see m2k_atcmdD()
 */
int m2k_sockDial(st_sock * sock);

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

#define ADDR_MAX 63
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

extern Atcmd atcmd;
extern Atcmd atcmdNV;

#define CHAR_ESC (atcmd.s[2])
#define CHAR_CR (atcmd.s[3])
#define CHAR_LF (atcmd.s[4])
#define CHAR_BS (atcmd.s[5])

void atcmdInit(struct st_cmdarg *cmdarg, st_sock * sock);


/**
 * The host/ip and port number to be dialed
 *
 * @param s the host or IP
 * @param at
 * @param pt
 * @returns void
 * @see m2k_sockDial()
 *
 * Example:
 * @code atcmdD("github.com 80", ATDA_STR, ATDP_NUM); @endcode
 */
void m2k_atcmdD(const char *s, AtdAType at, AtdPType pt);


int atcmdFake(const char *s, const char *vals);
int atcmdH(const char *s, st_sock * sock);
int atcmdI(const char *s);
int atcmdSQuery(const char *s);
int atcmdSSet(const char *s);
void atcmdZ(st_sock * sock);
void atcmdAW(void);
int atcmdPB(const char *s);
int atcmdPD(const char *s);
int atcmdPL(const char *s);
void atcmdPQ(st_sock * sock);
int atcmdPR(const char *s);
int atcmdPT(const char *s);
int atcmdPTSet(const char *s);
int atcmdPV(const char *s);

// commx
#ifdef HAVE_GRANTPT
void commxForkExec(const char *cmd, char *ptyslave);
#else
void commxForkExec(const char *cmd, char c10, char c01);
#endif

// sockbuf


/* reading socket */

extern struct st_sockBufR
{
  uchar buf[SOCKBUFR_SIZE];
  uchar *ptr;
  uchar *end;
} sockBufR;


void sockBufRReset(void);

int getSock1(void);

void sockBufRead(st_sock * sock);


/* writing socket */

#define SOCKBUFW_SIZE (2 * TTYBUFR_SIZE)        /* this seems to be any number */
#define SOCKBUFW_SIZE_A (SOCKBUFW_SIZE + TTYBUFR_SIZE)  /* important */

extern struct st_sockBufW
{
  uchar buf[SOCKBUFW_SIZE_A];
  uchar *top;
  uchar *ptr;
  int stop;
} sockBufW;

void sockBufWReset(void);

bool sockBufWHasData(void);

bool sockBufWReady(void);

void sockBufWrite(st_sock * sock);
void putSock1(uchar c);
void putSockN(const uchar * cp, int n);

// stty

void setTty(void);

// telopt

/* requirements for a telnet option */
typedef enum
{
  TOR_MUSTNOT,                  /* disable the opt or disconnect */
  TOR_BETTERNOT,                /* disable the opt or enable unwillingly */
  TOR_NEUTRAL,                  /* modemu doesn't initiate any action for the opt.
                                   {en,dis}able the opt as the peer requests. */
  TOR_BETTER,                   /* enable the opt or disable unwillingly */
  TOR_MUST                      /* enable the opt or disconnect */
} TelOptReq;

typedef struct
{
  TelOptReq req;                /* requirement for the opt */
  int state;                    /* current state (enabled:1 or disabled:0) */
  int pending;                  /* state is pending (requested but no reply yet) */
} TelOptState;

typedef struct
{
  int opt;                      /* an telnet option. TELOPT_XXX */
  TelOptState local;            /* local state of the option */
  TelOptState remote;           /* remote state of the option */
} TelOptStates;

extern struct st_telOpt
{
  int binsend;                  /* local binary opt is enabled */
  int binrecv;                  /* remote binary opt is enabled */
  int sgasend;                  /* local SGA opt is enabled (char-at-a-time mode) */
  int sentReqs;                 /* have sent option requests to the peer
                                   or skip sending them */
  TelOptStates **stTab;         /* = stTab[] in telopt.c */
} telOpt;

#define putOptCmd(s,c) { putSock1(IAC); putSock1(s); putSock1(c); }

void telOptReset(void);
void telOptInit(void);
void telOptPrintCmd(const char *str, int cmd);
void telOptSendReqs(void);
int telOptHandle(int cmd, int opt);
int telOptSBHandle(int opt);

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

extern struct st_tty tty;


/* reading tty */

struct st_ttyBufR
{
  unsigned char buf[TTYBUFR_SIZE];
  unsigned char *ptr;
  unsigned char *end;
  struct timeval newT;
  struct timeval prevT;
};

extern struct st_ttyBufR ttyBufR;

void ttyBufRReset(void);

int getTty1(void);

void ttyBufRead(st_sock * sock);


/* writing tty */

#define TTYBUFW_SIZE (2 * SOCKBUFR_SIZE)        /* this seems to be any number */
#define TTYBUFW_SIZE_A (TTYBUFW_SIZE + SOCKBUFR_SIZE)   /* important */

struct st_ttyBufW
{
  uchar buf[TTYBUFW_SIZE_A];
  uchar *top;
  uchar *ptr;
  int stop;
};

extern struct st_ttyBufW ttyBufW;

#define ttyBufWReset() { \
    ttyBufW.ptr = ttyBufW.top = ttyBufW.buf; \
    ttyBufW.stop = 0; \
}
#define ttyBufWHasData() (ttyBufW.ptr > ttyBufW.buf)
#define ttyBufWReady() (!ttyBufW.stop)
#define putTtyStr(s) putTtyN(s, sizeof(s)-1)

void ttyBufWrite(st_sock * sock);
void putTty1(unsigned char c);
void putTtyN(const char *cp, int n);

// utils

void *chk_alloc(void *ptr);

// verbose

#define VERB_MISC 1
#define VERB_TELOPT 2

void verboseOut(int mask, const char *format, ...);
void verbosePerror(int mask, const char *s);

// cmdlex

typedef enum
{
  CMDST_OK,
  CMDST_ERROR,
  CMDST_CONNECT,
  CMDST_NOCARRIER,
  CMDST_NOAT,
  CMDST_ATD,
  CMDST_ATO
} Cmdstat;

Cmdstat cmdLex(const char *ptr, st_sock * sock);
