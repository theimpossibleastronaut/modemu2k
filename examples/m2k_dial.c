/*
 * Compilable example for m2k_dial() — the synchronous dial entry point.
 * The actual dial happens only when run with an explicit host/port, so
 * the build's compile/link check stays side-effect-free.
 */
#include <stdio.h>
#include <unistd.h>
#include <modemu2k.h>

//! [dial]
/* ATS7 sets the dial timeout (seconds); AT%D1 disables non-TTY dial
   cancel so a stray stdin byte can't abort the dial. */
static int run_dial(m2k_t *ctx, const char *host, const char *port)
{
  m2k_atcmd(ctx, "ATS7=20");
  m2k_atcmd(ctx, "AT%D1");

  m2k_err_t err = m2k_dial(ctx, host, port);
  if (err != M2K_OK) {
    fprintf(stderr, "dial: %s\n", m2k_strerror(err));
    return 1;
  }
  sleep(1);
  m2k_hangup(ctx);
  return 0;
}
//! [dial]

int main(int argc, char **argv)
{
  m2k_t *ctx = m2k_new();
  if (!ctx) return 1;

  int rc = (argc > 2) ? run_dial(ctx, argv[1], argv[2]) : 0;

  m2k_free(ctx);
  return rc;
}
