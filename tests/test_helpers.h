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

/* Fork a child that connects once to an already-bound loopback listener
   on the given family (AF_INET/AF_INET6) and port, staying connected
   briefly so the parent's accept() returns. The child is reaped at
   process exit like start_loopback_listener's.

   @return the child pid (> 0) on success, -1 on fork failure. */
int start_connector(int family, int port);

/* Shared step-loop and loopback-socket helpers for step-API tests.
   test_step polls (timeout capped at 100 ms so tests stay snappy) and
   runs one m2k_step; test_step_drain additionally appends any app-IO
   output to buf (NUL-terminated, len updated). */
void test_step(m2k_t *ctx);
void test_step_drain(m2k_t *ctx, char *buf, size_t cap, size_t *len);

/* Connect a blocking IPv4 client to 127.0.0.1:port. Asserts on failure. */
int test_connect_client(int port);

/* Like test_connect_client, but the peer has a small receive buffer and
   the caller is expected not to read from it, so the connection
   saturates after a few KB instead of the hundreds the kernel would
   otherwise absorb. Lets a test put the socket write buffer under
   back-pressure in bounded time. Asserts on failure. */
int test_connect_slow_peer(int port);

/* One m2k_step with a non-blocking poll. test_step waits up to 100 ms,
   which dominates any loop that runs while nothing is readable -- a
   stalled socket, say. Use this where the loop drives the state itself
   rather than waiting on an event. */
void test_step_nowait(m2k_t *ctx);

/* Kernel-assigned local port of a bound socket fd (v4 or v6). */
int test_local_port(int fd);
