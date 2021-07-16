#include "test.h"
#include "atcmd.h"
#include "sock.h"

static void test_connect (void)
{
  // should connect
  atcmdD ("github.com 80", ATDA_STR, ATDP_STR);
  assert (sockDial() == 0);
  assert (close (sock.fd) == 0);

  // should connect
  atcmdD ("140.82.113.3 80", ATDA_NUM, ATDP_NUM);
  assert (sockDial() == 0);
  assert (close (sock.fd) == 0);

  // connect by using service name (http)
  atcmdD ("140.82.113.3 http", ATDA_NUM, ATDP_STR);
  assert (sockDial() == 0);
  assert (close (sock.fd) == 0);

  // should not connect
  atcmdD ("github.com 23", ATDA_STR, ATDP_STR);
  assert (sockDial() == 1);

  // should not connect (without port option, defaults to 23
  atcmdD ("github.com", ATDA_STR, ATDP_STR);
  assert (sockDial() == 1);

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

  return;
}

int main (void)
{
  test_connect ();
  return 0;
}