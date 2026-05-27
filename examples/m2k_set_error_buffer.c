/*
 * Compilable example for m2k_set_error_buffer().
 * Marker tags below are extracted into modemu2k.h via @snippet.
 */
#include <stdio.h>
#include <modemu2k.h>

int main(void)
{
  m2k_t *ctx = m2k_new();
  if (!ctx) return 1;

  //! [set_error_buffer]
  char errbuf[M2K_ERROR_BUFFER_SIZE];
  m2k_set_error_buffer(ctx, errbuf, sizeof(errbuf));

  if (m2k_atcmd(ctx, "AT~not~a~real~command") != M2K_OK)
    fprintf(stderr, "modemu2k: %s\n", errbuf);
  //! [set_error_buffer]

  m2k_free(ctx);
  return 0;
}
