/*
 * main.c
 *      Modemu2k adds Telnet capability to a comm program.
 *      It can redirect Telnet I/O to a pty so that a comm program
 *      can handle the pty as a tty with a real modem.
 *
 * Copyright 2018-2020 Andy Alt <arch_stanton5995@protonmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <modemu2k.h>
#include "cmdarg.h"
#include "config.h"

static void
stderr_log_fn(const char *msg, void *userdata)
{
  (void) userdata;
  fputs("modemu2k: ", stderr);
  fputs(msg, stderr);
}

int
main(int argc, char *const argv[])
{
#ifdef SOCKS
  SOCKSinit(argv[0]);
#endif

  if (argc == 1)
  {
    showUsage(argv);
    return 0;
  }

  m2k_t *ctx = m2k_new();
  if (ctx == NULL)
    return EXIT_FAILURE;

  struct st_cmdarg cmdarg;
  cmdargParse(argc, argv, &cmdarg);
  /* -v / --verbose describes how to run, not what to do. If no mode
     flag (-c/-d/-l/-s) and no -e were given, there's no operation
     to narrate — treat like a no-arg invocation and print usage. */
  if (cmdarg.ttymode == CA_STDINOUT && cmdarg.atcmd == NULL)
  {
    showUsage(argv);
    m2k_free(ctx);
    return 0;
  }
  if (cmdarg.verbose)
  {
    m2k_set_log_fn(ctx, stderr_log_fn, NULL);
    fputs("modemu2k: verbose logging enabled (MISC|TELOPT)\n", stderr);
  }
  puts(PACKAGE_STRING " " VERSION);
  puts("Enter 'at%q' (or Ctrl-C twice) to quit\n");

  switch (cmdarg.ttymode)
  {
  case CA_SHOWDEV:
  {
    const char *slave;
    if (m2k_setup_pty(ctx, &slave) != M2K_OK)
    {
      m2k_free(ctx);
      return EXIT_FAILURE;
    }
    puts(slave);
    m2k_free(ctx);
    return 0;
  }
  case CA_COMM_PROGRAM:
    if (m2k_setup_comm_program(ctx, cmdarg.comm_program) != M2K_OK)
    {
      m2k_free(ctx);
      return EXIT_FAILURE;
    }
    break;
  case CA_STDINOUT:
    m2k_setup_stdin(ctx);
    break;
  case CA_DEVGIVEN:
    if (m2k_setup_dev(ctx, cmdarg.dev) != M2K_OK)
    {
      m2k_free(ctx);
      return EXIT_FAILURE;
    }
    break;
  case CA_LISTEN:
    if (m2k_setup_listen(ctx, cmdarg.listen_port) != M2K_OK || m2k_listen_accept(ctx) != M2K_OK)
    {
      m2k_free(ctx);
      return EXIT_FAILURE;
    }
    break;
  }

  if (cmdarg.atcmd != NULL && m2k_atcmd(ctx, cmdarg.atcmd) != M2K_OK)
    fprintf(stderr, "Error in initialization commands.\r\n");

  /* Open the AT%V verbose mask AFTER -e processing, because the
     -e command may include ATZ (which resets atcmd.pv to the NV
     default of 0 via the atcmdNV copy in atcmdZ) and that would
     silence every verboseOut() site for the rest of the run. */
  if (cmdarg.verbose && m2k_atcmd(ctx, "AT%V3") != M2K_OK)
    fputs("modemu2k: failed to enable verbose mask\n", stderr);

  m2k_run(ctx);
  m2k_free(ctx);
  return 0;
}
