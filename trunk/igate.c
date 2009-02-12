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

static int aprx_tx_igate_enabled;

static regex_t **sourceregs;
static int sourceregscount;

static regex_t **destinationregs;
static int destinationregscount;

static regex_t **viaregs;
static int viaregscount;

static regex_t **dataregs;
static int dataregscount;


/*
 * Enable tx-igate functionality.
 *
 */
void enable_tx_igate(const char *p1, const char *p2)
{
	aprx_tx_igate_enabled = 1;
	if (debug) {
	  printf("enable-tx-igate\n");
	}
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

static const char *tnc2_verify_callsign_format(const char *t, int starok)
{
	const char *s = t;

	for (; *s; ++s) {
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

static const char *tnc2_forbidden_source_stationid(const char *t)
{
	int i;
	const char *s;

	s = tnc2_verify_callsign_format(t, 0);
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

static const char *tnc2_forbidden_destination_stationid(const char *t)
{
	int i;
	const char *s;

	s = tnc2_verify_callsign_format(t, 0);
	if (!s)
		return NULL;

	for (i = 0; i < destinationregscount; ++i) {
		int stat = regexec(destinationregs[i], t, 0, NULL, 0);
		if (stat == 0)
			return NULL;	/* MATCH! */
	}

	return s;
}

static const char *tnc2_forbidden_via_stationid(const char *t)
{
	int i;
	const char *s;

	s = tnc2_verify_callsign_format(t, 1);
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
	s = tnc2_forbidden_source_stationid(t);
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

	s = tnc2_forbidden_destination_stationid(t);
	if (s)
		t = (char *) s;
	else {
		if (debug)
			printf("TNC2 forbidden (by REGEX) destination stationid: '%.20s'\n", t);
		goto discard;
	}

	while (*t) {
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

		s = tnc2_forbidden_via_stationid(t);
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
		*t++ = 0;	/* turn it to NUL character */
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
		if (verbout) {
			printf("%ld\t%s", (long) now, portname);
			if (tncid)
				printf("_%d", tncid);
			printf("\t#");
			printf("%s:%s\n", tnc2buf, t0); // t0 is not NULL
		}
		if (rflogfile) {
			FILE *fp = fopen(rflogfile, "a");
			if (fp) {
				char timebuf[60];
				struct tm *t = gmtime(&now);
				strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S",
					 t);

				fprintf(fp, "%s %s", timebuf, portname);
				if (tncid)
					fprintf(fp, "_%d", tncid);
				fprintf(fp, " #%s:%s\n", tnc2buf, t0); // t0 is not nULL
				fclose(fp);
			}
		}

		/* Copy the 3rd-party message content into begining of the buffer... */
		++t;				/* Skip the '}'		*/
		tnc2len = e - t;		/* New length		*/
		e = tnc2buf + tnc2len;		/* New end pointer	*/
		memcpy(tnc2buf, t, tnc2len);	/* Move the content	*/

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


	t += strlen(t);		/* To the end of the string */

	/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

	discard = aprsis_queue(tnc2buf, portname, t0, e - t0);	/* Send it.. */

	if (0) {
 discard:;

		discard = -1;
	}

	if (discard) {
		erlang_add(NULL, portname, tncid, ERLANG_DROP, tnc2len, 1);
	}


	/* DEBUG OUTPUT TO STDOUT ! */
	if (verbout) {
		printf("%ld\t%s", (long) now, portname);
		if (tncid)
			printf("_%d", tncid);
		printf("\t");
		if (discard < 0) {
			printf("*");
		};
		if (discard > 0) {
			printf("#");
		};
		printf("%s:%s\n", tnc2buf, t0 ? t0 : ""); // t0 can be NULL
	}
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
			fprintf(fp, "%s:%s\n", tnc2buf, t0 ? t0 : ""); // t0 can be NULL

			fclose(fp);
		}
	}
}


/* ---------------------------------------------------------- */


/*
 * Study APRS-IS received message's address header part
 * to determine if it is not to be relayed back to RF..
 */
static int forbidden_to_gate_addr(const char *s, const int len)
{
	int i = 0;
	const char *e = s + len;

	while ( s < e ) {
		const char *p = s;
		int l;
		while (p < e) {
			char c = *p;
			if (c == '>' ||
			    c == ',' ||
			    c == ':') {
				break;
			}
			++p;
		}
		l = p - s;
		
		if (memcmp(s, "TCPXX", 5) == 0)
		  return 1; /* Forbidden to be relayed */
		if (memcmp(s, "NOGATE", 6) == 0)
		  return 1; /* Forbidden to be relayed */
		if (memcmp(s, "RFONLY", 6) == 0)
		  return 1; /* Forbidden to be relayed */
		if (memcmp(s, "qAX", 3) == 0)
		  return 1;
		if (*p == ':')
		  return 0; /* Found nothing forbidden */
		++p;
		s = p;
	}
	return 0; /* Found nothing forbidden */
}



/*
 * For APRSIS -> APRX -> RF gatewaying.
 * Have to convert incoming TNC2 format messge to AX.25..
 *
 * See:  http://www.aprs-is.net/IGateDetails.aspx
 *
 * TODO:
 *  aa) APRS-IS relayed third-party frames are ignored.
 *
 *  ac) The message path does not have TCPXX, NOGATE, RFONLY
 *     in it.
 *
 *  a) The receiving station has been heard recently
 *     within defined range limits, and more recently
 *     than since given interval N1. (Range as digi-hops
 *     or coordinates, or both.)
 *
 *  b) The sending station has not been heard via RF
 *     within timer interval N2. (Third-party relayed
 *     frames are not analyzed for this.)
 *
 * [c moved upwards as 'ac']
 *
 *  d) the sending station has not been heard via the Internet
 *     within a predefined time period.
 *     A station is said to be heard via the Internet if packets
 *     from the station contain TCPIP* or TCPXX* in the header or
 *     if gated (3rd-party) packets are seen on RF gated by the
 *     station and containing TCPIP or TCPXX in the 3rd-party
 *     header (in other words, the station is seen on RF as being
 *     an IGate). 
 *
 * e)  Gate all packets to RF based on criteria set by the sysop
 *     (such as callsign, object name, etc.).
 *
 * f)  Drop everything else.
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

void igate_from_aprsis(const char *ax25, int ax25len)
{
	const char *p = ax25;
	int colonidx;
	const char *b;
	const char *e = p + ax25len; /* string end pointer */
	char  axbuf[1000]; /* enough and then some more.. */

	if (!aprx_tx_igate_enabled) {
	  /* Not enabled */
	  return;
	}
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
	++b; /* Skip the ':' */

	/* aa) */
	/* Check for forbidden things that cause dropping the packet */
	if (*b == '}') /* Third-party packet from APRS-IS */
		return; /* drop it */

	/* ab) */
	if (forbidden_to_gate_addr(p, colonidx))
		return;

	/* a) */
	/* b) */
	/* d) */
	/* e) */
	/* f) */


	// netax25_sendax25(buf,len);
}

