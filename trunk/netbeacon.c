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
#include <math.h>

struct beaconmsg {
	time_t nexttime;
	const char *dest;
	const char *msg;
};

static struct beaconmsg **beacon_msgs;

static int beacon_msgs_count;
static int beacon_msgs_cursor;


static time_t beacon_nexttime;
static time_t beacon_last_nexttime;
static int    beacon_cycle_size = 20*60; // 20 minutes

static const char* scan_int(const char *p, int len, int *val, int *seen_space) {
	int i;
	char c;
	*val = 0;
	for (i = 0; i < len; ++i, ++p) {
		c = *p;
		if (('0' <= c && c <= '9') && !(*seen_space)) {
			*val = (*val) * 10 + (c - '0');
		} else if (c == ' ') {
			*val = (*val) * 10;
			*seen_space = 1;
		} else {
			return NULL;
		}
	}
	return p;
}

int validate_degmin_input(const char *s, int maxdeg)
{
	int deg;
	int m1, m2;
	char c;
	const char *t;
	int seen_space = 0;
	if (maxdeg > 90) {
		t = scan_int(s, 3, &deg, &seen_space);
		if (t != s+3) return 1; // scan failure
		if (deg > 179) return 1; // too large value
		s = t;
		t = scan_int(s, 2, &m1, &seen_space);
		if (t != s+2) return 1;
		if (m1 > 59) return 1;
		s = t;
		c = *s;
		if (!seen_space && c == '.') {
			// OK
		} else if (!seen_space && c == ' ') {
			seen_space = 1;
		} else {
			return 1; // Bad char..
		}
		++s;
		t = scan_int(s, 2, &m2, &seen_space);
		if (t != s+2) return 1;
		s = t;
		c = *s;
		if (c != 'E' && c != 'e' && c != 'W' && c != 'w') return 1;
	} else {
		t = scan_int(s, 2, &deg, &seen_space);
		if (t != s+2) return 1; // scan failure
		if (deg > 89) return 1; // too large value
		s = t;
		t = scan_int(s, 2, &m1, &seen_space);
		if (t != s+2) return 1;
		if (m1 > 59) return 1;
		s = t;
		c = *s;
		if (!seen_space && c == '.') {
			// OK
		} else if (!seen_space && c == ' ') {
			seen_space = 1;
		} else {
			return 1; // Bad char..
		}
		++s;
		t = scan_int(s, 2, &m2, &seen_space);
		if (t != s+2) return 1;
		s = t;
		c = *s;
		if (c != 'N' && c != 'n' && c != 'S' && c != 's') return 1;
	}
	return 0;		/* zero for OK */
}

void netbeacon_reset(void)
{
	beacon_nexttime = now + 30;	/* start 30 seconds from now */
	beacon_msgs_cursor = 0;
}

void netbeacon_set(const char *p1, char *str)
{
	const char *srcaddr  = NULL;
	const char *destaddr = NULL;
	const char *via      = NULL;
	char *buf  = alloca(strlen(p1) + strlen(str ? str : "") + 10);
	char *code = NULL;
	char *lat  = NULL;
	char *lon  = NULL;
	char *comment = NULL;
	char beaconaddr[64];
	int  beaconaddrlen;

	*buf = 0;
	struct beaconmsg *bm = malloc(sizeof(*bm));
	if (!bm)
		return;		/* sigh.. */
	memset(bm, 0, sizeof(*bm));

	if (debug)
		printf("NETBEACON parameters: ");

	while (*p1) {

		/* if (debug)
		   printf("p1='%s' ",p1); */

		if (strcmp(p1, "for") == 0) {

			srcaddr = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (validate_callsign_input((char *) srcaddr)) {
				;
			}

			if (debug)
				printf("for '%s' ", srcaddr);

		} else if (strcmp(p1, "dest") == 0) {

			destaddr = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("dest '%s' ", destaddr);

		} else if (strcmp(p1, "via") == 0) {

			via  = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("via '%s' ", via);

		} else if (strcmp(p1, "lat") == 0) {
			/*  ddmm.mmN   */

			lat = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (validate_degmin_input(lat, 90)) {
				;
			}

			if (debug)
				printf("lat '%s' ", lat);

		} else if (strcmp(p1, "lon") == 0) {
			/*  dddmm.mmE  */

			lon = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (validate_degmin_input(lat, 180)) {
				;
			}

			if (debug)
				printf("lon '%s' ", lon);

		} else if (strcmp(p1, "symbol") == 0) {
			/*   R&    */

			code = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("symbol '%s' ", code);

		} else if (strcmp(p1, "comment") == 0) {
			/* text up to .. 40 chars */

			comment = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("comment '%s' ", comment);

		} else if (strcmp(p1, "raw") == 0) {

			p1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			bm->msg = strdup(p1);

			if (debug)
				printf("raw '%s' ", bm->msg);

		} else {

#if 0
			if (debug)
				printf("Unknown keyword: '%s'", p1);

			p1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
#else
			/* Unknown keyword, a raw message ? */
			bm->msg = strdup(p1);

			if (debug)
				printf("raw '%s' ", bm->msg);

			break;
#endif
		}

		p1 = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);
	}
	if (debug)
		printf("\n");

	if (srcaddr == NULL) {
		if (debug)
			printf("Lacking the 'for' keyword for this beacon definition.");
		return;
	}


	if (destaddr == NULL)
		destaddr = "APRS";
	if (via == NULL) {
		beaconaddrlen = snprintf(beaconaddr, sizeof(beaconaddr),
					 "%s>%s", srcaddr, destaddr);
	} else {
		beaconaddrlen = snprintf(beaconaddr, sizeof(beaconaddr),
					 "%s>%s,%s", srcaddr, destaddr, via);
	}
	if (beaconaddrlen >= sizeof(beaconaddr)) {
		// BAD BAD!  Too big?
		if (debug)
		  printf("Constructed netbeacon address header is too big. (over %d chars long)",(int)sizeof(beaconaddr)-2);
		return;
	}
	bm->dest = strdup(beaconaddr);

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
		printf("NETBEACON  FOR '%s'  '%s'\n", bm->dest, bm->msg);

	/* realloc() works also when old ptr is NULL */
	beacon_msgs = realloc(beacon_msgs,
			      sizeof(bm) * (beacon_msgs_count + 3));

	beacon_msgs[beacon_msgs_count++] = bm;
	beacon_msgs[beacon_msgs_count] = NULL;

	netbeacon_reset();
}

int netbeacon_prepoll(struct aprxpolls *app)
{
	if (!aprsis_login)
		return 0;	/* No mycall !  hoh... */
	if (!beacon_msgs)
		return 0;	/* Nothing to do */

	if (beacon_nexttime < app->next_timeout)
		app->next_timeout = beacon_nexttime;

	return 0;		/* No poll descriptors, only time.. */
}


int netbeacon_postpoll(struct aprxpolls *app)
{
	int  destlen;
	int  txtlen;
	int  i;
	struct beaconmsg *bm;

	if (!aprsis_login)
		return 0;	/* No mycall !  hoh... */
	if (!beacon_msgs)
		return 0;	/* Nothing to do */
	if (beacon_nexttime > now)
		return 0;	/* Too early.. */

	if (beacon_msgs_cursor == 0) {
		float beacon_cycle, beacon_increment;
		int   r;
		srand(now);
		r = rand() % 1024;
		beacon_cycle = (beacon_cycle_size -
				0.2*beacon_cycle_size * (r*0.001));
		beacon_increment = beacon_cycle / beacon_msgs_count;
		if (beacon_increment < 3.0)
			beacon_increment = 3.0;	/* Minimum interval: 3 seconds ! */
		if (debug)
			printf("netbeacons cycle: %.2f minutes, r: %d, increment: %.1f seconds\n",
			       beacon_cycle/60.0, r, beacon_increment);
		for (i = 0; i < beacon_msgs_count; ++i) {
			beacon_msgs[i]->nexttime =
			  now + round(i * beacon_increment);
		}
		beacon_last_nexttime = now + round(beacon_msgs_count * beacon_increment);
	}

	/* --- now the business of sending ... */

	bm = beacon_msgs[beacon_msgs_cursor++];

	beacon_nexttime = bm->nexttime;
	if (beacon_msgs_cursor >= beacon_msgs_count) {	/* Last done.. */
		beacon_msgs_cursor = 0;
	        beacon_nexttime    = beacon_last_nexttime;
	}
	
	destlen = strlen(bm->dest);
	txtlen  = strlen(bm->msg);

	if (debug) {
		printf("Now beaconing: '%s' -> '%s',  next beacon in %.2f minutes\n",
		       bm->dest, bm->msg, round((beacon_nexttime - now)/60.0));
	}

	/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

	/* Send those (net)beacons.. */
	aprsis_queue(bm->dest, destlen, aprsis_login, bm->msg, txtlen);

	return 0;
}
