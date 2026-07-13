/* Compile-checked example for the m2k_describe_state() docblock. */
#include <stdio.h>
#include <modemu2k.h>

int
main(void)
{
  m2k_t *ctx = m2k_new();
  if (ctx == NULL)
    return 1;
  //! [describe_state]
  char state[512];
  m2k_describe_state(ctx, state, sizeof state);
  fprintf(stderr, "modem state:\n%s\n", state);   /* paste into bug reports */
  //! [describe_state]
  m2k_free(ctx);
  return 0;
}
