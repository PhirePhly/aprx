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

static int digi_count;
static struct digipeater *digis;


int  digipeater_prepoll(struct aprxpolls *app) {
	return 0;
}
int  digipeater_postpoll(struct aprxpolls *app) {
	return 0;
}


void digipeater_receive(struct digipeater_source *src, struct pbuf_t *pb)
{
}



void digipeater_config(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int has_fault = 0;

	struct aprx_interface *aif = NULL;
	int ratelimit = 300;
	int viscous_delay = 0;

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

		if (strcmp(name, "</digipeater>") == 0) {
			break;
		}
		if (strcmp(name, "transmit") == 0) {
			aif = find_interface_by_callsign(param1);
			if (aif != NULL && (!aif->txok)) {
			  aif = NULL; // Not 
			  printf("%s:%d This transmit interface has no TX-OK TRUE setting: '%s'\n",
				 cf->name, cf->linenum, param1);
			  has_fault = 1;
			} else if (aif == NULL) {
			  printf("%s:%d Unknown interface: '%s'\n",
				 cf->name, cf->linenum, param1);
			  has_fault = 1;
			}

		} else if (strcmp(name, "ratelimit") == 0) {
			ratelimit = atoi(param1);
			if (ratelimit < 10 || ratelimit > 300)
				ratelimit = 300;

		} else if (strcmp(name, "viscous-delay") == 0) {
			viscous_delay = atoi(param1);
			if (viscous_delay < 0) {
			  printf("%s:%d Bad value for viscous-delay: '%s'\n",
				 cf->name, cf->linenum, param1);
			  viscous_delay = 0;
			  has_fault = 1;
			}
			if (viscous_delay > 9) {
			  printf("%s:%d Bad value for viscous-delay: '%s'\n",
				 cf->name, cf->linenum, param1);
			  viscous_delay = 9;
			  has_fault = 1;
			}

		} else if (strcmp(name, "<trace>") == 0) {
		} else if (strcmp(name, "<wide>") == 0) {
		} else if (strcmp(name, "<source>") == 0) {
		} else {
		  printf("%s:%d Unknown config keyword: '%s'\n",
			 cf->name, cf->linenum, name);
		  has_fault = 1;
		  continue;
		}
	}
	if (has_fault) {
	  // Free allocated resources and link pointers, if any
	} else {
	  // Construct the digipeater
	}
}
