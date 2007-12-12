/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */


#include "aprx.h"


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

static int ax25_fmtaddress(char *dest, const unsigned char *src, int markflag)
{
	int i, c;

	/* We really should verify that  */

	/* 6 bytes of station callsigns in shifted ASCII format.. */
	for (i = 0; i < 6; ++i,++src) {
	  c = *src;
	  if (c & 1) return c;  /* Bad address-end flag ? */

	  /* Don't copy spaces or 0 bytes */
	  if (c != 0 && c != 0x40)
	    *dest++ = c >> 1;
	}
	/* 7th byte carries SSID et.al. bits */
	c = *src;
	if ((c >> 1) % 16) { /* don't print SSID==0 value */
	  dest += sprintf(dest, "-%d", (c >> 1) % 16);
	}

	if ((c & 0x80) && markflag) {
	  *dest++ = '*'; /* Has been repeated, or some such.. */
	}
	*dest = 0;

	return c;
}

static int ax25_forbidden_via_stationid(const char *t)
{
	return (memcmp("RFONLY",t,6) == 0 ||
		memcmp("NOGATE",t,6) == 0 ||
		memcmp("TCPIP",t,5)  == 0 ||
		memcmp("TCPXX",t,5)  == 0);
}

static int ax25_forbidden_source_stationid(const char *t)
{
	return (memcmp("WIDE",t,4)   == 0 || /* just plain wrong setting */
		memcmp("RELAY",t,5)  == 0 || /* just plain wrong setting */
		memcmp("TRACE",t,5)  == 0 || /* just plain wrong setting */
		memcmp("TCPIP",t,5)  == 0 || /* just plain wrong setting */
		memcmp("TCPXX",t,5)  == 0 || /* just plain wrong setting */
		memcmp("N0CALL",t,6) == 0 || /* TNC default setting */
		memcmp("NOCALL",t,6) == 0 ); /* TNC default setting */
}


void  ax25_to_tnc2(int cmdbyte, const unsigned char *frame, const int framelen)
{
	int i, j;
	const unsigned char *s = frame;
	const unsigned char *e = frame + framelen;
	int discard = 0;
	int thirdparty = 0;

	char tnc2buf[800];
	char *t = tnc2buf, *t0;

#if 0
	memset(tnc2buf, 0, sizeof(tnc2buf)); /* DEBUG STUFF */
#endif

	if (framelen > sizeof(tnc2buf)-80) {
	  /* Too much ! Too much! */
	  return;
	}



	/* Phase 1: scan address fields. */
	/* Source and Destination addresses must be printed in altered order.. */


	*t = 0;
	i = ax25_fmtaddress(t, frame+7, 0); /* source */

	/*
	 * If any of following matches, discard the packet!
	 * next if ($axpath =~ m/^WIDE/io); # Begins with = is sourced by..
	 * next if ($axpath =~ m/^RELAY/io);
	 * next if ($axpath =~ m/^TRACE/io);
	 */
	if (ax25_forbidden_source_stationid(t))
	  discard = 1; /* Forbidden in source fields.. */

	t += strlen(t);
	*t++ = '>';

	j = ax25_fmtaddress(t, frame+0, 0); /* destination */

	t += strlen(t);

	s = frame+14;

	/*
	 *  next if ($axpath =~ m/RFONLY/io); # Has any of these in via fields..
	 *  next if ($axpath =~ m/TCPIP/io);
	 *  next if ($axpath =~ m/TCPXX/io);
	 *  next if ($axpath =~ m/NOGATE/io); # .. drop it.
	 */

	if ((i & 1) == 0) { /* addresses continue after the source! */

	  for ( ; s < e;) {
	    *t++ = ','; /* separator char */
	    i = ax25_fmtaddress(t, s, 1);

	    if (ax25_forbidden_via_stationid(t))
	      discard = 1; /* Forbidden in via fields.. */

	    t += strlen(t);
	    s += 7;
	    if (i & 1)
	      break; /* last address */
	  }
	}

	/* Address completed */

	if ((s+2) >= e) return; /* never happens ?? */

	*t++ = 0; /* terminate the string */

	t0 = t; /* Start assembling packet here */

	if ((*s++ != 0x03) ||
	    (*s++ != 0xF0)) {
	  /* Not AX.25 UI frame */
	  return;
	}

	/* Will not relay messages that begin with '?' char: */
	if (*s == '?')
	  discard = 1;


	/* Will not relay messages that begin with '}' char: */
	if (*s == '}')
	  thirdparty = 1;

	/* Copy payload - stop at CR or LF chars */
	for ( ; s < e; ++s ) {
	  int c = *s;
	  if (c == '\n' || c == '\r' || c == 0)
	    break;
	  *t++ = c;
	}
	*t = 0;
	s = (unsigned char*) t0;

	/* TODO: Verify message being of recognized APRS packet type */
	/*   '\0x60', '\0x27':  MIC-E, len >= 9
	 *   '!','=','/','{':   Normal or compressed location packet..
	 *   '$':               NMEA data, if it begins as '$GP'
	 *   '$':               WX data (maybe) if not NMEA data
	 *   ';':	 	Object data, len >= 31
	 *   ')':	 	Item data, len >= 18
	 *   ':':	 	message, bulletin or aanouncement, len >= 11
	 *   '<':               Station Capabilities, len >= 2
	 *   '>':		Status report
	 *   '}':		Third-party message
	 * ...  and many more ...
	 */


#if 0
	while (thirdparty) {
	  /* Rule says either: discard all 3rd party headers, OR discard
	     at first existing address header and type ('}') char, then
	     reprocess internal data blob from beginning:
	          ADDR1>THERE:}ADDR2>HERE,ELSWRE:...

	     Great documentation...
	  */
	  /* Whatever happens, this line gets discarded, and reformatted
	     one may get sent.. */
	  if (verbout)
	    printf("%ld\t#%s:%s\n", (long)now, tnc2buf, t0);

	  thirdparty = discard = 0;
	  strcpy(tnc2buf, t0);

	  s = t = tnc2buf;

	  /* Analyze 'from' address */

	  /* Analyze 'to' address */

	  /* Analyze 'path' components */

	  t += strlen(t);
	}
#else
	discard = thirdparty; /* Discard all 3rd party messages */
#endif


	/* End the line with CRLF */
	*t++ = '\r';
	*t++ = '\n';
	*t = 0;

	if (!discard)
	  discard = aprsis_queue(tnc2buf, t0, t-t0);  /* Send it.. */
	else
	  discard = -1;

	/* DEBUG OUTPUT TO STDOUT ! */
	if (verbout) {
	  printf("%ld\t", (long)now);
	  if (discard < 0) { printf("*"); };  if(discard>0) { printf("#"); };
	  printf("%s:%s", tnc2buf, t0); /* newline is included, debug stuff */
	}
}
