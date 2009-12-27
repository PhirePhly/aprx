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
	int hopsreq;
	int hopsdone;
	int tracereq;
	int tracedone;
	int digireq;
	int digidone;;

	int fixthis;
	int fixall;
	int probably_heard_direct;

	int     ax25addrlen;
	uint8_t ax25addr[90]; // 70 for address, a bit more for "body"
};


static char * tracewords[] = { "WIDE","TRACE","RELAY" };
static int tracewordlens[] = { 4, 5, 5 };
static const struct tracewide default_trace_param = {
	4, 4, 1,
	3,
	tracewords,
	tracewordlens
};
static char * widewords[] = { "WIDE","RELAY" };
static int widewordlens[] = { 4,5 };
static const struct tracewide default_wide_param = {
	4, 4, 0,
	2,
	widewords,
	widewordlens
};


/*
 * regex_filter_add() -- adds configured regular expressions
 *                       into forbidden patterns list.
 * 
 * These are actually processed on TNC2 format text line, and not
 * AX.25 datastream per se.
 */
static int regex_filter_add(struct configfile *cf,
			    struct digipeater_source *src,
			    char *param1,
			    char *str)
{
	int rc;
	int groupcode = -1;
	regex_t re, *rep;
	char errbuf[2000];

	if (strcmp(param1, "source") == 0) {
		groupcode = 0;
	} else if (strcmp(param1, "destination") == 0) {
		groupcode = 1;
	} else if (strcmp(param1, "via") == 0) {
		groupcode = 2;
	} else if (strcmp(param1, "data") == 0) {
		groupcode = 3;
	} else {
		printf("%s:%d Bad RE target: '%s'  must be one of: source, destination, via\n",
		       cf->name, cf->linenum, param1);
		return 1;
	}

	if (!*str) {
		printf("%s:%d Expected RE pattern missing or a NUL string.\n",
		       cf->name, cf->linenum);
		return 1;		/* Bad input.. */
	}

	param1 = str;
	str = config_SKIPTEXT(str, NULL); // Handle quoted string
	str = config_SKIPSPACE(str);

	memset(&re, 0, sizeof(re));
	rc = regcomp(&re, param1, REG_EXTENDED | REG_NOSUB);

	if (rc != 0) {		/* Something is bad.. */
		*errbuf = 0;
		regerror(rc, &re, errbuf, sizeof(errbuf));
		printf("%s:%d Bad POSIX RE input, error: %s\n",
		       cf->name, cf->linenum, errbuf);
		return 1;
	}

	/* param1 and str were processed successfully ... */

	rep = malloc(sizeof(*rep));
	*rep = re;

	switch (groupcode) {
	case 0:
		src->sourceregscount += 1;
		src->sourceregs =
			realloc(src->sourceregs,
				src->sourceregscount * sizeof(void *));
		src->sourceregs[src->sourceregscount - 1] = rep;
		break;
	case 1:
		src->destinationregscount += 1;
		src->destinationregs =
			realloc(src->destinationregs,
				src->destinationregscount * sizeof(void *));
		src->destinationregs[src->destinationregscount - 1] = rep;
		break;
	case 2:
		src->viaregscount += 1;
		src->viaregs = realloc(src->viaregs,
				       src->viaregscount * sizeof(void *));
		src->viaregs[src->viaregscount - 1] = rep;
		break;
	case 3:
		src->dataregscount += 1;
		src->dataregs =
			realloc(src->dataregs,
				src->dataregscount * sizeof(void *));
		src->dataregs[src->dataregscount - 1] = rep;
		break;
	}
	return 0; // OK state
}


static int match_tracewide(const char *via, const struct tracewide *twp)
{
	int i;
	if (twp == NULL) return 0;

	for (i = 0; i < twp->nkeys; ++i) {
		// if (debug>2) printf(" match:'%s'",twp->keys[i]);
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

static int count_single_tnc2_tracewide(struct digistate *state, const char *viafield, const int istrace, const int matchlen, const int viaindex)
{
	const char *p = viafield + matchlen;
	const char reqc = p[0];
	const char c    = p[1];
	const char remc = p[2];
	int req, done;

	int hasHflag = (strchr(viafield,'*') != NULL);

	// Non-matched case, may have H-bit flag
	if (matchlen == 0) {
	  /*
		state->hopsreq  += 0;
		state->hopsdone += 0;
		state->tracereq  += 0;
		state->tracedone += 0;
	  */
		state->digireq  += 1;
		state->digidone += hasHflag;
		if (viaindex == 2 && !hasHflag)
		  state->probably_heard_direct = 1;
		// if (debug>1) printf(" a[req=%d,done=%d,trace=%d]",0,0,hasHflag);
		return 0;
	}

	// Is the character following matched part one of: [1-7]
	if (!('1' <= reqc && reqc <= '7')) {
		// Not a digit, this is single matcher..
		state->hopsreq  += 1;
		state->hopsdone += hasHflag;
		if (istrace) {
		  state->tracereq  += 1;
		  state->tracedone += hasHflag;
		}
		if (viaindex == 2 && !hasHflag)
		  state->probably_heard_direct = 1;
		// if (debug>1) printf(" d[req=%d,done=%d]",1,hasHflag);
		return 0;
	}

	req = reqc - '0';

	if (c == '*' && remc == 0) { // WIDE1*
		state->hopsreq  += req;
		state->hopsdone += req;
		if (istrace) {
		  state->tracereq  += req;
		  state->tracedone += req;
		}
		// if (debug>1) printf(" e[req=%d,done=%d]",req,req);
		return 0;
	}
	if (c == 0) { // Bogus WIDE1 - uidigi puts these out.
		state->fixthis = 1;
		state->hopsreq  += req;
		state->hopsdone += req;
		if (istrace) {
		  state->tracereq  += req;
		  state->tracedone += req;
		}
		// if (debug>1) printf(" E[req=%d,done=%d]",req,req);
		return 0;
	}
	// Not WIDE1-
	if (c != '-') {
		state->hopsreq  += 1;
		state->hopsdone += hasHflag;
		if (istrace) {
		  state->tracereq  += 1;
		  state->tracedone += hasHflag;
		}
		// if (debug>1) printf(" f[req=%d,done=%d]",1,hasHflag);
		return 0;
	}

	// OK, it is "WIDEn-" plus "N"
	if ('0' <= remc  && remc <= '7' && p[3] == 0) {
	  state->hopsreq  += req;
	  done = req - (remc - '0');
	  state->hopsdone += done;
	  if (done < 0) {
	    // Something like "WIDE3-7", which is definitely bogus!
	    state->fixall = 1;
	    if (viaindex == 2 && !hasHflag)
	      state->probably_heard_direct = 1;
	    return 0;
	  }
	  if (istrace) {
	    state->tracereq  += req;
	    state->tracedone += done;
	  }
	  if (viaindex == 2) {
	    if (memcmp("TRACE",viafield,5)==0) // A real "TRACE" in first slot?
	      state->probably_heard_direct = 1;

	    else if (!hasHflag && req == done) // WIDE3-3 on first slot
	      state->probably_heard_direct = 1;
	  }
	  // if (debug>1) printf(" g[req=%d,done=%d%s]",req,done,hasHflag ? ",Hflag!":"");
	  return 0;

	} else if (('8' <= remc && remc <= '9' && p[3] == 0) ||
		   (remc == '1' && '0' <= p[3] && p[3] <= '5' && p[4] == 0)) {
	  // The request has SSID value in range of 8 to 15
	  state->fixall = 1;
	  if (viaindex == 2 && !hasHflag)
	    state->probably_heard_direct = 1;
	  return 0;

	} else {
	  // Yuck, impossible/syntactically invalid
	  state->hopsreq  += 1;
	  state->hopsdone += hasHflag;
	  if (istrace) {
	    state->tracereq  += 1;
	    state->tracedone += hasHflag;
	  }
	  if (viaindex == 2 && !hasHflag)
	    state->probably_heard_direct = 1;
	  // if (debug>1) printf(" h[req=%d,done=%d]",1,hasHflag);
	  return 1;
	}
}

static int match_transmitter(const char *viafield, const struct digipeater_source *src)
{
	struct aprx_interface *aif = src->parent->transmitter;
	int tlen = strlen(aif->callsign);

	if (memcmp(viafield, aif->callsign, tlen) == 0) {
	  if (viafield[tlen] == '*')
	    return 1;
	}

	return 0;
}

static int try_reject_filters(const int  fieldtype,
			      const char *viafield,
			      struct digipeater_source *src)
{
	int i;
	int stat = 0;
	switch (fieldtype) {
	case 0: // Source
		for (i = 0; i < src->sourceregscount; ++i) {
			stat = regexec(src->sourceregs[i],
				       viafield, 0, NULL, 0);
			if (stat == 0)
				return 1;	/* MATCH! */
		}
		if (memcmp("MYCALL",viafield,6)==0) return 1;
		if (memcmp("N0CALL",viafield,6)==0) return 1;
		if (memcmp("NOCALL",viafield,6)==0) return 1;
		break;
	case 1: // Destination

		for (i = 0; i < src->destinationregscount; ++i) {
			int stat = regexec(src->destinationregs[i],
					   viafield, 0, NULL, 0);
			if (stat == 0)
				return 1;	/* MATCH! */
		}
		if (memcmp("MYCALL",viafield,6)==0) return 1;
		if (memcmp("N0CALL",viafield,6)==0) return 1;
		if (memcmp("NOCALL",viafield,6)==0) return 1;
		break;
	case 2: // Via

		for (i = 0; i < src->viaregscount; ++i) {
			int stat = regexec(src->viaregs[i],
					   viafield, 0, NULL, 0);
			if (stat == 0)
				return 1;	/* MATCH! */
		}
		if (memcmp("MYCALL",viafield,6)==0) return 1;
		if (memcmp("N0CALL",viafield,6)==0) return 1;
		if (memcmp("NOCALL",viafield,6)==0) return 1;
		break;
	case 3: // Data

		for (i = 0; i < src->dataregscount; ++i) {
			int stat = regexec(src->dataregs[i],
					   viafield, 0, NULL, 0);
			if (stat == 0)
				return 1;	/* MATCH! */
		}
		break;
	default:
		if (debug)
		  printf("try_reject_filters(fieldtype=%d) - CODE BUG\n",
			 fieldtype);
		return 1;
	}
	if (stat != 0 && stat != REG_NOMATCH) {
		// Some odd reason for an error?
		
	}
	return 0;
}

/* Parse executed and requested WIDEn-N/TRACEn-N info */
static int parse_tnc2_hops(struct digistate *state, struct digipeater_source *src, struct pbuf_t *pb)
{
	const char *p = pb->dstcall_end+1;
	const char *s;
	char viafield[14];
	int have_fault = 0;
	int viaindex = 1; // First via index will be 2..
	int len;

	if (debug>1) printf(" hops count: %s ",p);

	len = pb->srccall_end - pb->data;
	if (len >= sizeof(viafield)) len = sizeof(viafield)-1;
	memcpy(viafield, pb->data, len);
	viafield[len] = 0;
	// if (debug>2)printf(" srccall='%s'",viafield);
	if (try_reject_filters(0, viafield, src)) {
	  if (debug>1) printf(" - Src filters reject\n");
	  return 1; // Src reject filters
	}

	len = pb->dstcall_end - pb->destcall;
	if (len >= sizeof(viafield)) len = sizeof(viafield)-1;
	memcpy(viafield, pb->destcall, len);
	viafield[len] = 0;
	// if (debug>2)printf(" destcall='%s'",viafield);
	if (try_reject_filters(1, viafield, src)) {
	  if (debug>1) printf(" - Dest filters reject\n");
	  return 1; // Dest reject filters
	}


	while (p < pb->info_start && !have_fault) {
	  len = 0;

	  for (s = p; s < pb->info_start; ++s) {
	    if (*s == ',' || *s == ':') {
	      break;
	    }
	  }
	  if (*s == ':') break; // End of scannable area
	  // [p..s] is now one VIA field.
	  if (s == p && *p != ':') {  // BAD!
	    have_fault = 1;
	    if (debug>1) printf(" S==P ");
	    break;
	  }
	  if (*p == 'q') break; // APRSIS q-constructs..
	  ++viaindex;

	  len = s-p;
	  if (len >= sizeof(viafield)) len = sizeof(viafield)-1;
	  memcpy(viafield, p, len);
	  viafield[len] = 0;
	  if (*s == ',') ++s;
	  p = s;
	  
	  // VIA-field picked up, now analyze it..

	  if (try_reject_filters(2, viafield, src)) {
	    if (debug>1) printf(" - Via filters reject\n");
	    return 1; // via reject filters
	  }

	  if (match_transmitter(viafield, src)) {
	    if (debug>1) printf(" - Tx match reject\n");
	    return 1; /* Oops, LOOP!  I have transmit this in past
			 (according to my transmitter callsign present
			 in a VIA field!)
		      */
	  }

	  if ((len = match_tracewide(viafield, src->src_trace))) {
	    have_fault = count_single_tnc2_tracewide(state, viafield, 1, len, viaindex);
	  } else if ((len = match_tracewide(viafield, src->parent->trace))) {
	    have_fault = count_single_tnc2_tracewide(state, viafield, 1, len, viaindex);
	  } else if ((len = match_tracewide(viafield, src->src_wide))) {
	    have_fault = count_single_tnc2_tracewide(state, viafield, 0, len, viaindex);
	  } else if ((len = match_tracewide(viafield, src->parent->wide))) {
	    have_fault = count_single_tnc2_tracewide(state, viafield, 0, len, viaindex);
	  } else {
	    // Account traced nodes (or some such)
	    have_fault = count_single_tnc2_tracewide(state, viafield, 1, 0, viaindex);
	  }
	  if (state->fixthis || state->fixall) {
	    // Argh..  bogus WIDEn seen, which is what UIDIGIs put out..
	    // Also some other broken requests are "fixed": like WIDE3-7
	    // Fixing it: We set the missing H-bit, and continue processing.
	    // (That fixing is done in incoming AX25 address field, which
	    //  we generally do not touch - with this exception.)
	    pb->ax25addr[ 7*viaindex + 6 ] |= 0x80;
	    state->fixthis = 0;
	  }
	}
	if (debug>1) printf(" req=%d,done=%d [%s,%s,%s]\n",
			    state->hopsreq,state->hopsdone,
			    have_fault ? "FAULT":"OK",
			    (state->hopsreq > state->hopsdone) ? "DIGIPEAT":"DROP",
			    (state->tracereq > state->tracedone) ? "TRACE":"WIDE");
	return have_fault;
}


static void free_tracewide(struct tracewide *twp)
{
	int i;

	if (twp == NULL) return;
	if (twp->keys) {
	  for (i = 0; i < twp->nkeys; ++i)
	    free((void*)(twp->keys[i]));
	  free(twp->keys);
	}
	if (twp->keylens)
	  free((void*)(twp->keylens));

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
		  // if (debug) printf(" maxreq %d\n",maxreq);

		} else if (strcmp(name,"maxdone") == 0) {
		  maxdone = atoi(param1);
		  // if (debug) printf(" maxdone %d\n",maxdone);

		} else if (strcmp(name,"keys") == 0) {
		  char *k = strtok(param1, ",");
		  for (; k ; k = strtok(NULL,",")) {
		    ++nkeys;
		    // if (debug) printf(" n=%d key='%s'\n",nkeys,k);
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
	int viscous_delay = 0;

	struct aprx_interface *source_aif = NULL;
	struct digipeater_source  *source = NULL;
	digi_relaytype          relaytype = DIGIRELAY_DIGIPEAT;
	struct filter_t          *filters = NULL;
	struct tracewide    *source_trace = NULL;
	struct tracewide     *source_wide = NULL;
	struct digipeater_source regexsrc;
	char                    *via_path = NULL;

	memset(&regexsrc, 0, sizeof(regexsrc));

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
				param1 = (char*)mycall;

			source_aif = find_interface_by_callsign(param1);
			if (source_aif == NULL) {
				has_fault = 1;
				printf("%s:%d digipeater source '%s' not found\n",
				       cf->name, cf->linenum, param1);
			}

		} else if (strcmp(name, "viscous-delay") == 0) {
			viscous_delay = atoi(param1);
			if (debug) printf(" viscous-delay = %d\n",viscous_delay);
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

		} else if (strcmp(name,"regex-filter") == 0) {
			if (regex_filter_add(cf, &regexsrc, param1, str)) {
			  has_fault = 1;
			}

		} else if (strcmp(name, "via-path") == 0) {

			// FIXME: validate that source callsign is "APRSIS"

			via_path  = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
			config_STRUPPER(via_path);

			if (debug)
				printf("via-path '%s' ", via_path);

		} else if (strcmp(name,"<trace>") == 0) {
			source_trace = digipeater_config_tracewide(cf, 1);

		} else if (strcmp(name,"<wide>") == 0) {
			source_wide  = digipeater_config_tracewide(cf, 0);

		} else if (strcmp(name,"filter") == 0) {
			if (filter_parse(&filters, param1)) {
				// Error in filter parsing
				has_fault = 1;
			} else {
			}

		} else if (strcmp(name,"relay-type") == 0 ||
			   strcmp(name,"relay-format") == 0) {
			config_STRLOWER(param1);
			if (strcmp(param1,"digipeat") == 0) {
			  relaytype = DIGIRELAY_DIGIPEAT;
			} else if (strcmp(param1,"digipeated") == 0) {
			  relaytype = DIGIRELAY_DIGIPEAT;
			} else if (strcmp(param1,"digipeater") == 0) {
			  relaytype = DIGIRELAY_DIGIPEAT;
			} else if (strcmp(param1,"directonly") == 0) {
			  relaytype = DIGIRELAY_DIGIPEAT_DIRECTONLY;
			} else if (strcmp(param1,"third-party") == 0) {
			  relaytype = DIGIRELAY_THIRDPARTY;
			} else if (strcmp(param1,"3rd-party") == 0) {
			  relaytype = DIGIRELAY_THIRDPARTY;
			} else {
			  printf("%s:%d Digipeater <source>'s %s did not recognize: '%s' \n", cf->name, cf->linenum, name, param1);
			  has_fault = 1;
			}
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
		source->via_path      = via_path;

		source->viscous_delay = viscous_delay;

		// RE pattern reject filters
		source->sourceregscount      = regexsrc.sourceregscount;
		source->sourceregs           = regexsrc.sourceregs;
		source->destinationregscount = regexsrc.destinationregscount;
		source->destinationregs      = regexsrc.destinationregs;
		source->viaregscount         = regexsrc.viaregscount;
		source->viaregs              = regexsrc.viaregs;
		source->dataregscount        = regexsrc.dataregscount;
		source->dataregs             = regexsrc.dataregs;
	} else {
		free_tracewide(source_trace);
		free_tracewide(source_wide);
		// filters_free(filters);
		// free regexsrc's allocations
	}

	return source;
}

void digipeater_config(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int has_fault = 0;
	int i;
	const int line0 = cf->linenum;

	struct aprx_interface *aif = NULL;
	int ratelimit = 60;
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
		if (strcmp(name, "transmit") == 0 ||
		    strcmp(name, "transmitter") == 0) {
			if (strcmp(param1,"$mycall") == 0)
				param1 = (char*)mycall;

			aif = find_interface_by_callsign(param1);
			if (aif != NULL && (!aif->txok)) {
			  aif = NULL; // Not 
			  printf("%s:%d This transmit interface has no TX-OK TRUE setting: '%s'\n",
				 cf->name, cf->linenum, param1);
			  has_fault = 1;
			} else if (aif != NULL && aif->txrefcount > 0) {
			  aif = NULL;
			  printf("%s:%d This transmit interface is being used on multiple <digipeater>s as transmitter: '%s'\n",
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
				ratelimit = 60;

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
	// Check that source definitions are unique
	for ( i = 0; i < sourcecount; ++i ) {
		int j;
		for (j = i+1; j < sourcecount; ++j) {
			if (sources[i]->src_if == sources[j]->src_if) {
				has_fault = 1;
				printf("%s:%d Two <source>s on this <digipeater> definition use same <interface>: '%s'\n",
				       cf->name, line0, sources[i]->src_if->callsign);
			}
		}
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

			src->src_if->digisources = realloc( src->src_if->digisources,
							    (src->src_if->digisourcecount +1) * (sizeof(void*)));
			src->src_if->digisources[src->src_if->digisourcecount] = src;
			src->src_if->digisourcecount += 1;
		}

		aif->txrefcount += 1; // Increment Tx usage Reference count.
		                      // We permit only one <digipeater> to
				      // use any given Tx-interface. (Rx:es
				      // permit multiple uses.)
		digi->transmitter   = aif;
		digi->ratelimit     = ratelimit;
		digi->dupechecker   = dupecheck_new();

		digi->trace         = (traceparam != NULL) ? traceparam : & default_trace_param;
		digi->wide          = (wideparam  != NULL) ? wideparam  : & default_wide_param;

		digi->sourcecount   = sourcecount;
		digi->sources       = sources;

		digis = realloc( digis, sizeof(void*) * (digi_count+1));
		digis[digi_count] = digi;
		++digi_count;
	}
}


static int decrement_ssid(uint8_t *ax25addr)
{
	int ssid = (ax25addr[6] >> 1) & 0x0F;
	if (ssid > 0)
	  --ssid;
	ax25addr[6] = (ax25addr[6] & 0xE1) | (ssid << 1);
	return ssid;
}


static void digipeater_receive_backend(struct digipeater_source *src, struct pbuf_t *pb)
{
	int len, viaindex;
	struct digistate state;
	struct digistate viastate;
	struct digipeater *digi = src->parent;
	char viafield[14];

	if (src->src_filters != NULL) {
	  int rc = filter_process(pb, src->src_filters);
	  if (rc != 1) {
	    if (debug)
	      printf("Source filtering rejected the packet.\n");
	    return;
	  }
	  if (debug)
	    printf("Source filtering accepted the packet.\n");
	}

	memset(&state,    0, sizeof(state));
	memset(&viastate, 0, sizeof(viastate));

	//  2) Verify that none of our interface callsigns does match any
	//     of already DIGIPEATED via fields! (fields that have H-bit set)
	//   ( present implementation: this digi's transmitter callsign is
	//     verified)

	// Parse executed and requested WIDEn-N/TRACEn-N info
	if (parse_tnc2_hops(&state, src, pb)) {
		// A fault was observed! -- tests include "not this transmitter"
		return;
	}

	if (pb->is_aprs) {

		if (state.probably_heard_direct) {
		  // Collect a decaying average of distances to stations?
		  //  .. could auto-beacon an aloha-circle - maybe
		  //  .. note: this does not get packets that have no VIA fields.
		  // Score of direct DX:es?
		  //  .. note: this does not get packets that have no VIA fields.
		} else {
		  if (src->src_relaytype == DIGIRELAY_DIGIPEAT_DIRECTONLY) {
		    // Source relaytype is DIRECTONLY, and this was not
		    // likely directly heard...
		    if (debug) printf("DIRECTONLY -mode, and packet is not probably direct heard.");
		    return;
		  }
		}
		// Keep score of all DX packets?

		if (try_reject_filters(3, pb->info_start, src)) {
			if (debug>1)
			  printf(" - Data body regexp filters reject\n");
			return; // data body regexp reject filters
		}

// FIXME: 3) aprsc style filters checking in service area of the packet..

	}

	// 4) Hop-count filtering:

	// APRSIS sourced packets have different rules than DIGIPEAT
	// packets...
	if (state.hopsreq <= state.hopsdone) {
	  // if (debug) printf(" No remaining hops to execute.\n");
	  return;
	}
	if (state.hopsreq   > digi->trace->maxreq  ||
	    state.hopsreq   > digi->wide->maxreq   ||
	    state.tracereq  > digi->trace->maxreq  ||
	    state.hopsdone  > digi->trace->maxdone ||
	    state.hopsdone  > digi->wide->maxdone  ||
	    state.tracedone > digi->trace->maxdone) {
	  if (debug) printf(" Packet exceeds digipeat limits\n");
	  if (!state.probably_heard_direct)
	    return;
	  else
	    state.fixall = 1;
	}

	// if (debug) printf(" Packet accepted to digipeat!\n");

	state.ax25addrlen = pb->ax25addrlen;
	memcpy(state.ax25addr, pb->ax25addr, pb->ax25addrlen);
	uint8_t *axaddr = state.ax25addr + 14;
	uint8_t *e      = state.ax25addr + state.ax25addrlen;

	// Search for first AX.25 VIA field that does not have H-bit set:
	viaindex = 1; // First via field is number 2
	for (; axaddr < e; axaddr += 7, ++viaindex) {
	  ax25_to_tnc2_fmtaddress(viafield, axaddr, 0);
	  // if (debug>1) printf(" via: %s", viafield);

	  // Initial parsing said that things are seriously wrong..
	  // .. and we will digipeat the packet with all H-bits set.
	  if (state.fixall) axaddr[6] |= 0x80;

	  if (!(axaddr[6] & 0x80)) // No "Has Been Digipeated" bit set
	    break;
	}

	switch (src->src_relaytype) {
	case DIGIRELAY_THIRDPARTY:
		// Effectively disable the digipeat modifying of address
		axaddr = e;
		break;
	case DIGIRELAY_DIGIPEAT:
		// Normal functionality
		break;
	default: ;
	}

	// Unprocessed VIA field found
	if (axaddr < e) {	// VIA-field of interest has been found

// FIXME: 5) / 6) Cross-frequency/cross-band digipeat may add a special
//                label telling that the message originated on other band

	  // 7) WIDEn-N treatment (as well as transmitter matching digi)
	  if (pb->digi_like_aprs) {
	    if ((len = match_tracewide(viafield, src->src_trace))) {
	      count_single_tnc2_tracewide(&viastate, viafield, 1, len, viaindex);
	    } else if ((len = match_tracewide(viafield, digi->trace))) {
	      count_single_tnc2_tracewide(&viastate, viafield, 1, len, viaindex);
	    } else if ((len = match_tracewide(viafield, src->src_wide))) {
	      count_single_tnc2_tracewide(&viastate, viafield, 0, len, viaindex);
	    } else if ((len = match_tracewide(viafield, digi->wide))) {
	      count_single_tnc2_tracewide(&viastate, viafield, 0, len, viaindex);
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

	  } else { // Not "digi_as_aprs" rules

	    if (strcmp(viafield,digi->transmitter->callsign) == 0) {
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

	  } else if (viastate.hopsreq > viastate.hopsdone) {
	    if (debug) printf(" VIA on %s!\n",viafield);
	    int newssid = decrement_ssid(axaddr);
	    if (newssid <= 0)
	      axaddr[6] |= 0x80; // Set H-bit
	  }
	}
	if (debug) {
	  uint8_t *u = state.ax25addr + state.ax25addrlen;
	  *u++ = 0;
	  *u++ = 0;
	  *u++ = 0;

	  char tbuf[2800];
	  int is_ui = 0, ui_pid = -1, frameaddrlen = 0, tnc2addrlen = 0;
	  int t2l = ax25_format_to_tnc( state.ax25addr, state.ax25addrlen+6,
					tbuf, sizeof(tbuf),
					& frameaddrlen, &tnc2addrlen,
					& is_ui, &ui_pid );
	  tbuf[t2l] = 0;
	  printf(" out-hdr: '%s' data='",tbuf);
	  fwrite(pb->ax25data+2, pb->ax25datalen-2,  // without Control+PID
		 1, stdout);
	  printf("'\n");
	}

	if (pb->is_aprs && rflogfile) {
	  // Essentially Debug logging.. to file
	  char tbuf[2800];
	  int is_ui = 0, ui_pid = -1, frameaddrlen = 0, tnc2addrlen = 0;
	  int t2l = ax25_format_to_tnc( state.ax25addr, state.ax25addrlen+6,
					tbuf, sizeof(tbuf),
					& frameaddrlen, &tnc2addrlen,
					& is_ui, &ui_pid );
	  tbuf[t2l] = 0;
	  if (sizeof(tbuf) - pb->ax25datalen > t2l && t2l > 0) {
	    // Have space for body too, skip leading Ctrl+PID bytes
	    memcpy(tbuf+t2l, pb->ax25data+2, pb->ax25datalen-2); // Ctrl+PID skiped
	    t2l += pb->ax25datalen-2; // tbuf size sans Ctrl+PID

	    rflog( digi->transmitter->callsign, 1, 0, tbuf, t2l );
	  }
	}

	// Feed to interface_transmit_ax25() with new header and body
	interface_transmit_ax25( digi->transmitter,
				 state.ax25addr, state.ax25addrlen,
				 (const char*)pb->ax25data, pb->ax25datalen );
}


void digipeater_receive( struct digipeater_source *src,
			 struct pbuf_t *pb,
			 const int do_3rdparty )
{
	// Below numbers like "4)" refer to Requirement Specification
	// paper chapter 2.6: Digipeater Rules

	//  The dupe-filter exists for APRS frames, possibly for some
	// selected UI frame types, and definitely not for CONS frames.

	if (debug)
	  printf("digipeater_receive() from %s, is_aprs=%d viscous_delay=%d\n",
		 src->src_if->callsign, pb->is_aprs, src->viscous_delay);

	if (pb->is_aprs) {

		const int source_is_transmitter = (src->src_if ==
						   src->parent->transmitter);

		// 1) Feed to dupe-filter (transmitter specific)
		//    If the dupe detector on this packet has reached
		//    count > 1, drop it.

		dupe_record_t *dupe = dupecheck_pbuf( src->parent->dupechecker,
						      pb, src->viscous_delay);
		if (dupe == NULL) return; // Oops.. allocation error!

		// 1.1) optional viscous delay!

		if (src->viscous_delay == 0) { // No delay, direct cases

			// First packet on direct source arrives here
			// with  seen = 1

			// 1.x) Analyze dupe checking

			if (dupe->seen > 1) {
			  // N:th direct packet, duplicate.
			  // Drop this direct packet.
			  if (debug>1)
			    printf("Seen this packet %d times\n",
				   dupe->delayed_seen + dupe->seen);
			  return;
			}

			if (dupe->seen == 1 && dupe->delayed_seen > 0 &&
			    dupe->pbuf == NULL) {
			  // First direct, but dupe record does not have
			  // pbuf anymore indicating that a delayed 
			  // handling did process it sometime in past.
			  // Drop this direct packet.
			  return;
			}

			if (dupe->seen == 1 && dupe->delayed_seen > 0 &&
			    dupe->pbuf != NULL) {
			  // First direct, and pbuf exists in dupe record.
			  // It was added first to viscous queue, and
			  // a bit latter came this direct one.
			  // Remove one from viscous queue, and proceed
			  // with direct processing.

			  pbuf_put(dupe->pbuf);
			  dupe->pbuf = NULL;
			  dupe = NULL; // Do not do  dupecheck_put() here!
			}

		} else {  // src->viscous_delay > 0

			// First packet on viscous source arrives here
			// with   dupe->delayed_seen = 1

			// Has this been seen on direct channel?
			if (dupe->seen > 0) {
			  // Already processed thru direct processing,
			  // no point in adding this to viscous delay queue
			  if (debug>1)
			    printf("Seen this packet %d times\n",
				   dupe->delayed_seen + dupe->seen);
			  return;
			}

			// Depending on source definition, the transmitter is
			// either non-viscous or viscous.  We care about it
			// only when the source is viscous:
			if (source_is_transmitter)
				dupe->seen_on_transmitter += 1;

			if (dupe->delayed_seen > 1) {
			  // 2nd or more of same packet from delayed source
			  if (debug>1)
			    printf("Seen this packet %d times\n",
				   dupe->delayed_seen + dupe->seen);

			  // If any of them is transmitter interface, then
			  // drop the queued packet, and drop current one.
			  if (dupe->seen_on_transmitter > 0) {

			    // If pbuf is on delayed queue, drop it.
			    if (dupe->pbuf != NULL) {
			      pbuf_put(dupe->pbuf);
			      dupe->pbuf = NULL;
			      dupe = NULL; // Do not do  dupecheck_put() here!
			    }

			  }
			  return;
			}

			// First time that we have seen this packet at all.
			// Put the pbuf_t on viscous delay queue.. (Put
			// this dupe_record_t there, and the pbuf_t pointer
			// is already in that dupe_record_t.)
			src->viscous_queue_size += 1;
			if (src->viscous_queue_size > src->viscous_queue_space) {
			  src->viscous_queue_space += 16;
			  src->viscous_queue = realloc( src->viscous_queue,
							sizeof(void*) *
							src->viscous_queue_space );
			}
			src->viscous_queue[ src->viscous_queue_size -1 ]
				= dupecheck_get(dupe);
			
			if (debug) printf("%ld ENTER VISCOUS QUEUE: len=%d pbuf=%p\n",
					  now, src->viscous_queue_size, pb);
			return; // Put on viscous queue

		} 
	}
	// Send directly to backend
	digipeater_receive_backend(src, pb);
}

dupecheck_t *digipeater_find_dupecheck(const struct aprx_interface *aif)
{
	int i;
	for (i = 0; i < digi_count; ++i) {
	  if (aif == digis[i]->transmitter)
	    return digis[i]->dupechecker;
	}
	return NULL;
}



// Viscous queue processing needs poll digis <source>s for delayed actions
int  digipeater_prepoll(struct aprxpolls *app)
{
	int d, s;
	time_t t;
	// Over all digipeaters..
	for (d = 0; d < digi_count; ++d) {
	  struct digipeater *digi = digis[d];
	  // Over all sources in those digipeaters
	  for (s = 0; s < digi->sourcecount; ++s) {
	    struct digipeater_source * src = digi->sources[s];
	    // If viscous delay is zero, there is no work...
	    // if (src->viscous_delay == 0)
	    //   continue;
	    // Delay is non-zero, perhaps there is work?
	    if (src->viscous_queue_size == 0) // Empty queue
	      continue;
	    // First entry expires first
	    t = src->viscous_queue[0]->t + src->viscous_delay;
	    if (app->next_timeout > t)
	      app->next_timeout = t;
	  }
	}

	return 0;
}

int  digipeater_postpoll(struct aprxpolls *app)
{
	int d, s, i, donecount;
	// Over all digipeaters..
	for (d = 0; d < digi_count; ++d) {
	  struct digipeater *digi = digis[d];
	  // Over all sources in those digipeaters
	  for (s = 0; s < digi->sourcecount; ++s) {
	    struct digipeater_source * src = digi->sources[s];
	    // If viscous delay is zero, there is no work...
	    // if (src->viscous_delay == 0)
	    //   continue;
	    // Delay is non-zero, perhaps there is work?
	    if (src->viscous_queue_size == 0) // Empty queue
	      continue;
	    // Feed backend from viscous queue
	    donecount = 0;
	    for (i = 0; i < src->viscous_queue_size; ++i) {
	      struct dupe_record_t *dupe = src->viscous_queue[i];
	      time_t t = dupe->t + src->viscous_delay;
	      if (t <= now) {
		if (debug)printf("%ld LEAVE VISCOUS QUEUE: dupe=%p pbuf=%p\n",
				 now, dupe, dupe->pbuf);
		if (dupe->pbuf != NULL) {
                  // We send the pbuf from viscous queue, if it still is
		  // present in the dupe record.  (For example direct sourced
		  // packets remove a packet from queued dupe record.)
                  digipeater_receive_backend(src, dupe->pbuf);

		  // Remove the delayed pbuf from this dupe record.
                  pbuf_put(dupe->pbuf);
                  dupe->pbuf = NULL;
                }
                dupecheck_put(dupe);
                ++donecount;
	      } else {
		break; // found a case we are not yet interested in.
	      }
	    }
	    if (donecount > 0) {
	      if (donecount >= src->viscous_queue_size) {
		// All cleared
		src->viscous_queue_size = 0;
	      } else {
		// Compact the queue left after this processing round
		i = src->viscous_queue_size - donecount;
		memcpy(&src->viscous_queue[0],
		       &src->viscous_queue[donecount],
		       sizeof(void*) * i);
		src->viscous_queue_size = i;
	      }
	    }
	  }
	}

	return 0;
}
