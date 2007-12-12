/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */


#include "aprx.h"


/* The erlang module accounts data reception per 1m/10m/60m
   intervals, and reports them on verbout.. */


/* #define USE_ONE_MINUTE_INTERVAL 1 */


#ifdef USE_ONE_MINUTE_INTERVAL
static time_t	erlang_time_end_1min;
static float erlang_time_ival_1min  = 1.0;
#endif

static time_t	erlang_time_end_10min;
static float erlang_time_ival_10min = 1.0;

static time_t	erlang_time_end_60min;
static float erlang_time_ival_60min = 1.0;



struct erlangline {
	const void *refp;
	int	index;
	char name[32];

	long	rxbytes;	/* SNMPish counters					*/
	long    rxpackets;

	long	txbytes;
	long    txpackets;

	int     erlang_capa;	/* bytes, 1 minute					*/
#ifdef USE_ONE_MINUTE_INTERVAL
	int     erlang_1min_rx;	/* received bytes, 1 minute period			*/
	int     erlang_1min_tx;  /* sent bytes, 1 minute period				*/
	int     erlang_1min_pkt_rx; /* received packets, 1 minute period		*/
	int     erlang_1min_pkt_tx; /* sent packets, 1 minute period			*/
#endif
	int     erlang_10min_rx; /* received bytes, 10 minute period			*/
	int     erlang_10min_tx; /* sent bytes, 10 minute period			*/
	int     erlang_10min_pkt_rx; /* received packets, 10 minute period		*/
	int     erlang_10min_pkt_tx; /* sent packets, 10 minute period			*/

	int     erlang_60min_rx; /* received bytes, 60 minute period			*/
	int     erlang_60min_tx; /* sent bytes, 60 minute period			*/
	int     erlang_60min_pkt_rx; /* received packets, 60 minute period		*/
	int     erlang_60min_pkt_tx; /* sent packets, 60 minute period			*/
};

static struct erlangline *ErlangLines;
static int ErlangLineCount;


/*
 *  erlang_set()
 */
void erlang_set(const void *refp, const char *portname, int bytes_per_minute)
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
	/* If found -- err... why we are SETing it AGAIN ? */

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

	  E->erlang_capa = bytes_per_minute;
	  E->index = ErlangLineCount-1;
	}
}

/*
 *  erlang_add()
 */
void erlang_add(const void *refp, const char *portname, int rx_or_tx, int bytes, int packets)
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

	  E->erlang_capa = (1200*60)/9; /* Magic capacity constant */
	  E->index = ErlangLineCount-1;
	}

	if (rx_or_tx == ERLANG_RX) {
	  E->rxbytes             += bytes;
	  E->rxpackets           += packets;

#ifdef USE_ONE_MINUTE_INTERVAL
	  E->erlang_1min_rx      += bytes;
	  E->erlang_1min_pkt_rx  += packets;
#endif
	  E->erlang_10min_rx     += bytes;
	  E->erlang_10min_pkt_rx += packets;

	  E->erlang_60min_rx     += bytes;
	  E->erlang_60min_pkt_rx += packets;
	}

	if (rx_or_tx == ERLANG_TX) {
	  E->txbytes             += bytes;
	  E->txpackets           += packets;
#ifdef USE_ONE_MINUTE_INTERVAL
	  E->erlang_1min_tx      += bytes;
	  E->erlang_1min_pkt_tx  += packets;
#endif
	  E->erlang_10min_tx     += bytes;
	  E->erlang_10min_pkt_tx += packets;

	  E->erlang_60min_tx     += bytes;
	  E->erlang_60min_pkt_tx += packets;
	}
}


/*
 *  erlang_time_end() - process erlang measurement intercal time end event
 */
static void erlang_time_end(void)
{
	int i;
	char msgbuf[500];
	char logtime[40];
	struct tm *wallclock = localtime(&now);

	strftime(logtime, sizeof(logtime), "%Y-%m-%d %H:%M", wallclock);

#ifdef USE_ONE_MINUTE_INTERVAL
	if (now >= erlang_time_end_1min) {
	  erlang_time_end_1min += 60;
	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = & ErlangLines[i];
	    sprintf(msgbuf,
		    "%ld%cERLANG%-2d %s %s Raw Bytes/Pkts Rx %6d %3d  Tx %6d %3d  - %5.3f %5.3f",
		    now,(erlangout?'\t':' '),1,E->name,logtime,
		    E->erlang_1min_rx, E->erlang_1min_pkt_rx,
		    E->erlang_1min_tx, E->erlang_1min_pkt_tx,
		    ((float)E->erlang_1min_rx/(float)E->erlang_capa*erlang_time_ival_1min),
		    ((float)E->erlang_1min_tx/(float)E->erlang_capa*erlang_time_ival_1min)
		    );
	    if (erlangout)
	      printf("%s\n", msgbuf);
	    else
	      syslog(LOG_INFO, "%s", msgbuf);
	    E->erlang_1min_rx = 0;
	    E->erlang_1min_pkt_rx = 0;
	    E->erlang_1min_tx = 0;
	    E->erlang_1min_pkt_tx = 0;
	  }
	  erlang_time_ival_1min = 1.0;
	}
#endif
	if (now >= erlang_time_end_10min) {
	  erlang_time_end_10min += 600;

	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = & ErlangLines[i];
	    sprintf(msgbuf,
		    "%ld%cERLANG%-2d %s %s Raw Bytes/Pkts Rx %6d %3d  Tx %6d %3d  - %5.3f %5.3f",
		    now,(erlangout?'\t':' '),10,E->name,logtime,
		    E->erlang_10min_rx, E->erlang_10min_pkt_rx,
		    E->erlang_10min_tx, E->erlang_10min_pkt_tx,
		    ((float)E->erlang_10min_rx/((float)E->erlang_capa*10.0*erlang_time_ival_10min)),
		    ((float)E->erlang_10min_tx/((float)E->erlang_capa*10.0*erlang_time_ival_10min))
		    );
	    if (erlangout)
	      printf("%s\n", msgbuf);
	    else
	      syslog(LOG_INFO, "%s", msgbuf);
	    E->erlang_10min_rx = 0;
	    E->erlang_10min_pkt_rx = 0;
	    E->erlang_10min_tx = 0;
	    E->erlang_10min_pkt_tx = 0;
	  }
	  erlang_time_ival_10min = 1.0;
	}
	if (now >= erlang_time_end_60min) {
	  erlang_time_end_60min += 3600;

	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = & ErlangLines[i];
	    sprintf(msgbuf,
		    "%ld%cERLANG%-2d %s %s Raw Bytes/Pkts Rx %6d %3d  Tx %6d %3d  - %5.3f %5.3f",
		    now,(erlangout?'\t':' '),60,E->name,logtime,
		    E->erlang_60min_rx,  E->erlang_60min_pkt_rx,
		    E->erlang_60min_tx,  E->erlang_60min_pkt_tx,
		    ((float)E->erlang_60min_rx/((float)E->erlang_capa*60.0*erlang_time_ival_60min)),
		    ((float)E->erlang_60min_tx/((float)E->erlang_capa*60.0*erlang_time_ival_60min))
		    );
	    if (erlangout)
	      printf("%s\n", msgbuf);
	    else
	      syslog(LOG_INFO, "%s", msgbuf);
	    E->erlang_60min_rx = 0;
	    E->erlang_60min_pkt_rx = 0;
	    E->erlang_60min_tx = 0;
	    E->erlang_60min_pkt_tx = 0;
	  }
	  erlang_time_ival_60min = 1.0;
	}
}

int erlang_prepoll(int nfds, struct pollfd **fdsp, time_t *tout)
{

#ifdef USE_ONE_MINUTE_INTERVAL
	if (*tout > erlang_time_end_1min)
	  *tout = erlang_time_end_1min;
#endif
	if (*tout > erlang_time_end_10min)
	  *tout = erlang_time_end_10min;
	if (*tout > erlang_time_end_60min)
	  *tout = erlang_time_end_60min;

	return 0;
}

int erlang_postpoll(int nfds, struct pollfd *fds)
{
	if (
#ifdef USE_ONE_MINUTE_INTERVAL
	    now >= erlang_time_end_1min ||
#endif
	    now >= erlang_time_end_10min ||
	    now >= erlang_time_end_60min
	    )
	  erlang_time_end();

	return 0;
}

static struct syslog_facs {
	const char *name;
	int fac_code;
} syslog_facs[] = {
  { "LOG_DAEMON", LOG_DAEMON },
#ifdef LOG_FTP
  { "LOG_FTP",    LOG_FTP },
#endif
#ifdef LOG_LPR
  { "LOG_LPR",    LOG_LPR },
#endif
#ifdef LOG_MAIL
  { "LOG_MAIL",   LOG_MAIL },
#endif
#ifdef LOG_USER
  { "LOG_USER",   LOG_USER },
#endif
#ifdef LOG_UUCP
  { "LOG_UUCP",   LOG_UUCP },
#endif
  { "LOG_LOCAL0", LOG_LOCAL0 },
  { "LOG_LOCAL1", LOG_LOCAL1 },
  { "LOG_LOCAL2", LOG_LOCAL2 },
  { "LOG_LOCAL3", LOG_LOCAL3 },
  { "LOG_LOCAL4", LOG_LOCAL4 },
  { "LOG_LOCAL5", LOG_LOCAL5 },
  { "LOG_LOCAL6", LOG_LOCAL6 },
  { "LOG_LOCAL7", LOG_LOCAL7 },
  { NULL, 0 }
};

void erlang_init(const char *syslog_facility_name)
{
	int syslog_fac = LOG_DAEMON, i;
	static int done_once = 0;

	now = time(NULL);

	if (done_once) {
	  closelog(); /* We reconfigure from config file! */
	} else
	  ++done_once;

	/* Time intervals will end at next even
	       1 minute/10 minutes/60 minutes,
	   although said interval will be shorter than full. */


#ifdef USE_ONE_MINUTE_INTERVAL
	erlang_time_end_1min  = now +   60 - (now %   60);
	erlang_time_ival_1min =    (float)(60 - now % 60)/60.0;
#endif
	erlang_time_end_10min = now +  600 - (now %  600);
	erlang_time_ival_10min =  (float)(600 - now % 600)/600.0;

	erlang_time_end_60min = now + 3600 - (now % 3600);
	erlang_time_ival_60min = (float)(3600 - now % 3600)/3600.0;

	for (i = 0;; ++i) {
	  if (syslog_facs[i].name == NULL) {
	    fprintf(stderr, "Sorry, unknown erlang syslog facility code name: %s, not supported in this system.\n", syslog_facility_name);
	    fprintf(stderr, "Accepted list is:");
	    for (i = 0;; ++i) {
	      if (syslog_facs[i].name == NULL)
		break;
	      fprintf(stderr," %s",syslog_facs[i].name);
	    }
	    fprintf(stderr,"\n");
	    break;
	  }
	  if (strcasecmp(syslog_facs[i].name, syslog_facility_name) == 0) {
	    syslog_fac = syslog_facs[i].fac_code;
	    break;
	  }
	}

	openlog("aprx", LOG_NDELAY|LOG_PID, syslog_fac);

}
