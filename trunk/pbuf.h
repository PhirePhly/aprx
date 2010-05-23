/*
 *	aprsc
 *
 *	(c) Heikki Hannikainen, OH7LZB <hessu@hes.iki.fi>
 *
 *     This program is licensed under the BSD license, which can be found
 *     in the file LICENSE.
 *	
 */

/* Modified for  APRX by Matti Aarnio, OH2MQK
 * Altered name from  worker.h  to pbuf.h, and
 * dropped about 70% of worker.h stuff...
 */

#ifndef WORKER_H
#define WORKER_H

#include <time.h>
#include <stddef.h>
#include <stdint.h>

/* minimum and maximum length of a callsign on APRS-IS */
#define CALLSIGNLEN_MIN 3
#define CALLSIGNLEN_MAX 9

/* packet length limiters and buffer sizes */
#define PACKETLEN_MIN 10	/* minimum length for a valid APRS-IS packet: "A1A>B1B:\r\n" */
#define PACKETLEN_MAX 512	/* maximum length for a valid APRS-IS packet (incl. CRLF) */

/*
 *  Packet length statistics:
 *
 *   <=  80:  about  25%
 *   <=  90:  about  36%
 *   <= 100:  about  73%
 *   <= 110:  about  89%
 *   <= 120:  about  94%
 *   <= 130:  about  97%
 *   <= 140:  about  98.7%
 *   <= 150:  about  99.4%
 */

#define PACKETLEN_MAX_SMALL  100 
#define PACKETLEN_MAX_MEDIUM 180 /* about 99.5% are smaller than this */
#define PACKETLEN_MAX_LARGE  PACKETLEN_MAX

/* number of pbuf_t structures to allocate at a time */
#define PBUF_ALLOCATE_BUNCH_SMALL  2000 /* grow to 2000 in production use */
#define PBUF_ALLOCATE_BUNCH_MEDIUM 2000 /* grow to 2000 in production use */
#define PBUF_ALLOCATE_BUNCH_LARGE    50 /* grow to 50 in production use */

/* a packet buffer */
/* Type flags -- some can happen in combinations: T_CWOP + T_WX / T_CWOP + T_POSITION ... */
#define T_POSITION  (1 << 0) // Packet is of position type
#define T_OBJECT    (1 << 1) // packet is an object
#define T_ITEM      (1 << 2) // packet is an item
#define T_MESSAGE   (1 << 3) // packet is a message
#define T_NWS       (1 << 4) // packet is a NWS message
#define T_WX        (1 << 5) // packet is WX data
#define T_TELEMETRY (1 << 6) // packet is telemetry
#define T_QUERY     (1 << 7) // packet is a query
#define T_STATUS    (1 << 8) // packet is status 
#define T_USERDEF   (1 << 9) // packet is userdefined
#define T_CWOP      (1 << 10) // packet is recognized as CWOP
#define T_STATCAPA  (1 << 11) // packet is station capability response
#define T_THIRDPARTY (1 << 12)
#define T_ALL	    (1 << 15) // set on _all_ packets

#define F_DUPE    1	/* Duplicate of a previously seen packet */
#define F_HASPOS  2	/* This packet has valid parsed position */

struct pbuf_t {
	struct pbuf_t *next;

	int16_t	 is_aprs;	// If not, then just digipeated frame..
	int16_t	 digi_like_aprs;
	int16_t	 from_aprsis;

	int16_t  refcount;

	int16_t	 reqcount;      // How many digipeat hops are requested?
	int16_t	 donecount;	// How many digipeat hops are already done?

	time_t   t;		/* when the packet was received */
	uint32_t seqnum;	/* ever increasing counter, dupecheck sets */
	uint16_t packettype;	/* bitmask: one or more of T_* */
	uint16_t flags;		/* bitmask: one or more of F_* */
	uint16_t srcname_len;	/* parsed length of source (object, item, srcall) name 3..9 */
	uint16_t dstcall_len;	/* parsed length of destination callsign *including* SSID */
	uint16_t entrycall_len;
	
	int packet_len;		/* the actual length of the TNC2 packet */
	int buf_len;		/* the length of this buffer */
	
	const char *destcall;	   /* start of dest callsign with SSID */
	const char *dstcall_end;   /* end of dest callsign with SSID */
	const char *srccall_end;   /* source callsign with SSID */
//	const char *qconst_start;  /* "qAX,incomingSSID:"	-- for q and e filters  */
	const char *info_start;    /* pointer to start of info field */
	const char *srcname;       /* source's name (either srccall or object/item name) */
	const char *recipient;	   /* message recipient field */
	
	float lat;	/* if the packet is PT_POSITION, latitude and longitude go here */
	float lng;	/* .. in RADIAN */
	float cos_lat;	/* cache of COS of LATitude for radial distance filter    */

	char symbol[3]; /* 2(+1) chars of symbol, if any, NUL for not found */

	uint8_t *ax25addr;	// Start of AX.25 address
	int      ax25addrlen;	// length of AX.25 address

	uint8_t *ax25data;	// Start of AX.25 data after addresses
	int      ax25datalen;	// length of that data

	char data[1];
};

/* global packet buffer */
extern struct pbuf_t  *pbuf_global;
extern struct pbuf_t  *pbuf_global_last;
extern struct pbuf_t **pbuf_global_prevp;
extern struct pbuf_t  *pbuf_global_dupe;
extern struct pbuf_t  *pbuf_global_dupe_last;
extern struct pbuf_t **pbuf_global_dupe_prevp;


#endif
