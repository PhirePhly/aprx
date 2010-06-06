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
 *  http://www.aprs-is.net/DPRS.aspx
 *
 */

typedef struct dprs_gw {
	char *ggaline;
	char *rmcline;
	int ggaspace;
	int rmcspace;

} dprsgw_t;


void dprslog( const time_t stamp, const uint8_t *buf ) {
  FILE *fp = fopen("/tmp/dprslog.txt","a");

  fprintf(fp, "%ld\t%s\n", stamp, (const char *)buf);

  fclose(fp);
}


static void dprsgw_flush(dprsgw_t *dp) {
	if (dp->ggaline == NULL)
	  dp->ggaline = malloc(200);
	if (dp->rmcline == NULL)
	  dp->rmcline = malloc(200);
	dp->ggaspace = 200;
	dp->rmcspace = 200;
	dp->ggaline[0] = 0;
	dp->rmcline[0] = 0;
}

static void *dprsgw_new() {
	dprsgw_t *dp = malloc(sizeof(struct dprs_gw));
	memset(dp, 0, sizeof(*dp));
	dprsgw_flush(dp);
	return dp;
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
	  tnc2bodylen -= 10;
	  tnc2body = memchr( tnc2addr, ':', tnc2addrlen);
	  if (tnc2body != NULL) {
	    tnc2addrlen = tnc2body - tnc2addr;
	    tnc2body += 1;

	    // MAYBE RATELIMIT
	    // Acceptable packet, Rx-iGate it!
	    igate_to_aprsis( aif ? aif->callsign : NULL, 0, (const char *)tnc2addr, tnc2addrlen, tnc2bodylen, 0, 0);

/*
	    // MUST RATELIMIT!
	    interface_receive_3rdparty( aif,
					fromcall, origtocall,
					b, ax25len - (b-ax25) );
*/
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

	  if (debug) {
	    printf(" DPRS: ident='%s', GGA='%s', RMC='%s'\n", tnc2addr, dp->ggaline, dp->rmcline);
	  }

	  

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
	    if (memcmp("$$CRC", S->rdline, 5) == 0) {
	      // Valid GPS-A mode packet!
	      dprsgw_rxigate( S );
	      return; // Done!
	    }
	    // FIXME: Other packet formats!
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
		if (debug) printf("DPRS %ld %3d %02X '%c'\n", now, S->rdlinelen, c, c);

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
void igate_to_aprsis(const char *portname, const int tncid, const char *tnc2buf, int tnc2addrlen, int tnc2len, const int discard)
{
  printf("DPRS RX-IGATE: %s\n", tnc2buf);
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
