/*
 * defs.h
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

#include "config.h"

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
