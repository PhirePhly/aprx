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

time_t beacon_nexttime;


char **beacon_msgs;
int    beacon_msgs_count;


void beacon_reset(void)
{
	beacon_nexttime = time(NULL) + 30; /* 30 seconds from now */
}

void beacon_set(const char *s)
{
	int i;

	if (!beacon_msgs)
	  beacon_msgs = malloc(sizeof(char*) * 3);
	else
	  beacon_msgs = realloc(beacon_msgs, sizeof(char*) * (beacon_msgs_count+3));

	i = strlen(s) + 4;
	beacon_msgs[beacon_msgs_count] = malloc(i);
	sprintf(beacon_msgs[beacon_msgs_count], "%s\r\n", s);

	++beacon_msgs_count;
	beacon_msgs[beacon_msgs_count] = NULL;

	beacon_reset();
}

int  beacon_prepoll(int nfds, struct pollfd **fdsp, time_t *tout)
{
	char **b = beacon_msgs;
	if (!b) return 0; /* Nothing to do */

	if (beacon_nexttime < *tout)
	  *tout = beacon_nexttime;

	return 0; /* No poll descriptors, only time.. */
}


int  beacon_postpoll(int nfds, struct pollfd *fds)
{
	char **b = beacon_msgs;
	if (!b) return 0; /* Nothing to do */

	if (beacon_nexttime > now) return 0; /* Too early.. */

	beacon_nexttime = time(NULL) + 1200 + rand() % 600; /*  1200-1800 seconds from now */

	for (;*b; ++b)
	  aprsis_queue(*b, strlen(*b)); /* Send those (net)beacons.. */
}


