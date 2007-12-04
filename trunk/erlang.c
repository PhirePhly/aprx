/* **************************************************************** *
 *                                                                  *
 *  APRSG-NG -- 2nd generation receive-only APRS-i-gate with        *
 *              minimal requirement of esoteric facilities or       *
 *              libraries of any kind beyond UNIX system libc.      *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */


#include "aprsg.h"


/* The erlang module accounts data reception per 1m/10m/60m
   intervals, and reports them on verbout.. */

#define MAXTTYS 16

time_t	erlang_time_end_1min;
time_t	erlang_time_end_10min;
time_t	erlang_time_end_60min;


struct erlangline {
	const void *refp;
	char name[32];

	long	rxbytes;	/* SNMPish counters					*/
	long	txbytes;

	int     erlang_capa;	/* bytes, 1 minute					*/
	int     erlang_1min_rx;	/* received bytes, 1 minute period			*/
	int     erlang_10min_rx; /* received bytes, 10 minute period			*/
	int     erlang_60min_rx; /* received bytes, 60 minute period			*/
	int     erlang_1min_tx;  /* sent bytes, 1 minute period				*/
	int     erlang_10min_tx; /* sent bytes, 10 minute period			*/
	int     erlang_60min_tx; /* sent bytes, 60 minute period			*/
};

static struct erlangline *ErlangLines;
static int ErlangLineCount;


/*
 *  erlang_add()
 */
void erlang_add(const void *refp, const char *portname, int rxbytes, int txbytes)
{
	int i;
	struct erlangline *E = NULL;

	/* Allocate a new ErlangLines[] entry for this object, if no existing is found.. */

	if (ErlangLines) {
	  for (i=0; i < ErlangLineCount; ++i) {
	    if (refp && (refp != ErlangLines[i].refp))
	      continue; /* Was not this.. */
	    if (!refp && (strcmp(portname, ErlangLines[i].name) != 0))
	      continue; /* Was not this.. */
	    /* HOO-RAY!  It is this one! */
	    E = & ErlangLines[i];
	    break;
	  }
	}

	if (!E) {
	  /* Allocate a new one */
	  ++ErlangLineCount;
	  ErlangLines = realloc( ErlangLines,
				 ErlangLineCount * sizeof(*E) );

	  if (!ErlangLines) return; /* D'uh! */

	  E = & ErlangLines[ ErlangLineCount-1 ];
	  memset(E, 0, sizeof(*E));
	  E->refp = refp;
	  strncpy(E->name, portname, sizeof(E->name)-1);
	  E->name[sizeof(E->name)-1] = 0;
	}

	if (rxbytes) {
	  E->rxbytes         += rxbytes;
	  E->erlang_1min_rx  += rxbytes;
	  E->erlang_10min_rx += rxbytes;
	  E->erlang_60min_rx += rxbytes;
	}

	if (txbytes) {
	  E->txbytes         += txbytes;
	  E->erlang_1min_tx  += txbytes;
	  E->erlang_10min_tx += txbytes;
	  E->erlang_60min_tx += txbytes;
	}
}


/*
 *  erlang_time_end() - process erlang measurement intercal time end event
 */
static void erlang_time_end(void)
{
	int i;

	if (now >= erlang_time_end_1min) {
	  erlang_time_end_1min += 60;
#if 0
	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = & ErlangLines[i];
	    if (E->name[0] == 0) continue; /* No name, no look... */
	    if (verbout || erlangout) {
	      printf("%ld\tERLANG1 %s Raw Bytes Rx %d  Tx %d\n",now,E->name,E->erlang_1min_rx,E->erlang_1min_tx);
	    }
	    E->erlang_1min_rx = 0;
	    E->erlang_1min_tx = 0;
	  }
#endif
	}
	if (now >= erlang_time_end_10min) {
	  erlang_time_end_10min += 10*60;

	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = & ErlangLines[i];
	    if (E->name[0] == 0) continue; /* No name, no look... */
	    if (verbout || erlangout) {
	      printf("%ld\tERLANG10 %s Raw Bytes Rx %d  Tx %d\n",now,E->name,E->erlang_10min_rx,E->erlang_10min_tx);
	    }
	    E->erlang_10min_rx = 0;
	    E->erlang_10min_tx = 0;
	  }

	}
	if (now >= erlang_time_end_60min) {
	  erlang_time_end_60min += 60*60;

	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = & ErlangLines[i];
	    if (E->name[0] == 0) continue; /* No name, no look... */
	    if (verbout || erlangout) {
	      printf("%ld\tERLANG60 %s Raw Bytes Rx %d  Tx %d\n",now,E->name,E->erlang_60min_rx,E->erlang_60min_tx);
	    }
	    E->erlang_60min_rx = 0;
	    E->erlang_60min_tx = 0;
	  }
	}
}

int erlang_prepoll(int nfds, struct pollfd **fdsp, time_t *tout)
{

	if (*tout > erlang_time_end_1min)
	  *tout = erlang_time_end_1min;
	if (*tout > erlang_time_end_10min)
	  *tout = erlang_time_end_10min;
	if (*tout > erlang_time_end_60min)
	  *tout = erlang_time_end_60min;

	return 0;
}

int erlang_postpoll(int nfds, struct pollfd *fds)
{
	erlang_time_end();

	return 0;
}

void erlang_init(void)
{
	erlang_time_end_1min  = now + 60;
	erlang_time_end_10min = now + 10*60;
	erlang_time_end_60min = now + 60*60;
}
