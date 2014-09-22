/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation APRS iGate and digi with                 *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"


/*
 *  kissprocess()  --  the S->rdline[]  array has a KISS frame after
 *  KISS escape decode.  The frame begins with KISS command byte, then
 *  AX25 headers and payload, and possibly a CRC-checksum.
 *  Frame length is in S->rdlinelen variable.
 */

/* KA9Q describes the KISS frame format as follows:

   http://www.ka9q.net/papers/kiss.html

   - - - - - - - - -
   4. Control of the KISS TNC

   To distinguish between command and data frames on the host/TNC link,
   the first byte of each asynchronous frame between host and TNC is
   a "type" indicator.

   This type indicator byte is broken into two 4-bit nibbles so that
   the low-order nibble indicates the command number (given in the table
   below) and the high-order nibble indicates the port number for that
   particular command. In systems with only one HDLC port, it is by definition
   Port 0. In multi-port TNCs, the upper 4 bits of the type indicator
   byte can specify one of up to sixteen ports. The following commands
   are defined in frames to the TNC (the "Command" field is in hexadecimal):

   . . . . . .

   CMD code 0 is for the data frame, and is only one present coming from
   TNC to host.

   - - - - - - - - -

   SYMEK et al. have defined a way to run CRC inside KISS frames to
   verify that the KISS-frame itself is correct:

   http://www.symek.com/g/smack.html
   http://www.ir3ip.net/iw3fqg/doc/smak.htm

   SMACK variation recycles the top-most bit of the TNC-id nibble, and
   thus permits up to 8 TNC ports on line.  Top-most bit is always one
   on SMACK frames.

   SMACK runs CRC16 over whole KISS frame buffer, including the CMD byte.
   The CRC-code is thus _different_ from what will be sent out on radio,
   the latter being CRC-CCITT (see further below):

      Following CRC16-polynome is used:

         X^16 + X^15 + X^2 + 1

      The CRC-generator is preset to zero.

   Chosen initialize to zero does mean that after a correct packet with a
   correct checksum is ran thru this CRC, the output checksum will be zero.


   - - - - - - - - -

	Where is FLEXNET specification?
	

*/



/*
 * kissencoder():  If  (cmdbyte & 0x80) is set,  then this
 *                 produces SMACK format frame, otherwise 
 *                 plain KISS.
 *
 */

int kissencoder( void *kissbuf, int kissspace, LineType linetype,
		 const void *pktbuf, int pktlen, int cmdbyte )
{
	uint8_t *kb = kissbuf;
	uint8_t *ke = kb + kissspace - 3;
	const uint8_t *pkt = pktbuf;
	int i;
	uint16_t crc16;
	uint16_t crcflex;

	crc16   = crc16_table[cmdbyte & 0xFF];
	crcflex = 0xff00 ^ crc_flex_table[(~cmdbyte) & 0xff];

	/* Expect the KISS buffer to be at least ... 8 bytes.. */

	*kb++ = KISS_FEND;
	*kb++ = cmdbyte;

	for (i = 0; i < pktlen && kb < ke; ++i, ++pkt) {
		// Calc CRCs while encoding data..
		int b = *pkt;
		crc16 = ((crc16 >> 8) & 0xff) ^ crc16_table[(crc16 ^ b) & 0xFF];
		crcflex = (crcflex << 8) ^ crc_flex_table[((crcflex >> 8) ^ b) & 0xff];

		if (b == KISS_FEND) {
			*kb++ = KISS_FESC;
			*kb++ = KISS_TFEND;
		} else {
			*kb++ = b;
			if (b == KISS_FESC)
				*kb++ = KISS_TFESC;
		}
	}
	/* If caller is asking for SMACK format frame, then
	   store calculated CRC on frame. - CRC-bytes must be KISS escaped! */
	/* If caller is asking for SMACK/FLEXNET format frame, then
	   store calculated CRC on frame. - CRC-bytes must be KISS escaped! */
	if (linetype == LINETYPE_KISSSMACK ||
	    linetype == LINETYPE_KISSFLEXNET) {
		int crc, b;
		if (linetype == LINETYPE_KISSSMACK) {
		  crc = crc16;
		} else if (linetype == LINETYPE_KISSFLEXNET) {
		  crc = crcflex;
		} else {
                  // Silence compiler warning, this branch is never reached..
                  crc = 0;
                }

		b = crc & 0xFF;		/* low crc byte */
		if (b == KISS_FEND) {
		  if (kb < ke)
		    *kb++ = KISS_FESC;
		  if (kb < ke)
		    *kb++ = KISS_TFEND;
		} else {
		  if (kb < ke)
		    *kb++ = b;
		  if (b == KISS_FESC && kb < ke)
		    *kb++ = KISS_TFESC;
		}
		b = (crc >> 8) & 0xFF;	/* high crc byte */
		if (b == KISS_FEND) {
		  if (kb < ke)
		    *kb++ = KISS_FESC;
		  if (kb < ke)
		    *kb++ = KISS_TFEND;
		} else {
		  if (kb < ke)
		    *kb++ = b;
		  if (b == KISS_FESC && kb < ke)
		    *kb++ = KISS_TFESC;
		}
	}
	if (kb < ke) {
		*kb++ = KISS_FEND;
		return (kb - (uint8_t *) (kissbuf));
	} else {
		/* Didn't fit in... */
		return 0;
	}
}


static int kissprocess(struct serialport *S)
{
	int i;
	int cmdbyte = S->rdline[0];
	int tncid = (cmdbyte >> 4) & 0x0F;

	/* --
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

	/* printf("kissprocess()  cmdbyte=%02X len=%d ",cmdbyte,S->rdlinelen); */

	/* Ok, cmdbyte tells us something, and we should ignore the
	   frame if we don't know it... */

	if ((cmdbyte & 0x0F) != 0) {
		/* There should NEVER be any other value in the CMD bits
		   than 0  coming from TNC to host! */
		/* printf(" ..bad CMD byte\n"); */
		if (debug) {
		  printf("%ld\tTTY %s: Bad CMD byte on KISS frame: ", tick.tv_sec, S->ttyname);
		  hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
		  printf("\n");
		}
                rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
		erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */
		return -1;
	}

	if (S->linetype == LINETYPE_KISS && (cmdbyte & 0x20)) {
	  // Huh?  Perhaps a FLEXNET packet?
	  int crcflex = calc_crc_flex(S->rdline, S->rdlinelen);
	  if (crcflex == 0x7070) {
	    if (debug) printf("ALERT: Looks like received KISS frame is a FLEXNET with CRC!\n");
	    S->linetype = LINETYPE_KISSFLEXNET;
	  }
	}
	if (S->linetype == LINETYPE_KISS && (cmdbyte & 0x80)) {
	  // Huh?  Perhaps a SMACK packet?
	  int smack_ok = check_crc_16(S->rdline, S->rdlinelen);
	  if (smack_ok == 0) {
	    if (debug) printf("ALERT: Looks like received KISS frame is a SMACK with CRC!\n");
	    S->linetype = LINETYPE_KISSSMACK;
	  }
	}

	/* Are we expecting FLEXNET KISS ? */
	if (S->linetype == LINETYPE_KISSFLEXNET && (cmdbyte & 0x20)) {
	    int crc;
	    tncid &= ~0x20; // FlexNet puts 0x20 as indication of CRC presence..

	    if (S->ttycallsign[tncid] == NULL) {
	      /* D'OH!  received packet on multiplexer tncid without
		 callsign definition!  We discard this packet! */
	      if (debug > 0) {
		printf("%ld\tTTY %s: Bad TNCID on CMD byte on a KISS frame: %02x  No interface configured for it! ", tick.tv_sec, S->ttyname, cmdbyte);
		hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
		printf("\n");
	      }
              rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
	      erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */
	      return -1;
	    }
	    crc = calc_crc_flex(S->rdline, S->rdlinelen);
	    if (crc != 0x7070) {
	      if (debug) {
		printf("%ld\tTTY %s tncid %d: Received FLEXNET frame with invalid CRC %04x: ",
		       tick.tv_sec, S->ttyname, tncid, crc);
		hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
		printf("\n");
	      }
              rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
	      erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);  // Account one packet
	      return -1;	// The CRC was invalid..
	    }
	    S->rdlinelen -= 2; // remove 2 bytes!
	}

	/* Are we excepting BPQ "CRC" (XOR-sum of data) */
	if (S->linetype == LINETYPE_KISSBPQCRC) {
		/* TODO: in what conditions the "CRC" is calculated and when not ? */
		int xorsum = 0;

		if (S->ttycallsign[tncid] == NULL) {
		  /* D'OH!  received packet on multiplexer tncid without
		     callsign definition!  We discard this packet! */
		  if (debug > 0) {
		    printf("%ld\tTTY %s: Bad TNCID on CMD byte on a KISS frame: %02x  No interface configured for it! ", tick.tv_sec, S->ttyname, cmdbyte);
		    hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
		    printf("\n");
		  }
                  rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
		  erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */
		  return -1;
		}

		for (i = 1; i < S->rdlinelen; ++i)
			xorsum ^= S->rdline[i];
		xorsum &= 0xFF;
		if (xorsum != 0) {
			if (debug) {
			  printf("%ld\tTTY %s tncid %d: Received bad BPQCRC: %02x: ", tick.tv_sec, S->ttyname, tncid, xorsum);
			  hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
			  printf("\n");
			}
                        rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
			erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */
			return -1;
		}
		S->rdlinelen -= 1;	/* remove the sum-byte from tail */
		if (debug > 2)
			printf("%ld\tTTY %s tncid %d: Received OK BPQCRC frame\n", tick.tv_sec, S->ttyname, tncid);
	}
	/* Are we expecting SMACK ? */
	if (S->linetype == LINETYPE_KISSSMACK) {

	    tncid &= 0x07;	/* Chop off top bit */

	    if (S->ttycallsign[tncid] == NULL) {
	      /* D'OH!  received packet on multiplexer tncid without
		 callsign definition!  We discard this packet! */
	      if (debug > 0) {
		printf("%ld\tTTY %s: Bad TNCID on CMD byte on a KISS frame: %02x  No interface configured for it! ", tick.tv_sec, S->ttyname, cmdbyte);
		hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
		printf("\n");
	      }
              rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
	      erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */
	      return -1;
	    }

	    if ((cmdbyte & 0x8F) == 0x80) {
	        /* SMACK data frame */

		if (debug > 3)
		    printf("%ld\tTTY %s tncid %d: Received SMACK frame\n", tick.tv_sec, S->ttyname, tncid);

		if (!(S->smack_subids & (1 << tncid))) {
			aprxlog("Received SMACK frame TTY=%s tncid=%d",S->ttyname,tncid);
			if (debug)
				printf("%ld\t... marking received SMACK\n", tick.tv_sec);
		}
		S->smack_subids |= (1 << tncid);

		/* It is SMACK frame -- KISS with CRC16 at the tail.
		   Now we ignore the TNC-id number field.
		   Verify the CRC.. */

		// Whole buffer including CMD-byte!
		if (check_crc_16(S->rdline, S->rdlinelen) != 0) {
			if (debug) {
			  printf("%ld\tTTY %s tncid %d: Received SMACK frame with invalid CRC: ",
				 tick.tv_sec, S->ttyname, tncid);
			  hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
			  printf("\n");
			}
                        rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
			erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);  // Account one packet
			return -1;	/* The CRC was invalid.. */
		}

		S->rdlinelen -= 2;	/* Chop off the two CRC bytes */

	    } else if ((cmdbyte & 0x8F) == 0x00) {
	    	/*
		 * Expecting SMACK data, but got plain KISS data.
		 * Send a flow-rate limited probes to TNC to enable
		 * SMACK -- lets use 30 minutes window...
		 */


		S->smack_subids &= ~(1 << tncid); // Turn off the SMACK mode indication bit..

		if (debug > 2)
		    printf("%ld\tTTY %s tncid %d: Expected SMACK, got KISS.\n", tick.tv_sec, S->ttyname, tncid);

		if (timecmp(S->smack_probe[tncid], tick.tv_sec) < 0) {
		    uint8_t probe[4];
		    uint8_t kissbuf[12];
		    int kisslen;

		    probe[0] = cmdbyte | 0x80;  /* Make it into SMACK */
		    probe[1] = 0;

		    /* Convert the probe packet to KISS frame */
		    kisslen = kissencoder( kissbuf, sizeof(kissbuf), S->linetype,
					   &(probe[1]), 1, probe[0] );

		    /* Send probe message..  */
		    if (S->wrlen + kisslen < sizeof(S->wrbuf)) {
			/* There is enough space in writebuf! */

			memcpy(S->wrbuf + S->wrlen, kissbuf, kisslen);
			S->wrlen += kisslen;
			/* Flush it out..  and if not successfull,
			   poll(2) will take care of it soon enough.. */
			ttyreader_linewrite(S);

			S->smack_probe[tncid] = tick.tv_sec + 1800; /* 30 minutes */

			aprxlog("Sent SMACK activation probe TTY=%s tncid=%d",S->ttyname,tncid);
			if (debug)
				printf("%ld\tTTY %s tncid %d: Sending SMACK activation probe packet\n", tick.tv_sec, S->ttyname, tncid);

		    }
		    /* Else no space to write ?  Huh... */
		}
	    } else {
		// Else...  there should be no other kind data frames
		if (debug) {
		    printf("%ld\tTTY %s: Bad CMD byte on expected SMACK frame: %02x, len=%d: ",
			   tick.tv_sec, S->ttyname, cmdbyte, S->rdlinelen);
		    hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
		    printf("\n");
		}
                rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
		erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */
		return -1;
	    }
	}

	/* Are we expecting Basic KISS ? */
	if (S->linetype == LINETYPE_KISS) {
	    if (S->ttycallsign[tncid] == NULL) {
	      /* D'OH!  received packet on multiplexer tncid without
		 callsign definition!  We discard this packet! */
	      if (debug > 0) {
		printf("%ld\tTTY %s: Bad TNCID on CMD byte on a KISS frame: %02x  No interface configured for it! ", tick.tv_sec, S->ttyname, cmdbyte);
		hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
		printf("\n");
	      }
              rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
	      erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */
	      return -1;
	    }
	}


	if (S->rdlinelen < 17) {
		/* 7+7+2 bytes of minimal AX.25 frame + 1 for KISS CMD byte */

		/* Too short frame.. */
		/* printf(" ..too short a frame for anything\n");  */
                rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
		erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */
		return -1;
	}

	/* Valid AX.25 HDLC frame byte sequence is now at
	   S->rdline[1..S->rdlinelen-1]
	 */

	/* Send the frame to APRS-IS, return 1 if valid AX.25 UI message, does not
	   validate against valid APRS message rules... (TODO: it could do that too) */

	// The AX25_TO_TNC2 does validate the AX.25 packet,
	// converts it to "TNC2 monitor format" and sends it to
	// Rx-IGate functionality.  Returns non-zero only when
	// AX.25 header is OK, and packet is sane.

	erlang_add(S->ttycallsign[tncid], ERLANG_RX, S->rdlinelen, 1);	/* Account one packet */

	if (ax25_to_tnc2(S->interface[tncid], S->ttycallsign[tncid], tncid,
			 cmdbyte, S->rdline + 1, S->rdlinelen - 1)) {
		// The packet is valid per AX.25 header bit rules.

#ifdef PF_AX25	/* PF_AX25 exists -- highly likely a Linux system ! */
		/* Send the frame without cmdbyte to internal AX.25 network */
		if (S->netax25[tncid] != NULL)
			netax25_sendax25(S->netax25[tncid], S->rdline + 1, S->rdlinelen - 1);
#endif

	} else {
	  // The packet is not valid per AX.25 header bit rules
          rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
	  erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);	/* Account one packet */

	  if (aprxlogfile) {
            // NOT replaced with aprxlog() -- because this is a bit more complicated..
	    FILE *fp = fopen(aprxlogfile, "a");
	    if (fp) {
	      char timebuf[60];
	      printtime(timebuf, sizeof(timebuf));
              setlinebuf(fp);

	      fprintf(fp, "%s ax25_to_tnc2(%s,len=%d) rejected the message: ", timebuf, S->ttycallsign[tncid], S->rdlinelen-1);
	      hexdumpfp(fp, S->rdline, S->rdlinelen, 1);
	      fprintf(fp, "\n");
	      fclose(fp);
	    }
	  }
	}

	return 0;
}


/*
 * ttyreader_pullkiss()  --  pull KISS (or KISS+CRC) frame, and call KISS processor
 */

int kiss_pullkiss(struct serialport *S)
{
	/* printf("ttyreader_pullkiss()  rdlen=%d rdcursor=%d, state=%d\n",
	   S->rdlen, S->rdcursor, S->kissstate); fflush(stdout); */

	/* At incoming call there is at least one byte in between
	   S->rdcursor and S->rdlen  */

	/* Phases:
	   kissstate == 0: hunt for KISS_FEND, discard everything before it.
	   kissstate != 0: reading has globbed up preceding KISS_FENDs
	   ("HDLC flags") and the cursor is in front of a frame
	 */

	/* There are TNCs that use "shared flags" - only one FEND in between
	   data frames. */

	if (S->kissstate == KISSSTATE_SYNCHUNT) {
		/* Hunt for KISS_FEND, discard everything until then! */
		int c;
		for (;;) {
			c = ttyreader_getc(S);
			if (c < 0)
				return c;	/* Out of buffer, stay in state,
						   return latter when there is some
						   refill */
			if (c == KISS_FEND)	/* Found the sync-byte !  change state! */
				break;
		}
		S->kissstate = KISSSTATE_COLLECTING;
	}


	if (S->kissstate != KISSSTATE_SYNCHUNT) {
		/* Normal processing mode */

		int c;

		for (;;) {
			c = ttyreader_getc(S);
			if (c < 0)
				return c;	/* Out of input stream, exit now,
						   come back latter.. */

			/* printf(" %02X", c);
			   if (c == KISS_FEND) { printf("\n");fflush(stdout); }  */

			if (c == KISS_FEND) {
				/* Found end-of-frame character -- or possibly beginning..
				   This never exists in datastream except as itself. */

				if (S->rdlinelen > 0) {
					/* Non-zero sized frame  Process it away ! */
					kissprocess(S);
					S->kissstate =
						KISSSTATE_COLLECTING;
					S->rdlinelen = 0;
				}

				/* rdlinelen == 0 because we are receiving consequtive
				   FENDs, or just processed our previous frame.  Treat
				   them the same: discard this byte. */

				continue;
			}

			if (S->kissstate == KISSSTATE_KISSFESC) {

				/* We have some char, state switches to normal collecting */
				S->kissstate = KISSSTATE_COLLECTING;

				if (c == KISS_TFEND)
					c = KISS_FEND;
				else if (c == KISS_TFESC)
					c = KISS_FESC;
				else
					continue;	/* Accepted chars after KISS_FESC
							   are only TFEND and TFESC.
							   Others must be discarded. */

			} else {	/* Normal collection mode */

				if (c == KISS_FESC) {
					S->kissstate = KISSSTATE_KISSFESC;
					continue;	/* Back to top of the loop and continue.. */
				}

			}


			if (S->rdlinelen >= (sizeof(S->rdline) - 3)) {
				/* Too long !  Way too long ! */

				S->kissstate = KISSSTATE_SYNCHUNT;	/* Sigh.. discard it. */
				S->rdlinelen = 0;
				if (debug) {
				  printf("%ld\tTTY %s: Too long frame to be KISS: ", tick.tv_sec, S->ttyname);
				  hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
				  printf("\n");
				}
				continue;
			}

			/* Put it on record store: */
			S->rdline[S->rdlinelen++] = c;
		}		/* .. for(..) loop of data collecting */

	}
	/* .. normal consumption mode ... */
	return 0;
}


/*
 *  kiss_kisswrite()  -- write out buffered data
 */
void kiss_kisswrite(struct serialport *S, const int tncid, const uint8_t *ax25raw, const int ax25rawlen)
{
	int i, len, ssid;
	uint8_t kissbuf[2300];

	if (debug) {
	  printf("kiss_kisswrite(->%s, axlen=%d)\n", S->ttycallsign[tncid], ax25rawlen);
	}
	if (S->fd < 0) {
	  if (debug)
	    printf("NOTE: Write to non-open serial port discarded.");
	  return;
	}


	if ((S->linetype != LINETYPE_KISS)        && (S->linetype != LINETYPE_KISSSMACK) &&
	    (S->linetype != LINETYPE_KISSFLEXNET) && (S->linetype != LINETYPE_KISSBPQCRC)) {
		if (debug)
		  printf("WARNING: WRITING KISS FRAMES ON SERIAL/TCP LINE OF NO KISS TYPE IS UNSUPPORTED!\n");
		return;
	}


	if ((S->wrlen == 0) || (S->wrlen > 0 && S->wrcursor >= S->wrlen)) {
		S->wrlen = S->wrcursor = 0;
	} else {
	  /* There is some data in between wrcursor and wrlen */
	  len = S->wrlen - S->wrcursor;
	  if (len > 0) {
	    i = write(S->fd, S->wrbuf + S->wrcursor, len);
	  } else
	    i = 0;
	  if (i > 0) {		/* wrote something */
	    S->wrcursor += i;
	    len = S->wrlen - S->wrcursor;
	    if (len == 0) {
	      S->wrcursor = S->wrlen = 0;	/* wrote all ! */
	    } else {
	      /* compact the buffer a bit */
	      memcpy(S->wrbuf, S->wrbuf + S->wrcursor, len);
	      S->wrcursor = 0;
	      S->wrlen = len;
	    }
	  }
	}

	ssid = (tncid << 4);
	switch (S->linetype) {
	case LINETYPE_KISSFLEXNET:
	  len = kissencoder( kissbuf, sizeof(kissbuf), S->linetype, ax25raw, ax25rawlen, ssid |= 0x20 );
	  break;
	case LINETYPE_KISSSMACK:
	  if (S->smack_subids & (1 << tncid)) //if SMACK currently active
	    len = kissencoder( kissbuf, sizeof(kissbuf), S->linetype, ax25raw, ax25rawlen, ssid |= 0x80 );
	  else 
	    len = kissencoder( kissbuf, sizeof(kissbuf), LINETYPE_KISS, ax25raw, ax25rawlen, ssid );
	  break;
	default:
	  len = kissencoder( kissbuf, sizeof(kissbuf), S->linetype, ax25raw, ax25rawlen, ssid );
	  break;
	}

	if (debug>2) {
	  printf("ssid=%0x S->smack_subids=%0x\n",ssid,S->smack_subids);
	  printf("kiss-encoded: ");
	  hexdumpfp(stdout, kissbuf, len, 1);
	  printf("\n");
	}

	// Will the KISS encoded frame fit in the link buffer?
	if ((S->wrlen + len) < sizeof(S->wrbuf)) {
		memcpy(S->wrbuf + S->wrlen, kissbuf, len);
		S->wrlen += len;
		erlang_add(S->ttycallsign[tncid], ERLANG_TX, ax25rawlen, 1);

		if (debug)
		  printf(" .. put %d bytes of KISS frame on IO buffer\n",len);
	} else {
		// No fit!
		if (debug)
		  printf(" .. %d bytes of KISS frame did not fit on IO buffer\n",len);
		return;
	}

	// Try to write it immediately
	len = S->wrlen - S->wrcursor;
	if (len > 0)
	  i = write(S->fd, S->wrbuf + S->wrcursor, len);
	else
	  i = 0;
	if (i > 0) {		/* wrote something */
		S->wrcursor += i;
		len = S->wrlen - S->wrcursor; /* all done? */
		if (len == 0) {
			S->wrcursor = S->wrlen = 0;	/* wrote all ! */
		} else {
			/* compact the buffer a bit */
			memcpy(S->wrbuf, S->wrbuf + S->wrcursor, len);
			S->wrcursor = 0;
			S->wrlen = len;
		}
	}
}


void kiss_poll(struct serialport *S)
{
	uint8_t probe[1];
        uint8_t kissbuf[12];
        int kisslen;
        int tncid;

        for (tncid = 0; tncid < 16; ++tncid) {

		if (S->interface[tncid] == NULL) {
			// No sub-interface here..
			continue;
                }

                probe[0] = 0x0E | (tncid << 4);

                /* Convert the probe packet to KISS frame */
                kisslen = kissencoder( kissbuf, sizeof(kissbuf), S->linetype,
                                       &(probe[0]), 0, probe[0] );
                
                /* Send probe message..  */
                if (S->wrlen + kisslen < sizeof(S->wrbuf)) {
                	/* There is enough space in writebuf! */
          
	        	memcpy(S->wrbuf + S->wrlen, kissbuf, kisslen);
                        S->wrlen += kisslen;
                        /* Flush it out..  and if not successfull,
                           poll(2) will take care of it soon enough.. */
                        ttyreader_linewrite(S);
          
                        if (debug)
                          printf("%ld.%06d\tTTY %s tncid %d: Sending KISS POLL\n", (long)tick.tv_sec, (int)tick.tv_usec, S->ttyname, tncid);
		}
	}
}
