/*
 * Compilable example for m2k_setup_commx(). The %s in the command string
 * is replaced by modemu2k with the PTY slave device path before sh -c is
 * exec'd, so the comm program ends up talking to the modem-emulator PTY.
 */
#include <stdio.h>
#include <modemu2k.h>

int main(void)
{
  m2k_t *ctx = m2k_new();
  if (!ctx) return 1;

  //! [setup_commx]
  /* Fork minicom on the PTY slave; %s is substituted with the slave path. */
  m2k_err_t err = m2k_setup_commx(ctx, "minicom -l -tansi -con -p %s");
  if (err != M2K_OK) {
    fprintf(stderr, "setup_commx failed: %s\n", m2k_strerror(err));
    m2k_free(ctx);
    return 1;
  }
  m2k_run(ctx);   /* blocks until the comm program exits */
  //! [setup_commx]

  m2k_free(ctx);
  return 0;
}
