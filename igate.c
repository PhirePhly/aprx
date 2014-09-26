/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation APRS iGate and digi with                 *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 * IGATE:  Pass packets in between RF network and APRS-IS           *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"


const char *tnc2_verify_callsign_format(const char *t, int starok, int strictax25, const char *e)
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
		if (strictax25) {
		  if ('1' <= *s && *s <= '9')
		    ++s;
		  if ('0' <= *s && *s <= '9')
		    ++s;
		} else {
		  // Up to 2 of any alphanumeric
		  if (('0' <= *s && *s <= '9') ||
		      ('a' <= *s && *s <= 'z') ||
		      ('A' <= *s && *s <= 'Z'))
		    ++s;
		  if (('0' <= *s && *s <= '9') ||
		      ('a' <= *s && *s <= 'z') ||
		      ('A' <= *s && *s <= 'Z'))
		    ++s;
		}
	}

	if (*s == '*' /* && starok */ )	/* Star is present at way too many
					   SRC and DEST addresses, it is not
					   limited to VIA fields :-(  */
		++s;

	if (s > e) {
		if (debug)
			printf("callsign scanner ran over end of buffer");
		return NULL; /* Over the end-of-buffer */
	}
	if (s == t) {
		if (debug)
		  printf("%s callsign format verify got bad character: '%c' in string: '%.20s'\n", strictax25 ? "Strict":"Lenient", *s, t);
		return NULL;	/* Too short ? */
	}

	if (*s != '>' && *s != ',' && *s != ':' && *s != 0) {
		/* Terminates badly.. */
		if (debug)
			printf("%s callsign format verify got bad character: '%c' in string: '%.20s'\n", strictax25 ? "Strict":"Lenient", *s, t);
		return NULL;
	}

	return s;
}

#ifndef DISABLE_IGATE


/*
 * igate start -- make TX-igate allocations and inits
 */
void igate_start()
{
	// Always relay all traffic from RF to APRSIS, other
	// direction is handled per transmitter interface...
	// enable_aprsis_rx_dupecheck();
}

static const char *tnc2_forbidden_source_stationid(const char *t, const int strictax25,const char *e)
{
	const char *s;

	s = tnc2_verify_callsign_format(t, 0, strictax25, e);
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

	return s;
}

static const char *tnc2_forbidden_destination_stationid(const char *t, const int strictax25, const char *e)
{
	const char *s;

	s = tnc2_verify_callsign_format(t, 0, strictax25, e);
	if (!s)
		return NULL;

	if (memcmp("TCPIP", t, 5) == 0 ||	/* just plain wrong */
	    memcmp("TCPXX", t, 5) == 0 ||	/* Forbidden to gate */
	    memcmp("NOGATE", t, 5) == 0 ||	/* Forbidden to gate */
	    memcmp("RFONLY", t, 5) == 0 ||	/* Forbidden to gate */
	    memcmp("N0CALL", t, 6) == 0 ||	/* TNC default setting */
	    memcmp("NOCALL", t, 6) == 0)	/* TNC default setting */
		return NULL;

	return s;
}

static const char *tnc2_forbidden_via_stationid(const char *t, const int strictax25, const char *e)
{
	const char *s;

	s = tnc2_verify_callsign_format(t, 1, strictax25, e);
	if (!s)
		return NULL;

	if (memcmp("RFONLY", t, 6) == 0 ||
	    memcmp("NOGATE", t, 6) == 0 ||
	    memcmp("TCPIP", t, 5)  == 0 ||
	    memcmp("TCPXX", t, 5)  == 0)
		return NULL;

	return s;
}

/*
static int tnc2_forbidden_data(const char *t)
{
	int i;

	for (i = 0; i < dataregscount; ++i) {
		int stat = regexec(dataregs[i], t, 0, NULL, 0);
		if (stat == 0)
			return 1;	// MATCH!
	}

	return 0;
}
*/

void verblog(const char *portname, int istx, const char *tnc2buf, int tnc2len) {
    if (verbout) {
        printf("%ld\t%-9s ", (long) tick.tv_sec, portname);
	printf("%s \t", istx ? "T":"R");
	fwrite(tnc2buf, tnc2len, 1, stdout);
	printf("\n");
    }
}

/*
 * The  tnc2_rxgate()  is actual RX-iGate filter function, and processes
 * prepated TNC2 format text presentation of the packet.
 * It does presume that the record is in a buffer that can be written on!
 */

void igate_to_aprsis(const char *portname, const int tncid, const char *tnc2buf, int tnc2addrlen, int tnc2len, const int discard0, const int strictax25_)
{
	const char *tp, *t, *t0;
	const char *s;
	const char *ae;
	const char *e;
	int discard = discard0;
	int strictax25 = strictax25_; // Callsigns per strict AX25 (not 3rd-party)

	tp = tnc2buf;           // 3rd-party recursion moves tp
	ae = tp + tnc2addrlen;  // 3rd-party recursion moves ae
	e  = tp + tnc2len;      // stays the same all the time

	redo_frame_filter:;

	t  = tp;
	t0 = NULL;

	/* t == beginning of the TNC2 format packet */

	/*
	 * If any of following matches, discard the packet!
	 * next if ($axpath =~ m/^WIDE/io); # Begins with = is sourced by..
	 * next if ($axpath =~ m/^RELAY/io);
	 * next if ($axpath =~ m/^TRACE/io);
	 */
	s = tnc2_forbidden_source_stationid(t, strictax25, e);
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

	s = tnc2_forbidden_destination_stationid(t, strictax25, e);
	if (s)
		t = (char *) s;
	else {
		if (debug)
			printf("TNC2 forbidden (by REGEX) destination stationid: '%.20s'\n", t);
		goto discard;
	}

	while (*t && t < ae) {
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

		s = tnc2_forbidden_via_stationid(t, strictax25, e);
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
	t0 = t;  // Start of payload

	/* Now 't' points to data.. */


/*
	if (tnc2_forbidden_data(t)) {
		if (debug)
			printf("Forbidden data in TNC2 packet - REGEX match");
		goto discard;
	}
*/

	/* Will not relay messages that begin with '?' char: */
	if (*t == '?') {
		if (debug)
			printf("Will not relay packets where payload begins with '?'\n");
		goto discard;
	}

	/* Messages begining with '}' char are 3rd-party frames.. */
	if (*t == '}') {
		/* DEBUG OUTPUT TO STDOUT ! */
		verblog(portname, 0, tp, tnc2len);

		strictax25 = 0;
		/* Copy the 3rd-party message content into begining of the buffer... */
		++t;				/* Skip the '}'		*/
		tp = t;
		tnc2len = e - t;		/* New length		*/
		// end pointer (e) does not change
		// Address end must be searched again
		ae = memchr(tp, ':', tnc2len);
		if (ae == NULL) {
		  // Bad 3rd-party frame
		  goto discard;
		}
		tnc2addrlen = (int)(ae - tp);

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
	discard = aprsis_queue(tp, tnc2addrlen, qTYPE_IGATED, portname, t0, e - t0); /* Send it.. */
	/* DEBUG OUTPUT TO STDOUT ! */
	verblog(portname, 0, tp, tnc2len);

	if (0) {
 discard:;

		discard = -1;
	}

	if (discard) {
		erlang_add(portname, ERLANG_DROP, tnc2len, 1);
                rflog(portname, 'd', discard, tp, tnc2len);
	} else {
                rflog(portname, 'R', discard, tp, tnc2len);
	}
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
 * Following steps are done in  interface_receive_3rdparty()
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
 *  4) the receiving station has not been heard via the Internet
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
	// if (debug)printf(" head parse: ");
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
	    // if (debug) printf("  %-9s", p0);
	    break;
	  }
	}
	heads[*headscount] = NULL;
	// if (debug)printf("\n");
}

static void aprsis_commentframe(const char *tnc2buf, int tnc2len) {
  // TODO .. #TICK -> #TOCK  ??
}

void igate_from_aprsis(const char *ax25, int ax25len)
{
	// const char *p = ax25;
	int colonidx;
	int i;
	const char *b;
	// const char *e = p + ax25len; /* string end pointer */
//	char  axbuf[3000]; /* enough and then some more.. */
	// char  axbuf2[1000]; /* enough and then some more.. */
	char  *heads[20];
	char  *headsbuf;
	int    headscount = 0;
//	char  *s;
	char  *fromcall  = NULL;
	char  *origtocall = NULL;

	if (ax25[0] == '#') {  // Comment line, timer tick, something such...
          aprsis_commentframe(ax25, ax25len);
	  return;
        }

	if (ax25len > 520) {
	  /* Way too large a frame... */
	  if (debug)printf("APRSIS dataframe length is too large! (%d)\n",ax25len);
	  return;
	}

	b = memchr(ax25, ':', ax25len);
	if (b == NULL) {
	  if (debug)printf("APRSIS dataframe does not have ':' in it\n");
	  return; // Huh?  No double-colon on line, it is not proper packet line
	}

	colonidx = b-ax25;
	if (colonidx+3 >= ax25len) {
	  /* Not really any data there.. */
	  if (debug)printf("APRSIS dataframe too short to contain anything\n");
	  return;
	}

	rflog("APRSIS",'R',0,ax25, ax25len);

	headsbuf = alloca(colonidx+1);
	memcpy(headsbuf, ax25, colonidx+1);

	headscount = 0;
	pick_heads(headsbuf, colonidx, heads, &headscount);

	if (headscount < 4) {
	  // Less than 3 header fields coming from APRS-IS ?
	  if (debug)
	    printf("Not relayable packet! [1]\n");
	  return;
	}

	if (memcmp(heads[1],"RXTLM-",6)==0) {
	  if (debug)
	    printf("Not relayable packet! [2]\n");
	  return;
	}

	fromcall   = heads[0];
	origtocall = heads[1];
	for (i = 0; i < headscount; ++i) {
	  /* 3) */
	  if (forbidden_to_gate_addr(heads[i])) {
	    if (debug)
	      printf("Not relayable packet! [3]: %s\n", heads[i]);
	    return;
	  }

	}

	++b; /* Skip the ':' */

	/* a) */
	/* Check for forbidden things that cause dropping the packet */
	if (*b == '}') { /* Third-party packet from APRS-IS */
	  if (debug)
	    printf("Not relayable packet! [5]\n");
	  return; /* drop it */
	}

	// Following logic steps are done in  interface_receive_3rdparty!

	// FIXME: 1) - verify receiving station has been heard recently on radio
	// FIXME: 2) - sending station has not been heard recently on radio
	// FIXME: 4) - the receiving station has not been heard via the Internet within a predefined time period.
	// FIXME: f) - ??

	if (debug) printf(".. igate from aprsis\n");

	interface_receive_3rdparty( &aprsis_interface,
				    heads, headscount, "TCPIP",
				    b, ax25len - (b-ax25) );
}

#endif
