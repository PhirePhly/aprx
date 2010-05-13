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
 *	Uses RW-locking, W for inserts/cleanups, R for lookups.
 *
 *	Inserting does incidential cleanup scanning while traversing
 *	hash chains.
 *
 *	In APRS-IS there are about 25 000 distinct callsigns or
 *	item or object names with position information PER WEEK.
 *	DB lifetime of 48 hours cuts that down a bit more.
 *	Memory usage is around 3-4 MB
 */

#ifndef __HISTORYDB_H__
#define __HISTORYDB_H__


struct history_cell_t {
	struct history_cell_t *next;

	time_t       arrivaltime;
	int	     keylen;
	char         key[CALLSIGNLEN_MAX+2];
	unsigned int hash1;

	float	lat, coslat, lon;

	int  packettype;
	int  flags;

	int  packetlen;
	char *packet;
	char packetbuf[170]; /* Maybe a dozen packets are bigger than
				170 bytes long out of some 17 000 .. */
};


extern void historydb_init(void);

extern void historydb_dump(FILE *fp);

extern void historydb_cleanup(void);
extern void historydb_atend(void);

/* insert and lookup... */
// extern int historydb_insert(struct pbuf_t*);
extern int historydb_lookup(const char *keybuf, const int keylen, struct history_cell_t **result);

#endif
