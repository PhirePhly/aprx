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

static int crc_table[] = {
	0x0000, 0xc0c1, 0xc181, 0x0140,
	0xc301, 0x03c0, 0x0280,	0xc241,
	0xc601, 0x06c0, 0x0780, 0xc741,
	0x0500, 0xc5c1, 0xc481,	0x0440,

	0xcc01, 0x0cc0, 0x0d80, 0xcd41,
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

int crc16_calc(unsigned char *buf, int n)
{
	int crc = 0;

	while (--n >= 0)
		crc = ((crc >> 8) & 0xff) ^ crc_table[(crc ^ *buf++) & 0xFF];
	return crc;
}

/*
 * kissencoder():  If  (cmdbyte & 0x80) is set,  then this
 *                 produces SMACK format frame, otherwise 
 *                 plain KISS.
 *
 */

int kissencoder( void *kissbuf, int kissspace,
		 const void *pktbuf, int pktlen, int cmdbyte )
{
	unsigned char *kb = kissbuf;
	unsigned char *ke = kb + kissspace - 3;
	const unsigned char *pkt = pktbuf;
	int i;
	int crc = crc_table[cmdbyte & 0xFF];

	/* Expect the KISS buffer to be at least ... 8 bytes.. */

	*kb++ = KISS_FEND;
	*kb++ = cmdbyte;

	for (i = 0; i < pktlen && kb < ke; ++i, ++pkt) {
		/* Calc CRC16 (SMACK CRC) - while encoding data.. */
		int b = *pkt;
		crc = ((crc >> 8) & 0xff) ^ crc_table[(crc ^ b) & 0xFF];
		
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
	   store calculated CRC on frame. */
	if ((cmdbyte & 0x8F) == 0x80) {
		if (kb < ke) {
			*kb++ = crc & 0xFF;		/* low crc byte */
			*kb++ = (crc >> 8) & 0xFF;	/* high crc byte */
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
