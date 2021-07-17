/*
 * sock.h
 *
 * Copyright 2018-2020 Andy Alt <andy400-dev@yahoo.com>
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


#ifdef TERMNET
#include <termnet.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#ifdef USE_FILIO_H
#include <sys/filio.h>
#else
#include <sys/ioctl.h>
#endif
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

struct st_sock
{
  int fd;
  int alive;
  struct addrinfo *rp;
};

extern struct st_sock sock;

#define sockIsAlive() (sock.alive)

int sockClose (void);
int sockShutdown (void);
int sockDial (void);
