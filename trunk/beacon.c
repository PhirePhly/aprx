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

struct beaconmsg {
	const char *destaddr;
	const char *msg;
};

static struct beaconmsg **beacon_msgs;

static int beacon_msgs_count;
static int beacon_msgs_cursor;


static time_t beacon_nexttime;
static int beacon_increment;


int validate_degmin_input(const char *s, int maxdeg)
{
	int i;
	return 0;		/* zero for OK */
}

void beacon_reset(void)
{
	beacon_nexttime = now + 30;	/* start 30 seconds from now */
	beacon_msgs_cursor = beacon_msgs_count;	/* and start the sequence
						   from the beginning */
}

void beacon_set(const char *p1, char *str)
{
	const char *for_ = mycall;
	char *buf = alloca(strlen(p1) + strlen(str ? str : "") + 10);
	char *code = NULL;
	char *lat = NULL;
	char *lon = NULL;
	char *comment = NULL;

	*buf = 0;
	struct beaconmsg *bm = malloc(sizeof(*bm));
	if (!bm)
		return;		/* sigh.. */
	memset(bm, 0, sizeof(*bm));

	bm->destaddr = mycall;

	if (debug)
		printf("NETBEACON parameters: ");

	while (*p1) {

		/* if (debug)
		   printf("p1='%s' ",p1); */

		if (strcmp(p1, "for") == 0) {

			for_ = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			if (validate_callsign_input((char *) for_)) {
				;
			}

			p1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			bm->destaddr = strdup(for_);

			if (debug)
				printf("for '%s' ", for_);

		} else if (strcmp(p1, "lat") == 0) {
			/*  ddmm.mmN   */

			lat = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			if (validate_degmin_input(lat, 90)) {
				;
			}

			p1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("lat '%s' ", lat);

		} else if (strcmp(p1, "lon") == 0) {
			/*  dddmm.mmE  */

			lon = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			if (validate_degmin_input(lat, 180)) {
				;
			}

			p1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("lon '%s' ", lon);

		} else if (strcmp(p1, "symbol") == 0) {
			/*   R&    */

			code = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			p1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("symbol '%s' ", code);

		} else if (strcmp(p1, "comment") == 0) {
			/* text up to .. 40 chars */

			comment = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			p1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("comment '%s' ", comment);

		} else if (strcmp(p1, "raw") == 0) {

			p1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			bm->msg = strdup(p1);

			p1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("raw '%s' ", bm->msg);

		} else {

#if 0
			if (debug)
				printf("Unknown keyword: '%s'", p1);

			p1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);
#else
			/* Unknown keyword, a raw message ? */
			bm->msg = strdup(p1);

			if (debug)
				printf("raw '%s' ", bm->msg);

			break;
#endif
		}

	}
	if (debug)
		printf("\n");

	if (!bm->msg) {
		/* Not raw packet, perhaps composite ? */
		if (code && strlen(code) == 2 && lat && strlen(lat) == 8 &&
		    lon && strlen(lon) == 9) {
			sprintf(buf, "!%s%c%s%c", lat, code[0], lon,
				code[1]);
			if (comment)
				strcat(buf, comment);
			bm->msg = strdup(buf);
		} else {
			if (debug) {
				if (!code || (code && strlen(code) != 2))
					printf(" .. NETBEACON definition failure; symbol parameter missing or wrong size\n");
				if (!lat || (lat && strlen(lat) != 8))
					printf(" .. NETBEACON definition failure; lat(itude) parameter missing or wrong size\n");
				if (!lon || (lon && strlen(lon) != 9))
					printf(" .. NETBEACON definition failure; lon(gitude) parameter missing or wrong size\n");
			}
			/* parse failure, abandon the alloc too */
			free(bm);
			return;
		}
	}

	if (debug)
		printf("NETBEACON  FOR '%s'  '%s'\n", bm->destaddr,
		       bm->msg);

	/* realloc() works also when old ptr is NULL */
	beacon_msgs = realloc(beacon_msgs,
			      sizeof(bm) * (beacon_msgs_count + 3));

	beacon_msgs[beacon_msgs_count++] = bm;
	beacon_msgs[beacon_msgs_count] = NULL;

	beacon_reset();
}

int beacon_prepoll(struct aprxpolls *app)
{
	if (!beacon_msgs)
		return 0;	/* Nothing to do */

	if (beacon_nexttime < app->next_timeout)
		app->next_timeout = beacon_nexttime;

	return 0;		/* No poll descriptors, only time.. */
}


int beacon_postpoll(struct aprxpolls *app)
{
	char beaconaddr[64];
	int txtlen;
	struct beaconmsg *bm;

	if (!beacon_msgs)
		return 0;	/* Nothing to do */

	if (beacon_nexttime > now)
		return 0;	/* Too early.. */

	if (beacon_msgs_cursor >= beacon_msgs_count)	/* Last done.. */
		beacon_msgs_cursor = 0;

	if (beacon_msgs_cursor == 0) {
		int beacon_interval = 1200 + rand() % 600;	/*  1200-1800 seconds from now */
		beacon_increment = beacon_interval / beacon_msgs_count;
		if (beacon_increment < 3)
			beacon_increment = 3;	/* Minimum interval: 3 seconds ! */
	}

	beacon_nexttime += beacon_increment;

	if (!mycall)
		return 0;	/* No mycall !  hoh... */

	/* --- now the business of sending ... */

	bm = beacon_msgs[beacon_msgs_cursor++];

	sprintf(beaconaddr, "%s>APRS", bm->destaddr);
	txtlen = strlen(bm->msg);

	/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

	/* Send those (net)beacons.. */
	aprsis_queue(beaconaddr, mycall, bm->msg, txtlen);

	return 0;
}
