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

int ax25_to_tnc2_fmtaddress(char *dest, const uint8_t *src, int markflag)
{
	int i, c;
	int ssid;
	int seen_space = 0;

	/* 6 bytes of station callsigns in shifted ASCII format.. */
	for (i = 0; i < 6; ++i, ++src) {
		c = (*src) & 0xFF;
		if (c & 1) {
			*dest = 0;
			return ~c;	/* Bad address-end flag ? */
		}

		/* Don't copy spaces or 0 bytes */
		c = c >> 1;
		if (c == 0 || c == 0x20) {
			seen_space = 1;
			continue;
		}
		if (!seen_space &&
		    (('A' <= c && c <= 'Z') ||
		     ('0' <= c && c <= '9'))) {
			*dest++ = c;
		} else {
			*dest = 0;
			return ~c; // Bad character in callsign
		}
	}
	/* 7th byte carries SSID et.al. bits */
	c = (*src) & 0xFF;
	/* (c & 1) can be non-zero - at last address! */

	ssid = (c >> 1) & 0x0F;
	if (ssid) {	/* don't print SSID==0 value */
		dest += sprintf(dest, "-%d", ssid);
	}

	if ((c & 0x80) && markflag) {
		*dest++ = '*';	/* Has been digipeated.. */
	}
	*dest = 0;

	return c;
}

// Return 0 on OK, != 0 on errors
int parse_ax25addr(uint8_t ax25[7], const char *text, int ssidflags)
{
	int i = 0;
	int ssid = 0;
	char c;

	while (i < 6) {
		c = *text;

		if (c == '-' || c == '*' || c == '\0')
			break;
		if (!(('A' <= c && c <= 'Z') || ('0' <= c && c <= '9'))) {
			// Valid chars: [A-Z0-9]
			return 1;
		}

		ax25[i] = c << 1;

		++text;
		++i;
	}

	while (i < 6) {
		ax25[i] = ' ' << 1; // they are wanted as spaces..
		++i;
	}

	ax25[6] = ssidflags;
	if (*text == 0) return 0;

	if (*text == '-') {
		++text;
	} else if ( *text != '*' && *text != 0) {
		return 1;
	}

	for (; (*text != '\0') && (*text != '*') &&
	       ('0' <= *text) && (*text <= '9'); ++text) {

		ssid = ssid * 10 + (*text - '0');
	}

	if (*text == '*') {
		++text;
		ssidflags |= 0x80; // Set H-bit..
		ax25[6]   |= 0x80; // Set H-bit..
	}

	if (ssid > 15 || *text != '\0') {
		return 1; // Bad values
	}
	ssid &= 0x0F; // Limit it to 4 bits

	ax25[6] = (ssid << 1) | ssidflags;

	return 0;
}

/* Convert TNC2 monitor text format to binary AX.25 packet */

void tnc2_to_ax25()
{
	// int i;

	// TNC2 format:   src> dest, via1, via2, via3, ... via8
	// AX25 format:   dest, src, via1, via2, via3, ... via8
}

int ax25_format_to_tnc(const uint8_t *frame, const int framelen,
		       char *tnc2buf, const int tnc2buflen,
		       int *frameaddrlen, int *tnc2addrlen,
		       int *is_aprs, int *ui_pid)
{
	int i, j;
	const uint8_t *s = frame;
	const uint8_t *e = frame + framelen;
	char *t = tnc2buf;
	int viacount = 0;

	if (debug>1) {
	  printf("ax25_format_to_tnc() len=%d ",framelen);
	  hexdumpfp(stdout, frame, framelen, 1);
	  printf("\n");
	}

	if (framelen > sizeof(tnc2buf) - 80) {
		/* Too much ! Too much! */
		return 0;
	}


	/* Phase 1: scan address fields. */
	/* Source and Destination addresses must be printed in altered order.. */


	*t = 0;
	i = ax25_to_tnc2_fmtaddress(t, frame + 7, 0);	/* source */
	t += strlen(t);
	*t++ = '>';
	*t = 0; // end-string, just in case..

	j = ax25_to_tnc2_fmtaddress(t, frame + 0, 0);	/* destination */
	t += strlen(t);

//	if (!((i & 0xE0) == 0x60 && (j & 0xE0) == 0xE0)) {
//	  if (debug) printf("Ax25FmtToTNC2: %s SSID-bytes: %02x,%02x\n", tnc2buf, i,j);
//	}

	if (i < 0 /*  || ((i & 0xE0) != 0x60)*/) { // Top 3 bits should be: 011
		/* Bad format */
		if (debug)
		  printf("Ax25FmtToTNC2: Bad source address; SSID-byte=0x%02x\n",i);
		return 0;
	}
	if (j < 0/* || ((j & 0xE0) != 0xE0)*/) { // Top 3 bits should be: 111
		/* Bad format */
		if (debug)
		  printf("Ax25FmtToTNC2: Bad destination address; SSID-byte=0x%x\n",j);
		return 0;
	}


	s = frame + 14;

	if ((i & 1) == 0) {	/* addresses continue after the source! */

		for (; s < e;) {
			*t++ = ',';	/* separator char */
			*t = 0; // end-string, just in case..
			i = ax25_to_tnc2_fmtaddress(t, s, 1); // Top 3 bits are:  H11  ( H = "has been digipeated" )
			if (i < 0 /* || ((i & 0x60) != 0x60) */) {
				/* Bad format */
			  if (debug) printf("Ax25FmtToTNC2: Bad via address; addr='%s' SSID-byte=0x%x\n",t,i);
				return 0;
			}

			t += strlen(t);
			s += 7;
			++ viacount;
			if (i & 1)
				break;	/* last address */
		}
	}
	if (viacount > 8) {
		if (debug)
		  printf("Ax25FmtToTNC2: Found %d via fields, limit is 8!\n", viacount);
		return 0;
	}

	*frameaddrlen = s - frame;
	*tnc2addrlen  = t - tnc2buf;

	/* Address completed */

	if ((s + 2) >= e) // too short payload
		return 0;		/* never happens ?? */

	*t++ = ':';		/* end of address */
	*t = 0; // end-string, just in case..

	if (s[0] != 0x03) {
		// Not AX.25 UI frame
		*ui_pid = -1; 
		return t - tnc2buf;
		/* But say that the frame is OK, and
		   let it be possibly copied to Linux
		   internal AX.25 network. */
	}
	if (s[0] == 0x03 && s[1] != 0xF0) {
		// AX.25 UI frame, but no with APRS's PID value
		*ui_pid = s[1];
		return t - tnc2buf;
	}

	s += 2; // Skip over Control and PID bytes
	*ui_pid = 0xF0; // This was previously verified

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

	*is_aprs = 1;
	return t - tnc2buf;
}

/* Convert the binary packet to TNC2 monitor text format.
   Return 0 if conversion fails (format errors), 1 when format is OK. */

int ax25_to_tnc2(const struct aprx_interface *aif, const char *portname,
		 const int tncid, const int cmdbyte,
		 const uint8_t *frame, const int framelen)
{
	int frameaddrlen = 0;

	char tnc2buf[2800];
	int tnc2len = 0, tnc2addrlen = 0, is_aprs = 0, ui_pid = 0;

	tnc2len = ax25_format_to_tnc( frame, framelen,
				      tnc2buf, sizeof(tnc2buf),
				      & frameaddrlen, &tnc2addrlen,
				      & is_aprs, &ui_pid );

	if (tnc2len == 0) return 0; // Bad parse result

	// APRS type packets are first rx-igated (and rflog()ed)
	if (is_aprs) {
	  igate_to_aprsis(portname, tncid, tnc2buf, tnc2addrlen, tnc2len, 0, 1);
	}

	// Send to interface system to receive it..  (digipeater!)
	// A noop if the interface is actually NULL.
	interface_receive_ax25(aif, portname, is_aprs, ui_pid,
			       frame, frameaddrlen, framelen,
			       tnc2buf, tnc2addrlen, tnc2len);

	return 1;
}
