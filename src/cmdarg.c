/*
 * cmdarg.c
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

#include <stdio.h> /*stderr */
#include <stdlib.h>
#include <unistd.h> /*isatty */
#include <getopt.h>
#include "cmdarg.h"      /*cmdarg */
#include "m2k_private.h" /*VERSION_... */
#include "config.h"

/* LIT(A) -> "10" */
#define LIT_(s) #s
#define LIT(s) LIT_(s)

void
showUsage(char *const argv[])
{
  /* Emit ANSI styling only on a TTY, and honor the NO_COLOR convention
     (https://no-color.org). Off => empty strings, leaving the layout
     identical to the previous plain-text help.
       sec = bold cyan   -> section labels (Usage:, Note:, Example:, ...)
       opt = bold yellow -> option short+long flag names
       ex  = bold green  -> concrete example commands the user might type
       r   = reset                                                       */
  int color = isatty(STDOUT_FILENO) && getenv("NO_COLOR") == NULL;
  const char *sec = color ? "\033[1;36m" : "";
  const char *opt = color ? "\033[1;33m" : "";
  const char *ex = color ? "\033[1;32m" : "";
  const char *r = color ? "\033[0m" : "";

  printf("%sUsage:%s %s [OPTION]...\n\n", sec, r, argv[0]);

  printf("  %s-c, --commprog=%s\"<comm_prog> <args>\"     invoke a comm program using [arguments]\n", opt, r);
  printf("  %s-d, --device=%s<pty_master>               talk through [pty_master]\n", opt, r);
  printf("  %s-e, --atstring=%s\"<ATxxx>\"                execute [ATxxx] commands at startup,\n"
         "                                          then read AT commands interactively from stdin\n",
         opt, r);
  printf("  %s-l, --listen=%s<port>                     listen for an incoming TCP connection on [port]\n", opt, r);
  printf("  %s-s, --show%s                              show which device will be used\n", opt, r);
  puts("");
  printf("  %s-h, --help%s                              display help\n", opt, r);
  printf("  %s-v, --version%s                           display version\n", opt, r);
  printf("  %s-w, --warranty%s                          display warranty\n", opt, r);
  puts("");
  printf("%sNote:%s The -c, -d, -l, and -s options are mutually exclusive; passing\n"
         "more than one exits with an error. With none of them given but -e\n"
         "present (e.g. 'modemu2k -e AT'), modemu2k reads AT commands from\n"
         "stdin/stdout. Invoked with no arguments at all, it prints this help.\n",
         sec, r);
  puts("");
  printf("%sExample:%s launch minicom and enable 8-bit binary mode (same invocation\n"
         "used by the m2k-minicom helper script):\n"
         "\n"
         "  %smodemu2k -e \"AT%%B0=1%%B1=1&W\" -c \"minicom -l -tansi -con -p %%s\"%s\n"
         "\n"
         "The arguments to -c/--commprog and -e/--atstring must be quoted, and\n"
         "the arguments inside -c must be those of the comm program, not modemu2k.\n",
         sec, r, ex, r);
  puts("");
  printf("%sDialing:%s separate host and port with a space, not a colon, e.g.\n"
         "  %satd\"bbs.example.org 2030%s\n",
         sec, r, ex, r);
  puts("");
  puts("\
The modemu2k project and support site is at\n\
<https://github.com/theimpossibleastronaut/modemu2k>\n");
}

static void
warranty(void)
{
  printf("\
THERE IS NO WARRANTY FOR THE PROGRAM, TO THE EXTENT PERMITTED BY\n\
APPLICABLE LAW.  EXCEPT WHEN OTHERWISE STATED IN WRITING THE COPYRIGHT\n\
HOLDERS AND/OR OTHER PARTIES PROVIDE THE PROGRAM \"AS IS\" WITHOUT WARRANTY\n\
OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO,\n\
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR\n\
PURPOSE.  THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE PROGRAM\n\
IS WITH YOU.  SHOULD THE PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF\n\
ALL NECESSARY SERVICING, REPAIR OR CORRECTION.\n");
}

static void
version(char *const argv[])
{
  printf("\
modemu2k %s\n\
Maintainer: Andy Alt\n\
This program comes with ABSOLUTELY NO WARRANTY; for details type '%s -w.'\n\
This is free software, and you are welcome to redistribute it\n\
under certain conditions; see <http://www.gnu.org/licenses/gpl.html>\n\
for details.\n",
         VERSION, argv[0]);
}

void
cmdargParse(const int argc, char *const argv[], struct st_cmdarg *x)
{
  const char *const short_options = "c:d:e:hl:svw";

  const struct option long_options[] = {
    {"commprog", 1, NULL, 'c'},
    {"device", 1, NULL, 'd'},
    {"atstring", 1, NULL, 'e'},
    {"help", 0, NULL, 'h'},
    {"listen", 1, NULL, 'l'},
    {"show", 0, NULL, 's'},
    {"version", 0, NULL, 'v'},
    {"warranty", 0, NULL, 'w'},
    {NULL, 0, NULL, 0}};

  int next_option = 0;
  x->ttymode = CA_STDINOUT;
  x->comm_program = NULL;
  x->atcmd = NULL;
  x->dev = NULL;
  x->listen_port = NULL;

  /* Track the first mode-setting option (-c/-d/-l/-s) seen so we can
     reject conflicting combinations explicitly instead of silently
     honoring the last one and surprising the user. */
  char mode_flag = 0;
#define SET_MODE(flag)                                       \
  do                                                         \
  {                                                          \
    if (mode_flag && mode_flag != (flag))                    \
    {                                                        \
      fprintf(stderr,                                        \
              "%s: -%c conflicts with -%c; "                 \
              "-c, -d, -l, and -s are mutually exclusive\n", \
              argv[0], (flag), mode_flag);                   \
      exit(2);                                               \
    }                                                        \
    mode_flag = (flag);                                      \
  } while (0)

  do
  {
    next_option = getopt_long(argc, argv, short_options, long_options, NULL);

    switch ((char) next_option)
    {
    case 'c': /* -c <comm-program args> */
      SET_MODE('c');
      x->ttymode = CA_COMM_PROGRAM;
      x->comm_program = optarg;
      break;
    case 'd': /* -d <pty_device> */
      SET_MODE('d');
      x->ttymode = CA_DEVGIVEN;
      x->dev = optarg;
      break;
    case 'e': /* -a <atcommands> */
      x->atcmd = optarg;
      break;
    case 'l': /* -l <port> */
      SET_MODE('l');
      x->ttymode = CA_LISTEN;
      x->listen_port = optarg;
      break;
    case 'h': /* -h */
      showUsage(argv);
      exit(0);
    case 's': /* -s */
      SET_MODE('s');
      x->ttymode = CA_SHOWDEV;
      break;
    case 'v':
      version(argv);
      exit(0);
    case 'w':
      warranty();
      exit(0);
    default:
      break;
    }
  } while (next_option != -1);

#undef SET_MODE
}
