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

    // should connect
  atcmdD ("140.82.113.3 80", ATDA_NUM, ATDP_NUM);
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

  /* IPv6 connection
   *
   * this test is disabled for now because my ISP apparently doesn't support
   * ipv6, therefore make check or distcheck will fail
   */

  /***************************************************
  atcmdD ("ipv6.google.com 80", ATDA_STR, ATDP_STR);
  assert (sockDial() == 0);
  assert (close (sock.fd) == 0);
  ***************************************************/

  return 0;
}
