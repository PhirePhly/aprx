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

struct beaconmsg {
	const char *destaddr;
	const char *msg;
};

static struct beaconmsg **beacon_msgs;

static int    beacon_msgs_count;
static int    beacon_msgs_cursor;


static time_t beacon_nexttime;
static int    beacon_increment;



void beacon_reset(void)
{
	beacon_nexttime = now + 30; /* start 30 seconds from now */
	beacon_msgs_cursor = beacon_msgs_count; /* and start the sequence
						   from the beginning */
}

void beacon_set(const char *p1, char *str)
{
	int i;
	const char *for_ = mycall;

	struct beaconmsg *bm = malloc(sizeof(*bm));
	if (!bm) return; /* sigh.. */
	memset(bm, 0, sizeof(*bm));

	bm->destaddr = mycall;

	if (strcmp(p1,"for") == 0) {

	  for_ = str;
	  str = config_SKIPTEXT (str);
	  str = config_SKIPSPACE (str);

	  p1 = str;
	  str = config_SKIPTEXT (str);
	  str = config_SKIPSPACE (str);

	  bm->destaddr = strdup(for_);
	}

	bm->msg = strdup(p1);

	/* realloc() works also when old ptr is NULL */
	beacon_msgs = realloc(beacon_msgs,
			      sizeof(bm) * (beacon_msgs_count+3));

	beacon_msgs[beacon_msgs_count++] = bm;
	beacon_msgs[beacon_msgs_count] = NULL;

	beacon_reset();
}

int  beacon_prepoll(struct aprxpolls *app)
{
	if (!beacon_msgs) return 0; /* Nothing to do */

	if (beacon_nexttime < app->next_timeout)
	  app->next_timeout = beacon_nexttime;

	return 0; /* No poll descriptors, only time.. */
}


int  beacon_postpoll(struct aprxpolls *app)
{
	char beacontext[1024];
	char beaconaddr[64];
	int txtlen;
	struct beaconmsg *bm;

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

	/* --- now the business of sending ... */

	bm = beacon_msgs[beacon_msgs_cursor++];

	sprintf(beaconaddr, "%s>APRS", bm->destaddr);
	/* sprintf(beacontext, "%s", beacon_msgs[beacon_msgs_cursor++]); */
	txtlen = strlen(bm->msg);

	/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

	/* Send those (net)beacons.. */
	aprsis_queue(beaconaddr, mycall, bm->msg, txtlen);

	return 0;
}
