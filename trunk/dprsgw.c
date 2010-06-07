/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2010                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

/*
 *  The DPRS RX Gateway
 *
 *  Receive data from DPRS.
 *  Convert to 3rd-party frame.
 *  Send out to APRSIS and Digipeaters.
 *
 *      http://www.aprs-is.net/DPRS.aspx
 *
 *
 *
 *  GPSxyz -> APRS symbols mapping:
 *
 *      http://www.aprs.org/symbols/symbolsX.txt
 */

typedef struct dprsgw_history {
	time_t gated;
	char   callsign[10];
} dprsgw_history_t;

typedef struct dprs_gw {
	char *ggaline;
	char *rmcline;
	int ggaspace;
	int rmcspace;

	// Up to 30 history entries to not to send same callsign too often
	dprsgw_history_t history[30];
} dprsgw_t;


void dprslog( const time_t stamp, const uint8_t *buf ) {
  FILE *fp = fopen("/tmp/dprslog.txt","a");

  fprintf(fp, "%ld\t%s\n", stamp, (const char *)buf);

  fclose(fp);
}


static void dprsgw_flush(dprsgw_t *dp) {
	if (dp->ggaline == NULL) {
	  dp->ggaspace = 200;
	  dp->ggaline = malloc(200);
	}
	if (dp->rmcline == NULL) {
	  dp->rmcspace = 200;
	  dp->rmcline = malloc(200);
	}
	dp->ggaline[0] = 0;
	dp->rmcline[0] = 0;
}

static void *dprsgw_new() {
	dprsgw_t *dp = malloc(sizeof(struct dprs_gw));
	memset(dp, 0, sizeof(*dp));
	dprsgw_flush(dp);
	return dp;
}

// Ratelimit returns 0 for "can send", 1 for "too soon"
static int dprsgw_ratelimit( dprsgw_t *dp, const void *tnc2buf ) {
	int i, n;
	char callsign[10];
	time_t expiry = now - 30; // FIXME: hard-coded 30 second delay for DPRS repeats

	memcpy(callsign, tnc2buf, sizeof(callsign));
	callsign[sizeof(callsign)-1] = 0;
	for (i = 0; i < sizeof(callsign); ++i) {
	  char c = callsign[i];
	  if (c == '>') {
	    callsign[i] = 0;
	    break;
	  }
	}
	n = -1;
	for (i = 0; i < 30; ++i) {
	  if (dp->history[i].gated > expiry) {
	    // Fresh enough to be interesting!
	    if (strcmp(dp->history[i].callsign, callsign) == 0) {
	      // This callsign!
	      return 1;
	    }
	  } else {
	    dp->history[i].callsign[0] = 0; // discard it..
	    if (n < 0) 
	      n = i; // save first free slot's index
	  }
	}
	if (n >= 0) {
	  memcpy(dp->history[n].callsign, callsign, sizeof(callsign));
	  dp->history[n].gated = now;
	}
	return 0;
}


// The "Specification" says to use this checksum method..
// It uses right-left inverted version of the polynome
// of CCITT-CRC-16 but processing is not correct..
// Thus the result is NOT CCITT-CRC-16, but something
// uniquely ICOM D-STAR..

static int dprsgw_crccheck( const uint8_t *s, int len )
{
	int icomcrc = 0xffff;
	for ( ; len > 0; ++s, --len) {
	  uint8_t ch = *s;
	  int i;
	  for (i = 0; i < 8; i++) {
	    int xorflag = (icomcrc ^ ch) & 0x01;
	    icomcrc >>= 1;
	    if (xorflag)
	      icomcrc ^= 0x8408;
	    ch >>= 1;
	  }
	}
	return (~icomcrc) & 0xffff;
}

static int dprsgw_isvalid( struct serialport *S )
{
	int i;

	if (S->rdlinelen < 20) {
	  if (debug) printf("Too short a line for DPRS");
	  return 0; // definitely not!
	}

	if (memcmp("$$CRC", S->rdline, 5) == 0 && S->rdline[9] == ',') {
	  // Maybe a $$CRCB727,OH3BK-D>APRATS,DSTAR*:@165340h6128.23N/02353.52E-D-RATS (GPS-A) /A=000377
	  int crc;
	  int csum = -1;

	  S->rdline[S->rdlinelen] = '\r';
	  crc = dprsgw_crccheck(S->rdline+10, S->rdlinelen+1-10); // INCLUDE the CR on CRC calculation!
	  S->rdline[S->rdlinelen] = 0;
	  i = sscanf((const char*)(S->rdline), "$$CRC%04x,", &csum);
	  if (i != 1 || csum != crc) {
	    if (debug) printf("Bad DPRS APRS CRC: l=%d, i=%d, %04x/%04x vs. %s\n", S->rdlinelen, i, crc, csum, S->rdline);
	    // return 0;
	  } else {
	    if (debug) printf("Good DPRS APRS CRC: l=%d, i=%d, %04x/%04x vs. %s\n", S->rdlinelen, i, crc, csum, S->rdline);
	    return 1;
	  }
	  return 0;

	} else if (memcmp("$GP", S->rdline, 3) == 0) {
	  // Maybe  $GPRMC,170130.02,A,6131.6583,N,02339.1552,E,0.00,154.8,290510,6.5,E,A*02  ?
	  int xor = 0;
	  int csum = -1;
	  char c;
	  // if (debug) printf("NMEA: '%s'\n", S->rdline);
	  for (i = 1; i < S->rdlinelen; ++i) {
	    c = S->rdline[i];
	    if (c == '*' && (i >= S->rdlinelen - 3)) {
	      break;
	    }
	    xor ^= c;
	  }
	  xor &= 0xFF;
	  if (i != S->rdlinelen -3 || S->rdline[i] != '*')
	    return 0; // Wrong place to stop
	  if (sscanf((const char *)(S->rdline+i), "*%02x%c", &csum, &c) != 1) {
	    return 0; // Too little or too much
	  }
	  if (xor != csum) {
	    if (debug) printf("Bad DPRS $GP... checksum: %02x vs. %02x\n", csum, xor);
	    return 0;
	  }
	  return 1;
	} else {
	  int xor = 0;
	  int csum = -1;
	  char c;
	  // .. uh?  maybe?  Precisely 29 characters:
	  // "OH3KGR M,                    "
	  if (S->rdlinelen != 29 || S->rdline[8] != ',') {
	    if (debug) printf("Bad DPRS identification(?) packet - length(%d) != 29 || line[8] != ',': %s\n",
			      S->rdlinelen, S->rdline);
	    return 0;
	  }
	  if (debug) printf("DPRS NMEA: '%s'\n", S->rdline);
	  for (i = 0; i < S->rdlinelen; ++i) {
	    c = S->rdline[i];
	    if (c == '*') {
	      break;
	    }
	    xor ^= c;
	  }
	  xor &= 0xFF;
	  if (sscanf((const char *)(S->rdline+i), "*%x%c", &csum, &c) < 1) {
	    if (memcmp(S->rdline+8, ",                    ", 21) == 0) {
	      if (debug) printf("DPRS IDENT LINE OK: '%s'\n", S->rdline);
	      return 1;
	    }
	    // if (debug) printf("csum bad NMEA: '%s'\n", S->rdline);
	    return 0; // Too little or too much
	  }
	  if (xor != csum) {
	    if (debug) printf("Bad DPRS IDENT LINE checksum: %02x vs. %02x\n", csum, xor);
	    return 0;
	  }
	  if (debug) printf("DPRS IDENT LINE OK: '%s'\n", S->rdline);
	  // if (debug) printf("csum valid NMEA: '%s'\n", S->rdline);
	  return 1; // Maybe valid?
	}
}


// Split NMEA text line at ',' characters
static int dprsgw_nmea_split(char *nmea, char *fields[], int n) {
	int i = 0;
	--n;
	fields[i] = nmea;
	for ( ; *nmea; ++nmea ) {
	  for ( ; *nmea != 0 && *nmea != ','; ++nmea )
	    ;
	  if (*nmea == 0) break; // THE END!
	  if (*nmea == ',')
	    *nmea++ = 0; // COMMA terminates a field, change to SPACE
	  if (i < n) ++i;  // Prep next field index
	  fields[i] = nmea; // save field pointer
	}
	fields[i] = NULL;
	return i;
}

static void dprsgw_nmea_igate( const struct aprx_interface *aif,
			       const uint8_t *ident, dprsgw_t *dp ) {
	int i;
	char *gga[20];
	char *rmc[20];
	char tnc2buf[2000];
	int  tnc2addrlen;
	int  tnc2buflen;
	char *p, *p2;
	const char *p0;
	const char *s;
	int  alt_feet = -9999999;
	char aprssym[3];

	if (debug) {
	  printf(" DPRS: ident='%s', GGA='%s', RMC='%s'\n", ident, dp->ggaline, dp->rmcline);
	}

	strcpy(aprssym, "/>"); // Default..
	parse_gps2aprs_symbol((const char *)ident+9, aprssym);

	memset(gga, 0, sizeof(gga));
	memset(rmc, 0, sizeof(rmc));

	// $GPGGA,hhmmss.dd,xxmm.dddd,<N|S>,yyymm.dddd,<E|W>,v,
	//        ss,d.d,h.h,M,g.g,M,a.a,xxxx*hh<CR><LF>

	// $GPRMC,hhmmss.dd,S,xxmm.dddd,<N|S>,yyymm.dddd,<E|W>,
	//        s.s,h.h,ddmmyy,d.d, <E|W>,M*hh<CR><LF>
	//    ,S, = Status:  'A' = Valid, 'V' = Invalid

	if (dp->ggaline[0] != 0)
	  dprsgw_nmea_split(dp->ggaline, gga, 20);
	if (dp->rmcline[0] != 0)
	  dprsgw_nmea_split(dp->rmcline, rmc, 20);

	if (rmc[2] != NULL && strcmp(rmc[2],"A") != 0) {
	  if (debug) printf("Invalid DPRS $GPRMC packet (validity='%s')\n",
			    rmc[2]);
	  return;
	}
	if (gga[6] != NULL && strcmp(gga[6],"1") != 0) {
	  if (debug) printf("Invalid DPRS $GPGGA packet (validity='%s')\n",
			    gga[6]);
	  return;
	}

	p = tnc2buf;
	for (i = 0; i < 8; ++i) {
	  if (ident[i] == ' ') {
	    *p++ = '-';
	    for ( ; ident[i+1] == ' ' && i < 8; ++i );
	    continue;
	  }
	  *p++ = ident[i];
	}

	p += sprintf(p, ">APDPRS,DSTAR*");


	tnc2addrlen = p - tnc2buf;
	*p++ = ':';
	*p++ = '!'; // Std position w/o messaging
	p2 = p;
	if (gga[2] != NULL) {
	  s = gga[2];
	} else if (rmc[3] != NULL) {
	  s = rmc[3];
	} else {
	  // No coordinate!
	  if (debug) printf("dprsgw: neither GGA nor RMC coordinates available, discarding!\n");
	  return;
	}
	p0 = strchr(s, '.');
	if (!p0) {
	  if (debug) printf("dprsgw: invalid format NMEA North coordinate: '%s'\n", s);
	  return;
	}
	while (p0 - s < 4) {
	  *p++ = '0';
	  ++p0; // move virtually
	}
	p += sprintf(p, "%s", s);
	if (p2+7 < p) { // Too long!
	  p = p2+7;
	}
	while (p2+7 > p) { // Too short!
	  *p++ = ' '; // unprecise position
	}
	if (gga[2] != NULL) {
	  s = gga[3]; // <N|S>
	} else if (rmc[3] != NULL) {
	  s = rmc[4]; // <N|S>
	}
	p += sprintf(p, "%s", s);

	*p++ = aprssym[0];
	p2 = p;
	if (gga[2] != NULL) {
	  s = gga[4]; // yyymm.dddd
	} else if (rmc[3] != NULL) {
	  s = rmc[5]; // yyymm.dddd
	}
	p0 = strchr(s, '.');
	if (!p0) {
	  if (debug) printf("dprsgw: invalid format NMEA East coordinate: '%s'\n", s);
	  return;
	}
	while (p0 - s < 5) {
	  *p++ = '0';
	  ++p0; // move virtually
	}

	p += sprintf(p, "%s", s);
	if (p2+8 < p) { // Too long!
	  p = p2+8;
	}
	while (p2+8 > p) { // Too short!
	  *p++ = ' '; // unprecise position
	}
	if (gga[2] != NULL) {
	  p += sprintf(p, "%s", gga[5]); // <E|W>
	} else if (rmc[3] != NULL) {
	  p += sprintf(p, "%s", rmc[6]); // <E|W>
	}

	*p++ = aprssym[1];

	//  DPRS: ident='OH3BK  D,BN  *59             ', GGA='$GPGGA,204805,6128.230,N,2353.520,E,1,3,0,115,M,0,M,,*6d', RMC=''
	//  DPRSGW GPS data: OH3BK-D>APDPRS,DSTAR*:!6128.23N/02353.52E>

	if (gga[2] != NULL) {
	  if (gga[9] != NULL && gga[9][0] != 0)
	    alt_feet = strtol(gga[9], NULL, 10);
	  if (strcmp(gga[10],"M") == 0) {
	    // Meters!  Convert to feet..
	    alt_feet = (10000 * alt_feet) / 3048;
	  } else {
	    // Already feet - presumably
	  }
	}

	// FIXME: more data!
	// RMC HEADING/SPEED

	p0 = (const char *)ident + 29;
	s  = (const char *)ident + 9+4;
	p2 = p;

	for ( ; s < p0; ++s ) {
	  if (*s != ' ')
	    break;
	}
	if (s < p0)
	  *p++ = ' ';
	for ( ; s < p0; ++s ) {
	  if (*s == '*')
	    break;
	  *p++ = *s;
	}
	if (p > p2)
	  *p++ = ' ';

	if (alt_feet > -9999999) {
	  p += sprintf(p, "/A=%06d", alt_feet);
	}

	*p = 0;
	tnc2buflen = p - tnc2buf;

	if (debug) printf("DPRSGW GPS data: %s\n", tnc2buf);

	if (!dprsgw_ratelimit( dp, tnc2buf )) {

	  char *fromcall, *origtocall;
	  char *b;

	  igate_to_aprsis( aif ? aif->callsign : NULL, 0, (const char *)tnc2buf, tnc2addrlen, tnc2buflen, 0, 0);

	  fromcall = tnc2buf;
	  p = fromcall;
	  origtocall = NULL;
	  while (*p != '>' && *p != 0) ++s;
	  if (*p == '>') {
	    *p++ = 0;
	    origtocall = p;
	  } else
	    return; // BAD :-(
	  p = origtocall;
	  while (p != NULL && *p != ':' &&  *p != 0 && *p != ',') ++p;
	  if (p != NULL && (*p == ':' || *p == ',')) {
	    *p++ = 0;
	  }
	  b = tnc2buf + tnc2addrlen +1;
	  interface_receive_3rdparty( aif,
				      fromcall, origtocall, "DSTAR*",
				      b, tnc2buflen - (b-tnc2buf) );
	}
}

static void dprsgw_rxigate( struct serialport *S )
{
	const struct aprx_interface *aif = S->interface[0];
	uint8_t *tnc2addr    = S->rdline;
	int      tnc2addrlen = S->rdlinelen;
	uint8_t *tnc2body    = S->rdline;
	int      tnc2bodylen = S->rdlinelen;

#ifndef DPRSGW_DEBUG_MAIN
	if (aif == NULL) {
	  if (debug) printf("OOPS! NO <interface> ON DPRS SERIAL PORT! BUG!\n");
	  return;
	}
#endif

	if (memcmp("$$CRC", tnc2addr, 5) == 0 && tnc2addrlen > 20) {
	  tnc2addr += 10;
	  tnc2addrlen -= 10;
	  tnc2bodylen -= 10; // header + body together
	  tnc2body = memchr( tnc2addr, ':', tnc2addrlen);
	  if (tnc2body != NULL) {
	    char *fromcall, *origtocall;
	    char *s;

	    tnc2addrlen = tnc2body - tnc2addr;
	    ++tnc2body;

	    if (dprsgw_ratelimit(S->dprsgw, tnc2addr)) {
	      // Rate-limit ordered rejection
	      return;
	    }

	    // Acceptable packet, Rx-iGate it!
	    igate_to_aprsis( aif ? aif->callsign : NULL, 0, (const char *)tnc2addr, tnc2addrlen, tnc2bodylen, 0, 0);

	    fromcall = (char*)tnc2addr;
	    s = fromcall;
	    origtocall = NULL;
	    while (*s != '>' && *s != 0) ++s;
	    if (*s == '>') {
	      *s++ = 0;
	      origtocall = s;
	    } else
	      return; // BAD :-(
	    s = origtocall;
	    while (s != NULL && *s != ':' &&  *s != 0 && *s != ',') ++s;
	    if (s != NULL && (*s == ':' || *s == ',')) {
	      *s++ = 0;
	    }

	    interface_receive_3rdparty( aif,
					fromcall, origtocall, "DSTAR*",
					(const char*)tnc2body, tnc2bodylen - (tnc2body-tnc2addr) );
	    return;
	    
	  } else {
	    // Bad packet!
	    if (debug) printf("Bad DPRS packet! %s\n", S->rdline);
	    return;
	  }

	} else if (memcmp("$GPGGA,", tnc2addr, 7) == 0) {
	  dprsgw_t *dp = S->dprsgw;
	  
	  if (dp->ggaspace <= S->rdlinelen) {
	    dp->ggaline  = realloc(dp->ggaline, S->rdlinelen+1);
	    dp->ggaspace = S->rdlinelen;
	  }
	  memcpy(dp->ggaline, tnc2addr, tnc2bodylen);
	  dp->ggaline[tnc2bodylen] = 0;
	  if (debug) printf("DPRS GGA: %s\n", dp->ggaline);

	} else if (memcmp("$GPRMC,", tnc2addr, 7) == 0) {
	  dprsgw_t *dp = S->dprsgw;
	  
	  if (dp->rmcspace <= S->rdlinelen) {
	    dp->rmcline  = realloc(dp->rmcline, S->rdlinelen+1);
	    dp->rmcspace = S->rdlinelen;
	  }
	  memcpy(dp->rmcline, tnc2addr, tnc2bodylen);
	  dp->rmcline[tnc2bodylen] = 0;
	  if (debug) printf("DPRS RMC: %s\n", dp->rmcline);

	} else if (tnc2addr[8] == ',' && tnc2bodylen == 29) {
	  // Acceptable DPRS "ident" line
	  dprsgw_t *dp = S->dprsgw;
	  tnc2addr[tnc2bodylen] = 0; // zero-terminate just in case
	  dprsgw_nmea_igate(aif, tnc2addr, dp);

	} else {
	  // this should never be called...
	  if (debug) printf("Unrecognized DPRS packet: %s\n", S->rdline);
	  return;
	}
}

/*
 *  Receive one text line from serial port
 *  It will end with 0x00 byte, and not contain \r nor \n.
 *
 *  It MAY have junk at the start.
 *
 *
 */

static void dprsgw_receive( struct serialport *S )
{
	int i;

	dprslog(S->rdline_time, S->rdline);

	do {
	  if (dprsgw_isvalid(S)) {
	    // Feed it to DPRS-APRS-GATE
	    dprsgw_rxigate( S );
	    return; // Done!
	  } else {
	    // Not a good packet! See if there is a good packet inside?
	    dprsgw_flush(S->dprsgw);  // bad input -> discard accumulated data

	    uint8_t *p = memchr(S->rdline+1, '$', S->rdlinelen-1);
	    if (p == NULL)
	      break; // No '$' to start something
	    i = S->rdlinelen - (p - S->rdline);
	    if (i <= 0)
	      break; // exhausted!
	    S->rdlinelen = i;
	    memcpy(S->rdline, p, S->rdlinelen);
	    S->rdline[i] = 0;
	    continue;
	  }
	} while(1);
}


/*
 *  Receive more data from DPRS type serial port
 *  This handles correct data accumulation and sync hunting
 */
int dprsgw_pulldprs( struct serialport *S )
{
	int c;
	int i;

	if (S->dprsgw == NULL)
	  S->dprsgw = dprsgw_new();

	time_t rdtime = S->rdline_time;
	if (rdtime+2 < now) {
		// A timeout has happen? Either data is added constantly,
		// or nothing was received from DPRS datastream!

		if (S->rdlinelen > 0)
		  if (debug)printf("dprsgw: previous data is %d sec old, discarding its state: %s\n",((int)(now-rdtime)), S->rdline);

		S->rdline[S->rdlinelen] = 0;
		if (S->rdlinelen > 0)
		  dprslog(rdtime, S->rdline);
		S->rdlinelen = 0;

		dprsgw_flush(S->dprsgw);  // timeout -> discard accumulated data
	}
	S->rdline_time = now;

	for (i=0 ; ; ++i) {

		c = ttyreader_getc(S);
		if (c < 0) {
		  // if (debug) printf("dprsgw_pulldprs: read %d chars\n", i);
			return c;	/* Out of input.. */
		}
		if (debug>2) printf("DPRS %ld %3d %02X '%c'\n", now, S->rdlinelen, c, c);

		/* S->dprsstate != 0: read data into S->rdline,
		   == 0: discard data until CR|LF.
		   Zero-size read line is discarded as well
		   (only CR|LF on input frame)  */

		/* Looking for CR or LF.. */
		if (c == '\n' || c == '\r') {
		  /* End of line seen! */
		  if (S->rdlinelen > 0) {
		  
		    /* Non-zero-size string, put terminating 0 byte on it. */
		    S->rdline[S->rdlinelen] = 0;
		  
		    dprsgw_receive(S);
		  }
		  S->rdlinelen = 0;
		  continue;
		}
		// A '$' starts possible data..
		if (c == '$' && S->rdlinelen == 0) {
		  S->rdline[S->rdlinelen++] = c;
		  continue;
		}
		// More fits in?
		if (S->rdlinelen >= sizeof(S->rdline)-3) {
		  // Too long a line...
		  do {
		    int len;
		    dprsgw_flush(S->dprsgw);
		    
		    // Look for first '$' in buffer _after_ first char
		    uint8_t *p = memchr(S->rdline+1, '$', S->rdlinelen-1);
		    if (!p) {
		      S->rdlinelen = 0;
		      break; // Not found
		    }
		    len = S->rdlinelen - (p - S->rdline);
		    if (len <= 0) {
		      S->rdlinelen = 0;
		      break; // exhausted
		    }
		    memcpy(S->rdline, p, len);
		    S->rdline[len] = 0;
		    S->rdlinelen = len;
		    if (len >= 3) {
		      if (memcmp("$$C", S->rdline, 3) != 0 &&
			  memcmp("$GP", S->rdline, 3) != 0) {
			// Not acceptable 3-char prefix
			// Eat away the collected prefixes..
			continue;
		      }
		    }
		    break;
		  } while(1);
		}
		S->rdline[S->rdlinelen++] = c;

/*
		// Too short to say anything?
		if (S->rdlinelen < 3) {
		  continue;
		}
		if (S->rdlinelen == 3 &&
		    (memcmp("$$C", S->rdline, 3) != 0 &&
		     memcmp("$GP", S->rdline, 3) != 0)) {
		  // No correct start, discard...
		  dprsgw_flush(S->dprsgw);
		  S->rdlinelen = 2;
		  memcpy(S->rdline, S->rdline+1, 2);
		  S->rdline[S->rdlinelen] = 0;
		  if (S->rdline[0] != '$') {
		    // Didn't start with a '$'
		    S->rdlinelen = 0;
		  }
		  continue;
		}
*/
	}			/* .. input loop */

	return 0;		/* not reached */
}


int  dprsgw_prepoll(struct aprxpolls *app)
{
	return 0;
}

int  dprsgw_postpoll(struct aprxpolls *app)
{
	return 0;
}



#ifdef DPRSGW_DEBUG_MAIN

int freadln(FILE *fp, char *p, int buflen)
{
  int n = 0;
  while (!feof(fp)) {
    int c = fgetc(fp);
    if (c == EOF) break;
    if (n >= buflen) break;
    *p++ = c;
    ++n;
    if (c == '\n') break;
    if (c == '\r') break;
  }
  return n;
}

int ttyreader_getc(struct serialport *S)
{
	if (S->rdcursor >= S->rdlen) {	/* Out of data ? */
		if (S->rdcursor)
			S->rdcursor = S->rdlen = 0;
		/* printf("-\n"); */
		return -1;
	}

	/* printf(" %02X", 0xFF & S->rdbuf[S->rdcursor++]); */

	return (0xFF & S->rdbuf[S->rdcursor++]);
}
void igate_to_aprsis(const char *portname, const int tncid, const char *tnc2buf, int tnc2addrlen, int tnc2len, const int discard, const int strictax25_)
{
  printf("DPRS RX-IGATE: %s\n", tnc2buf);
}

void interface_receive_3rdparty(const struct aprx_interface *aif, const char *fromcall, const char *origtocall, const char *gwtype, const char *tnc2data, const int tnc2datalen)
{
  printf("DPRS 3RDPARTY RX: ....:}%s>%s,%s,GWCALLSIGN*:%s\n",
	 fromcall, origtocall, gwtype, tnc2data);
}

int debug = 1;
time_t now;
int main(int argc, char *argv[]) {
  struct serialport S;
  memset(&S, 0, sizeof(S));

#if 0
  // A test where string has initially some incomplete data, then finally a real data
  printf("\nFIRST TEST\n");
  strcpy((void*)S.rdline, "x$x4$GPPP$$$GP  $$CRCB727,OH3BK-D>$$CRCB727,OH3BK-D>APRATS,DSTAR*:@165340h6128.23N/02353.52E-D-RATS (GPS-A) /A=000377");
  S.rdlinelen = strlen((void*)S.rdline);
  dprsgw_receive(&S);

  printf("\nSECOND TEST\n");
  strcpy((void*)S.rdline, "\304\3559\202\333$$CRCC3F5,OH3KGR-M>API282,DSTAR*:/123035h6131.29N/02340.45E>/IC-E2820");
  S.rdlinelen = strlen((void*)S.rdline);
  dprsgw_receive(&S);

  printf("\nTHIRD TEST\n");
  strcpy((void*)S.rdline, "[SOB]\"=@=@=@=>7\310=@\010!~~~~~~~!~~~~~~~\001\001\001\001\001\001\001\001[EOB]$$CRCBFB7,OH3BK>APRATS,DSTAR*:@124202h6128.23N/02353.52E-D-RATS (GPS-A) /A=000377");
  S.rdlinelen = strlen((void*)S.rdline);
  dprsgw_receive(&S);

  printf("\nTEST 4.\n");
  strcpy((void*)S.rdline, "$GPGGA,164829.02,6131.6572,N,02339.1567,E,1,08,1.1,111.3,M,19.0,M,,*61");
  S.rdlinelen = strlen((void*)S.rdline);
  dprsgw_receive(&S);

  printf("\nTEST 5.\n");
  strcpy((void*)S.rdline, "$GPRMC,170130.02,A,6131.6583,N,02339.1552,E,0.00,154.8,290510,6.5,E,A*02");
  // strcpy((void*)S.rdline, "$GPRMC,164830.02,A,6131.6572,N,02339.1567,E,0.00,182.2,290510,6.5,E,A*07");
  S.rdlinelen = strlen((void*)S.rdline);
  dprsgw_receive(&S);

  printf("\nTEST 6.\n");
  strcpy((void*)S.rdline, "OH3BK  D,BN  *59             ");
  S.rdlinelen = strlen((void*)S.rdline);
  dprsgw_receive(&S);
#endif

  S.ttyname = "testfile";
  S.ttycallsign[0] = "OH2MQK-DR";
  FILE *fp = fopen("tt.log", "r");
  for (;;) {
    char buf1[3000];
    int n = freadln(fp, buf1, sizeof(buf1));
    if (n == 0) break;
    char *ep;
    now = strtol(buf1, &ep, 10);
    if (*ep == '\t') ++ep;
    int len = n - (ep - buf1);
    if (len > 0) {
      memcpy(S.rdbuf+S.rdlen, ep, len);
      S.rdlen += len;
    }
    if (S.rdlen > 0)
      dprsgw_pulldprs(&S);

  }
  fclose(fp);

  return 0;
}
#endif
