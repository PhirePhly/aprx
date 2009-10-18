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

struct digistate {
	int reqhops;
	int donehops;
	int tracereq;
	int tracedone;

	int traces;

	int           ax25addrlen;
	unsigned char ax25addr[90]; // 70 for address, a bit more for "body"
};


static char * tracewords[] = { "WIDE","TRACE","RELAY" };
static int tracewordlens[] = { 4, 5, 5 };
static struct tracewide default_trace_param = {
	4, 4, 1,
	3,
	&tracewords,
	&tracewordlens
};
static char * widewords[] = { "WIDE","RELAY" };
static int widewordlens[] = { 4,5 };
static struct tracewide default_wide_param = {
	4, 4, 0,
	2,
	&widewords,
	&widewordlens
};

static int match_tracewide(const char *via, struct tracewide *twp)
{
	int i;
	if (twp == NULL) return 0;

	for (i = 0; i < twp->nkeys; ++i) {
	  if (debug>2) printf(" match:'%s'",twp->keys[i]);
		if (memcmp(via, twp->keys[i], twp->keylens[i]) == 0) {
			return twp->keylens[i];
		}
	}
	return 0;
}

static int match_aliases(const char *via, struct aprx_interface *txif)
{
	int i;
	for (i = 0; i < txif->aliascount; ++i) {
		if (strcmp(via, txif->aliases[i]) == 0)
			return 1;
	}
	return 0;
}

static void count_single_tnc2_tracewide(struct digistate *state, const char *viafield, const int istrace, const int matchlen)
{
	const char *p = viafield + matchlen;
	const char reqc = p[0];
	const char c    = p[1];
	const char remc = p[2];
	int req, done;

	int hasHflag = (strchr(viafield,'*') != NULL);

	// Non-matched case, may have H-bit flag
	if (matchlen == 0) {
		state->reqhops  += 0;
		state->donehops += 0;
		state->traces   += hasHflag;
		if (debug>1) printf(" a[req=%d,done=%d,trace=%d]",0,0,hasHflag);
		return;
	}

/*  // Handle these two via transmitter aliases
	// WIDE*  ?
	if (reqc == '*' && c == 0) {
		state->reqhops  += 1;
		state->donehops += 1;
		if (istrace) {
		  state->tracereq  += 1;
		  state->tracedone += 1;
		}
		if (debug>1) printf(" b[req=%d,done=%d]",1,1);
		return;
	}
	// WIDE  ?
	if (reqc == 0) {
		state->reqhops  += 1;
		// state->donehops += 0;
		if (istrace) {
		  state->tracereq  += 1;
		  //state->tracedone += 1;
		}
		if (debug>1) printf(" c[req=%d,done=%d]",1,0);
		return;
	}
*/
	// Is the character following matched part one of: [1-7]
	if (!('1' <= reqc && reqc <= '7')) {
		// Not a digit, this is single matcher..
		state->reqhops  += 1;
		state->donehops += hasHflag;
		if (istrace) {
		  state->tracereq  += 1;
		  state->tracedone += hasHflag;
		}
		if (debug>1) printf(" d[req=%d,done=%d]",1,hasHflag);
		return;
	}

	req = reqc - '0';

	// WIDE1 or WIDE1*
	if (c == 0 || (c == '*' && remc == 0)) {
		state->reqhops  += req;
		state->donehops += req;
		if (istrace) {
		  state->tracereq  += req;
		  state->tracedone += req;
		}
		if (debug>1) printf(" e[req=%d,done=%d]",req,req);
		return;
	}
	// Not WIDE1-
	if (c != '-') {
		state->reqhops  += 1;
		state->donehops += hasHflag;
		if (istrace) {
		  state->tracereq  += 1;
		  state->tracedone += hasHflag;
		}
		if (debug>1) printf(" f[req=%d,done=%d]",1,hasHflag);
		return;
	}

	// OK, it is "WIDEn-" plus something
	if ('0' <= remc  && remc <= '7') {
	  state->reqhops  += req;
	  done = req - (remc - '0');
	  state->donehops += done;
	  if (istrace) {
	    state->tracereq  += req;
	    state->tracedone += done;
	  }
	  if (debug>1) printf(" g[req=%d,done=%d%s]",req,done,
			    hasHflag ? ",Hflag!":"");
	} else {
	  // Yuck, impossible/syntactically invalid
	  state->reqhops  += 1;
	  state->donehops += hasHflag;
	  if (istrace) {
	    state->tracereq  += 1;
	    state->tracedone += hasHflag;
	  }
	  if (debug>1) printf(" h[req=%d,done=%d]",1,hasHflag);
	}
}

/* Parse executed and requested WIDEn-N/TRACEn-N info */
static int parse_tnc2_hops(struct digistate *state, struct digipeater_source *src, struct pbuf_t *pb)
{
	const char *p = pb->dstcall_end+1;
	const char *s;
	char viafield[14];
	int have_fault = 0;

	if(debug>1) printf(" hops count: %s ",p);

	while (p < pb->info_start) {
	  for (s = p; s < pb->info_start; ++s) {
	    if (*s == ',' || *s == ':') {
	      break;
	    }
	  }
	  // p..s is now one VIA field.
	  if (s == p && *p != ':') {  // BAD!
	    have_fault = 1;
	    if (debug>1) printf(" S==P ");
	    break;
	  }
	  if (*p == 'q') break; // APRSIS q-constructs..

	  memcpy(viafield, p, s-p);
	  viafield[s-p] = 0;
	  if (*s == ',') ++s;
	  p = s;
	  
	  // VIA-field picked up, now analyze it..
	  for (;;) {
	    int len = 0;
	    if ((len = match_tracewide(viafield, src->src_trace))) {
	      count_single_tnc2_tracewide(state, viafield, 1, len);
	    } else if ((len = match_tracewide(viafield, src->parent->trace))) {
	      count_single_tnc2_tracewide(state, viafield, 1, len);
	    } else if ((len = match_tracewide(viafield, src->src_wide))) {
	      count_single_tnc2_tracewide(state, viafield, 0, len);
	    } else if ((len = match_tracewide(viafield, src->parent->wide))) {
	      count_single_tnc2_tracewide(state, viafield, 0, len);
	    } else {
	      // Account traced nodes (or some such)
	      count_single_tnc2_tracewide(state, viafield, 1, 0);
	    }
	    break;
	  }
	}
	if (debug>1) printf(" req=%d,done=%d [%s,%s,%s]\n",
			    state->reqhops,state->donehops,
			    have_fault ? "FAULT":"OK",
			    (state->reqhops > state->donehops) ? "DIGIPEAT":"DROP",
			    (state->tracereq > state->tracedone) ? "TRACE":"WIDE"
			    );
	return have_fault;
}


static void free_tracewide(struct tracewide *twp)
{
	int i;

	if (twp == NULL) return;
	if (twp->keys) {
	  for (i = 0; i < twp->nkeys; ++i)
	    free(twp->keys[i]);
	  free(twp->keys);
	}
	if (twp->keylens)
	  free(twp->keylens);

	free(twp);
}
static void free_source(struct digipeater_source *src)
{
	if (src == NULL) return;
	free(src);
}

static struct tracewide *digipeater_config_tracewide(struct configfile *cf, int is_trace)
{
	char  *name, *param1;
	char  *str       = cf->buf;
	int    has_fault = 0;
	int    nkeys     = 0;
	char **keywords  = NULL;
	int   *keylens   = NULL;
	int    maxreq    = 4;
	int    maxdone   = 4;
	struct tracewide *tw;

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
		if (strcmp(name,"maxreq") == 0) {
		  maxreq = atoi(param1);
		  if (debug) printf(" maxreq %d\n",maxreq);

		} else if (strcmp(name,"maxdone") == 0) {
		  maxdone = atoi(param1);
		  if (debug) printf(" maxdone %d\n",maxdone);

		} else if (strcmp(name,"keys") == 0) {
		  char *k = strtok(param1, ",");
		  for (; k ; k = strtok(NULL,",")) {
		    ++nkeys;
		    if (debug) printf(" n=%d key='%s'\n",nkeys,k);
		    keywords = realloc(keywords, sizeof(char*) * nkeys);
		    keywords[nkeys-1] = strdup(k);

		    keylens  = realloc(keylens,  sizeof(int) * nkeys);
		    keylens[nkeys-1] = strlen(k);
		  }

		} else {
		  has_fault = 1;
		}
	}

	if (has_fault) {
	  int i;
	  for (i = 0; i < nkeys; ++i)
	    free(keywords[i]);
	  if (keywords != NULL)
	    free(keywords);
	  if (keylens != NULL)
	    free(keylens);
	  return NULL;
	}

	tw = malloc(sizeof(*tw));
	memset(tw, 0, sizeof(*tw));

	tw->maxreq   = maxreq;
	tw->maxdone  = maxdone;
	tw->is_trace = is_trace;
	tw->nkeys    = nkeys;
	tw->keys     = keywords;
	tw->keylens  = keylens;

	return tw;
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
	struct tracewide    *source_trace = NULL;
	struct tracewide     *source_wide = NULL;

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

		} else if (strcmp(name,"<trace>") == 0) {
			source_trace = digipeater_config_tracewide(cf, 1);

		} else if (strcmp(name,"<wide>") == 0) {
			source_wide  = digipeater_config_tracewide(cf, 0);

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
		source->src_trace     = source_trace;
		source->src_wide      = source_wide;
	} else {
		free_tracewide(source_trace);
		free_tracewide(source_wide);
		// filters_free(filters);
	}

	return source;
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
		if (sources != NULL)
			free(sources);

		free_tracewide(traceparam);
		free_tracewide(wideparam);

	} else {
		// Construct the digipeater

		digi = malloc(sizeof(*digi));

		// up-link all interfaces used as sources
		for ( i = 0; i < sourcecount; ++i ) {
			struct digipeater_source *src = sources[i];
			src->parent = digi; // Set parent link

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


static int decrement_ssid(unsigned char *ax25addr)
{
	int ssid = (ax25addr[6] >> 1) & 0x0F;
	if (ssid > 0)
	  --ssid;
	ax25addr[6] = (ax25addr[6] & 0xE1) | (ssid << 1);
	return ssid;
}



void digipeater_receive(struct digipeater_source *src, struct pbuf_t *pb)
{
	int len;
	struct digistate state;
	struct digistate viastate;
	char viafield[14];
	struct digipeater *digi = src->parent;

	memset(&state,    0, sizeof(state));
	memset(&viastate, 0, sizeof(viastate));

	// if (debug) printf("digipeater_receive() from %s\n", src->src_if->callsign);

	// Parse executed and requested WIDEn-N/TRACEn-N info
	parse_tnc2_hops(&state, src, pb);

	// APRSIS sourced packets have different rules than DIGIPEAT
	// packets...
	if (state.reqhops <= state.donehops) {
	  // if (debug) printf(" No remaining hops to execute.\n");
	  return;
	}
	if (state.reqhops > digi->trace->maxreq ||
	    state.reqhops > digi->wide->maxreq ||
	    state.tracereq > digi->trace->maxreq ||
	    state.donehops > digi->trace->maxdone ||
	    state.donehops > digi->wide->maxdone ||
	    state.tracedone > digi->trace->maxdone) {
	  if (debug) printf(" Packet exceeds digipeat limits\n");
	  return;
	}

	// if (debug) printf(" Packet accepted to digipeat!\n");

	state.ax25addrlen = pb->ax25addrlen;
	memcpy(state.ax25addr, pb->ax25addr, pb->ax25addrlen);
	unsigned char *axaddr = state.ax25addr + 14;
	unsigned char *e      = state.ax25addr + state.ax25addrlen;

	// Search for first AX.25 VIA field that does not have H-bit set:
	for (; axaddr < e; axaddr += 7) {
	  ax25_to_tnc2_fmtaddress(viafield, axaddr, 0);
	  if (debug>1) printf(" via: %s", viafield);
	  if (!(axaddr[6] & 0x80)) // No "Has Been Digipeated" bit set
	    break;
	}

	if (axaddr < e) {	// VIA-field of interest has been found
	  if ((len = match_tracewide(viafield, src->src_trace))) {
	    count_single_tnc2_tracewide(&viastate, viafield, 1, len);
	  } else if ((len = match_tracewide(viafield, digi->trace))) {
	    count_single_tnc2_tracewide(&viastate, viafield, 1, len);
	  } else if ((len = match_tracewide(viafield, src->src_wide))) {
	    count_single_tnc2_tracewide(&viastate, viafield, 0, len);
	  } else if ((len = match_tracewide(viafield, digi->wide))) {
	    count_single_tnc2_tracewide(&viastate, viafield, 0, len);
	  } else if (strcmp(viafield,digi->transmitter->callsign) == 0) {
	    // Match on the transmitter callsign without the star.
	    // Treat it as a TRACE request.
	    int acont = axaddr[6] & 0x01; // save old address continuation bit
	    // Put the transmitter callsign in, and set the H-bit.
	    memcpy(axaddr, digi->transmitter->ax25call, 7);
	    axaddr[6] |= (0x80 | acont); // Set H-bit

	  } else if (match_aliases(viafield, digi->transmitter)) {
	    // Match on the aliases.
	    // Treat it as a TRACE request.
	    int acont = axaddr[6] & 0x01; // save old address continuation bit
	    // Put the transmitter callsign in, and set the H-bit.
	    memcpy(axaddr, digi->transmitter->ax25call, 7);
	    axaddr[6] |= (0x80 | acont); // Set H-bit

	  }

	  if (viastate.tracereq > viastate.tracedone) {
	    // if (debug) printf(" TRACE on %s!\n",viafield);
	    // Must move it up in memory to be able to put
	    // transmitter callsign in
	    int taillen = e-axaddr;
	    if (state.ax25addrlen >= 70) {
	      if (debug) printf(" TRACE overgrows the VIA fields!\n");
	      return;
	    }
	    memmove(axaddr+7, axaddr, taillen);
	    state.ax25addrlen += 7;

	    int newssid = decrement_ssid(axaddr+7);
	    if (newssid <= 0)
	      axaddr[6+7] |= 0x80; // Set H-bit
	    // Put the transmitter callsign in, and set the H-bit.
	    memcpy(axaddr, digi->transmitter->ax25call, 7);
	    axaddr[6] |= 0x80; // Set H-bit

	  } else if (viastate.reqhops > viastate.donehops) {
	    if (debug) printf(" VIA on %s!\n",viafield);
	    int newssid = decrement_ssid(axaddr);
	    if (newssid <= 0)
	      axaddr[6] |= 0x80; // Set H-bit
	  }
	}
	if (debug) {
	  unsigned char *u = state.ax25addr + state.ax25addrlen;
	  *u++ = 0;
	  *u++ = 0;
	  *u++ = 0;

	  char tbuf[2800];
	  int is_ui = 0, frameaddrlen = 0, tnc2addrlen = 0;
	  int t2l = ax25_format_to_tnc( state.ax25addr, state.ax25addrlen+6,
					tbuf, sizeof(tbuf),
					& frameaddrlen, &tnc2addrlen,
					& is_ui );
	  tbuf[t2l] = 0;
	  printf("\n out-hdr: '%s'\n",tbuf);
	}
}




int  digipeater_prepoll(struct aprxpolls *app) {
	return 0;
}
int  digipeater_postpoll(struct aprxpolls *app) {
	return 0;
}
