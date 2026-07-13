/*
 * Compilable example for m2k_setup_answer(). Binding is kept inside a
 * guarded function so the build check verifies signatures without
 * opening a port during ninja.
 */
#include <stdio.h>
#include <modemu2k.h>

//! [setup_answer]
/* Bind TCP port 2030 as the "phone line": while a caller waits, command
   mode emits RING; ATA (or ATS0=1 auto-answer) accepts the call. Pair
   with -a/--answer on the CLI for the same flow. */
static int run_answerer(m2k_t *ctx, const char *port)
{
  m2k_err_t err = m2k_setup_answer(ctx, port);
  if (err != M2K_OK) {
    fprintf(stderr, "setup_answer: %s\n", m2k_strerror(err));
    return 1;
  }
  m2k_run(ctx);   /* callers now RING; answer with ATA or S0 */
  return 0;
}
//! [setup_answer]

int main(int argc, char **argv)
{
  m2k_t *ctx = m2k_new();
  if (!ctx) return 1;

  /* Only actually bind a socket when explicitly invoked with a port,
     so the build's compile/link check stays side-effect-free. */
  int rc = (argc > 1) ? run_answerer(ctx, argv[1]) : 0;

  m2k_free(ctx);
  return rc;
}
