/*
 *	aprsc
 *
 *	(c) Heikki Hannikainen, OH7LZB <hessu@hes.iki.fi>
 *
 *     This program is licensed under the BSD license, which can be found
 *     in the file LICENSE.
 *	
 */

/*
 *	A simple APRS parser for aprsc. Translated from Ham::APRS::FAP
 *	perl module (by OH2KKU).
 *
 *	Only needs to get lat/lng out of the packet, other features would
 *	be unnecessary in this application, and slow down the parser.
 *      ... but lets still classify the packet, output filter needs that.
 *	
 */

#include "aprx.h"
#include <math.h>

#define DEBUG_LOG(fmt) if(debug)printf(fmt)

float filter_lat2rad(float lat)
{
  return (lat * (M_PI / 180.0));
}

float filter_lon2rad(float lon)
{
  return (lon * (M_PI / 180.0));
}

/*
 *	Check if the given character is a valid symbol table identifier
 *	or an overlay character. The set is different for compressed
 *	and uncompressed packets - the former has the overlaid number (0-9)
 *	replaced with n-j.
 */

static int valid_sym_table_compressed(char c)
{
	return (c == '/' || c == '\\' || (c >= 0x41 && c <= 0x5A)
		    || (c >= 0x61 && c <= 0x6A)); /* [\/\\A-Za-j] */
}

static int valid_sym_table_uncompressed(char c)
{
	return (c == '/' || c == '\\' || (c >= 0x41 && c <= 0x5A)
		    || (c >= 0x48 && c <= 0x57)); /* [\/\\A-Z0-9] */
}

/*
 *	Fill the pbuf_t structure with a parsed position and
 *	symbol table & code. Also does range checking for lat/lng
 *	and pre-calculates cos(lat) for range filters.
 */

static int pbuf_fill_pos(struct pbuf_t *pb, const float lat, const float lng, const char sym_table, const char sym_code)
{
	int bad = 0;
	/* symbol table and code */
	pb->symbol[0] = sym_table;
	pb->symbol[1] = sym_code;
	pb->symbol[2] = 0;
	
	/* Is it perhaps a weather report ? */
	if (sym_code == '_' && (sym_table == '/' || sym_table == '\\')) 
		pb->packettype |= T_WX;
	if (sym_code == '@' && (sym_table == '/' || sym_table == '\\')) 
		pb->packettype |= T_WX;	/* Hurricane */

	bad |= (lat < -89.9 && -0.0001 <= lng && lng <= 0.0001);
	bad |= (lat >  89.9 && -0.0001 <= lng && lng <= 0.0001);

	if (-0.0001 <= lat && lat <= 0.0001) {
	  bad |= ( -0.0001 <= lng && lng <= 0.0001);
	  bad |= ( -90.01  <= lng && lng <= -89.99);
	  bad |= (  89.99  <= lng && lng <=  90.01);
	}


	if (bad || lat < -90.0 || lat > 90.0 || lng < -180.0 || lng > 180.0) {
		if (debug)
			printf("\tposition out of range: lat %.3f lng %.3f", lat, lng);

		return 0; /* out of range */
	}
	
	if (debug)
		printf("\tposition ok: lat %.3f lng %.3f", lat, lng);

	/* Pre-calculations for A/R/F/M-filter tests */
	pb->lat     = filter_lat2rad(lat);  /* deg-to-radians */
	pb->cos_lat = cosf(lat);            /* used in range filters */
	pb->lng     = filter_lon2rad(lng);  /* deg-to-radians */
	
	pb->flags |= F_HASPOS;	/* the packet has positional data */

	return 1;
}


/*
 *	Parse NMEA position packets.
 */

static int parse_aprs_nmea(struct pbuf_t *pb, const char *body, const char *body_end)
{
	float lat, lng;
	const char *latp, *lngp;
	int i, la, lo;
	char lac, loc;
	
	if (memcmp(body,"ULT",3) == 0) {
		/* Ah..  "$ULT..." - that is, Ultimeter 2000 weather instrument */
		pb->packettype |= T_WX;
		return 1;
	}
	
	lat  = lng  = 0.0;
	latp = lngp = NULL;
	
	/* NMEA sentences to understand:
	   $GPGGA  Global Positioning System Fix Data
	   $GPGLL  Geographic Position, Latitude/Longitude Data
	   $GPRMC  Remommended Minimum Specific GPS/Transit Data
	   $GPWPT  Way Point Location ?? (bug in APRS specs ?)
	   $GPWPL  Waypoint Load (not in APRS specs, but in NMEA specs)
	   $PNTS   Seen on APRS-IS, private sentense based on NMEA..
	   $xxTLL  Not seen on radio network, usually $RATLL - Target positions
	           reported by RAdar.
	 */
	 
	if (memcmp(body, "GPGGA,", 6) == 0) {
		/* GPGGA,175059,3347.4969,N,11805.7319,W,2,12,1.0,6.8,M,-32.1,M,,*7D
		//   v=1, looks fine
		// GPGGA,000000,5132.038,N,11310.221,W,1,09,0.8,940.0,M,-17.7,,
		//   v=1, timestamp odd, coords look fine
		// GPGGA,,,,,,0,00,,,,,,,*66
		//   v=0, invalid
		// GPGGA,121230,4518.7931,N,07322.3202,W,2,08,1.0,40.0,M,-32.4,M,,*46
		//   v=2, looks valid ?
		// GPGGA,193115.00,3302.50182,N,11651.22581,W,1,08,01.6,00465.90,M,-32.891,M,,*5F
		// $GPGGA,hhmmss.dd,xxmm.dddd,<N|S>,yyymm.dddd,<E|W>,v,
		//        ss,d.d,h.h,M,g.g,M,a.a,xxxx*hh<CR><LF>
		*/
		
		latp = body+6; // over the keyword
		while (latp < body_end && *latp != ',')
			latp++; // scan over the timestamp
		if (*latp == ',')
			latp++; // .. and into latitude.
		lngp = latp;
		while (lngp < body_end && *lngp != ',')
			lngp++;
		if (*lngp == ',')
			lngp++;
		if (*lngp != ',')
			lngp++;
		if (*lngp == ',')
			lngp++;
			
		/* latp, and lngp  point to start of latitude and longitude substrings
		// respectively.
		*/
	
	} else if (memcmp(body, "GPGLL,", 6) == 0) {
		/* $GPGLL,xxmm.dddd,<N|S>,yyymm.dddd,<E|W>,hhmmss.dd,S,M*hh<CR><LF>  */
		latp = body+6; // over the keyword
		lngp = latp;
		while (lngp < body_end && *lngp != ',') // over latitude
			lngp++;
		if (*lngp == ',')
			lngp++; // and lat designator
		if (*lngp != ',')
			lngp++; // and lat designator
		if (*lngp == ',')
			lngp++;
		/* latp, and lngp  point to start of latitude and longitude substrings
		// respectively
		*/
	} else if (memcmp(body, "GPRMC,", 6) == 0) {
		/* $GPRMC,hhmmss.dd,S,xxmm.dddd,<N|S>,yyymm.dddd,<E|W>,s.s,h.h,ddmmyy,d.d, <E|W>,M*hh<CR><LF>
		// ,S, = Status:  'A' = Valid, 'V' = Invalid
		// 
		// GPRMC,175050,A,4117.8935,N,10535.0871,W,0.0,324.3,100208,10.0,E,A*3B
		// GPRMC,000000,V,0000.0000,0,00000.0000,0,000,000,000000,,*01/It wasn't me :)
		//    invalid..
		// GPRMC,000043,V,4411.7761,N,07927.0448,W,0.000,0.0,290697,10.7,W*57
		// GPRMC,003803,A,3347.1727,N,11812.7184,W,000.0,000.0,140208,013.7,E*67
		// GPRMC,050058,A,4609.1143,N,12258.8184,W,0.000,0.0,100208,18.0,E*5B
		*/
		
		latp = body+6; // over the keyword
		while (latp < body_end && *latp != ',')
			latp++; // scan over the timestamp
		if (*latp == ',')
			latp++; // .. and into VALIDITY
		if (*latp != 'A' && *latp != 'V')
			return 0; // INVALID !
		if (*latp != ',')
			latp++;
		if (*latp == ',')
			latp++;
		
		/* now it points to latitude substring */
		lngp = latp;
		while (lngp < body_end && *lngp != ',')
			lngp++;
		
		if (*lngp == ',')
			lngp++;
		if (*lngp != ',')
			lngp++;
		if (*lngp == ',')
			lngp++;
		
		/* latp, and lngp  point to start of latitude and longitude substrings
		// respectively.
		*/
		
	} else if (memcmp(body, "GPWPL,", 6) == 0) {
		/* $GPWPL,4610.586,N,00607.754,E,4*70
		// $GPWPL,4610.452,N,00607.759,E,5*74
		*/
		latp = body+6;
		
	} else if (memcmp(body, "PNTS,1,", 7) == 0) { /* PNTS version 1 */
		/* $PNTS,1,0,11,01,2002,231932,3539.687,N,13944.480,E,0,000,5,Roppongi UID RELAY,000,1*35
		// $PNTS,1,0,14,01,2007,131449,3535.182,N,13941.200,E,0,0.0,6,Oota-Ku KissUIDigi,000,1*1D
		// $PNTS,1,0,17,02,2008,120824,3117.165,N,13036.481,E,49,059,1,Kagoshima,000,1*71
		// $PNTS,1,0,17,02,2008,120948,3504.283,N,13657.933,E,00,000.0,6,,000,1*36
		// 
		// From Alinco EJ-41U Terminal Node Controller manual:
		// 
		// 5-4-7 $PNTS
		// This is a private-sentence based on NMEA-0183.  The data contains date,
		// time, latitude, longitude, moving speed, direction, altitude plus a short
		// message, group codes, and icon numbers. The EJ-41U does not analyze this
		// format but can re-structure it.
		// The data contains the following information:
		//  l $PNTS Starts the $PNTS sentence
		//  l version
		//  l the registered information. [0]=normal geographical location data.
		//    This is the only data EJ-41U can re-structure. [s]=Initial position
		//    for the course setting [E]=ending position for the course setting
		//    [1]=the course data between initial and ending [P]=the check point
		//    registration [A]=check data when the automatic position transmission
		//    is set OFF [R]=check data when the course data or check point data is
		//    received.
		//  l dd,mm,yyyy,hhmmss: Date and time indication.
		//  l Latitude in DMD followed by N or S
		//  l Longitude in DMD followed by E or W
		//  l Direction: Shown with the number 360 degrees divided by 64.
		//    00 stands for true north, 16 for east. Speed in Km/h
		//  l One of 15 characters [0] to [9], [A] to [E].
		//    NTSMRK command determines this character when EJ-41U is used.
		//  l A short message up to 20 bites. Use NTSMSG command to determine this message.
		//  l A group code: 3 letters with a combination of [0] to [9], [A] to [Z].
		//    Use NTSGRP command to determine.
		//  l Status: [1] for usable information, [0] for non-usable information.
		//  l *hh<CR><LF> the check-sum and end of PNTS sentence.
		*/

		if (body+55 > body_end) {
			DEBUG_LOG("body too short");
			return 0; /* Too short.. */
		}
		latp = body+7; /* Over the keyword */
		/* Accept any registered information code */
		if (*latp++ == ',') return 0;
		if (*latp++ != ',') return 0;
		/* Scan over date+time info */
		while (*latp != ',' && latp <= body_end) ++latp;
		if (*latp == ',') ++latp;
		while (*latp != ',' && latp <= body_end) ++latp;
		if (*latp == ',') ++latp;
		while (*latp != ',' && latp <= body_end) ++latp;
		if (*latp == ',') ++latp;
		while (*latp != ',' && latp <= body_end) ++latp;
		if (*latp == ',') ++latp;
		/* now it points to latitude substring */
		lngp = latp;
		while (lngp < body_end && *lngp != ',')
			lngp++;
		
		if (*lngp == ',')
			lngp++;
		if (*lngp != ',')
			lngp++;
		if (*lngp == ',')
			lngp++;
		
		/* latp, and lngp  point to start of latitude and longitude substrings
		// respectively.
		*/
#if 1
	} else if (memcmp(body, "GPGSA,", 6) == 0 ||
		   memcmp(body, "GPVTG,", 6) == 0 ||
		   memcmp(body, "GPGSV,", 6) == 0) {
		/* Recognized but ignored */
		return 1;
#endif
	}
	
	if (!latp || !lngp) {
		if (debug)
			fprintf(stderr, "Unknown NMEA: '%.11s' %.*s", pb->data, (int)(body_end - body), body);
		return 0; /* Well..  Not NMEA frame */
	}

	// hlog(LOG_DEBUG, "NMEA parsing: %.*s", (int)(body_end - body), body);
	// hlog(LOG_DEBUG, "     lat=%.10s   lng=%.10s", latp, lngp);

	i = sscanf(latp, "%2d%f,%c,", &la, &lat, &lac);
	if (i != 3)
		return 0; // parse failure
	
	i = sscanf(lngp, "%3d%f,%c,", &lo, &lng, &loc);
	if (i != 3)
		return 0; // parse failure
	
	if (lac != 'N' && lac != 'S' && lac != 'n' && lac != 's')
		return 0; // bad indicator value
	if (loc != 'E' && loc != 'W' && loc != 'e' && loc != 'w')
		return 0; // bad indicator value
		
	// hlog(LOG_DEBUG, "   lat: %c %2d %7.4f   lng: %c %2d %7.4f",
	//                 lac, la, lat, loc, lo, lng);

	lat = (float)la + lat/60.0;
	lng = (float)lo + lng/60.0;
	
	if (lac == 'S' || lac == 's')
		lat = -lat;
	if (loc == 'W' || loc == 'w')
		lng = -lng;
	
	pb->packettype |= T_POSITION;
	
	// FIXME: Symbol ???
	// -- practically all SSIDs are used in source addresses,
	//    including zero.

	return pbuf_fill_pos(pb, lat, lng, 0, 0);
}

static int parse_aprs_telem(struct pbuf_t *pb, const char *body, const char *body_end)
{
	// float lat = 0.0, lng = 0.0;

	DEBUG_LOG("parse_aprs_telem");

	//pbuf_fill_pos(pb, lat, lng, 0, 0);
	return 1;
}

/*
 *	Parse a MIC-E position packet
 *
 *	APRS PROTOCOL REFERENCE 1.0.1 Chapter 10, page 42 (52 in PDF)
 */

static int parse_aprs_mice(struct pbuf_t *pb, const char *body, const char *body_end)
{
	float lat = 0.0, lng = 0.0;
	unsigned int lat_deg = 0, lat_min = 0, lat_min_frag = 0, lng_deg = 0, lng_min = 0, lng_min_frag = 0;
	const char *d_start;
	char dstcall[7];
	char *p;
	char sym_table, sym_code;
	int posambiguity = 0;
	int i;
	
	DEBUG_LOG("parse_aprs_mice");
	
	/* check packet length */
	if (body_end - body < 8)
		return 0;
	
	/* check that the destination call exists and is of the right size for mic-e */
	d_start = pb->srccall_end+1;
	if (pb->dstcall_end - d_start != 6) {
		DEBUG_LOG(".. bad destcall length! ");
		return 0; /* eh...? */
	}
	
	/* validate destination call:
	 * A-K characters are not used in the last 3 characters
	 * and MNO are never used
	 */
	if(debug)printf(" destcall='%6.6s'",d_start);
	for (i = 0; i < 3; i++)
		if (!((d_start[i] >= '0' && d_start[i] <= '9')
			|| (d_start[i] >= 'A' && d_start[i] <= 'L')
			|| (d_start[i] >= 'P' && d_start[i] <= 'Z'))) {
			DEBUG_LOG(".. bad destcall characters in posits 1..3");
			return 0;
		}
	
	for (i = 3; i < 6; i++)
		if (!((d_start[i] >= '0' && d_start[i] <= '9')
			|| (d_start[i] == 'L')
			|| (d_start[i] >= 'P' && d_start[i] <= 'Z'))) {
			DEBUG_LOG(".. bad destcall characters in posits 4..6");
			return 0;
		}
	
	DEBUG_LOG("\tpassed dstcall format check");
	
	/* validate information field (longitude, course, speed and
	 * symbol table and code are checked). Not bullet proof..
	 *
	 *   0          1          23            4          5          6              7
	 * /^[\x26-\x7f][\x26-\x61][\x1c-\x7f]{2}[\x1c-\x7d][\x1c-\x7f][\x21-\x7b\x7d][\/\\A-Z0-9]/
	 */
	if (body[0] < 0x26 || (uint8_t)body[0] > 0x7f) {
		DEBUG_LOG("..bad infofield column 1");
		return 0;
	}
	if (body[1] < 0x26 || (uint8_t)body[1] > 0x61) {
		DEBUG_LOG("..bad infofield column 2");
		return 0;
	}
	if (body[2] < 0x1c || (uint8_t)body[2] > 0x7f) {
		DEBUG_LOG("..bad infofield column 3");
		return 0;
	}
	if (body[3] < 0x1c || (uint8_t)body[3] > 0x7f) {
		DEBUG_LOG("..bad infofield column 4");
		return 0;
	}
	if (body[4] < 0x1c || (uint8_t)body[4] > 0x7d) {
		DEBUG_LOG("..bad infofield column 5");
		return 0;
	}
	if (body[5] < 0x1c || (uint8_t)body[5] > 0x7f) {
		DEBUG_LOG("..bad infofield column 6");
		return 0;
	}
	if ((body[6] < 0x21 || (uint8_t)body[6] > 0x7b)
		&& (uint8_t)body[6] != 0x7d) {
		DEBUG_LOG("..bad infofield column 7");
		return 0;
	}
	if (!valid_sym_table_uncompressed(body[7])) {
		DEBUG_LOG("..bad symbol table entry on column 8");
		return 0;
	}
	
	DEBUG_LOG("\tpassed info format check");
	
	/* make a local copy, we're going to modify it */
	strncpy(dstcall, d_start, 6);
	dstcall[6] = 0;
	
	/* First do the destination callsign
	 * (latitude, message bits, N/S and W/E indicators and long. offset)
	 *
	 * Translate the characters to get the latitude
	 */
	 
	//fprintf(stderr, "\tuntranslated dstcall: %s\n", dstcall);
	for (p = dstcall; *p; p++) {
		if (*p >= 'A' && *p <= 'J')
			*p -= 'A' - '0';
		else if (*p >= 'P' && *p <= 'Y')
			*p -= 'P' - '0';
		else if (*p == 'K' || *p == 'L' || *p == 'Z')
			*p = '_';
	}
	//fprintf(stderr, "\ttranslated dstcall: %s\n", dstcall);
	
	/* position ambiquity is going to get ignored now, it's not needed in this application. */
	if (dstcall[5] == '_') { dstcall[5] = '5'; posambiguity = 1; }
	if (dstcall[4] == '_') { dstcall[4] = '5'; posambiguity = 2; }
	if (dstcall[3] == '_') { dstcall[3] = '5'; posambiguity = 3; }
	if (dstcall[2] == '_') { dstcall[2] = '3'; posambiguity = 4; }
	if (dstcall[1] == '_' || dstcall[0] == '_') {
		DEBUG_LOG("..bad pos-ambiguity on destcall");
		return 0;
	} /* cannot use posamb here */
	
	/* convert to degrees, minutes and decimal degrees, and then to a float lat */
	if (sscanf(dstcall, "%2u%2u%2u",
	    &lat_deg, &lat_min, &lat_min_frag) != 3) {
		DEBUG_LOG("\tsscanf failed");
		return 0;
	}
	lat = (float)lat_deg + (float)lat_min / 60.0 + (float)lat_min_frag / 100.0 / 60.0;
	
	/* check the north/south direction and correct the latitude if necessary */
	if (d_start[3] <= 0x4c)
		lat = 0 - lat;
	
	/* Decode the longitude, the first three bytes of the body after the data
	 * type indicator. First longitude degrees, remember the longitude offset.
	 */
	lng_deg = body[0] - 28;
	if (body[4] >= 0x50)
		lng_deg += 100;
	if (lng_deg >= 180 && lng_deg <= 189)
		lng_deg -= 80;
	else if (lng_deg >= 190 && lng_deg <= 199)
		lng_deg -= 190;
	
	/* Decode the longitude minutes */
	lng_min = body[1] - 28;
	if (lng_min >= 60)
		lng_min -= 60;
		
	/* ... and minute decimals */
	lng_min_frag = body[2] - 28;
	
	/* apply position ambiguity to longitude */
	switch (posambiguity) {
	case 0:
		/* use everything */
		lng = (float)lng_deg + (float)lng_min / 60.0
			+ (float)lng_min_frag / 100.0 / 60.0;
		break;
	case 1:
		/* ignore last number of lng_min_frag */
		lng = (float)lng_deg + (float)lng_min / 60.0
			+ (float)(lng_min_frag - lng_min_frag % 10 + 5) / 100.0 / 60.0;
		break;
	case 2:
		/* ignore lng_min_frag */
		lng = (float)lng_deg + ((float)lng_min + 0.5) / 60.0;
		break;
	case 3:
		/* ignore lng_min_frag and last number of lng_min */
		lng = (float)lng_deg + (float)(lng_min - lng_min % 10 + 5) / 60.0;
		break;
	case 4:
		/* minute is unused -> add 0.5 degrees to longitude */
		lng = (float)lng_deg + 0.5;
		break;
	default:
		DEBUG_LOG(".. posambiguity code BUG!");
		return 0;
	}
	
	/* check the longitude E/W sign */
	if (d_start[5] >= 0x50)
		lng = 0 - lng;
	
	/* save the symbol table and code */
	sym_code = body[6];
	sym_table = body[7];
	
	/* ok, we're done */
	/*
	fprintf(stderr, "\tlat %u %u.%u (%.4f) lng %u %u.%u (%.4f)\n",
	 	lat_deg, lat_min, lat_min_frag, lat,
	 	lng_deg, lng_min, lng_min_frag, lng);
	fprintf(stderr, "\tsym '%c' '%c'\n", sym_table, sym_code);
	*/
	
	return pbuf_fill_pos(pb, lat, lng, sym_table, sym_code);
}

/*
 *	Parse a compressed APRS position packet
 *
 *	APRS PROTOCOL REFERENCE 1.0.1 Chapter 9, page 36 (46 in PDF)
 */

static int parse_aprs_compressed(struct pbuf_t *pb, const char *body, const char *body_end)
{
	char sym_table, sym_code;
	int i;
	int lat1, lat2, lat3, lat4, lng1, lng2, lng3, lng4;
	double lat = 0.0, lng = 0.0;
	
	DEBUG_LOG("parse_aprs_compressed");
	
	/* A compressed position is always 13 characters long.
	 * Make sure we get at least 13 characters and that they are ok.
	 * Also check the allowed base-91 characters at the same time.
	 */ 
	
	if (body_end - body < 13) {
		DEBUG_LOG("\ttoo short");
		return 0; /* too short. */
	}
	
	sym_table = body[0]; /* has been validated before entering this function */
	sym_code = body[9];
	
	/* base-91 check */
	for (i = 1; i <= 8; i++)
		if (body[i] < 0x21 || body[i] > 0x7b)
			return 0;
	
	// fprintf(stderr, "\tpassed length and format checks, sym %c%c\n", sym_table, sym_code);
	
	/* decode */
	lat1 = body[1] - 33;
	lat2 = body[2] - 33;
	lat3 = body[3] - 33;
	lat4 = body[4] - 33;
	lng1 = body[5] - 33;
	lng2 = body[6] - 33;
	lng3 = body[7] - 33;
	lng4 = body[8] - 33;
	
	/* calculate latitude and longitude */
	lat = 90.0 - ((double)(lat1 * 91 * 91 * 91 + lat2 * 91 * 91 + lat3 * 91 + lat4) / (double)380926.0);
	lng = -180.0 + ((double)(lng1 * 91 * 91 * 91 + lng2 * 91 * 91 + lng3 * 91 + lng4) / (double)190463.0);
	
	return pbuf_fill_pos(pb, lat, lng, sym_table, sym_code);
}

/*
 *	Parse an uncompressed "normal" APRS packet
 *
 *	APRS PROTOCOL REFERENCE 1.0.1 Chapter 8, page 32 (42 in PDF)
 */

static int parse_aprs_uncompressed(struct pbuf_t *pb, const char *body, const char *body_end)
{
	char posbuf[20];
	unsigned int lat_deg = 0, lat_min = 0, lat_min_frag = 0, lng_deg = 0, lng_min = 0, lng_min_frag = 0;
	float lat, lng;
	char lat_hemi, lng_hemi;
	char sym_table, sym_code;
	int issouth = 0;
	int iswest = 0;
	
	DEBUG_LOG("parse_aprs_uncompressed");
	
	if (body_end - body < 19) {
		DEBUG_LOG("\ttoo short");
		return 0;
	}
	
	/* make a local copy, so we can overwrite it at will. */
	memcpy(posbuf, body, 19);
	posbuf[19] = 0;
	// fprintf(stderr, "\tposbuf: %s\n", posbuf);
	
	/* position ambiquity is going to get ignored now,
	   it's not needed in this application. */

	/* lat */
	if (posbuf[2] == ' ') posbuf[2] = '3';
	if (posbuf[3] == ' ') posbuf[3] = '5';
	if (posbuf[5] == ' ') posbuf[5] = '5';
	if (posbuf[6] == ' ') posbuf[6] = '5';
	/* lng */
	if (posbuf[12] == ' ') posbuf[12] = '3';
	if (posbuf[13] == ' ') posbuf[13] = '5';
	if (posbuf[15] == ' ') posbuf[15] = '5';
	if (posbuf[16] == ' ') posbuf[16] = '5';
	
	// fprintf(stderr, "\tafter filling amb: %s\n", posbuf);
	/* 3210.70N/13132.15E# */
	if (sscanf(posbuf, "%2u%2u.%2u%c%c%3u%2u.%2u%c%c",
	    &lat_deg, &lat_min, &lat_min_frag, &lat_hemi, &sym_table,
	    &lng_deg, &lng_min, &lng_min_frag, &lng_hemi, &sym_code) != 10) {
		DEBUG_LOG("\tsscanf failed");
		return 0;
	}
	
	if (!valid_sym_table_uncompressed(sym_table))
		sym_table = 0;
	
	if (lat_hemi == 'S' || lat_hemi == 's')
		issouth = 1;
	else if (lat_hemi != 'N' && lat_hemi != 'n')
		return 0; /* neither north or south? bail out... */
	
	if (lng_hemi == 'W' || lng_hemi == 'w')
		iswest = 1;
	else if (lng_hemi != 'E' && lng_hemi != 'e')
		return 0; /* neither west or east? bail out ... */
	
	if (lat_deg > 89 || lng_deg > 179)
		return 0; /* too large values for lat/lng degrees */
	
	lat = (float)lat_deg + (float)lat_min / 60.0 + (float)lat_min_frag / 100.0 / 60.0;
	lng = (float)lng_deg + (float)lng_min / 60.0 + (float)lng_min_frag / 100.0 / 60.0;
	
	/* Finally apply south/west indicators */
	if (issouth)
		lat = 0.0 - lat;
	if (iswest)
		lng = 0.0 - lng;
	
	// fprintf(stderr, "\tlat %u %u.%u %c (%.3f) lng %u %u.%u %c (%.3f)\n",
	// 	lat_deg, lat_min, lat_min_frag, (int)lat_hemi, lat,
	// 	lng_deg, lng_min, lng_min_frag, (int)lng_hemi, lng);
	// fprintf(stderr, "\tsym '%c' '%c'\n", sym_table, sym_code);

	return pbuf_fill_pos(pb, lat, lng, sym_table, sym_code);
}

/*
 *	Parse an APRS object 
 *
 *	APRS PROTOCOL REFERENCE 1.0.1 Chapter 11, page 58 (68 in PDF)
 */

static int parse_aprs_object(struct pbuf_t *pb, const char *body, const char *body_end)
{
	int i;
	int namelen = -1;
	
	pb->packettype |= T_OBJECT;
	
	DEBUG_LOG("parse_aprs_object");
	
	/* check that the object name ends with either * or _ */
	if (*(body + 9) != '*' && *(body + 9) != '_') {
		DEBUG_LOG("\tinvalid object kill character");
		return 0;
	}
	
	/* check that the timestamp ends with a z */
	if (*(body + 16) != 'z' && *(body + 16) != 'Z') {
		// fprintf(stderr, "\tinvalid object timestamp z character\n");
		return 0;
	}
	
	/* check object's name - scan for non-printable characters and the last
	 * non-space character
	 */
	for (i = 0; i < 9; i++) {
		if (body[i] < 0x20 || body[i] > 0x7e) {
			DEBUG_LOG("\tobject name has unprintable characters");
			return 0; /* non-printable */
		}
		if (body[i] != ' ')
			namelen = i;
	}
	
	if (namelen < 0) {
		DEBUG_LOG("\tobject has empty name");
		return 0;
	}
	
	pb->srcname = body;
	pb->srcname_len = namelen+1;
	
	// fprintf(stderr, "\tobject name: '%.*s'\n", pb->srcname_len, pb->srcname);
	
	/* Forward the location parsing onwards */
	if (valid_sym_table_compressed(body[17]))
		return parse_aprs_compressed(pb, body + 17, body_end);
	
	if (body[17] >= '0' && body[17] <= '9')
		return parse_aprs_uncompressed(pb, body + 17, body_end);
	
	DEBUG_LOG("\tno valid position in object");
	
	return 0;
}

/*
 *	Parse an APRS item
 *
 *	APRS PROTOCOL REFERENCE 1.0.1 Chapter 11, page 59 (69 in PDF)
 */

static int parse_aprs_item(struct pbuf_t *pb, const char *body, const char *body_end)
{
	int i;
	
	pb->packettype |= T_ITEM;
	
	DEBUG_LOG("parse_aprs_item");
	
	/* check item's name - scan for non-printable characters and the
	 * ending character ! or _
	 */
	for (i = 0; i < 9 && body[i] != '!' && body[i] != '_'; i++) {
		if (body[i] < 0x20 || body[i] > 0x7e) {
			DEBUG_LOG("\titem name has unprintable characters");
			return 0; /* non-printable */
		}
	}
	
	if (body[i] != '!' && body[i] != '_') {
		DEBUG_LOG("\titem name ends with neither ! or _");
		return 0;
	}
	
	if (i < 3 || i > 9) {
		DEBUG_LOG("\titem name has invalid length");
		return 0;
	}
	
	pb->srcname = body;
	pb->srcname_len = i;
	
	//fprintf(stderr, "\titem name: '%.*s'\n", pb->srcname_len, pb->srcname);
	
	/* Forward the location parsing onwards */
	i++;
	if (valid_sym_table_compressed(body[i]))
		return parse_aprs_compressed(pb, body + i, body_end);
	
	if (body[i] >= '0' && body[i] <= '9')
		return parse_aprs_uncompressed(pb, body + i, body_end);
	
	DEBUG_LOG("\tno valid position in item");
	
	return 0;
}


/*
 *	Try to parse an APRS packet.
 *	Returns 1 if position was parsed successfully,
 *	0 if parsing failed.
 *
 *	Does also front-end part of the output filter's
 *	packet type classification job.
 *
 * TODO: Recognize TELEM packets in !/=@ packets too!
 *
 */

int parse_aprs(struct pbuf_t *pb)
{
	char packettype, poschar;
	int paclen;
	const char *body;
	const char *body_end;
	const char *pos_start;

	if (!pb->info_start)
		return 0;

	pb->packettype = T_ALL;
	pb->flags      = 0;

	if (pb->data[0] == 'C' && /* Perhaps CWOP ? */
	    pb->data[1] == 'W') {
		const char *s  = pb->data + 2;
		const char *pe = pb->data + pb->packet_len;
		for ( ; *s && s < pe ; ++s ) {
			int c = *s;
			if (c < '0' || c > '9')
				break;
		}
		if (*s == '>')
			pb->packettype |= T_CWOP;
	}

	/* the following parsing logic has been translated from Ham::APRS::FAP
	 * Perl module to C
	 */
	
	/* length of the info field: length of the packet - length of header - CRLF */
	paclen = pb->packet_len - (pb->info_start - pb->data) - 2;
	/* Check the first character of the packet and determine the packet type */
	packettype = *pb->info_start;
	
	/* failed parsing */
	// fprintf(stderr, "parse_aprs (%d):\n", paclen);
	// fwrite(pb->info_start, paclen, 1, stderr);
	// fprintf(stderr, "\n");
	
	/* body is right after the packet type character */
	body = pb->info_start + 1;
	/* ignore the CRLF in the end of the body */
	body_end = pb->data + pb->packet_len - 2;
	
	switch (packettype) {
	/* the following are obsolete mic-e types: 0x1c 0x1d 
	 * case 0x1c:
	 * case 0x1d:
	 */
	case 0x27: /* ' */
	case 0x60: /* ` */
		/* could be mic-e, minimum body length 9 chars */
		if (paclen >= 9) {
			pb->packettype |= T_POSITION;
			return parse_aprs_mice(pb, body, body_end);
		}
		return 0;

	case '!':
		if (pb->info_start[1] == '!') { /* Ultimeter 2000 */
			pb->packettype |= T_WX;
			return 0;
		}
	case '=':
	case '/':
	case '@':
		/* check that we won't run over right away */
		if (body_end - body < 10)
			return 0;
		/* Normal or compressed location packet, with or without
		 * timestamp, with or without messaging capability
		 *
		 * ! and / have messaging, / and @ have a prepended timestamp
		 */
		pb->packettype |= T_POSITION;
		if (packettype == '/' || packettype == '@') {
			/* With a prepended timestamp, jump over it. */
			body += 7;
		}
		poschar = *body;
		if (valid_sym_table_compressed(poschar)) { /* [\/\\A-Za-j] */
		    	/* compressed position packet */
			if (body_end - body >= 13)
				return parse_aprs_compressed(pb, body, body_end);
			
		} else if (poschar >= 0x30 && poschar <= 0x39) { /* [0-9] */
			/* normal uncompressed position */
			if (body_end - body >= 19)
				return parse_aprs_uncompressed(pb, body, body_end);
		}
		return 0;

	case '$':
		if (body_end - body > 10) {
			// Is it OK to declare it as position packet ?
			return parse_aprs_nmea(pb, body, body_end);
		}
		return 0;

	case ':':
		pb->packettype |= T_MESSAGE;
		// quick and loose way to identify NWS and SKYWARN messages
		// they do apparently originate from "WXSRV", but that is not
		// guaranteed thing...
		if (memcmp(body,"NWS-",4) == 0) // as seen on specification
			pb->packettype |= T_NWS;
		if (memcmp(body,"NWS_",4) == 0) // as seen on data
			pb->packettype |= T_NWS;
		if (memcmp(body,"SKY",3) == 0)  // as seen on specification
			pb->packettype |= T_NWS;

		// Is it perhaps TELEMETRY related "message" ?
		if ( body[9] == ':' &&
		     ( memcmp( body+9, ":PARM.", 6 ) == 0 ||
		       memcmp( body+9, ":UNIT.", 6 ) == 0 ||
		       memcmp( body+9, ":EQNS.", 6 ) == 0 ||
		       memcmp( body+9, ":BITS.", 6 ) == 0    )) {
			pb->packettype &= ~T_MESSAGE;
			pb->packettype |=  T_TELEMETRY;
			// Fall through to recipient location lookup
		}

		// Or perhaps a DIRECTED QUERY ?
		if (body[9] == ':' && body[10] == '?') {
			pb->packettype &= ~T_MESSAGE;
			pb->packettype |=  T_QUERY;
			// Fall through to recipient location lookup
		}

		// Now find out if the message RECIPIENT address is known
		// to have some location data ?  Because then we can treat
		// them the same way in filters as we do those with real
		// positions..
		{
			char keybuf[CALLSIGNLEN_MAX+1];
			const char *p;
			int i;
			struct history_cell_t *history;

			p = body+1;
			for (i = 0; i < CALLSIGNLEN_MAX; ++i) {
				keybuf[i] = *p;
				// the recipient address is space padded
				// to 9 chars, while our historydb is not.
				if (*p == 0 || *p == ' ' || *p == ':')
					break;
			}
			keybuf[i] = 0;

			i = historydb_lookup( keybuf, i, &history );
			if (i > 0) {
				pb->lat     = history->lat;
				pb->lng     = history->lon;
				pb->cos_lat = history->coslat;

				pb->flags  |= F_HASPOS;
			}
		}
		return 1;

	case ';':
		if (body_end - body > 29)
			return parse_aprs_object(pb, body, body_end);
		return 0;

	case '>':
		pb->packettype |= T_STATUS;
		return 1;

	case '<':
		pb->packettype |= T_STATCAPA;
		return 1;

	case '?':
		pb->packettype |= T_QUERY;
		return 1;

	case ')':
		if (body_end - body > 18) {
			return parse_aprs_item(pb, body, body_end);
		}
		return 0;

	case 'T':
		if (body_end - body > 18) {
			pb->packettype |= T_TELEMETRY;
			return parse_aprs_telem(pb, body, body_end);
		}
		return 0;

	case '#': /* Peet Bros U-II Weather Station */
	case '*': /* Peet Bros U-I  Weather Station */
	case '_': /* Weather report without position */
		pb->packettype |= T_WX;
		return 1;

	case '{':
		pb->packettype |= T_USERDEF;
		return 1;

	case '}':
		pb->packettype |= T_THIRDPARTY;
		return 1;

	default:
		break;
	}

	/* When all else fails, try to look for a !-position that can
	 * occur anywhere within the 40 first characters according
	 * to the spec.  (X1J TNC digipeater bugs...)
	 */
	pos_start = memchr(body, '!', body_end - body);
	if ((pos_start) && pos_start - body <= 39) {
		poschar = *pos_start;
		if (valid_sym_table_compressed(poschar)) { /* [\/\\A-Za-j] */
		    	/* compressed position packet */
		    	if (body_end - pos_start >= 13)
		    		return parse_aprs_compressed(pb, pos_start, body_end);
			return 0;
		} else if (poschar >= 0x30 && poschar <= 0x39) { /* [0-9] */
			/* normal uncompressed position */
			if (body_end - pos_start >= 19)
				return parse_aprs_uncompressed(pb, pos_start, body_end);
			return 0;
		}
	}
	
	return 0;
}
