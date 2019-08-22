#include <assert.h>
#include "defs.h"
#include "sock.h"
#include "atcmd.h"

/* Including the 'c' file here, instead of linking to the compiled
 * object, because getNumArg() is declared statically */
// #include "atcmd.c"

int main (void)
{
  // should connect
  atcmdD ("github.com 80", ATDA_STR, ATDP_STR);
  assert (sockDial() == 0);
  assert (close (sock.fd) == 0);

  // should not connect
  atcmdD ("github.com 23", ATDA_STR, ATDP_STR);
  assert (sockDial() == 1);
  assert (close (sock.fd) == 0);

  // should not connect (without port option, defaults to 23
  atcmdD ("github.com", ATDA_STR, ATDP_STR);
  assert (sockDial() == 1);
  assert (close (sock.fd) == 0);
  return 0;
}
