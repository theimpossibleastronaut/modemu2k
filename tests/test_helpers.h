#pragma once

/* Bind a loopback TCP listener on an ephemeral port and fork an
   accept-and-close child so dial-side tests have a reachable peer
   without depending on the public internet.

   @param family    AF_INET or AF_INET6.
   @param port_out  Receives the OS-assigned port on success.
   @return 0 on success, -1 on failure (socket / bind / listen / fork
           failure, or an unsupported family). The forked child is
           reaped at process exit via an atexit() handler installed on
           first use. */
int start_loopback_listener(int family, int *port_out);
