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


typedef struct dprsgw_receiver {
	int i;
} dprsgw_receiver_t;


/*
 * ??
 */

void *dprsgw_new( void )
{
}


void dprslog( const char *buf ) {
  FILE *fp = fopen("/tmp/dprslog.txt","a");

  fprintf(fp, "%ld\t%s\n", now, buf);

  fclose(fp);
}

// "Specification" says to use this checksum..
// But this is just horribly inefficient way to calculate
// CCITT-CRC-16 -- for which we have crc16_calc()
static int dprsgw_crccheck( const char *s, int len )
{
	int icomcrc = 0xffff;
	for ( ; len > 0; ++s, --len) {
	  int ch = (*s) & 0xff;
	  int i;
	  for (i = 0; i < 8; i++) {
	      int xorflag = (((icomcrc ^ ch) & 0x01) == 0x01);
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
	  int crc = crc16_calc(S->rdline+10, S->rdlinelen-10);
	  int csum = -1;
	  i = sscanf(S->rdline, "$$CRC%04x,", &csum);
	  if (csum != crc) {
	    if (debug) printf("Bad DPRS APRS CRC: %04x vs. %s", crc, S->rdline);
	    return 0;
	  }
	  if (debug) printf("DPRS APRS packet is OK: %s\n", S->rdline+10);
	  return 1;

	} else if (memcmp("$GP", S->rdline, 3) == 0) {
	  // Maybe  $GPRMC,170130.02,A,6131.6583,N,02339.1552,E,0.00,154.8,290510,6.5,E,A*02  ?
	  int xor = 0;
	  int csum = -1;
	  char *p;
	  for (i = 0; i < S->rdlinelen; ++i) {
	    char c = S->rdline[i];
	    if (c == '*' && (i == S->rdlinelen - 3))
	      break;
	    xor ^= c;
	  }
	  xor &= 0xFF;
	  if (i != S->rdlinelen -3 || S->rdline[i] != '*')
	    return 0; // Wrong place to stop
	  if (sscanf(S->rdline+i, "*%02x%c", &csum, &csum) != 1) {
	    return 0; // Too little or too much
	  }
	  if (xor != csum) {
	    if (debug) printf("Bad DPRS $GP... checksum: %s vs. %02x\n", S->rdline+i, xor);
	    return 0;
	  }
	  return 1;
	} else {
	  // .. uh?  maybe?  Precisely 29 characters:
	  // "OH3KGR M,                    "
	  if (S->rdlinelen != 29 || S->rdline[8] != ',') {
	    if (debug) printf("Bad DPRS identification(?) packet - length(%d) != 29 || line[8] != ',': %s\n",
			      S->rdlinelen, S->rdline);
	    return 0;
	  }
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

	dprslog(S->rdline);

	if (dprsgw_isvalid(S)) {
	  // FIXME: Feed it to DPRS-APRS-GATE
	}
}


/*
 *  Receive more data from DPRS type serial port
 *  This handles correct data accumulation and sync hunting
 */
int dprsgw_pulldprs( struct serialport *S )
{
	int c;

	time_t rdtime = S->rdline_time;
	if (rdtime+2 < now) {
		// A timeout has happen? Either data is added constantly,
		// or nothing was received from DPRS datastream!

		S->rdline[S->rdlinelen] = 0;
		if (S->rdlinelen > 0)
		  dprslog(S->rdline);

		S->rdlinelen = 0;
		S->dprsstate = DPRSSTATE_SYNCHUNT;
	}
	S->rdline_time = now;

	for (;;) {

		c = ttyreader_getc(S);
		if (c < 0)
			return c;	/* Out of input.. */

		/* S->dprsstate != 0: read data into S->rdline,
		   == 0: discard data until CR|LF.
		   Zero-size read line is discarded as well
		   (only CR|LF on input frame)  */

		if (S->dprsstate == DPRSSTATE_SYNCHUNT) {
			/* Looking for CR or LF.. */
			if (c == '\n' || c == '\r') {
				// This should have re-synced before..
				S->rdlinelen = 0;
				continue;
			}
			// A '$' starts possible data..
			if (c == '$' && S->rdlinelen == 0) {
				S->rdline[S->rdlinelen++] = c;
				continue;
			}
			// More fits in?
			if (S->rdlinelen > 0 && S->rdlinelen >= sizeof(S->rdline)-3) {
			        // Too long a line...
				do {
					int len;
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

			// Too short to say anything?
			if (S->rdlinelen < 3) {
				continue;
			}
			if (S->rdlinelen == 3 &&
			    (memcmp("$$C", S->rdline, 3) != 0 &&
			     memcmp("$GP", S->rdline, 3) != 0)) {
				// No correct start, discard...
				S->rdlinelen = 2;
				memcpy(S->rdline, S->rdline+1, 2);
				if (S->rdline[0] != '$') {
					// Didn't start with a '$'
					S->rdlinelen = 0;
				}
				continue;
			}
			if (S->rdlinelen == 3 && memcmp(S->rdline, "$GP", 3) == 0) {
				S->dprsstate = DPRSSTATE_COLLECTING;
				continue;
			} else if (S->rdlinelen == 5 && memcmp(S->rdline, "$$CRC", 5) == 0) {
				S->dprsstate = DPRSSTATE_COLLECTING;
				continue;
			}
		}

		/* Now: (S->dprsstate != DPRSSTATE_SYNCHUNT)  */

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

		// Now see if the rdline is overflowing or not.
		// If it is, hunt for possible start-of-frame within the buffer,
		// and discard preceding data..
		if (S->rdlinelen >= (sizeof(S->rdline) - 3)) {	/* Too long !  Way too long ! */
			// Too long a line...
			do {
				int len;
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

		/* Put it on line store: */
		S->rdline[S->rdlinelen++] = c;

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

