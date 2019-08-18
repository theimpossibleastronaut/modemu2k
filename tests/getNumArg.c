
#include <assert.h>

/* Including the 'c' file here, instead of linking to the compiled
 * object, because getNumArg() is declared statically */
#include "atcmd.c"

int main (void)
{
  assert (getNumArg ("foo ba3r") == 3);
  assert (getNumArg ("foo bar") == 0);
  return 0;
}
