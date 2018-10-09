/*
 * atcmd.h
 *
 * Copyright 2018 Andy Alt <andy400-dev@yahoo.com>
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

#if 0
#include "defs.h"       /*->atcmd.h (uchar)*/
#endif

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
  uchar s[SREG_MAX + 1];
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

Atcmd atcmd, atcmdNV;

#define CHAR_ESC (atcmd.s[2])
#define CHAR_CR (atcmd.s[3])
#define CHAR_LF (atcmd.s[4])
#define CHAR_BS (atcmd.s[5])

void atcmdInit (void);
void atcmdD (const char *s, AtdAType at, AtdPType pt);
int atcmdFake (const char *s, const char *vals);
int atcmdH (const char *s);
int atcmdI (const char *s);
int atcmdSQuery (const char *s);
int atcmdSSet (const char *s);
void atcmdZ (void);
void atcmdAW (void);
int atcmdPB (const char *s);
int atcmdPD (const char *s);
int atcmdPL (const char *s);
void atcmdPQ (void);
int atcmdPR (const char *s);
int atcmdPT (const char *s);
int atcmdPTSet (const char *s);
int atcmdPV (const char *s);
