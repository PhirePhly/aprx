/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * IGATE:  Pass packets in between RF network and APRS-IS           *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"
#include <regex.h>

static regex_t **sourceregs;
static int sourceregscount;

static regex_t **destinationregs;
static int destinationregscount;

static regex_t **viaregs;
static int viaregscount;

static regex_t **dataregs;
static int dataregscount;

static dupecheck_t *aprsisdupe; /* for messages being sent TO APRSIS */


/*
 * igate start -- make TX-igate allocations and inits
 */
void igate_start()
{
	aprsisdupe = dupecheck_new();

}

/*
 * ax25_filter_add() -- adds configured regular expressions
 *                      into forbidden patterns list.
 * 
 * These are actually processed on TNC2 format text line, and not
 * AX.25 datastream per se.
 */
void ax25_filter_add(const char *p1, const char *p2)
{
	int rc;
	int groupcode = -1;
	regex_t re, *rep;
	char errbuf[2000];

	if (strcmp(p1, "source") == 0) {
		groupcode = 0;
	} else if (strcmp(p1, "destination") == 0) {
		groupcode = 1;
	} else if (strcmp(p1, "via") == 0) {
		groupcode = 2;
	} else if (strcmp(p1, "data") == 0) {
		groupcode = 3;
	} else {
		printf("Bad RE target: '%s'  must be one of: source, destination, via\n", p1);
		return;
	}

	if (!*p2)
		return;		/* Bad input.. */

	memset(&re, 0, sizeof(re));
	rc = regcomp(&re, p2, REG_EXTENDED | REG_NOSUB);

	if (rc != 0) {		/* Something is bad.. */
		if (debug) {
			*errbuf = 0;
			regerror(rc, &re, errbuf, sizeof(errbuf));
			printf("Bad POSIX RE input, error: %s\n", errbuf);
		}
	}

	/* p1 and p2 were processed successfully ... */

	rep = malloc(sizeof(*rep));
	*rep = re;

	switch (groupcode) {
	case 0:
		++sourceregscount;
		sourceregs =
			realloc(sourceregs,
				sourceregscount * sizeof(void *));
		sourceregs[sourceregscount - 1] = rep;
		break;
	case 1:
		++destinationregscount;
		destinationregs =
			realloc(destinationregs,
				destinationregscount * sizeof(void *));
		destinationregs[destinationregscount - 1] = rep;
		break;
	case 2:
		++viaregscount;
		viaregs = realloc(viaregs, viaregscount * sizeof(void *));
		viaregs[viaregscount - 1] = rep;
		break;
	case 3:
		++dataregscount;
		dataregs =
			realloc(dataregs, dataregscount * sizeof(void *));
		dataregs[dataregscount - 1] = rep;
		break;
	}
}

static const char *tnc2_verify_callsign_format(const char *t, int starok, const char *e)
{
	const char *s = t;

	for (; *s && s < e; ++s) {
		/* Valid station-id charset is:  [A-Z0-9] */
		int c = *s;
		if (!(('A' <= c && c <= 'Z') || ('0' <= c && c <= '9'))) {
			/* Not A-Z, 0-9 */
			break;
		}
	}
	/* Now *s can be any of: '>,*-:' */
	if (*s == '-') {
		/* Minus and digits.. */
		++s;
		if ('1' <= *s && *s <= '9')
			++s;
		if ('0' <= *s && *s <= '9')
			++s;
	}

	if (*s == '*' /* && starok */ )	/* Star is present at way too many
					   SRC and DEST addresses, it is not
					   limited to VIA fields :-(  */
		++s;

	if (s >= e) {
		if (debug)
			printf("callsign scanner ran over end of buffer");
		return NULL; /* Over the end-of-buffer */
	}
	if (s == t) {
		if (debug)
			printf("callsign format verify got bad character: '%c' in string: '%.20s'\n", *s, t);
		return NULL;	/* Too short ? */
	}

	if (*s != '>' && *s != ',' && *s != ':') {
		/* Terminates badly.. */
		if (debug)
			printf("callsign format verify got bad character: '%c' in string: '%.20s'\n", *s, t);
		return NULL;
	}

	return s;
}

static const char *tnc2_forbidden_source_stationid(const char *t, const char *e)
{
	int i;
	const char *s;

	s = tnc2_verify_callsign_format(t, 0, e);
	if (!s)
		return NULL;

	if (memcmp("WIDE", t, 4) == 0 ||	/* just plain wrong setting */
	    memcmp("RELAY", t, 5) == 0 ||	/* just plain wrong setting */
	    memcmp("TRACE", t, 5) == 0 ||	/* just plain wrong setting */
	    memcmp("TCPIP", t, 5) == 0 ||	/* just plain wrong setting */
	    memcmp("TCPXX", t, 5) == 0 ||	/* just plain wrong setting */
	    memcmp("N0CALL", t, 6) == 0 ||	/* TNC default setting */
	    memcmp("NOCALL", t, 6) == 0)	/* TNC default setting */
		return NULL;

	for (i = 0; i < sourceregscount; ++i) {
		int stat = regexec(sourceregs[i], t, 0, NULL, 0);
		if (stat == 0)
			return NULL;	/* MATCH! */
	}

	return s;
}

static const char *tnc2_forbidden_destination_stationid(const char *t, const char *e)
{
	int i;
	const char *s;

	s = tnc2_verify_callsign_format(t, 0, e);
	if (!s)
		return NULL;

	for (i = 0; i < destinationregscount; ++i) {
	  if (memcmp("TCPIP", t, 5) == 0 ||	/* just plain wrong */
	      memcmp("TCPXX", t, 5) == 0 ||	/* Forbidden to gate */
	      memcmp("NOGATE", t, 5) == 0 ||	/* Forbidden to gate */
	      memcmp("RFONLY", t, 5) == 0 ||	/* Forbidden to gate */
	      memcmp("N0CALL", t, 6) == 0 ||	/* TNC default setting */
	      memcmp("NOCALL", t, 6) == 0)	/* TNC default setting */
		return NULL;
		int stat = regexec(destinationregs[i], t, 0, NULL, 0);
		if (stat == 0)
			return NULL;	/* MATCH! */
	}

	return s;
}

static const char *tnc2_forbidden_via_stationid(const char *t, const char *e)
{
	int i;
	const char *s;

	s = tnc2_verify_callsign_format(t, 1, e);
	if (!s)
		return NULL;

	if (memcmp("RFONLY", t, 6) == 0 ||
	    memcmp("NOGATE", t, 6) == 0 ||
	    memcmp("TCPIP", t, 5)  == 0 ||
	    memcmp("TCPXX", t, 5)  == 0)
		return NULL;

	for (i = 0; i < viaregscount; ++i) {
		int stat = regexec(viaregs[i], t, 0, NULL, 0);
		if (stat == 0)
			return NULL;	/* MATCH! */
	}

	return s;
}

static int tnc2_forbidden_data(const char *t)
{
	int i;

	for (i = 0; i < dataregscount; ++i) {
		int stat = regexec(dataregs[i], t, 0, NULL, 0);
		if (stat == 0)
			return 1;	/* MATCH! */
	}

	return 0;
}

/* ---------------------------------------------------------- */

static void rflog(const char *portname, int tncid, int discard, const char *tnc2buf, int tnc2len) {
    if (rflogfile) {
	FILE *fp = fopen(rflogfile, "a");
    
	if (fp) {
		char timebuf[60];
		struct tm *t = gmtime(&now);
		strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);
	  
		fprintf(fp, "%s %s", timebuf, portname);
		if (tncid)
			fprintf(fp, "_%d", tncid);
		fprintf(fp, " ");
		if (discard < 0) {
			fprintf(fp, "*");
		}
		if (discard > 0) {
			fprintf(fp, "#");
		}
		fwrite( tnc2buf, tnc2len, 1, fp);
		fprintf( fp, "\n" );
		fclose(fp);
	}
    }
}

void verblog(const char *portname, int tncid, const char *tnc2buf, int tnc2len) {
    if (verbout) {
        printf("%ld\t%s", (long) now, portname);
	if (tncid)
	    printf("_%d", tncid);
	printf("\t#");
	fwrite(tnc2buf, tnc2len, 1, stdout);
	printf("\n");
    }
}

/*
 * The  tnc2_rxgate()  is actual RX-iGate filter function, and processes
 * prepated TNC2 format text presentation of the packet.
 * It does presume that the record is in a buffer that can be written on!
 */

void igate_to_aprsis(const char *portname, int tncid, char *tnc2buf, int tnc2len, int discard)
{
	char *t, *t0;
	const char *s;
	const char *e;

      redo_frame_filter:;

	t = tnc2buf;
	e = tnc2buf + tnc2len;
	t0 = NULL;

	/* t == beginning of the TNC2 format packet */

	/*
	 * If any of following matches, discard the packet!
	 * next if ($axpath =~ m/^WIDE/io); # Begins with = is sourced by..
	 * next if ($axpath =~ m/^RELAY/io);
	 * next if ($axpath =~ m/^TRACE/io);
	 */
	s = tnc2_forbidden_source_stationid(t, e);
	if (s)
		t = (char *) s;
	else {
		/* Forbidden in source fields.. */
		if (debug)
			printf("TNC2 forbidden source stationid: '%.20s'\n", t);
		goto discard;
	}

	/*  SOURCE>DESTIN,VIA,VIA:payload */

	if (*t == '>') {
		++t;
	} else {
		if (debug)
		    printf("TNC2 bad address format, expected '>', got: '%.20s'\n", t);
		goto discard;
	}

	s = tnc2_forbidden_destination_stationid(t, e);
	if (s)
		t = (char *) s;
	else {
		if (debug)
			printf("TNC2 forbidden (by REGEX) destination stationid: '%.20s'\n", t);
		goto discard;
	}

	while (*t && t < e) {
		if (*t == ':')
			break;
		if (*t == ',') {
			++t;
		} else {
			if (debug)
				printf("TNC2 via address syntax bug, wanted ',' or ':', got: '%.20s'\n", t);
			goto discard;
		}

		/*
		 *  next if ($axpath =~ m/RFONLY/io); # Has any of these in via fields..
		 *  next if ($axpath =~ m/TCPIP/io);
		 *  next if ($axpath =~ m/TCPXX/io);
		 *  next if ($axpath =~ m/NOGATE/io); # .. drop it.
		 */

		s = tnc2_forbidden_via_stationid(t, e);
		if (!s) {
			/* Forbidden in via fields.. */
			if (debug)
				printf("TNC2 forbidden VIA stationid, got: '%.20s'\n", t);
			goto discard;
		} else
			t = (char *) s;


	}
	/* Now we have processed the address, this should be ABORT time if
	   the current character is not ':' !  */
	if (*t == ':') {
#if 0
	  // *t++ = 0;	/* turn it to NUL character */
#else
	  /* Don't zero! */
	  ++t;
#endif
	  ;
	} else {
		if (debug)
			printf("TNC2 address parsing did not find ':':  '%.20s'\n",t);
		goto discard;
	}
	t0 = t;

	/* Now 't' points to data.. */


	if (tnc2_forbidden_data(t)) {
		if (debug)
			printf("Forbidden data in TNC2 packet - REGEX match");
		goto discard;
	}

	/* Will not relay messages that begin with '?' char: */
	if (*t == '?') {
		if (debug)
			printf("Will not relay packets where payload begins with '?'\n");
		goto discard;
	}

	/* Messages begining with '}' char are 3rd-party frames.. */
	if (*t == '}') {
		/* DEBUG OUTPUT TO STDOUT ! */
		verblog(portname, tncid, tnc2buf, tnc2len);
		rflog(portname, tncid, discard, tnc2buf, tnc2len);

		/* Copy the 3rd-party message content into begining of the buffer... */
		++t;				/* Skip the '}'		*/
		tnc2len = e - t;		/* New length		*/
		e = tnc2buf + tnc2len;		/* New end pointer	*/
		memcpy(tnc2buf, t, tnc2len);	/* Move the content	*/
		tnc2buf[tnc2len] = 0;

		/* .. and redo the filtering. */
		goto redo_frame_filter;
	}



	/* TODO: Verify message being of recognized APRS packet type */
	/*   '\0x60', '\0x27':  MIC-E, len >= 9
	 *   '!','=','/','{':   Normal or compressed location packet..
	 *   '$':               NMEA data, if it begins as '$GP'
	 *   '$':               WX data (maybe) if not NMEA data
	 *   ';':               Object data, len >= 31
	 *   ')':               Item data, len >= 18
	 *   ':':               message, bulletin or aanouncement, len >= 11
	 *   '<':               Station Capabilities, len >= 2
	 *   '>':               Status report
	 *   '}':               Third-party message
	 * ...  and many more ...
	 */

	// FIXME: Duplicate filter messages to APRSIS
	

	/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

	/*
	  printf("alen=%d  tlen=%d  tnc2buf=%s\n",t0-1-tnc2buf, e-t0, tnc2buf);
	*/
	discard = aprsis_queue(tnc2buf, t0-1-tnc2buf, portname, t0, e - t0); /* Send it.. */

	if (0) {
 discard:;

		discard = -1;
	}

	if (discard) {
		erlang_add(portname, ERLANG_DROP, tnc2len, 1);
	}


	/* DEBUG OUTPUT TO STDOUT ! */
	verblog(portname, tncid, tnc2buf, tnc2len);
	rflog(portname, tncid, discard, tnc2buf, tnc2len);
}



/* ---------------------------------------------------------- */


/*
 * Study APRS-IS received message's address header part
 * to determine if it is not to be relayed back to RF..
 */
static int forbidden_to_gate_addr(const char *s)
{
	if (memcmp(s, "TCPXX", 5) == 0)
	  return 1; /* Forbidden to be relayed */
	if (memcmp(s, "NOGATE", 6) == 0)
	  return 1; /* Forbidden to be relayed */
	if (memcmp(s, "RFONLY", 6) == 0)
	  return 1; /* Forbidden to be relayed */
	if (memcmp(s, "qAX", 3) == 0)
	  return 1;

	return 0; /* Found nothing forbidden */
}



/*
 * For APRSIS -> APRX -> RF gatewaying.
 * Have to convert incoming TNC2 format messge to AX.25..
 *
 * See:  http://www.aprs-is.net/IGateDetails.aspx
 *
 * ----------------------------------------------------------------
 * 
 *  Gate message packets and associated posits to RF if
 *
 *  1. the receiving station has been heard within range within
 *     a predefined time period (range defined as digi hops,
 *     distance, or both).
 *  2. the sending station has not been heard via RF within
 *     a predefined time period (packets gated from the Internet
 *     by other stations are excluded from this test).
 *  3. the sending station does not have TCPXX, NOGATE, or RFONLY
 *     in the header.
 *  4. the receiving station has not been heard via the Internet
 *     within a predefined time period.
 *
 * A station is said to be heard via the Internet if packets from
 * the station contain TCPIP* or TCPXX* in the header or if gated
 * (3rd-party) packets are seen on RF gated by the station and
 * containing TCPIP or TCPXX in the 3rd-party header (in other
 * words, the station is seen on RF as being an IGate).
 *
 * Gate all packets to RF based on criteria set by the sysop (such
 * as callsign, object name, etc.).
 *
 * ----------------------------------------------------------------
 *
 * TODO:
 *  a) APRS-IS relayed third-party frames are ignored.
 *
 *  3) The message path does not have TCPXX, NOGATE, RFONLY
 *     in it.
 *
 *  1) The receiving station has been heard recently
 *     within defined range limits, and more recently
 *     than since given interval T1. (Range as digi-hops [N1]
 *     or coordinates, or both.)
 *
 *  2) The sending station has not been heard via RF
 *     within timer interval T2. (Third-party relayed
 *     frames are not analyzed for this.)
 *
 *  4) the sending station has not been heard via the Internet
 *     within a predefined time period.
 *     A station is said to be heard via the Internet if packets
 *     from the station contain TCPIP* or TCPXX* in the header or
 *     if gated (3rd-party) packets are seen on RF gated by the
 *     station and containing TCPIP or TCPXX in the 3rd-party
 *     header (in other words, the station is seen on RF as being
 *     an IGate). 
 *
 * 5)  Gate all packets to RF based on criteria set by the sysop
 *     (such as callsign, object name, etc.).
 *
 * c)  Drop everything else.
 *
 *  Paths
 *
 * IGates should use the 3rd-party format on RF of
 * IGATECALL>APRS,GATEPATH}FROMCALL>TOCALL,TCPIP,IGATECALL*:original packet data
 * where GATEPATH is the path that the gated packet is to follow
 * on RF. This format will allow IGates to prevent gating the packet
 * back to APRS-IS.
 * 
 * q constructs should never appear on RF.
 * The I construct should never appear on RF.
 * Except for within gated packets, TCPIP and TCPXX should not be
 * used on RF.
 */

static void pick_heads(char *ax25, int headlen,
		       char *heads[20], int *headscount) 
{
	char *p = ax25;
	char *e = ax25 + headlen;

	char *p0 = p;
	if (debug)
	  printf(" head parse: ");
	while (p <= e) {
	  p0 = p;
	  while (p <= e) {
	    const char c = *p;
	    if (c != '>' && c != ',' && c != ':') {
	      ++p;
	      continue;
	    }
	    *p++ = 0;
	    if (*headscount >= 19)
	      continue; /* too many head parts.. */
	    heads[*headscount] = p0;
	    *headscount += 1;
	    if (debug) {
	      printf("  %-9s", p0);
	    }
	    break;
	  }
	}
	heads[*headscount] = NULL;
	if (debug)
	  printf("\n");
}

void igate_from_aprsis(const char *ax25, int ax25len)
{
	// const char *p = ax25;
	int colonidx;
	const char *b;
	// const char *e = p + ax25len; /* string end pointer */
	char  axbuf[2000]; /* enough and then some more.. */
	// char  axbuf2[1000]; /* enough and then some more.. */
	char  *heads[20];
	int    headscount = 0;

	if (ax25[0] == '#')
	  return; // Comment line, timer tick, something such spurious..

	if (ax25len > 520) {
	  /* Way too large a frame... */
	  return;
	}

	b = memchr(ax25, ':', ax25len);
	if (b == NULL) {
	  return; // Huh?  No double-colon on line, it is not proper packet line
	}

	colonidx = b-ax25;
	if (colonidx+3 >= ax25len) {
	  /* Not really any data there.. */
	  return;
	}

	memcpy(axbuf, ax25, ax25len);
	headscount = 0;
	pick_heads(axbuf, colonidx, heads, &headscount);

	int ok_to_relay = 0;
	int i;
	if (headscount < 4) {
	  // Less than 3 header fields coming from APRS-IS ?
	  if (debug)
	    printf("Not relayable packet! [1]\n");
	  return;
	}
	/*
	    if (strncmp(heads[1],"RXTLM-",6)==0) {
	      if (debug)
	        printf("Not relayable packet! [2]\n");
	      return;
	    }
	*/

	for (i = 0; i < headscount; ++i) {
	  /* 3) */
	  if (forbidden_to_gate_addr(heads[i])) {
	    if (debug)
	      printf("Not relayable packet! [3]\n");
	    return;
	  }

// FIXME: Hmm.. Really ??
	  if (heads[i][0] == 'q') {
	    if (strcmp(heads[i], "qAR") == 0) {
	      // qAR packets will be relayed
	      ok_to_relay = 1;
	      heads[i] = NULL;
	      break;
	    } else {
	      // Other than "qAR", not accepted.
	      ok_to_relay = 0;
	      break;
	    }
	  }
	}
	if (!ok_to_relay) {
	  if (debug)
	    printf("Not relayable packet! [4]\n");
	  return;
	}

	++b; /* Skip the ':' */

	/* a) */
	/* Check for forbidden things that cause dropping the packet */
	if (*b == '}') { /* Third-party packet from APRS-IS */
	  if (debug)
	    printf("Not relayable packet! [5]\n");
	  return; /* drop it */
	}

	/* 1) - verify receiving station has been heard recently on radio */
	

	/* 2) - sending station has not been heard recently on radio */
	

	/* 4) */
	

	/* f) */
	
	interface_receive_tnc2( &aprsis_interface, aprsis_interface.callsign,
				ax25, ax25len );
}

