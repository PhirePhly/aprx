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
static struct digipeater **digis;


static struct tracewide default_trace_param = {
	4, 4, 
	3, { "WIDE","TRACE","RELAY" }
};
static struct tracewide default_wide_param = {
	4, 4, 
	1, { "WIDE" }
};

int  digipeater_prepoll(struct aprxpolls *app) {
	return 0;
}
int  digipeater_postpoll(struct aprxpolls *app) {
	return 0;
}


void digipeater_receive(struct digipeater_source *src, struct pbuf_t *pb)
{
	int i;

	if (debug)
	  printf("digipeater_receive() from %s\n", src->src_if->callsign);
}



static void free_tracewide(struct tracewide *twp)
{
	if (twp == NULL) return;
	free(twp);
}
static void free_source(struct digipeater_source *src)
{
	if (src == NULL) return;
	free(src);
}

static struct digipeater_source *digipeater_config_source(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int has_fault = 0;

	struct aprx_interface *source_aif = NULL;
	struct digipeater_source  *source = NULL;
	digi_relaytype          relaytype = DIGIRELAY_UNSET;
	struct aprx_filter       *filters = NULL;

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

		if (strcmp(name, "</source>") == 0) {
			break;

			// ... actual parameters
		} else if (strcmp(name,"source") == 0) {
			if (debug)
			  printf("%s:%d <source> source = '%s'\n",
				 cf->name, cf->linenum, param1);

			if (strcmp(param1,"$mycall") == 0)
				param1 = strdup(mycall);

			source_aif = find_interface_by_callsign(param1);
			if (source_aif == NULL) {
				has_fault = 1;
				printf("%s:%d digipeater source '%s' not found\n",
				       cf->name, cf->linenum, param1);
			}

		} else if (strcmp(name,"filter") == 0) {
		} else if (strcmp(name,"relay-format") == 0) {
		} else {
			has_fault = 1;
		}
	}

	if (!has_fault && (source_aif != NULL)) {
		source = malloc(sizeof(*source));
		memset(source, 0, sizeof(*source));
		
		source->src_if        = source_aif;
		source->src_relaytype = relaytype;
		source->src_filters   = filters;
	}

	return source;
}

static struct tracewide *digipeater_config_tracewide(struct configfile *cf, int is_trace)
{
	char *name, *param1;
	char *str = cf->buf;
	int has_fault = 0;


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

		if (is_trace) {
		  if (strcmp(name, "</trace>") == 0) {
		    break;
		  }
		} else {
		  if (strcmp(name, "</wide>") == 0) {
		    break;
		  }
		}

		// ... actual parameters
	}


	return NULL;
}

void digipeater_config(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int has_fault = 0;
	int i;

	struct aprx_interface *aif = NULL;
	int ratelimit = 300;
	int viscous_delay = 0;
	int sourcecount = 0;
	struct digipeater_source **sources = NULL;
	struct digipeater *digi = NULL;
	struct tracewide *traceparam = NULL;
	struct tracewide *wideparam  = NULL;

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
			if (strcmp(param1,"$mycall") == 0)
			  param1 = strdup(mycall);

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
			traceparam = digipeater_config_tracewide(cf, 1);
			if (traceparam == NULL)
				has_fault = 1;

		} else if (strcmp(name, "<wide>") == 0) {
			wideparam = digipeater_config_tracewide(cf, 0);
			if (wideparam == NULL)
				has_fault = 1;

		} else if (strcmp(name, "<source>") == 0) {
			struct digipeater_source *src =
				digipeater_config_source(cf);
			if (src != NULL) {
				// Found a source, link it!
				sources = realloc(sources, sizeof(void*) * (sourcecount+1));
				sources[sourcecount] = src;
				++sourcecount;
			} else {
				has_fault = 1;
			}

		} else {
		  printf("%s:%d Unknown config keyword: '%s'\n",
			 cf->name, cf->linenum, name);
		  has_fault = 1;
		  continue;
		}
	}

	if (aif == NULL && !has_fault) {
		printf("%s:%d Digipeater defined without transmit interface.\n",
		       cf->name, cf->linenum);
		has_fault = 1;
	}
	if (sourcecount == 0 && !has_fault) {
		printf("%s:%d Digipeater defined without <source>:s.\n",
		       cf->name, cf->linenum);
		has_fault = 1;
	}

	if (has_fault) {
		// Free allocated resources and link pointers, if any
		for ( i = 0; i < sourcecount; ++i ) {
			free_source(sources[i]);
		}
		free(sources);
		free_tracewide(traceparam);
		free_tracewide(wideparam);

	} else {
		// Construct the digipeater

		digi = malloc(sizeof(*digi));

		// up-link all interfaces used as sources
		for ( i = 0; i < sourcecount; ++i ) {
			struct digipeater_source *src = sources[i];

			src->src_if->digipeaters = realloc( src->src_if->digipeaters,
							    (src->src_if->digicount +1) * (sizeof(void*)));
			src->src_if->digipeaters[src->src_if->digicount] = src;
			src->src_if->digicount += 1;
		}
		
		digi->transmitter   = aif;
		digi->ratelimit     = ratelimit;
		digi->viscous_delay = viscous_delay;
		digi->viscous_queue = NULL;

		digi->trace         = (traceparam != NULL) ? traceparam : & default_trace_param;
		digi->wide          = (wideparam  != NULL) ? wideparam  : & default_wide_param;

		digi->sourcecount   = sourcecount;
		digi->sources       = sources;


		digis = realloc( digis, sizeof(void*) * (digi_count+1));
		digis[digi_count] = digi;
		++digi_count;
	}
}
