/*
 * Compilable example for m2k_setup_listen() + m2k_listen_accept(). The
 * accept step blocks waiting for a client, so we keep the call inside a
 * guarded function so the build check verifies signatures without binding
 * to a port or waiting on accept during ninja.
 */
#include <stdio.h>
#include <modemu2k.h>

//! [setup_listen]
/* Bind TCP port 5000, accept one connection, then drive the modem state
   machine over the accepted socket. Pair with -l/--listen on the CLI for
   the same flow. */
static int run_listener(m2k_t *ctx, const char *port)
{
  m2k_err_t err = m2k_setup_listen(ctx, port);
  if (err != M2K_OK) {
    fprintf(stderr, "setup_listen: %s\n", m2k_strerror(err));
    return 1;
  }
  err = m2k_listen_accept(ctx);   /* blocks until a client connects */
  if (err != M2K_OK) {
    fprintf(stderr, "listen_accept: %s\n", m2k_strerror(err));
    return 1;
  }
  m2k_run(ctx);
  return 0;
}
//! [setup_listen]

int main(int argc, char **argv)
{
  m2k_t *ctx = m2k_new();
  if (!ctx) return 1;

  /* Only actually bind a socket when explicitly invoked with a port,
     so the build's compile/link check stays side-effect-free. */
  int rc = (argc > 1) ? run_listener(ctx, argv[1]) : 0;

  m2k_free(ctx);
  return rc;
}
