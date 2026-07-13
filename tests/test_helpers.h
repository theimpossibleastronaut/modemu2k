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

/* Shared step-loop and loopback-socket helpers for step-API tests.
   test_step polls (timeout capped at 100 ms so tests stay snappy) and
   runs one m2k_step; test_step_drain additionally appends any app-IO
   output to buf (NUL-terminated, len updated). */
void test_step(m2k_t *ctx);
void test_step_drain(m2k_t *ctx, char *buf, size_t cap, size_t *len);

/* Connect a blocking IPv4 client to 127.0.0.1:port. Asserts on failure. */
int test_connect_client(int port);

/* Kernel-assigned local port of a bound socket fd (v4 or v6). */
int test_local_port(int fd);
