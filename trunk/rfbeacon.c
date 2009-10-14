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
	const struct aprx_interface *interface;
	const char *dest;
	const char *msg;
};

static struct beaconmsg **beacon_msgs;

static int beacon_msgs_count;
static int beacon_msgs_cursor;


static time_t beacon_nexttime;
static time_t beacon_last_nexttime;
static int    beacon_cycle_size = 20*60; // 20 minutes


static void rfbeacon_reset(void)
{
	beacon_nexttime = now + 30;	/* start 30 seconds from now */
	beacon_msgs_cursor = 0;
}

static void rfbeacon_set(struct configfile *cf, const char *p1, char *str, const int netonly)
{
	const char *srcaddr  = NULL;
	const char *destaddr = NULL;
	const char *via      = NULL;
	char *buf  = alloca(strlen(p1) + strlen(str ? str : "") + 10);
	char *to   = NULL;
	char *code = NULL;
	char *lat  = NULL;
	char *lon  = NULL;
	char *comment = NULL;
	char  beaconaddr[64];
	int   beaconaddrlen;
	const struct aprx_interface *aif = NULL;

	*buf = 0;
	struct beaconmsg *bm = malloc(sizeof(*bm));
	memset(bm, 0, sizeof(*bm));

	if (debug) {
	  if (netonly)
		printf("NETBEACON parameters: ");
	  else
		printf("RFBEACON parameters: ");
	}

	while (*p1) {

		/* if (debug)
		   printf("p1='%s' ",p1); */

		if (strcmp(p1, "to") == 0) {

			to = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			config_STRUPPER(to);
			aif = find_interface_by_callsign(to);
			if ((aif != NULL) && !aif->txok) {
				aif = NULL;  // Not an TX interface :-(
				printf("%s:%d Sorry, <rfbeacon> to '%s' that is not a TX capable interface.\n",
				       cf->name, cf->linenum, to);
				goto discard_bm; // sigh..
			}

			if (debug)
				printf("to '%s' ", to);

		} else if (strcmp(p1, "for") == 0) {

			srcaddr = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			// What about ITEM and OBJECT ?

			// if (validate_callsign_input((char *) srcaddr),1) {
			//   printf("Invalid rfbeacon FOR callsign");
			// }

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

	if (aif == NULL && !netonly) {
		printf("%s:%d Lacking 'to' keyword for this beacon definition.\n", cf->name, cf->linenum);
		goto discard_bm;
	}

	if (srcaddr == NULL) {
		if (debug)
			printf("%s:%d Lacking the 'for' keyword for this beacon definition.\n", cf->name, cf->linenum);
		goto discard_bm;
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
		  printf("Constructed beacon address header is too big. (over %d chars long)\n",(int)sizeof(beaconaddr)-2);
		goto discard_bm;
	}
	bm->dest      = strdup(beaconaddr);
	bm->interface = aif;

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
			if (!code || (code && strlen(code) != 2))
				printf("%s:%d .. BEACON definition failure; symbol parameter missing or wrong size\n", cf->name, cf->linenum);
			if (!lat || (lat && strlen(lat) != 8))
				printf("%s:%d .. BEACON definition failure; lat(itude) parameter missing or wrong size\n", cf->name, cf->linenum);
			if (!lon || (lon && strlen(lon) != 9))
				printf("%s:%d .. BEACON definition failure; lon(gitude) parameter missing or wrong size\n", cf->name, cf->linenum);
			/* parse failure, abandon the alloc too */
			goto discard_bm;
		}
	}

	if (debug) {
	  if (netonly)
		printf("NETBEACON FOR '%s'  '%s'\n", bm->dest, bm->msg);
	  else
		printf("RFBEACON FOR '%s'  '%s'\n", bm->dest, bm->msg);
	}

	/* realloc() works also when old ptr is NULL */
	beacon_msgs = realloc(beacon_msgs,
			      sizeof(bm) * (beacon_msgs_count + 3));

	beacon_msgs[beacon_msgs_count++] = bm;
	beacon_msgs[beacon_msgs_count] = NULL;

	rfbeacon_reset();

	if (0) {
	discard_bm:
	  if (bm->dest != NULL) free((void*)(bm->dest));
	  if (bm->msg  != NULL) free((void*)(bm->msg));
	  free(bm);
	}
	return;
}

int rfbeacon_prepoll(struct aprxpolls *app)
{
	if (!aprsis_login)
		return 0;	/* No mycall !  hoh... */
	if (!beacon_msgs)
		return 0;	/* Nothing to do */

	if (beacon_nexttime < app->next_timeout)
		app->next_timeout = beacon_nexttime;

	return 0;		/* No poll descriptors, only time.. */
}


int rfbeacon_postpoll(struct aprxpolls *app)
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
			printf("rfbeacons cycle: %.2f minutes, r: %d, increment: %.1f seconds\n",
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
	  if (bm->interface != NULL)
		printf("Now beaconing: rfbeacon to %s: '%s' -> '%s',  next beacon in %.2f minutes\n",
		       bm->interface->callsign,
		       bm->dest, bm->msg,
		       round((beacon_nexttime - now)/60.0));
	  else
		printf("Now beaconing: netbeacon '%s' -> '%s',  next beacon in %.2f minutes\n",
		       bm->dest, bm->msg,
		       round((beacon_nexttime - now)/60.0));
	}

	/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

	/* Send those (rf)beacons.. */
	if (bm->interface != NULL)
		interface_receive_tnc2(bm->interface,
				       bm->dest, bm->msg, txtlen);

	/* Send them all also as netbeacons.. */
	aprsis_queue(bm->dest, destlen, aprsis_login, bm->msg, txtlen);

	return 0;
}

void rfbeacon_config(struct configfile *cf, const int netonly)
{
	char *name, *param1;
	char *str = cf->buf;

	while (readconfigline(cf) != NULL) {
		if (configline_is_comment(cf))
			continue;	/* Comment line, or empty line */

		// It can be severely indented...
		str = config_SKIPSPACE(cf->buf);

		name = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);
		config_STRLOWER(name);

		param1 = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		if (netonly) {
		  if (strcmp(name, "</netbeacon>") == 0)
		    break;
		} else {
		  if (strcmp(name, "</rfbeacon>") == 0)
		    break;
		}

		if (strcmp(name, "beacon") == 0) {
		  rfbeacon_set(cf, param1, str, netonly);
		} else {
		  printf("%s:%d Unknown config keyword: '%s'\n",name);
		  continue;
		}
	}
}
