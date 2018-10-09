/*
 * cmdarg.c
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

#include <stdio.h>              /*stderr */
#include <stdlib.h>
#include "cmdarg.h"             /*cmdarg */
#include "defs.h"               /*VERSION_... */

static const char *argv0;

/* LIT(A) -> "10" */
#define LIT_(s) #s
#define LIT(s) LIT_(s)

static void
showUsage (void)
{
  printf ("\
Usage: %s [OPTION]...\n\
\n\
  -c comm_prog            invoke [comm_prog]\n\
  -d pty_master           talk through [pty_master]\n\
  -e ATxxx]               issue [ATxxx] commands at startup\n\
  -s                      show which device will be used\n\
  -                       talks through the standard input/output. (Default)\n\
\n\
  -h                      display help\n\
  -v                      display version\n\
\n\
Note:  -c,  -d,  -s  and - options are exclusive of each other.  If two or\n\
more of the options are specified, only the last one is effective.\n\
\n", argv0);
}

void
cmdargParse (const char **argv)
{
  argv0 = argv[0];
  for (argv++; *argv != NULL; argv++)
  {
    if ((*argv)[0] == '-')
    {
      switch ((*argv)[1])
      {
      case 'c':                /* -c <commx args> */
        cmdarg.ttymode = CA_COMMX;
        cmdarg.commx = *++argv;
        if (cmdarg.commx == NULL)
          goto FEWARG;
        break;
      case 'd':                /* -d <pty_device> */
        cmdarg.ttymode = CA_DEVGIVEN;
        cmdarg.dev = *++argv;
        if (cmdarg.dev == NULL)
          goto FEWARG;
        break;
      case 'e':                /* -e <atcommands> */
        cmdarg.atcmd = *++argv;
        if (cmdarg.atcmd == NULL)
          goto FEWARG;
        break;
      case 'h':                /* -h */
        showUsage ();
        exit (0);
      case 's':                /* -s */
        cmdarg.ttymode = CA_SHOWDEV;
        break;
      case 'v':
        printf (PACKAGE_NAME " version " VERSION "\n");
        exit (0);
        break;
      case '\0':               /* - */
        cmdarg.ttymode = CA_STDINOUT;
        break;
      default:
        fprintf (stderr, "Unknown option -%c.\n", (*argv)[1]);
        showUsage ();
        exit (1);
      }
    }
    else
    {
      fprintf (stderr, "Error in command line.\n");
      showUsage ();
      exit (1);
    }
  }
  return;

FEWARG:
  fprintf (stderr, "'-%c' requires an argument.\n", (*--argv)[1]);
  exit (1);
}
