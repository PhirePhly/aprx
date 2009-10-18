/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * **************************************************************** */


#include "aprx.h"

time_t now;
int debug; /* linkage dummy */
int erlangout;
int epochtime;
const char *aprxlogfile;	/* linkage dummy */
const char *aprsis_login;		/* linkage dummy */

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
		       (int) (now - E->last_update));
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
		       (int) (now - E->last_update));

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

	now = time(NULL);

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
