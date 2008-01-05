/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007,2008                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

static time_t beacon_nexttime;
static int    beacon_increment;
static char **beacon_msgs;
static int    beacon_msgs_count;
static int    beacon_msgs_cursor;


void beacon_reset(void)
{
	beacon_nexttime = now + 30; /* start 30 seconds from now */
	beacon_msgs_cursor = beacon_msgs_count; /* and start the sequence
						   from the beginning */
}

void beacon_set(const char *s)
{
	int i;

	/* realloc() works also when old ptr is NULL */
	beacon_msgs = realloc(beacon_msgs,
			      sizeof(char*) * (beacon_msgs_count+3));

	i = strlen(s) + 2;
	beacon_msgs[beacon_msgs_count] = malloc(i);
	strcpy(beacon_msgs[beacon_msgs_count], s);

	++beacon_msgs_count;
	beacon_msgs[beacon_msgs_count] = NULL;

	beacon_reset();
}

int  beacon_prepoll(struct aprxpolls *app)
{
	char **b = beacon_msgs;
	if (!b) return 0; /* Nothing to do */

	if (beacon_nexttime < app->next_timeout)
	  app->next_timeout = beacon_nexttime;

	return 0; /* No poll descriptors, only time.. */
}


int  beacon_postpoll(struct aprxpolls *app)
{
	char beacontext[1024];
	char beaconaddr[64];
	int txtlen;

	if (!beacon_msgs) return 0; /* Nothing to do */

	if (beacon_nexttime > now) return 0; /* Too early.. */

	if (beacon_msgs_cursor >= beacon_msgs_count) /* Last done.. */
	  beacon_msgs_cursor = 0;

	if (beacon_msgs_cursor == 0) {
	  int beacon_interval = 1200 + rand() % 600; /*  1200-1800 seconds from now */
	  beacon_increment = beacon_interval / beacon_msgs_count;
	  if (beacon_increment < 3)
	    beacon_increment = 3; /* Minimum interval: 3 seconds ! */
	}

	beacon_nexttime += beacon_increment;

	if (!mycall) return 0; /* No mycall !  hoh... */
	sprintf(beaconaddr, "%s>APRS", mycall);
	/* sprintf(beacontext, "%s", beacon_msgs[beacon_msgs_cursor++]); */
	txtlen = sprintf(beacontext, "%s", beacon_msgs[beacon_msgs_cursor++]);

	/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

	/* Send those (net)beacons.. */
	aprsis_queue(beaconaddr, beacontext, txtlen);

	return 0;
}
