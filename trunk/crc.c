/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2011                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"


/*
	3 different CRC algorithms:
	    1)  CRC-16
	    2)  CRC-CCITT
	    3)  CRC-FLEXNET

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


--  ITU-T V.42 / 1993:

   8.1.1.6.1	16-bit frame check sequence

	The FCS field shall be the sixteen-bit sequence preceding the closing
	flag. The 16-bit FCS shall be the ones complement of the sum (modulo 2)
	of
           a)	the remainder of x^k (x^15 + x^14 + x^13 + x^12 +
	        x^11 + x^10 + x^9 + x^8 + x^7 + x^6 + x^5 + x^4 +
		x^3 + x^2 + x^1 + 1) divided (modulo 2) by the generator
		polynomial x^16 + x^12 + x^5 + 1, where k is the number
		of bits in the frame existing between, but not including,
		the final bit of the opening flag and the first bit of the
		FCS, excluding bits inserted for transparency; and
	   b)	the remainder of the division (modulo 2) by the generator
	        polynomial x^16 + x^12 + x^5 + 1, of the product of x^16
		by the content of the frame existing between, but not
		including, the final bit of the opening flag and the first
		bit of the FCS, excluding bits inserted for transparency.

	As a typical implementation at the transmitter, the initial content
	of the register of the device computing the remainder of the division
	is preset to all 1s and is then modified by division by the generator
	polynomial (as described above) of the address, control and information
	fields; the ones complement of the resulting remainder is transmitted
	as the sixteen-bit FCS.

	As a typical implementation at the receiver, the initial content of
	the register of the device computing the remainder is preset to all 1s.
	The final remainder, after multiplication by x^16 and then division
	(modulo 2) by the generator polynomial x^16 + x^12 + x^5 + 1 of the
	serial incoming protected bits and the FCS, will be
	“0001 1101 0000 1111” (x^15 through x^0, respectively) in the absence
	of transmission errors.


  Same wording is also on ITU-T X.25 specification.

   - - - - - - - - -

	Where is FLEXNET CRC specification?
	

*/


// Polynome 0xA001
// referred from kiss.c !
const uint16_t crc16_table[] = {
	0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280,	0xc241,
	0xc601, 0x06c0, 0x0780, 0xc741,	0x0500, 0xc5c1, 0xc481,	0x0440,
	0xcc01, 0x0cc0, 0x0d80, 0xcd41,	0x0f00, 0xcfc1, 0xce81,	0x0e40,
	0x0a00, 0xcac1, 0xcb81, 0x0b40,	0xc901, 0x09c0, 0x0880,	0xc841,

	0xd801, 0x18c0, 0x1980, 0xd941,	0x1b00, 0xdbc1, 0xda81,	0x1a40,
	0x1e00, 0xdec1, 0xdf81, 0x1f40,	0xdd01, 0x1dc0, 0x1c80,	0xdc41,
	0x1400, 0xd4c1, 0xd581, 0x1540,	0xd701, 0x17c0, 0x1680,	0xd641,
	0xd201, 0x12c0, 0x1380, 0xd341,	0x1100, 0xd1c1, 0xd081,	0x1040,


	0xf001, 0x30c0, 0x3180, 0xf141,	0x3300, 0xf3c1, 0xf281,	0x3240,
	0x3600, 0xf6c1, 0xf781, 0x3740,	0xf501, 0x35c0, 0x3480,	0xf441,
	0x3c00, 0xfcc1, 0xfd81, 0x3d40,	0xff01, 0x3fc0, 0x3e80,	0xfe41,
	0xfa01, 0x3ac0, 0x3b80, 0xfb41,	0x3900, 0xf9c1, 0xf881,	0x3840,

	0x2800, 0xe8c1, 0xe981, 0x2940,	0xeb01, 0x2bc0, 0x2a80,	0xea41,
	0xee01, 0x2ec0, 0x2f80, 0xef41,	0x2d00, 0xedc1, 0xec81,	0x2c40,
	0xe401, 0x24c0, 0x2580, 0xe541,	0x2700, 0xe7c1, 0xe681,	0x2640,
	0x2200, 0xe2c1, 0xe381, 0x2340,	0xe101, 0x21c0, 0x2080,	0xe041,


	0xa001, 0x60c0, 0x6180, 0xa141,	0x6300, 0xa3c1, 0xa281,	0x6240,
	0x6600, 0xa6c1, 0xa781, 0x6740,	0xa501, 0x65c0, 0x6480,	0xa441,
	0x6c00, 0xacc1, 0xad81, 0x6d40,	0xaf01, 0x6fc0, 0x6e80,	0xae41,
	0xaa01, 0x6ac0, 0x6b80, 0xab41,	0x6900, 0xa9c1, 0xa881,	0x6840,

	0x7800, 0xb8c1, 0xb981, 0x7940,	0xbb01, 0x7bc0, 0x7a80,	0xba41,
	0xbe01, 0x7ec0, 0x7f80, 0xbf41,	0x7d00, 0xbdc1, 0xbc81,	0x7c40,
	0xb401, 0x74c0, 0x7580, 0xb541,	0x7700, 0xb7c1, 0xb681,	0x7640,
	0x7200, 0xb2c1, 0xb381, 0x7340,	0xb101, 0x71c0, 0x7080,	0xb041,


	0x5000, 0x90c1, 0x9181, 0x5140,	0x9301, 0x53c0, 0x5280,	0x9241,
	0x9601, 0x56c0, 0x5780, 0x9741,	0x5500, 0x95c1, 0x9481,	0x5440,
	0x9c01, 0x5cc0, 0x5d80, 0x9d41,	0x5f00, 0x9fc1, 0x9e81,	0x5e40,
	0x5a00, 0x9ac1, 0x9b81, 0x5b40,	0x9901, 0x59c0, 0x5880,	0x9841,

	0x8801, 0x48c0, 0x4980, 0x8941,	0x4b00, 0x8bc1, 0x8a81,	0x4a40,
	0x4e00, 0x8ec1, 0x8f81, 0x4f40,	0x8d01, 0x4dc0, 0x4c80,	0x8c41,
	0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680,	0x8641,
	0x8201, 0x42c0, 0x4380, 0x8341,	0x4100, 0x81c1, 0x8081,	0x4040
};

uint16_t calc_crc_16(const uint8_t *buf, int n)
{
	uint16_t crc = 0;

	while (--n >= 0) {
		crc = (((crc >> 8) & 0xff) ^
		       crc16_table[(crc ^ *buf++) & 0xFF]);
	}
	return crc;
}

// Return 0 for correct result, anything else for incorrect one
int check_crc_16(const uint8_t *buf, int n)
{
	uint16_t crc = calc_crc_16(buf, n);
	return (crc != 0); // Correct result is when crc == 0
}


// Polynome 0x8408
const uint16_t crc_ccitt_table[256] = {
        0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
        0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
        0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
        0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
        0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
        0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
        0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
        0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
        0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
        0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
        0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
        0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
        0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
        0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
        0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
        0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
        0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
        0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
        0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
        0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
        0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
        0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
        0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
        0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
        0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
        0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
        0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
        0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
        0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
        0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
        0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
        0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

uint16_t calc_crc_ccitt(uint16_t crc, const uint8_t *buffer, int len)
{
        while (len--) {
		uint8_t c = *buffer++;
                crc = (crc >> 8) ^ crc_ccitt_table[(crc ^ c) & 0xff];
	}
        return crc;
}

#if 0 // not used!
// From start of packet to end of packet _including_ 16 bits of FCS
// Return 0 for OK, other values for errors
int check_crc_ccitt(const uint8_t *buf, int n) {
	uint16_t crc = calc_crc_ccitt(0xFFFF, buf, n);

	return (crc != 0xF0B8);
}
#endif


const uint16_t crc_flex_table[] = {
	0x0f87, 0x1e0e, 0x2c95, 0x3d1c, 0x49a3, 0x582a, 0x6ab1, 0x7b38,
	0x83cf, 0x9246, 0xa0dd, 0xb154, 0xc5eb, 0xd462, 0xe6f9, 0xf770,
	0x1f06, 0x0e8f, 0x3c14, 0x2d9d, 0x5922, 0x48ab, 0x7a30, 0x6bb9,
	0x934e, 0x82c7, 0xb05c, 0xa1d5, 0xd56a, 0xc4e3, 0xf678, 0xe7f1,

	0x2e85, 0x3f0c, 0x0d97, 0x1c1e, 0x68a1, 0x7928, 0x4bb3, 0x5a3a,
	0xa2cd, 0xb344, 0x81df, 0x9056, 0xe4e9, 0xf560, 0xc7fb, 0xd672,
	0x3e04, 0x2f8d, 0x1d16, 0x0c9f, 0x7820, 0x69a9, 0x5b32, 0x4abb,
	0xb24c, 0xa3c5, 0x915e, 0x80d7, 0xf468, 0xe5e1, 0xd77a, 0xc6f3,


	0x4d83, 0x5c0a, 0x6e91, 0x7f18, 0x0ba7, 0x1a2e, 0x28b5, 0x393c,
	0xc1cb, 0xd042, 0xe2d9, 0xf350, 0x87ef, 0x9666, 0xa4fd, 0xb574,
	0x5d02, 0x4c8b, 0x7e10, 0x6f99, 0x1b26, 0x0aaf, 0x3834, 0x29bd,
	0xd14a, 0xc0c3, 0xf258, 0xe3d1, 0x976e, 0x86e7, 0xb47c, 0xa5f5,

	0x6c81, 0x7d08, 0x4f93, 0x5e1a, 0x2aa5, 0x3b2c, 0x09b7, 0x183e,
	0xe0c9, 0xf140, 0xc3db, 0xd252, 0xa6ed, 0xb764, 0x85ff, 0x9476,
	0x7c00, 0x6d89, 0x5f12, 0x4e9b, 0x3a24, 0x2bad, 0x1936, 0x08bf,
	0xf048, 0xe1c1, 0xd35a, 0xc2d3, 0xb66c, 0xa7e5, 0x957e, 0x84f7,


	0x8b8f, 0x9a06, 0xa89d, 0xb914, 0xcdab, 0xdc22, 0xeeb9, 0xff30,
	0x07c7, 0x164e, 0x24d5, 0x355c, 0x41e3, 0x506a, 0x62f1, 0x7378,
	0x9b0e, 0x8a87, 0xb81c, 0xa995, 0xdd2a, 0xcca3, 0xfe38, 0xefb1,
	0x1746, 0x06cf, 0x3454, 0x25dd, 0x5162, 0x40eb, 0x7270, 0x63f9,

	0xaa8d, 0xbb04, 0x899f, 0x9816, 0xeca9, 0xfd20, 0xcfbb, 0xde32,
	0x26c5, 0x374c, 0x05d7, 0x145e, 0x60e1, 0x7168, 0x43f3, 0x527a,
	0xba0c, 0xab85, 0x991e, 0x8897, 0xfc28, 0xeda1, 0xdf3a, 0xceb3,
	0x3644, 0x27cd, 0x1556, 0x04df, 0x7060, 0x61e9, 0x5372, 0x42fb,


	0xc98b, 0xd802, 0xea99, 0xfb10, 0x8faf, 0x9e26, 0xacbd, 0xbd34,
	0x45c3, 0x544a, 0x66d1, 0x7758, 0x03e7, 0x126e, 0x20f5, 0x317c,
	0xd90a, 0xc883, 0xfa18, 0xeb91, 0x9f2e, 0x8ea7, 0xbc3c, 0xadb5,
	0x5542, 0x44cb, 0x7650, 0x67d9, 0x1366, 0x02ef, 0x3074, 0x21fd,

	0xe889, 0xf900, 0xcb9b, 0xda12, 0xaead, 0xbf24, 0x8dbf, 0x9c36,
	0x64c1, 0x7548, 0x47d3, 0x565a, 0x22e5, 0x336c, 0x01f7, 0x107e,
	0xf808, 0xe981, 0xdb1a, 0xca93, 0xbe2c, 0xafa5, 0x9d3e, 0x8cb7,
	0x7440, 0x65c9, 0x5752, 0x46db, 0x3264, 0x23ed, 0x1176, 0x00ff
};

uint16_t calc_crc_flex(const uint8_t *cp, int size)
{
	uint16_t crc = 0xffff;

	while (size--) {
	  uint8_t c = *cp++;
	  crc = (crc << 8) ^ crc_flex_table[((crc >> 8) ^ c) & 0xff];
	}

	return crc;
}

#if 0 // not used!
int check_crc_flex(const uint8_t *cp, int size)
{
	uint16_t crc = calc_crc_flex(cp, size);

	if (size < 3)
		return -1;

	if (crc != 0x7070)
		return -1;

	return 0;
}
#endif
