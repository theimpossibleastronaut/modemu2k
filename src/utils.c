/*
 * utils.c
 *      Modemu2k adds Telnet capability to a comm program.
 *      It can redirect Telnet I/O to a pty so that a comm program
 *      can handle the pty as a tty with a real modem.
 *
 * Copyright 2018-2021 Andy Alt <andy400-dev@yahoo.com>
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

#include <stdlib.h>
#include <stdio.h>

void *chk_alloc (void *ptr)
{
  if (ptr != NULL)
    return ptr;

  fprintf (stderr, "Error allocating memory\n");
  return NULL;
}
