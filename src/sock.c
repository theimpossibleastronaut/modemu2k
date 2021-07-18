/*
 *
 * Copyright 2018-2019 Andy Alt <andy400-dev@yahoo.com>
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

#include "modemu2k.h"

struct st_sock sock;

static void
sockInit (struct st_sock *sock)
{
  sock->fd = 0;
  sock->alive = 0;
  sock->rp = NULL;
}

int
sockClose (void)
{
  if (sock.fd <= 0)
    return 0;

  int r = close (sock.fd);
  if (r != 0)
    perror ("sockClose()");

  sock.fd = sock.alive = 0;
  return r;
}

int
sockShutdown (void)
{
  if (sock.fd <= 0)
    return 0;
  shutdown (sock.fd, 2);
  return sockClose ();
}

#define DEFAULT_PORT 23


int m2k_sockDial (void)
{
  struct addrinfo hints;
  memset (&hints, 0, sizeof (struct addrinfo));

  sockInit (&sock);
  struct addrinfo *result = NULL;

  int s;

  hints.ai_family = AF_UNSPEC;  /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;        /* Any protocol */

  char out_port[PORT_MAX + 1];

  if (atcmd.d.port.type == ATDP_NUL)
    snprintf (out_port, sizeof out_port, "%d", DEFAULT_PORT);
  else
  {
    strcpy (out_port, atcmd.d.port.str);
    telOpt.sentReqs = 1;        /* skip sending option requests */
  }

  s = getaddrinfo (atcmd.d.addr.str, out_port, &hints, &result);
  if (s != 0)
  {
    fprintf (stderr, _("Host address lookup failed: %s\n"), gai_strerror (s));
    return 1;
  }

  for (sock.rp = result; sock.rp != NULL; sock.rp = sock.rp->ai_next)
  {
    sock.fd = socket (sock.rp->ai_family, sock.rp->ai_socktype, sock.rp->ai_protocol);
    if (sock.fd != -1)
      break;
  }

  if (sock.fd == -1)
  {                             /* No address succeeded */
    perror ("socket");
    if (result)
      freeaddrinfo (result);
    return 1;
  }

  int tmp = 1;
  if (setsockopt (sock.fd, SOL_SOCKET, SO_OOBINLINE, &tmp, sizeof (tmp)) < 0)
  {
    sockClose ();
    perror ("setsockopt()");
    return 1;
  }

#ifdef NO_DIAL_CANCELING
  /* blocking connect. */
  if (connect (sock.fd, sock.rp->ai_addr, sock.rp->ai_addrlen) != 0)
  {
    if (result)
      freeaddrinfo (result);
    sockShutdown ();
    perror ("connect()");
    return 1;
  }

  sock.alive = 1;
  return 0;
#else /*!ifdef NO_DIAL_CANCELING */
  {
    /* nonblocking connect. */
    /* SOCKS version 4.2 or higher is required for SOCKS support */
    fd_set rfds, wfds;
    struct timeval tv;
    struct timeval to, t;

    tmp = 1;
    ioctl (sock.fd, FIONBIO, &tmp);     /* non-blocking i/o */

    /* but Term's connect() blocks here... */
    if (connect (sock.fd, sock.rp->ai_addr, sock.rp->ai_addrlen) < 0
        && errno != EINPROGRESS)
    {
      perror ("connect()");
      if (result)
        freeaddrinfo (result);
      sockShutdown ();
      return 1;
    }

    FD_ZERO (&rfds);
    FD_ZERO (&wfds);
    tv.tv_sec = 0;

    timevalSet10ms (&t, atcmd.s[7] * 100);      /* S7 sec */
    gettimeofday (&to, NULL);
    timevalAdd (&to, &t);       /* S7 sec after */

    /* SOCKS Rselect() first checks if connected, then select(). */
    /* so, select() with large timeval is inappropriate */
    do
    {
      if (!atcmd.pd)
        FD_SET (tty.rfd, &rfds);
      FD_SET (sock.fd, &wfds);
      tv.tv_usec = 200 * 1000;  /* 0.2sec period */

    RETRY:
      if (select (sock.fd + 1, &rfds, &wfds, NULL, &tv) < 0)
      {
        if (errno == EINTR)
          goto RETRY;
        perror ("select()");
        sockShutdown ();
        return 1;
      }
#if 0
      verboseOut (VERB_MISC, "tty=%d, sock=%d\r\n",
                  FD_ISSET (tty.rfd, &rfds), FD_ISSET (sock.fd, &wfds));
#endif
      if (FD_ISSET (tty.rfd, &rfds))
      {
        sockShutdown ();
        verboseOut (VERB_MISC,
                    _("Connecting attempt canceled by user input.\r\n"));
        return 1;
      }
      /* check if really connected or not */

      /*if (FD_ISSET(sock.fd, &wfds)
         && getpeername(sock.fd, (struct sockaddr *)&sa, &tmp) == 0) */

      /* SOCKS requires this check method (ref: What_SOCKS_expects) */
      if (FD_ISSET (sock.fd, &wfds))
      {
        if (connect (sock.fd, sock.rp->ai_addr, sock.rp->ai_addrlen) < 0
            && errno != EISCONN)
        {
          perror ("connect()-2");
          if (result)
            freeaddrinfo (result);
          sockShutdown ();
          return 1;
        }

        tmp = 0;
        ioctl (sock.fd, FIONBIO, &tmp); /* blocking i/o */
        sock.alive = 1;
        return 0;
      }

      gettimeofday (&t, NULL);
    }
    while (timevalCmp (&t, &to) < 0);

    sockShutdown ();
    verboseOut (VERB_MISC, _("Connection attempt timed out.\r\n"));
    return 1;                   /* timeout */
  }
#endif /*ifdef NO_DIAL_CANCELING */
}
