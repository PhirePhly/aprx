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
#include <regex.h>

static regex_t **sourceregs;
static int       sourceregscount;

static regex_t **destinationregs;
static int       destinationregscount;

static regex_t **viaregs;
static int       viaregscount;

static regex_t **dataregs;
static int       dataregscount;


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
	  if (c & 1) return (-(int)(c));  /* Bad address-end flag ? */

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


void ax25_filter_add(const char *p1, const char *p2)
{
	int rc;
	int groupcode = -1;
	regex_t re, *rep;
	char errbuf[2000];

	if (strcmp(p1,"source")==0) {
	  groupcode = 0;
	} else if (strcmp(p1,"destination")==0) {
	  groupcode = 1;
	} else if (strcmp(p1,"via")==0) {
	  groupcode = 2;
	} else if (strcmp(p1,"data")==0) {
	  groupcode = 3;
	} else {
	  printf("Bad RE target: '%s'  must be one of: source, destination, via\n", p1);
	  return;
	}

	if (!*p2) return; /* Bad input.. */

	memset(&re, 0, sizeof(re));
	rc = regcomp(&re, p2, REG_EXTENDED|REG_NOSUB);

	if (rc != 0) {  /* Something is bad.. */
	  if (debug) {
	    *errbuf = 0;
	    regerror(rc, &re, errbuf, sizeof(errbuf));
	    printf("Bad POSIX RE input, error: %s\n", errbuf);
	  }
	}

	/* p1 and p2 were processed successfully ... */

	rep = malloc(sizeof(*rep));
	*rep = re;

	switch(groupcode) {
	case 0:
	  ++sourceregscount;
	  sourceregs = realloc(sourceregs, sourceregscount * sizeof(void*));
	  sourceregs[sourceregscount-1] = rep;
	  break;
	case 1:
	  ++destinationregscount;
	  destinationregs = realloc(destinationregs, destinationregscount * sizeof(void*));
	  destinationregs[destinationregscount-1] = rep;
	  break;
	case 2:
	  ++viaregscount;
	  viaregs = realloc(viaregs, viaregscount * sizeof(void*));
	  viaregs[viaregscount-1] = rep;
	  break;
	case 3:
	  ++dataregscount;
	  dataregs = realloc(dataregs, dataregscount * sizeof(void*));
	  dataregs[dataregscount-1] = rep;
	  break;
	}
}


static int ax25_forbidden_source_stationid(const char *t)
{
	int i;

	if (memcmp("WIDE",t,4)   == 0 || /* just plain wrong setting */
	    memcmp("RELAY",t,5)  == 0 || /* just plain wrong setting */
	    memcmp("TRACE",t,5)  == 0 || /* just plain wrong setting */
	    memcmp("TCPIP",t,5)  == 0 || /* just plain wrong setting */
	    memcmp("TCPXX",t,5)  == 0 || /* just plain wrong setting */
	    memcmp("N0CALL",t,6) == 0 || /* TNC default setting */
	    memcmp("NOCALL",t,6) == 0 ) /* TNC default setting */
	  return 1;

	for (i = 0; i < sourceregscount; ++i) {
	  int stat = regexec(sourceregs[i], t, 0, NULL, 0);
	  if (stat == 0)
	    return 1; /* MATCH! */
	}

	return 0;
}

static int ax25_forbidden_destination_stationid(const char *t)
{
	int i;

	for (i = 0; i < destinationregscount; ++i) {
	  int stat = regexec(destinationregs[i], t, 0, NULL, 0);
	  if (stat == 0)
	    return 1; /* MATCH! */
	}

	return 0;
}

static int ax25_forbidden_via_stationid(const char *t)
{
	int i;

	if (memcmp("RFONLY",t,6) == 0 ||
	    memcmp("NOGATE",t,6) == 0 ||
	    memcmp("TCPIP",t,5)  == 0 ||
	    memcmp("TCPXX",t,5)  == 0)
	  return 1;

	for (i = 0; i < viaregscount; ++i) {
	  int stat = regexec(viaregs[i], t, 0, NULL, 0);
	  if (stat == 0)
	    return 1; /* MATCH! */
	}

	return 0;
}

static int ax25_forbidden_data(const char *t)
{
	int i;

	for (i = 0; i < dataregscount; ++i) {
	  int stat = regexec(dataregs[i], t, 0, NULL, 0);
	  if (stat == 0)
	    return 1; /* MATCH! */
	}

	return 0;
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
	if (i < 0)
	  discard = 1; /* Bad format */

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
	if (i < 0)
	  discard = 1; /* Bad format */

	if (ax25_forbidden_destination_stationid(t))
	  discard = 1;

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
	    if (i < 0) {
	      discard = 1; /* Bad format */
	      break;
	    }

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

	if (ax25_forbidden_data(t0))
	  discard = 1;

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
	  if (discard<0) { printf("*"); };
	  if (discard>0) { printf("#"); };
	  printf("%s:%s", tnc2buf, t0); /* newline is included, debug stuff */
	}
	if (rflogfile) {
	  FILE *fp = fopen(rflogfile,"a");

	  if (fp) {
	    char timebuf[60];
	    struct tm *t = gmtime(&now);
	    strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);

	    fprintf(fp, "%s ",timebuf);
	    if (discard < 0) { fprintf(fp, "*"); }
	    if (discard > 0) { fprintf(fp, "#"); }
	    fprintf(fp, "%s:%s", tnc2buf, t0);

	    fclose(fp);
	  }
	}
}
