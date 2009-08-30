/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * **************************************************************** */


#include "aprx.h"
#include <sys/socket.h>
#include <netdb.h>


/* The ttyreader does read TTY ports into a big buffer, and then from there
   to packet frames depending on what is attached...  */


typedef enum {
	LINETYPE_KISS,		/* all KISS variants without CRC on line */
	LINETYPE_KISSSMACK,	/* KISS/SMACK variants with CRC on line */
	LINETYPE_KISSBPQCRC,	/* BPQCRC - really XOR sum of data bytes,
				   also "AEACRC"                        */
	LINETYPE_TNC2,		/* text line from TNC2 in monitor mode  */
	LINETYPE_AEA		/* not implemented...                   */
} LineType;

typedef enum {
	KISSSTATE_SYNCHUNT = 0,
	KISSSTATE_COLLECTING,
	KISSSTATE_KISSFESC
} KissState;


struct serialport {
	int fd;			/* UNIX fd of the port                  */

	time_t wait_until;
	time_t last_read_something;	/* Used by serial port functionality
					   watchdog */
	int read_timeout;	/* seconds                              */

	LineType linetype;

	KissState kissstate;	/* state for KISS frame reader,
				   also for line collector              */

	/* NOTE: The smack_probe is separate on all
	**       sub-tnc:s on SMACK loop
	*/
	time_t smack_probe[8];	/* if need to send SMACK probe, use this
				   to limit their transmit frequency.	*/
	int    smack_subids;    /* bitset; 0..7; could use char...	*/


	struct termios tio;	/* tcsetattr(fd, TCSAFLUSH, &tio)       */
	/*  stty speed 19200 sane clocal pass8 min 1 time 5 -hupcl ignbrk -echo -ixon -ixoff -icanon  */

	const char *ttyname;	/* "/dev/ttyUSB1234-bar22-xyz7" --
				   Linux TTY-names can be long..        */
	const char *ttycallsign;/* callsign                             */
	char *initstring;	/* optional init-string to be sent to
				   the TNC, NULL OK                     */
	int initlen;		/* .. as it can have even NUL-bytes,
				   length is important!                 */

	unsigned char rdbuf[1000];	/* buffering area for raw stream read */
	int rdlen, rdcursor;	/* rdlen = last byte in buffer,
				   rdcursor = next to read.
				   When rdlen == 0, buffer is empty.    */
	unsigned char rdline[330];	/* processed into lines/records       */
	int rdlinelen;		/* length of this record                */

	unsigned char wrbuf[1000];	/* buffering area for raw stream read */
	int wrlen, wrcursor;	/* wrlen = last byte in buffer,
				   wrcursor = next to write.
				   When wrlen == 0, buffer is empty.    */
};

static struct serialport **ttys;
static int ttycount;		/* How many are defined ? */

static void ttyreader_linewrite(struct serialport *S); /* forward declaration */

#define TTY_OPEN_RETRY_DELAY_SECS 30


/* KISS protocol encoder/decoder specials */

#define KISS_FEND  (0xC0)
#define KISS_FESC  (0xDB)
#define KISS_TFEND (0xDC)
#define KISS_TFESC (0xDD)


/*
 *  ttyreader_kissprocess()  --  the S->rdline[]  array has a KISS frame after
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
   The CRC-code is thus _different_ from what will be sent out on radio.

*/


int crc16_calc(unsigned char *buf, int n)
{

	static int crc_table[] = {
		0x0000, 0xc0c1, 0xc181, 0x0140,
		0xc301, 0x03c0, 0x0280,	0xc241,
		0xc601, 0x06c0, 0x0780, 0xc741,
		0x0500, 0xc5c1, 0xc481,	0x0440,
		0xcc01, 0xcc0, 0x0d80, 0xcd41,
		0x0f00, 0xcfc1, 0xce81,	0x0e40,
		0x0a00, 0xcac1, 0xcb81, 0x0b40,
		0xc901, 0x09c0, 0x0880,	0xc841,
		0xd801, 0x18c0, 0x1980, 0xd941,
		0x1b00, 0xdbc1, 0xda81,	0x1a40,
		0x1e00, 0xdec1, 0xdf81, 0x1f40,
		0xdd01, 0x1dc0, 0x1c80,	0xdc41,
		0x1400, 0xd4c1, 0xd581, 0x1540,
		0xd701, 0x17c0, 0x1680,	0xd641,
		0xd201, 0x12c0, 0x1380, 0xd341,
		0x1100, 0xd1c1, 0xd081,	0x1040,
		0xf001, 0x30c0, 0x3180, 0xf141,
		0x3300, 0xf3c1, 0xf281,	0x3240,
		0x3600, 0xf6c1, 0xf781, 0x3740,
		0xf501, 0x35c0, 0x3480,	0xf441,
		0x3c00, 0xfcc1, 0xfd81, 0x3d40,
		0xff01, 0x3fc0, 0x3e80,	0xfe41,
		0xfa01, 0x3ac0, 0x3b80, 0xfb41,
		0x3900, 0xf9c1, 0xf881,	0x3840,
		0x2800, 0xe8c1, 0xe981, 0x2940,
		0xeb01, 0x2bc0, 0x2a80,	0xea41,
		0xee01, 0x2ec0, 0x2f80, 0xef41,
		0x2d00, 0xedc1, 0xec81,	0x2c40,
		0xe401, 0x24c0, 0x2580, 0xe541,
		0x2700, 0xe7c1, 0xe681,	0x2640,
		0x2200, 0xe2c1, 0xe381, 0x2340,
		0xe101, 0x21c0, 0x2080,	0xe041,
		0xa001, 0x60c0, 0x6180, 0xa141,
		0x6300, 0xa3c1, 0xa281,	0x6240,
		0x6600, 0xa6c1, 0xa781, 0x6740,
		0xa501, 0x65c0, 0x6480,	0xa441,
		0x6c00, 0xacc1, 0xad81, 0x6d40,
		0xaf01, 0x6fc0, 0x6e80,	0xae41,
		0xaa01, 0x6ac0, 0x6b80, 0xab41,
		0x6900, 0xa9c1, 0xa881,	0x6840,
		0x7800, 0xb8c1, 0xb981, 0x7940,
		0xbb01, 0x7bc0, 0x7a80,	0xba41,
		0xbe01, 0x7ec0, 0x7f80, 0xbf41,
		0x7d00, 0xbdc1, 0xbc81,	0x7c40,
		0xb401, 0x74c0, 0x7580, 0xb541,
		0x7700, 0xb7c1, 0xb681,	0x7640,
		0x7200, 0xb2c1, 0xb381, 0x7340,
		0xb101, 0x71c0, 0x7080,	0xb041,
		0x5000, 0x90c1, 0x9181, 0x5140,
		0x9301, 0x53c0, 0x5280,	0x9241,
		0x9601, 0x56c0, 0x5780, 0x9741,
		0x5500, 0x95c1, 0x9481,	0x5440,
		0x9c01, 0x5cc0, 0x5d80, 0x9d41,
		0x5f00, 0x9fc1, 0x9e81,	0x5e40,
		0x5a00, 0x9ac1, 0x9b81, 0x5b40,
		0x9901, 0x59c0, 0x5880,	0x9841,
		0x8801, 0x48c0, 0x4980, 0x8941,
		0x4b00, 0x8bc1, 0x8a81,	0x4a40,
		0x4e00, 0x8ec1, 0x8f81, 0x4f40,
		0x8d01, 0x4dc0, 0x4c80,	0x8c41,
		0x4400, 0x84c1, 0x8581, 0x4540,
		0x8701, 0x47c0, 0x4680,	0x8641,
		0x8201, 0x42c0, 0x4380, 0x8341,
		0x4100, 0x81c1, 0x8081,	0x4040
	};

	int crc = 0;

	while (--n >= 0)
		crc = ((crc >> 8) & 0xff) ^ crc_table[(crc ^ *buf++) &
						      0xFF];
	return crc;
}

int kissencoder( void *kissbuf, int kissspace,
		 const void *pktbuf, int pktlen, int cmdbyte )
{
	unsigned char *kb = kissbuf;
	unsigned char *ke = kb + kissspace - 3;
	const unsigned char *pkt = pktbuf;
	int i;

	/* Expect the KISS buffer to be at least ... 6 bytes.. */

	*kb++ = KISS_FEND;
	*kb++ = cmdbyte;
	for (i = 0; i < pktlen && kb < ke; ++i, ++pkt) {
		/* todo: add here crc-calculators.. */
		if (*pkt == KISS_FEND) {
			*kb++ = KISS_FESC;
			*kb++ = KISS_TFEND;
		} else {
			*kb++ = *pkt;
			if (*pkt == KISS_FESC)
				*kb++ = KISS_TFESC;
		}
	}
	if (kb < ke) {
		*kb++ = KISS_FEND;
		return (kb - (unsigned char *) (kissbuf));
	} else {
		/* Didn't fit in... */
		return 0;
	}
}

static int ttyreader_kissprocess(struct serialport *S)
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

	/* printf("ttyreader_kissprocess()  cmdbyte=%02X len=%d ",cmdbyte,S->rdlinelen); */

	/* Ok, cmdbyte tells us something, and we should ignore the
	   frame if we don't know it... */

	if ((cmdbyte & 0x0F) != 0) {
		/* There should NEVER be any other value in the CMD bits
		   than 0  coming from TNC to host! */
		/* printf(" ..bad CMD byte\n"); */
		if (debug)
			printf("%ld\tTTY %s: Bad CMD byte on KISS frame: %02x\n", now, S->ttyname, cmdbyte);
		return -1;
	}

	/* Are we excepting BPQ "CRC" (XOR-sum of data) */
	if (S->linetype == LINETYPE_KISSBPQCRC) {
		/* TODO: in what conditions the "CRC" is calculated and when not ? */
		int xorsum = 0;
		for (i = 1; i < S->rdlinelen; ++i)
			xorsum ^= S->rdline[i];
		xorsum &= 0xFF;
		if (xorsum != 0) {
			if (debug)
				printf("%ld\tTTY %s tncid %d: Received bad BPQCRC: %02x\n", now, S->ttyname, tncid, xorsum);
			return -1;
		}
		S->rdlinelen -= 1;	/* remove the sum-byte from tail */
		if (debug > 2)
			printf("%ld\tTTY %s tncid %d: Received OK BPQCRC frame\n", now, S->ttyname, tncid);
	}
	/* Are we expecting SMACK ? */
	if (S->linetype == LINETYPE_KISSSMACK) {

	    tncid &= 0x07;	/* Chop off top bit */

	    if ((cmdbyte & 0x8F) == 0x80) {
	        /* SMACK data frame */

		if (debug > 3)
		    printf("%ld\tTTY %s tncid %d: Received SMACK frame\n", now, S->ttyname, tncid);

		if (!(S->smack_subids & (1 << tncid))) {
		    if (debug)
			printf("%ld\t... marking received SMACK\n", now);
		}
		S->smack_subids |= (1 << tncid);

		/* It is SMACK frame -- KISS with CRC16 at the tail.
		   Now we ignore the TNC-id number field.
		   Verify the CRC.. */

		// Whole buffer including CMD-byte!
		if (crc16_calc(S->rdline, S->rdlinelen) != 0) {
			if (debug)
				printf("%ld\tTTY %s tncid %d: Received SMACK frame with invalid CRC\n",
				       now, S->ttyname, tncid);
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
		    printf("%ld\tTTY %s tncid %d: Expected SMACK, got KISS.\n", now, S->ttyname, tncid);

		if (S->smack_probe[tncid] < now) {
		    unsigned char probe[4];
		    unsigned char kissbuf[12];
		    int kisslen;
		    int crc;

		    probe[0] = cmdbyte | 0x80;  /* Make it into SMACK */
		    probe[1] = 0;
		    crc = crc16_calc(probe, 2);
		    probe[2] =  crc       & 0xFF;  /* low  CRC byte */
		    probe[3] = (crc >> 8) & 0xFF;  /* high CRC byte */

		    /* Convert the probe packet to KISS frame */
		    kisslen = kissencoder( kissbuf, sizeof(kissbuf),
					   probe+1, 4-1, probe[0] );

		    /* Send probe message..  */
		    if (S->wrlen + kisslen < sizeof(S->wrbuf)) {
			/* There is enough space in writebuf! */

			memcpy(S->wrbuf + S->wrlen, kissbuf, kisslen);
			S->wrlen += kisslen;
			/* Flush it out..  and if not successfull,
			   poll(2) will take care of it soon enough.. */
			ttyreader_linewrite(S);

			S->smack_probe[tncid] = now + 30*60; /* 30 minutes */

			if (debug)
			    printf("%ld\tTTY %s tncid %d: Sending SMACK activation probe packet\n", now, S->ttyname, tncid);

		    }
		    /* Else no space to write ?  Huh... */
		}
	    } else {
		// Else...  there should be no other kind data frames
		if (debug)
		    printf("%ld\tTTY %s: Bad CMD byte on expected SMACK frame: %02x,  len=%d\n",
			   now, S->ttyname, cmdbyte, S->rdlinelen);
	    }
	}

	if (S->rdlinelen < 17) {
		/* 7+7+2 bytes of minimal AX.25 frame + 1 for KISS CMD byte */

		/* Too short frame.. */
		/* printf(" ..too short a frame for anything\n");  */
		return -1;
	}

	/* Valid AX.25 HDLC frame byte sequence is now at
	   S->rdline[1..S->rdlinelen-1]
	 */

	/* Send the frame without cmdbyte to internal AX.25 network */
	netax25_sendax25(S->rdline + 1, S->rdlinelen - 1);

	/* Send the frame to APRS-IS */
	ax25_to_tnc2(S->ttycallsign, tncid, cmdbyte, S->rdline + 1, S->rdlinelen - 1);
	erlang_add(S, S->ttycallsign, tncid, ERLANG_RX, S->rdlinelen, 1);	/* Account one packet */

	return 0;
}


/*
 *  ttyreader_getc()  -- pick one char ( >= 0 ) out of input buffer, or -1 if out of buffer
 */
static int ttyreader_getc(struct serialport *S)
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


/*
 * ttyreader_pullkiss()  --  pull KISS (or KISS+CRC) frame, and call KISS processor
 */

static int ttyreader_pullkiss(struct serialport *S)
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
					ttyreader_kissprocess(S);
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
				if (debug)
					printf("%ld\tTTY %s: Too long frame to be KISS..\n", now, S->ttyname);
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
 *  ttyreader_pulltnc2()  --  process a line of text by calling
 *				TNC2 UI Monitor analyzer
 */

static int ttyreader_pulltnc2(struct serialport *S)
{
	/* Send the frame to internal AX.25 network */
	/* netax25_sendax25_tnc2(S->rdline, S->rdlinelen); */

	/* S->rdline[] has text line without line ending CR/LF chars   */
	igate_to_aprsis(S->ttycallsign, 0, (char *) (S->rdline), S->rdlinelen, 0);

	erlang_add(S, S->ttycallsign, 0, ERLANG_RX, S->rdlinelen, 1);	/* Account one packet */

	return 0;
}

#if 0
/*
 * ttyreader_pullaea()  --  process a line of text by calling
 * 			    AEA MONITOR 1 analyzer
 */

static int ttyreader_pullaea(struct serialport *S)
{
	int i;

	if (S->rdline[S->rdlinelen - 1] == ':') {
		/* Could this be the AX25 header ? */
		char *s = strchr(S->rdline, '>');
		if (s) {
			/* Ah yes, it well could be.. */
			strcpy(S->rdline2, S->rdline);
			return;
		}
	}

	/* FIXME: re-arrange the  S->rdline2  contained AX25 address tokens 
	   and flags..

	   perl code:
	   @addrs = split('>', $rdline2);
	   $out = shift @addrs; # pop first token in sequence
	   $out .= '>';
	   $out .= pop @addrs;  # pop last token in sequence
	   foreach $a (@addrs) { # rest of the tokens in sequence, if any
	   $out .= ',' . $a;
	   }
	   # now $out has address data in TNC2 sequence.
	 */

	/* printf("%s%s\n", S->rdline2, S->rdline); fflush(stdout); */

	return 0;
}
#endif


/*
 *  ttyreader_pulltext()  -- process a line of text from the serial port..
 */

static int ttyreader_pulltext(struct serialport *S)
{
	int c;

	for (;;) {

		c = ttyreader_getc(S);
		if (c < 0)
			return c;	/* Out of input.. */

		/* S->kissstate != 0: read data into S->rdline,
		   == 0: discard data until CR|LF.
		   Zero-size read line is discarded as well
		   (only CR|LF on input frame)  */

		if (S->kissstate == KISSSTATE_SYNCHUNT) {
			/* Looking for CR or LF.. */
			if (c == '\n' || c == '\r')
				S->kissstate = KISSSTATE_COLLECTING;

			S->rdlinelen = 0;
			continue;
		}

		/* Now: (S->kissstate != KISSSTATE_SYNCHUNT)  */

		if (c == '\n' || c == '\r') {
			/* End of line seen! */
			if (S->rdlinelen > 0) {

				/* Non-zero-size string, put terminating 0 byte on it. */
				S->rdline[S->rdlinelen] = 0;

				/* .. and process it depending ..  */

				if (S->linetype == LINETYPE_TNC2) {
					ttyreader_pulltnc2(S);
#if 0
				} else {	/* .. it is LINETYPE_AEA ? */
					ttyreader_pullaea(S);
#endif
				}
			}
			S->rdlinelen = 0;
			continue;
		}

		/* Now place the char in the linebuffer, if there is space.. */
		if (S->rdlinelen >= (sizeof(S->rdline) - 3)) {	/* Too long !  Way too long ! */
			S->kissstate = KISSSTATE_SYNCHUNT;	/* Sigh.. discard it. */
			S->rdlinelen = 0;
			continue;
		}

		/* Put it on line store: */
		S->rdline[S->rdlinelen++] = c;

	}			/* .. input loop */

	return 0;		/* not reached */
}


/*
 *  ttyreader_linewrite()  -- write out buffered data
 */
static void ttyreader_linewrite(struct serialport *S)
{
	int i, len;

	if ((S->wrlen == 0) || (S->wrlen > 0 && S->wrcursor >= S->wrlen)) {
		S->wrlen = S->wrcursor = 0;	/* already all written */
		return;
	}

	/* Now there is some data in between wrcursor and wrlen */

	len = S->wrlen - S->wrcursor;
	i = write(S->fd, S->wrbuf + S->wrcursor, len);
	if (i > 0) {		/* wrote something */
		S->wrcursor += i;
		erlang_add(S, S->ttycallsign, 0, ERLANG_TX, i, 0);
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


/*
 *  ttyreader_lineread()  --  read what there is into our buffer,
 *			      and process the buffer..
 */

static void ttyreader_lineread(struct serialport *S)
{
	int i;

	int rdspace = sizeof(S->rdbuf) - S->rdlen;

	if (S->rdcursor > 0) {
		/* Read-out cursor is not at block beginning,
		   is there unread data too ?  */
		if (S->rdlen > S->rdcursor) {
			/* Uh..  lets move buffer down a bit,
			   to make room for more to the end.. */
			memcpy(S->rdbuf, S->rdbuf + S->rdcursor,
			       S->rdlen - S->rdcursor);
			S->rdlen = S->rdlen - S->rdcursor;
		} else
			S->rdlen = 0;	/* all processed, mark its size zero */
		/* Cursor to zero, rdspace recalculated */
		S->rdcursor = 0;

		/* recalculate */
		rdspace = sizeof(S->rdbuf) - S->rdlen;
	}

	if (rdspace > 0) {	/* We have room to read into.. */
		i = read(S->fd, S->rdbuf + S->rdlen, rdspace);
		if (i == 0) {	/* EOF ?  USB unplugged ? */
			close(S->fd);
			S->fd = -1;
			S->wait_until = now + TTY_OPEN_RETRY_DELAY_SECS;
			if (debug)
				printf("%ld\tTTY %s EOF - CLOSED, WAITING %d SECS\n", now, S->ttyname, TTY_OPEN_RETRY_DELAY_SECS);
			return;
		}
		if (i < 0)	/* EAGAIN or whatever.. */
			return;

		/* Some data has been accumulated ! */
		S->rdlen += i;
		S->last_read_something = now;
	}

	/* Done reading, maybe.  Now processing.
	   The pullXX does read up all input, and does
	   however many frames there are in, and pauses
	   when there is no enough input data for a full
	   frame/line/whatever.
	 */

	if (S->linetype == LINETYPE_KISS ||
	    S->linetype == LINETYPE_KISSSMACK) {

		ttyreader_pullkiss(S);


	} else if (S->linetype == LINETYPE_TNC2
#if 0
		   || S->linetype == LINETYPE_AEA
#endif
		) {

		ttyreader_pulltext(S);

	} else {
		close(S->fd);	/* Urgh ?? Bad linetype value ?? */
		S->fd = -1;
		S->wait_until = now + TTY_OPEN_RETRY_DELAY_SECS;
	}

	/* Consumed something, and our read cursor is not in the beginning ? */
	if (S->rdcursor > 0 && S->rdcursor < S->rdlen) {
		/* Compact the input buffer! */
		memmove(S->rdbuf, S->rdbuf + S->rdcursor,
			S->rdlen - S->rdcursor);
	}
	S->rdlen -= S->rdcursor;
	S->rdcursor = 0;
}


/*
 * ttyreader_linesetup()  --  open and configure the serial port
 */

static void ttyreader_linesetup(struct serialport *S)
{
	int i;

	S->wait_until = 0;	/* Zero it just to be safe */

	S->wrlen = S->wrcursor = 0;	/* init them at first */

	if (memcmp(S->ttyname, "tcp!", 4) != 0) {

		S->fd = open(S->ttyname, O_RDWR | O_NOCTTY | O_NONBLOCK,
			     0);
		if (debug)
			printf("%ld\tTTY %s OPEN - ", now, S->ttyname);
		if (S->fd < 0) {	/* Urgh.. an error.. */
			S->wait_until = now + TTY_OPEN_RETRY_DELAY_SECS;
			if (debug)
				printf("FAILED, WAITING %d SECS\n",
				       TTY_OPEN_RETRY_DELAY_SECS);
			return;
		}
		if (debug)
			printf("OK\n");

		/* Set attributes, and flush the serial port queue */
		i = tcsetattr(S->fd, TCSAFLUSH, &S->tio);

		if (i < 0) {
			close(S->fd);
			S->fd = -1;
			S->wait_until = now + TTY_OPEN_RETRY_DELAY_SECS;
			return;
		}
		/* FIXME: ??  Set baud-rates ?
		   Used system (Linux) has them in   'struct termios'  so they
		   are now set, but other systems may have different ways..
		 */

		if (S->initstring != NULL) {
			memcpy(S->wrbuf + S->wrlen, S->initstring, S->initlen);
			S->wrlen += S->initlen;

			/* Flush it out..  and if not successfull,
			   poll(2) will take care of it soon enough.. */
			ttyreader_linewrite(S);
		}
	} else {		/* socket connection to remote TTY.. */
		/*   "tcp!hostname-or-ip!port!opt-parameters" */
		char *par = strdup(S->ttyname);
		char *host = NULL, *port = NULL, *opts = NULL;
		struct addrinfo req, *ai;
		int i;

		if (debug)
			printf("socket connect() preparing: %s\n", par);

		for (;;) {
			host = strchr(par, '!');
			if (host)
				++host;
			else
				break;	/* Found no '!' ! */
			port = strchr(host, '!');
			if (port)
				*port++ = 0;
			else
				break;	/* Found no '!' ! */
			opts = strchr(port, '!');
			if (opts)
				*opts++ = 0;
			break;
		}

		if (!port) {
			/* Still error condition.. no port data */
		}

		memset(&req, 0, sizeof(req));
		req.ai_socktype = SOCK_STREAM;
		req.ai_protocol = IPPROTO_TCP;
		req.ai_flags = 0;
#if 1
		req.ai_family = AF_UNSPEC;	/* IPv4 and IPv6 are both OK */
#else
		req.ai_family = AF_INET;	/* IPv4 only */
#endif
		ai = NULL;

		i = getaddrinfo(host, port, &req, &ai);

		if (ai) {
			S->fd = socket(ai->ai_family, SOCK_STREAM, 0);
			if (S->fd >= 0) {

				fd_nonblockingmode(S->fd);

				i = connect(S->fd, ai->ai_addr,
					    ai->ai_addrlen);
				if ((i != 0) && (errno != EINPROGRESS)) {
					/* non-blocking connect() yeilds EINPROGRESS,
					   anything else and we fail entirely...      */
					if (debug)
						printf("ttyreader socket connect call failed: %d : %s\n", errno, strerror(errno));
					close(S->fd);
					S->fd = -1;
				}
			}

			freeaddrinfo(ai);
		}
		free(par);
	}

	S->last_read_something = now;	/* mark the timeout for future.. */

	S->rdlen = S->rdcursor = S->rdlinelen = 0;
	S->kissstate = 0;	/* Zero it, whatever protocol we actually use
				   will consider it as 'hunt for sync' state. */

	memset( S->smack_probe, 0, sizeof(S->smack_probe) );
	S->smack_subids = 0;
}

/*
 *  ttyreader_init()
 */

void ttyreader_init(void)
{
	/* nothing.. */
}



/*
 *  ttyreader_prepoll()  --  prepare system for next round of polling
 */

int ttyreader_prepoll(struct aprxpolls *app)
{
	int idx = 0;		/* returns number of *fds filled.. */
	int i;
	struct serialport *S;
	struct pollfd *pfd;

	for (i = 0; i < ttycount; ++i) {
		S = ttys[i];
		if (!S->ttyname)
			continue;	/* No name, no look... */
		if (S->fd < 0) {
			/* Not an open TTY, but perhaps waiting ? */
			if ((S->wait_until != 0) && (S->wait_until > now)) {
				/* .. waiting for future! */
				if (app->next_timeout > S->wait_until)
					app->next_timeout = S->wait_until;
				/* .. but only until our timeout,
				   if it is sooner than global one. */
				continue;	/* Waiting on this one.. */
			}

			/* Waiting or not, FD is not open, and deadline is past.
			   Lets try to open! */

			ttyreader_linesetup(S);

		}
		/* .. No open FD */
		/* Still no open FD ? */
		if (S->fd < 0)
			continue;

		/* FD is open, check read/idle timeout ... */
		if ((S->read_timeout > 0) &&
		    (now > (S->last_read_something + S->read_timeout))) {
			close(S->fd);	/* Close and mark for re-open */
			S->fd = -1;
			S->wait_until = now + TTY_OPEN_RETRY_DELAY_SECS;
			continue;
		}

		/* FD is open, lets mark it for poll read.. */
		pfd = aprxpolls_new(app);
		pfd->fd = S->fd;
		pfd->events = POLLIN | POLLPRI;
		pfd->revents = 0;
		if (S->wrlen > 0 && S->wrlen > S->wrcursor)
			pfd->events |= POLLOUT;

		++idx;
	}
	return idx;
}

/*
 *  ttyreader_postpoll()  -- Done polling, what happened ?
 */

int ttyreader_postpoll(struct aprxpolls *app)
{
	int idx, i;

	struct serialport *S;
	struct pollfd *P;
	for (idx = 0, P = app->polls; idx < app->pollcount; ++idx, ++P) {

		if (!(P->revents & (POLLIN | POLLPRI | POLLERR | POLLHUP)))
			continue;	/* No read event we are interested in... */

		for (i = 0; i < ttycount; ++i) {
			S = ttys[i];
			if (S->fd != P->fd)
				continue;	/* Not this one ? */
			/* It is this one! */

			if (P->revents & POLLOUT)
				ttyreader_linewrite(S);

			ttyreader_lineread(S);
		}
	}

	return 0;
}

#ifndef HAVE_CFMAKERAW  /* Extract from FreeBSD termios.c */
/*
 * Make a pre-existing termios structure into "raw" mode: character-at-a-time
 * mode with no characters interpreted, 8-bit data path.
 */
void
cfmakeraw(t)
	struct termios *t;
{

	t->c_iflag &= ~(IMAXBEL|IXOFF|INPCK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON|IGNPAR);
	t->c_iflag |= IGNBRK;
	t->c_oflag &= ~OPOST;
	t->c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|ICANON|ISIG|IEXTEN|NOFLSH|TOSTOP|PENDIN);
	t->c_cflag &= ~(CSIZE|PARENB);
	t->c_cflag |= CS8|CREAD;
	t->c_cc[VMIN] = 1;
	t->c_cc[VTIME] = 0;
}
#endif


const char *ttyreader_serialcfg(char *param1, char *str)
{				/* serialport /dev/ttyUSB123   19200  8n1   {KISS|TNC2|AEA|..}  */
	int i;
	speed_t baud;
	struct serialport *tty;
	int tcpport = 0;

	/*
	   serialport /dev/ttyUSB123 [19200 [8n1]]
	   radio serial /dev/ttyUSB123  [19200 [8n1]]  KISS
	   radio tcp 12.34.56.78 4001 KISS

	 */

	if (*param1 == 0)
		return "Bad mode keyword";
	if (*str == 0)
		return "Bad tty-name/param";

	/* Grow the array as is needed.. - this is array of pointers,
	   not array of blocks so that memory allocation does not
	   grow into way too big chunks. */
	ttys = realloc(ttys, sizeof(void *) * (ttycount + 1));

	tty = malloc(sizeof(*tty));
	memset(tty, 0, sizeof(*tty));
	ttys[ttycount++] = tty;

	tty->fd = -1;
	tty->wait_until = now - 1;	/* begin opening immediately */
	tty->last_read_something = now;	/* well, not really.. */
	tty->linetype  = LINETYPE_KISS;	/* default */
	tty->kissstate = KISSSTATE_SYNCHUNT;

	tty->ttyname = NULL;

	if (memcmp(param1, "socket!", 7) == 0) {
		/* Uh oh..  old style..
		   Convert to:  tcp!12.34.56.78!4001!
		 */
		sprintf((char *) (tty->ttyname), "tcp!%s", param1 + 7);
		tcpport = 1;

		if (debug)
			printf(".. old style socket!\n");

	} else if (*param1 == '/') {
		/* Old style  'serialport /dev/... */

		tty->ttyname = strdup(param1);

		if (debug)
			printf("..old style /... device\n");

	} else if (strcmp(param1, "serial") == 0) {
		/* New style! */
		free((char *) (tty->ttyname));

		param1 = str;
		str = config_SKIPTEXT(str);
		str = config_SKIPSPACE(str);

		tty->ttyname = strdup(param1);

		if (debug)
			printf(".. new style serial:  '%s' '%s'..\n",
			       tty->ttyname, str);

	} else if (strcmp(param1, "tcp") == 0) {
		/* New style! */
		int len;
		char *host, *port;

		free((char *) (tty->ttyname));

		host = str;
		str = config_SKIPTEXT(str);
		str = config_SKIPSPACE(str);

		port = str;
		str = config_SKIPTEXT(str);
		str = config_SKIPSPACE(str);

		if (debug)
			printf(".. new style tcp!:  '%s' '%s' '%s'..\n",
			       host, port, str);

		len = strlen(host) + strlen(port) + 8;

		tty->ttyname = malloc(len);
		sprintf((char *) (tty->ttyname), "tcp!%s!%s!", host, port);
		tcpport = 1;

	}

	/* setup termios parameters for this line.. */
	cfmakeraw(&tty->tio);
	tty->tio.c_cc[VMIN] = 1;	/* pick at least one char .. */
	tty->tio.c_cc[VTIME] = 3;	/* 0.3 seconds timeout - 36 chars @ 1200 baud */
	tty->tio.c_cflag |= (CREAD | CLOCAL);


	baud = B1200;
	cfsetispeed(&tty->tio, baud);
	cfsetospeed(&tty->tio, baud);

	config_STRLOWER(str);	/* until end of line */

	/* FIXME: analyze correct serial port data and parity format settings,
	   now hardwired to 8-n-1 -- does not work without for KISS anyway.. */

	/* Optional parameters */
	while (*str != 0) {
		param1 = str;
		str = config_SKIPTEXT(str);
		str = config_SKIPSPACE(str);

		/* See if it is baud-rate ? */
		i = atol(param1);	/* serial port speed - baud rate */
		baud = B1200;
		switch (i) {
		case 1200:
			baud = B1200;
			break;
		case 2400:
			baud = B2400;
			break;
		case 4800:
			baud = B4800;
			break;
		case 9600:
			baud = B9600;
			break;
		case 19200:
			baud = B19200;
			break;
		case 38400:
			baud = B38400;
			break;
		default:
			i = -1;
			break;
		}
		if (baud != B1200) {
			cfsetispeed(&tty->tio, baud);
			cfsetospeed(&tty->tio, baud);
		}
		if (i > 0) {
			;
		} else if (strcmp(param1, "8n1") == 0) {
			/* default behaviour, ignore */
		} else if (strcmp(param1, "kiss") == 0) {
			tty->linetype = LINETYPE_KISS;	/* plain basic KISS */

		} else if (strcmp(param1, "xorsum") == 0) {
			tty->linetype = LINETYPE_KISSBPQCRC;	/* KISS with BPQ "CRC" */
		} else if (strcmp(param1, "bpqcrc") == 0) {
			tty->linetype = LINETYPE_KISSBPQCRC;	/* KISS with BPQ "CRC" */

		} else if (strcmp(param1, "smack") == 0) {
			tty->linetype = LINETYPE_KISSSMACK;	/* KISS with SMACK / CRC16 */
		} else if (strcmp(param1, "crc16") == 0) {
			tty->linetype = LINETYPE_KISSSMACK;	/* KISS with SMACK / CRC16 */

		} else if (strcmp(param1, "poll") == 0) {
			/* FIXME: Some systems want polling... */

		} else if (strcmp(param1, "callsign") == 0 ||
			   strcmp(param1, "alias") == 0) {
			param1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);
			config_STRUPPER(param1);
			tty->ttycallsign = strdup(param1);

		} else if (strcmp(param1, "timeout") == 0) {
			param1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);
			tty->read_timeout = atol(param1);

		} else if (strcmp(param1, "tnc2") == 0) {
			tty->linetype = LINETYPE_TNC2;	/* TNC2 monitor */

		} else if (strcmp(param1, "initstring") == 0) {
			param1 = str;
			str = config_SKIPTEXT(str);
			str = config_SKIPSPACE(str);
			tty->initstring = strdup(param1);
		}

	}

	if (!tty->ttycallsign)
		tty->ttycallsign = aprsis_login;



	/* Use side-effect: this defines the tty into erlang accounting */
	erlang_set(tty, tty->ttycallsign, 0, (int) ((1200.0 * 60) / 8.2));	/* Magic constant for channel capa.. */

	return NULL;
}

