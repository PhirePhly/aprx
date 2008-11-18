/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007,2008                            *
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


/*
 * --
 * C0 00
 * 82 A0 B4 9A 88 A4 60
 * 9E 90 64 90 A0 9C 72
 * 9E 90 64 A4 88 A6 E0
 * A4 8C 9E 9C 98 B2 61
 * 03 F0
 * 21 36 30 32 39 2E 35 30 4E 2F 30 32 35 30 35 2E 34 33 45 3E 20 47 43 53 2D 38 30 31 20
 * C0
 * --
 */

static int ax25_to_tnc2_fmtaddress(char *dest, const unsigned char *src,
				   int markflag)
{
	int i, c;

	/* We really should verify that  */

	/* 6 bytes of station callsigns in shifted ASCII format.. */
	for (i = 0; i < 6; ++i, ++src) {
		c = *src;
		if (c & 1)
			return (-(int) (c));	/* Bad address-end flag ? */

		/* Don't copy spaces or 0 bytes */
		if (c != 0 && c != 0x40)
			*dest++ = c >> 1;
	}
	/* 7th byte carries SSID et.al. bits */
	c = *src;
	if ((c >> 1) % 16) {	/* don't print SSID==0 value */
		dest += sprintf(dest, "-%d", (c >> 1) % 16);
	}

	if ((c & 0x80) && markflag) {
		*dest++ = '*';	/* Has been repeated, or some such.. */
	}
	*dest = 0;

	return c;
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

void tnc2_rxgate(const char *portname, int tncid, char *tnc2buf, int tnc2len, int discard)
{
	char *t, *t0;
	const char *s;
	const char *e;

      redo_frame_filter:;

	t = tnc2buf;
	e = tnc2buf + tnc2len;
	t0 = t; /* will be reset to packet content latter.. */

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
		discard = -1;
	}

	/*  SOURCE>DESTIN,VIA,VIA:payload */

	if (*t == '>') {
		++t;
	} else {
		if (debug)
		    printf("TNC2 bad address format, expected '>', got: '%.20s'\n", t);
		discard = -1;
	}

	s = tnc2_forbidden_destination_stationid(t);
	if (s)
		t = (char *) s;
	else {
		if (debug)
			printf("TNC2 forbidden (by REGEX) destination stationid: '%.20s'\n", t);
		discard = -1;
	}

	while (*t) {
		if (*t == ':')
			break;
		if (*t == ',') {
			++t;
		} else {
			if (debug)
				printf("TNC2 via address syntax bug, wanted ',' or ':', got: '%.20s'\n", t);
			discard = -1;
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
			discard = -1;
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
		discard = -1;
	}
	t0 = t;

	if (discard) {
	    goto discard;
	}

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
			printf("%s:%s\n", tnc2buf, t0);
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
				fprintf(fp, " #%s:%s\n", tnc2buf, t0);
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

	if (discard)
		erlang_add(NULL, portname, tncid, ERLANG_DROP,
			   (int) (t - t0), 1);


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
		printf("%s:%s\n", tnc2buf, t0);
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
			fprintf(fp, "%s:%s\n", tnc2buf, t0);

			fclose(fp);
		}
	}
}


int parse_ax25addr(unsigned char ax25[7], const char *text, int ssidflags)
{
	int i = 0;
	int ssid = 0;
	char c;

	while (i < 6) {
		c = *text;

		if (c == '-' || c == '\0')
			break;

		ax25[i] = c << 1;

		++text;
		++i;
	}

	while (i < 6) {
		ax25[i] = ' ' << 1;
		++i;
	}

	if (*text != '\0') {
		++text;
		if (sscanf(text, "%d", &ssid) != 1 || ssid < 0
		    || ssid > 15) {
			return -1;
		}
	}

	ax25[6] = (ssid << 1) | ssidflags;

	return 0;
}

/* Convert TNC2 monitor text format to binary AX.25 packet */

void tnc2_to_ax25()
{
}

/* Convert the binary packet to TNC2 monitor text format  */

void ax25_to_tnc2(const char *portname, int tncid, int cmdbyte,
		  const unsigned char *frame, const int framelen)
{
	int i, j;
	const unsigned char *s = frame;
	const unsigned char *e = frame + framelen;
	int discard = 0;

	char tnc2buf[800];
	char *t = tnc2buf;
	int tnc2len;


	if (framelen > sizeof(tnc2buf) - 80) {
		/* Too much ! Too much! */
		return;
	}


	/* Phase 1: scan address fields. */
	/* Source and Destination addresses must be printed in altered order.. */


	*t = 0;
	i = ax25_to_tnc2_fmtaddress(t, frame + 7, 0);	/* source */
	if (i < 0) {
		discard = -1;	/* Bad format */
		if (debug)
			printf("Ax25toTNC2: Bad destination address\n");
	}

	t += strlen(t);
	*t++ = '>';

	j = ax25_to_tnc2_fmtaddress(t, frame + 0, 0);	/* destination */
	if (i < 0) {
		discard = -1;	/* Bad format */
		if (debug)
			printf("Ax25toTNC2: Bad source address\n");
	}

	t += strlen(t);

	s = frame + 14;

	if ((i & 1) == 0) {	/* addresses continue after the source! */

		for (; s < e;) {
			*t++ = ',';	/* separator char */
			i = ax25_to_tnc2_fmtaddress(t, s, 1);
			if (i < 0) {
				discard = -1;	/* Bad format */
				if (debug)
					printf("Ax25toTNC2: Bad via address\n");
				break;
			}

			t += strlen(t);
			s += 7;
			if (i & 1)
				break;	/* last address */
		}
	}

	/* Address completed */

	if ((s + 2) >= e)
		return;		/* never happens ?? */

	*t++ = ':';		/* end of address */

	if ((*s++ != 0x03) || (*s++ != 0xF0)) {
		/* Not AX.25 UI frame */
		return;
	}

	/* Copy payload - stop at first LF char */
	for (; s < e; ++s) {
		if (*s == '\n') /* Stop at first LF */
		  break;
		*t++ = *s;
	}
	*t = 0;

	/* Chop off possible immediately trailing CR characters */
	for ( ;t > tnc2buf; --t ) {
	    int c = t[-1];
	    if (c != '\r') {
		break;
	    }
	    t[-1] = 0;
	}

	tnc2len = t - tnc2buf;

	/* 
	   if (!discard)
	   aprsdigi(tnc2buf, portname, t0, t-t0);
	 */

	tnc2_rxgate(portname, tncid, tnc2buf, tnc2len, discard);
}
