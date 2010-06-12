/********************************************************************
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2010                            *
 *                                                                  *
 ********************************************************************/


/*
 *	The historydb contains positional packet data in form of:
 *	  - position packet
 *	  - objects
 *	  - items
 *	Keying varies, origination callsign of positions, name
 *	for object/item.
 *
 *	Inserting does incidential cleanup scanning while traversing
 *	hash chains.
 *
 *	In APRS-IS there are about 25 000 distinct callsigns or
 *	item or object names with position information PER WEEK.
 *	DB lifetime of 48 hours cuts that down a bit more.
 *	Memory usage is around 3-4 MB
 *
 *  --------------
 *
 *      On Tx-IGate the number of distinct callsigns is definitely
 *      lower...
 *
 */

#ifndef __HISTORYDB_H__
#define __HISTORYDB_H__

#include <stdint.h>
#define HISTORYDB_HASH_MODULO 128 /* fold bits: 7 & 14 */

struct pbuf_t;      // forward declarator
struct historydb_t; // forward..

typedef struct history_cell_t {
	struct history_cell_t *next;
	struct historydb_t    *db;

	time_t       arrivaltime;
	time_t	     from_aprsis;
	time_t	     from_radio;
	time_t       positiontime; // When last position was received
	uint16_t     packettype;
	uint16_t     flags;
	uint16_t     packetlen;
	uint8_t	     keylen;
	char         key[CALLSIGNLEN_MAX+2];

	float	lat, coslat, lon;
	uint32_t hash1;

	char *packet;
	char packetbuf[170]; /* Maybe a dozen packets are bigger than
				170 bytes long out of some 17 000 .. */
} history_cell_t;

typedef struct historydb_t {
	struct history_cell_t *hash[HISTORYDB_HASH_MODULO];

	// monitor counters and gauges
	long historydb_inserts;
	long historydb_lookups;
	long historydb_hashmatches;
	long historydb_keymatches;
	long historydb_cellgauge;
	long historydb_noposcount;
} historydb_t;


extern void historydb_init(void);

extern historydb_t *historydb_new(void);

extern void historydb_dump(const historydb_t *, FILE *fp);

extern void historydb_atend(void);

extern int  historydb_prepoll(struct aprxpolls *app);
extern int  historydb_postpoll(struct aprxpolls *app);

/* insert and lookup... */
extern int historydb_insert(historydb_t *db, const struct pbuf_t*);
extern history_cell_t *historydb_lookup(historydb_t *db, const char *keybuf, const int keylen);

#endif
