/*
 * Compilable example for m2k_set_log_fn().
 * The //! [set_log_fn-*] markers delineate regions that doxygen extracts
 * via @snippet in modemu2k.h — building this file as part of the regular
 * build guarantees the signatures in the docs match real code.
 */
#include <stdio.h>
#include <modemu2k.h>

//! [set_log_fn-callback]
/* Userdata is passed through unchanged to the callback. */
static void my_logger(const char *msg, void *userdata)
{
  FILE *log = userdata;
  fputs(msg, log);
}
//! [set_log_fn-callback]

int main(void)
{
  m2k_t *ctx = m2k_new();
  if (!ctx) return 1;

  //! [set_log_fn-install]
  m2k_set_log_fn(ctx, my_logger, stderr);
  //! [set_log_fn-install]

  m2k_free(ctx);
  return 0;
}
