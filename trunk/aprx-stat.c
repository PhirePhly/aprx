/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2013                            *
 *                                                                  *
 * **************************************************************** */


#include "aprx.h"

#ifdef ERLANGSTORAGE
int debug; /* linkage dummy */
int erlangout;
int epochtime;
const char *aprxlogfile;	/* linkage dummy */
const char *mycall;		/* linkage dummy */
struct timeval now;

void printtime(char *buf, int buflen)
{
	struct tm *t = gmtime(&now.tv_sec);
	// strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);
	sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
		t->tm_year+1900,t->tm_mon+1,t->tm_mday,
		t->tm_hour,t->tm_min,t->tm_sec);
}


void erlang_snmp(void)
{
	int i;

	/* SNMP data output - continuously growing counters
	 */

	printf("APRX.pid     %8ld\n", (long) ErlangHead->server_pid);
	printf("APRX.uptime  %8ld\n",
	       (long) (time(NULL) - ErlangHead->start_time));
	printf("APRX.mycall  %s\n", ErlangHead->mycall);

	for (i = 0; i < ErlangLinesCount; ++i) {
		struct erlangline *E = ErlangLines[i];

		printf("%s", E->name);
		printf("   %ld %ld   %ld  %ld  %ld  %ld    %d\n",
		       E->SNMP.bytes_rx, E->SNMP.packets_rx,
		       E->SNMP.bytes_rxdrop, E->SNMP.packets_rxdrop,
		       E->SNMP.bytes_tx, E->SNMP.packets_tx,
		       (int) (now.tv_sec - E->last_update));
	}
}

void erlang_xml(int topmode)
{
	int i, j, k, t;

	/* What this outputs is not XML, but a mild approximation
	   of the data that XML version would output.. 
	   It is not even the whole dataset, just last 60 samples
	   of each type.
	 */


	printf("APRX.pid     %8ld\n", (long) ErlangHead->server_pid);
	printf("APRX.uptime  %8ld\n",
	       (long) (time(NULL) - ErlangHead->start_time));
	printf("APRX.mycall  %s\n", ErlangHead->mycall);

	for (i = 0; i < ErlangLinesCount; ++i) {
		struct erlangline *E = ErlangLines[i];
		char logtime[40];
		struct tm *wallclock;

		printf("\nSNMP  %s", E->name);
		printf("   %ld %ld   %ld  %ld  %ld  %ld   %d\n",
		       E->SNMP.bytes_rx, E->SNMP.packets_rx,
		       E->SNMP.bytes_rxdrop, E->SNMP.packets_rxdrop,
		       E->SNMP.bytes_tx, E->SNMP.packets_tx,
		       (int) (now.tv_sec - E->last_update));

		printf("\n1min data\n");
		k = E->e1_cursor;
		t = E->e1_max;
		if (topmode)
			t = 90;
		for (j = 0; j < t; ++j) {
			--k;
			if (k < 0)
				k = E->e1_max - 1;
			if (E->e1[k].update == 0)
				continue;
			if (epochtime) {
				sprintf(logtime, "%ld",
					(long) E->e1[k].update);
			} else {
				wallclock = gmtime(&E->e1[k].update);
				strftime(logtime, sizeof(logtime),
					 "%Y-%m-%d %H:%M", wallclock);
			}
			printf("%s  %s", logtime, E->name);
			printf(" %2dm  %5ld  %3ld  %5ld  %3ld  %5ld  %3ld %5.3f  %5.3f  %5.3f\n",
			       1,
			       E->e1[k].bytes_rx,     E->e1[k].packets_rx,
			       E->e1[k].bytes_rxdrop, E->e1[k].packets_rxdrop,
			       E->e1[k].bytes_tx,     E->e1[k].packets_tx,
			       (float) E->e1[k].bytes_rx /
			       ((float) E->erlang_capa * 60.0),
			       (float) E->e1[k].bytes_rxdrop /
			       ((float) E->erlang_capa * 60.0),
			       (float)E->e1[k].bytes_tx/((float)E->erlang_capa*60.0)
				);
		}


		printf("\n10min data\n");
		k = E->e10_cursor;
		t = E->e10_max;
		if (topmode)
			t = 10;
		for (j = 0; j < t; ++j) {
			--k;
			if (k < 0)
				k = E->e10_max - 1;
			if (E->e10[k].update == 0)
				continue;
			if (epochtime) {
				sprintf(logtime, "%ld",
					(long) E->e10[k].update);
			} else {
				wallclock = gmtime(&E->e10[k].update);
				strftime(logtime, sizeof(logtime),
					 "%Y-%m-%d %H:%M", wallclock);
			}
			printf("%s  %s", logtime, E->name);
			printf(" %2dm  %5ld  %3ld  %5ld  %3ld  %5ld  %3ld %5.3f  %5.3f  %5.3f\n",
			       10,
			       E->e10[k].bytes_rx,     E->e10[k].packets_rx,
			       E->e10[k].bytes_rxdrop, E->e10[k].packets_rxdrop,
			       E->e10[k].bytes_tx,     E->e10[k].packets_tx,
			       (float) E->e10[k].bytes_rx /
			       ((float) E->erlang_capa * 60.0),
			       (float) E->e10[k].bytes_rxdrop /
			       ((float) E->erlang_capa * 60.0),
			       (float)E->e10[k].bytes_tx/((float)E->erlang_capa*60.0)
				);
		}


		printf("\n60min data\n");
		k = E->e60_cursor;
		t = E->e60_max;
		if (topmode)
			t = 3;
		for (j = 0; j < t; ++j) {
			--k;
			if (k < 0)
				k = E->e60_max - 1;
			if (E->e60[k].update == 0)
				continue;
			if (epochtime) {
				sprintf(logtime, "%ld",
					(long) E->e60[k].update);
			} else {
				wallclock = gmtime(&E->e60[k].update);
				strftime(logtime, sizeof(logtime),
					 "%Y-%m-%d %H:%M", wallclock);
			}
			printf("%s  %s", logtime, E->name);
			printf(" %2dm  %5ld  %3ld  %5ld  %3ld  %5ld  %3ld %5.3f  %5.3f  %5.3f\n",
			       60,
			       E->e60[k].bytes_rx,     E->e60[k].packets_rx,
			       E->e60[k].bytes_rxdrop, E->e60[k].packets_rxdrop,
			       E->e60[k].bytes_tx,     E->e60[k].packets_tx,
			       (float) E->e60[k].bytes_rx /
			       ((float) E->erlang_capa * 60.0),
			       (float) E->e60[k].bytes_rxdrop /
			       ((float) E->erlang_capa * 60.0),
			       (float)E->e60[k].bytes_tx/((float)E->erlang_capa*60.0)
				);
		}

	}


	exit(0);
}


void usage(void)
{
	printf("Usage: aprx-stat [-t] [-f arpx-erlang.dat] {-S|-x|-X}\n");
	exit(64);
}

int main(int argc, char **argv)
{
	int opt;
	int mode_snmp = 0;
	int mode_xml = 0;

        gettimeofday(&now, NULL);

	while ((opt = getopt(argc, argv, "f:StxX?h")) != -1) {
		switch (opt) {
		case 'f':
			erlang_backingstore = optarg;
			break;
		case 'S':	/* SNMP */
			++mode_snmp;
			break;
		case 'X':
			mode_xml = 1;
			break;
		case 'x':
			mode_xml = 2;
			break;
		case 't':
			epochtime = 1;
			break;
		default:
			usage();
			break;
		}
	}

	erlang_start(0);	/* Open the backing-store */

	if (!ErlangHead)
		exit(1);

	if (mode_snmp) {
		erlang_snmp();
	} else if (mode_xml == 1) {
		erlang_xml(0);
	} else if (mode_xml == 2) {
		erlang_xml(1);
	} else
		usage();

	return 0;
}

#else

struct timeval now;
int debug;			/* linkage dummy */
int erlangout;
int epochtime;
const char *aprxlogfile;	/* linkage dummy */
const char *mycall;		/* linkage dummy */

void printtime(char *buf, int buflen) {} /* linkage dummy */
void aprx_syslog_init(const char *p) {}

int main(int argc, char **argv)
{
  fprintf(stderr,"Sorry - aprx-stat program not available in system configured without ERLANGSTORAGE\n");
  return 1;
}
#endif

int tv_timerdelta_millis(struct timeval *_now, struct timeval *_target)
{
	int deltasec  = _target->tv_sec  - _now->tv_sec;
        int deltausec = _target->tv_usec - _now->tv_usec;
        while (deltausec < 0) {
        	deltausec += 1000000;
                --deltasec;
        }
        return deltasec * 1000 + deltausec / 1000;
}

void tv_timeradd_millis(struct timeval *res, struct timeval *a, int millis)
{
	if (res != a) {
          // Copy if different pointers..
          *res = *a;
        }
        int usec = (int)(res->tv_usec) + millis * 1000;
        if (usec >= 1000000) {
          int dsec = (usec / 1000000);
          res->tv_sec += dsec;
          usec %= 1000000;
          if (debug>3) printf("tv_timeadd_millis() dsec=%d dusec=%d\n",dsec, usec);
        }
        res->tv_usec = usec;
}

void tv_timeradd_seconds(struct timeval *res, struct timeval *a, int seconds)
{
	if (res != a) {
          // Copy if different pointers..
          *res = *a;
        }
        res->tv_sec += seconds;
}

int tv_timercmp(struct timeval *a, struct timeval *b)
{
	if (a->tv_sec < b->tv_sec) {
          return -1;
        }
	if (a->tv_sec > b->tv_sec) {
          return 1;
        }
        if (a->tv_usec < b->tv_usec) {
          return -1;
        }
        if (a->tv_usec > b->tv_usec) {
          return 1;
        }
        return 0; // equals!
}
