#include "test.h"

#include <unistd.h>
#include "modemu2k.h"

static void test_connect (void)
{
  st_sock sock;
  atcmd.s[7] = 20;  /* S7: connection timeout in seconds */
  atcmd.pd = 1;    /* disable dial canceling; stdin is not a tty in tests */

  // should connect
  telOptReset ();
  m2k_atcmdD ("github.com 80", ATDA_STR, ATDP_NUM);
  assert (m2k_sockDial(&sock) == 0);
  sleep (2);
  assert (sockShutdown (&sock) == 0);

  // should connect
  telOptReset ();
  m2k_atcmdD ("140.82.113.3 80", ATDA_NUM, ATDP_NUM);
  assert (m2k_sockDial(&sock) == 0);
  sleep (2);
  assert (sockShutdown (&sock) == 0);

  return;
}

int main (void)
{
  test_connect ();
  return 0;
}
