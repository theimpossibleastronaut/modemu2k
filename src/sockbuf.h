
#include <stdbool.h>
#if 0
#include "defs.h"       /*->sockbuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/
#endif


/* reading socket */

extern struct st_sockBufR
{
  uchar buf[SOCKBUFR_SIZE];
  uchar *ptr;
  uchar *end;
} sockBufR;;


void
sockBufRReset(void);

int
getSock1(void);

void sockBufRead (void);


/* writing socket */

#define SOCKBUFW_SIZE (2 * TTYBUFR_SIZE)        /* this seems to be any number */
#define SOCKBUFW_SIZE_A (SOCKBUFW_SIZE + TTYBUFR_SIZE)  /* important */

extern struct st_sockBufW
{
  uchar buf[SOCKBUFW_SIZE_A];
  uchar *top;
  uchar *ptr;
  int stop;
} sockBufW;

void
sockBufWReset(void);

bool
sockBufWHasData(void);

bool
sockBufWReady(void);

void sockBufWrite (void);
void putSock1 (uchar c);
void putSockN (const uchar * cp, int n);
