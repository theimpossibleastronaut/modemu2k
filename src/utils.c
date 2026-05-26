/*
 *      Modemu2k adds Telnet capability to a comm program.
 *      It can redirect Telnet I/O to a pty so that a comm program
 *      can handle the pty as a tty with a real modem.
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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "m2k_private.h"
#include "m2k_ctx.h"

void *
m2k_alloc_at(m2k_t *ctx, size_t size, const char *func, int line)
{
  void *p = malloc(size);
  if (!p)
  {
    int err = errno;
    m2k_log(ctx, "malloc(%zu) failed at %s:%d: %s\n",
            size, func, line, strerror(err));
  }
  return p;
}

void *
m2k_calloc_at(m2k_t *ctx, size_t n, size_t size,
              const char *func, int line)
{
  void *p = calloc(n, size);
  if (!p)
  {
    int err = errno;
    m2k_log(ctx, "calloc(%zu, %zu) failed at %s:%d: %s\n",
            n, size, func, line, strerror(err));
  }
  return p;
}
