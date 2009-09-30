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

static int ax25_to_tnc2_fmtaddress(char *dest, const unsigned char *src,
				   int markflag)
{
	int i, c;

	/* We really should verify that  */

	/* 6 bytes of station callsigns in shifted ASCII format.. */
	for (i = 0; i < 6; ++i, ++src) {
		c = *src;
		if (c & 1)
			return (-(int) (c));	/* Bad address-end flag ? */

		/* Don't copy spaces or 0 bytes */
		if (c != 0 && c != 0x40)
			*dest++ = c >> 1;
	}
	/* 7th byte carries SSID et.al. bits */
	c = *src;
	if ((c >> 1) % 16) {	/* don't print SSID==0 value */
		dest += sprintf(dest, "-%d", (c >> 1) % 16);
	}

	if ((c & 0x80) && markflag) {
		*dest++ = '*';	/* Has been repeated, or some such.. */
	}
	*dest = 0;

	return c;
}


int parse_ax25addr(unsigned char ax25[7], const char *text, int ssidflags)
{
	int i = 0;
	int ssid = 0;
	char c;

	while (i < 6) {
		c = *text;

		if (c == '-' || c == '\0')
			break;

		ax25[i] = c << 1;

		++text;
		++i;
	}

	while (i < 6) {
		ax25[i] = ' ' << 1;
		++i;
	}

	ax25[6] = ssidflags;

	if (*text != '\0') {
		++text;
		if (sscanf(text, "%d", &ssid) != 1 || ssid < 0
		    || ssid > 15) {
			return -1;
		}
	}

	ax25[6] = (ssid << 1) | ssidflags;

	return 0;
}

/* Convert TNC2 monitor text format to binary AX.25 packet */

void tnc2_to_ax25()
{
	int i;

	// TNC2 format:   src> dest, via1, via2, via3, ... via8
	// AX25 format:   dest, src, via1, via2, via3, ... via8
}

/* Convert the binary packet to TNC2 monitor text format  */

void ax25_to_tnc2(const char *portname, int tncid, int cmdbyte,
		  const unsigned char *frame, const int framelen)
{
	int i, j;
	const unsigned char *s = frame;
	const unsigned char *e = frame + framelen;
	int discard = 0;

	char tnc2buf[2800];
	char *t = tnc2buf;
	int tnc2len;


	if (framelen > sizeof(tnc2buf) - 80) {
		/* Too much ! Too much! */
		return;
	}


	/* Phase 1: scan address fields. */
	/* Source and Destination addresses must be printed in altered order.. */


	*t = 0;
	i = ax25_to_tnc2_fmtaddress(t, frame + 7, 0);	/* source */
	if (i < 0) {
		discard = -1;	/* Bad format */
		if (debug)
			printf("Ax25toTNC2: Bad destination address\n");
	}

	t += strlen(t);
	*t++ = '>';

	j = ax25_to_tnc2_fmtaddress(t, frame + 0, 0);	/* destination */
	if (i < 0) {
		discard = -1;	/* Bad format */
		if (debug)
			printf("Ax25toTNC2: Bad source address\n");
	}

	t += strlen(t);

	s = frame + 14;

	if ((i & 1) == 0) {	/* addresses continue after the source! */

		for (; s < e;) {
			*t++ = ',';	/* separator char */
			i = ax25_to_tnc2_fmtaddress(t, s, 1);
			if (i < 0) {
				discard = -1;	/* Bad format */
				if (debug)
					printf("Ax25toTNC2: Bad via address\n");
				break;
			}

			t += strlen(t);
			s += 7;
			if (i & 1)
				break;	/* last address */
		}
	}

	/* Address completed */

	if ((s + 2) >= e)
		return;		/* never happens ?? */

	*t++ = ':';		/* end of address */

	if ((*s++ != 0x03) || (*s++ != 0xF0)) {
		/* Not AX.25 UI frame */
		return;
	}

	/* Copy payload - stop at first LF char */
	for (; s < e; ++s) {
		if (*s == '\n') /* Stop at first LF */
		  break;
		*t++ = *s;
	}
	*t = 0;

	/* Chop off possible immediately trailing CR characters */
	for ( ;t > tnc2buf; --t ) {
	    int c = t[-1];
	    if (c != '\r') {
		break;
	    }
	    t[-1] = 0;
	}

	tnc2len = t - tnc2buf;

	/* 
	   if (!discard)
	   aprsdigi(tnc2buf, portname, t0, t-t0);
	 */

	igate_to_aprsis(portname, tncid, tnc2buf, tnc2len, discard);
}
