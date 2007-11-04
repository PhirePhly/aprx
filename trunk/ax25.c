/* **************************************************************** *
 *                                                                  *
 *  APRSG-NG -- 2nd generation receive-only APRS-i-gate with        *
 *              minimal requirement of esoteric facilities or       *
 *              libraries of any kind beyond UNIX system libc.      *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */


#include "aprsg.h"


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

static int ax25_fmtaddress(char *dest, const char *src, int markflag)
{
	int i, c;

	/* We really should verify that  */

	/* 6 bytes of station callsigns in shifted ASCII format.. */
	for (i = 0; i < 6; ++i,++src) {
	  c = (*src) & 0xFF;
	  if (c & 1) return c;  /* Bad address-end flag ? */

	  /* Don't copy spaces or 0 bytes */
	  if (c != 0 && c != 0x40)
	    *dest++ = c >> 1;
	}
	/* 7th byte carries SSID et.al. bits */
	c = (*src) & 0xFF;
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
		memcmp("NOGATE",t,6) == 0);
}

static int ax25_forbidden_source_stationid(const char *t)
{
	return (memcmp("WIDE",t,4) == 0 ||   /* just plain wrong setting */
		memcmp("RELAY",t,5) == 0 ||  /* just plain wrong setting */
		memcmp("TRACE",t,5) == 0 ||  /* just plain wrong setting */
		memcmp("N0CALL",t,6) == 0 || /* TNC default setting */
		memcmp("NOCALL",t,6) == 0 ); /* TNC default setting */
}


void  ax25_to_tnc2(int cmdbyte, const char *frame, const int framelen)
{
	int i, j;
	const char *s = frame;
	const char *e = frame + framelen;
	int discard = 0;

	char tnc2buf[800];
	char *t = tnc2buf;
	const char *t2 = t + sizeof(tnc2buf)-50;

	setlinebuf(stdout); setlinebuf(stderr);

	/* memset(tnc2buf, 0, sizeof(tnc2buf)); /* DEBUG STUFF */

	if (framelen > sizeof(tnc2buf)-20) {
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


	/* printf("%s\n", tnc2buf); */
	/* t = tnc2buf; */

	if ((s+2) >= e) return; /* never happens ?? */

	/* Now insert ",$MYCALL,I:"  */
	t += sprintf(t, ",%s,I:", mycall);

	if ((*s++ != 0x03) ||
	    (*s++ != (char)0xF0)) {
	  /* Not APRS UI frame */
	  return;
	}


	/* Will not relay messages that begin with '}' char: */
	if (*s == '}')
	  discard = 1;


	/* Copy payload - stop at CR or LF chars */
	for ( ; s < e; ++s ) {
	  int c = *s;
	  if (c == '\n' || c == '\r' || c == 0)
	    break;
	  *t++ = c;
	}
	/* End the line with CRLF */
	*t++ = '\r';
	*t++ = '\n';
	*t = 0;

	if (!discard)
	  discard = aprsis_queue(tnc2buf, t-tnc2buf);  /* Send it.. */
	else
	  discard = -1;

	/* DEBUG OUTPUT TO STDOUT ! */
	printf("%ld\t", (long)now);
	if (discard < 0) { printf("*"); };  if(discard>0) { printf("#"); };
	printf("%s", tnc2buf); /* newline is included, debug stuff */
}
