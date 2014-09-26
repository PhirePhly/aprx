/********************************************************************
 *  APRX -- 2nd generation APRS iGate and digi with                 *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 ********************************************************************/

#include "aprx.h"

#ifndef DISABLE_IGATE

#include <strings.h>
#include <ctype.h>
#include <math.h>


int lastposition_storetime = 3600; // 1 hour

static historydb_t **_dbs;
static int           _dbs_count;

void historydb_nopos(void) {}         /* profiler call counter items */
void historydb_nointerest(void) {}
void historydb_hashmatch(void) {}
void historydb_keymatch(void) {}
void historydb_dataupdate(void) {}

// Single aprx wide alloc system
static cellarena_t   *historydb_cells;

const int historydb_cellsize  = sizeof(struct history_cell_t);
const int historydb_cellalign = __alignof__(struct history_cell_t);

void historydb_init(void)
{
	// printf("historydb_init() sizeof(mutex)=%d sizeof(rwlock)=%d\n",
	//       sizeof(pthread_mutex_t), sizeof(rwlock_t));

	// _dbs = malloc(sizeof(void*));
	// _dbs_count = 0;

	historydb_cells = cellinit( "historydb",
				    historydb_cellsize,
				    historydb_cellalign, 
				    CELLMALLOC_POLICY_FIFO,
				    32 /* 32 kB */,
				    0 /* minfree */ );
}

/* new instance - for new digipeater tx */
historydb_t *historydb_new(void)
{
	historydb_t *db = calloc(1, sizeof(*db));

	++_dbs_count;
	_dbs = realloc(_dbs, sizeof(void*)*_dbs_count);
	_dbs[_dbs_count-1] = db;

	return db;
}



/* Called only under WR-LOCK */
void historydb_free(struct history_cell_t *p)
{
	if (p->packet != p->packetbuf)
		free(p->packet);
	if (p->last_heard != p->last_heard_buf)
		free(p->last_heard);

	--p->db->historydb_cellgauge;

	cellfree( historydb_cells, p );
}

/* Called only under WR-LOCK */
struct history_cell_t *historydb_alloc(historydb_t *db, int packet_len)
{
	struct history_cell_t *ret = cellmalloc( historydb_cells );
	if (!ret) return NULL;
	++db->historydb_cellgauge;
	ret->db = db;
	ret->last_heard = ((top_interfaces_group <= MAX_IF_GROUP) ?
			   ret->last_heard_buf :
			   malloc(sizeof(time_t)*top_interfaces_group));
	return ret;
}

/*
 *     The  historydb_atend()  does exist primarily to make valgrind
 *     happy about lost memory object tracking.
 */
void historydb_atend(void)
{
	int j, i;
	for (j = 0; j < _dbs_count; ++j) {
	  historydb_t *db = _dbs[j];
	  struct history_cell_t *hp, *hp2;
	  for (i = 0; i < HISTORYDB_HASH_MODULO; ++i) {
	    hp = db->hash[i];
	    while (hp) {
	      hp2 = hp->next;
	      historydb_free(hp);
	      hp = hp2;
	    }
	  }
	}
}

void historydb_dump_entry(FILE *fp, const struct history_cell_t *hp)
{
	fprintf(fp, "%ld\t", hp->arrivaltime);
	(void)fwrite(hp->key, hp->keylen, 1, fp);
	fprintf(fp, "\t");
	fprintf(fp, "%d\t%d\t", hp->packettype, hp->flags);
	fprintf(fp, "%f\t%f\t", hp->lat, hp->lon);
	fprintf(fp, "%d\t", hp->packetlen);
	(void)fwrite(hp->packet, hp->packetlen, 1, fp);
	fprintf(fp, "\n"); /* newline */
}

void historydb_dump(const historydb_t *db, FILE *fp)
{
	/* Dump the historydb out on text format */
	int i;
	struct history_cell_t *hp;
	time_t expirytime   = tick.tv_sec - lastposition_storetime;

	for ( i = 0; i < HISTORYDB_HASH_MODULO; ++i ) {
		hp = db->hash[i];
		for ( ; hp ; hp = hp->next )
                	if (timecmp(hp->arrivaltime, expirytime) > 0)
				historydb_dump_entry(fp, hp);
	}
}


static int foldhash( const unsigned int h1 )
{
	unsigned int h2 = h1 ^ (h1 >> 7) ^ (h1 >> 14); /* fold hash bits.. */
	return (h2 % HISTORYDB_HASH_MODULO);
}


/* insert... */

history_cell_t *historydb_insert(historydb_t *db, const struct pbuf_t *pb)
{
	return historydb_insert_(db, pb, 0);
}

history_cell_t *historydb_insert_(historydb_t *db, const struct pbuf_t *pb, const int insertall)
{
	int i;
	unsigned int h1;
	int isdead = 0, keylen;
	struct history_cell_t **hp, *cp, *cp1;

	time_t expirytime   = tick.tv_sec - lastposition_storetime;

	char keybuf[CALLSIGNLEN_MAX+2];
	char *s;

	// (pb->flags & F_HASPOS) <-- that indicates that at parse time
	//                            the packet either had a position, or
	//                            a position information was supplemented
	//                            to it via historydb lookup

	if (!insertall && !(pb->packettype & T_POSITION)) {  // <-- packet has position data
		++db->historydb_noposcount;
		historydb_nopos(); /* debug thing -- profiling counter */
		return NULL; /* No positional data... */
	}

	/* NOTE: Parser does set on MESSAGES the RECIPIENTS
	**       location if such is known! We do not want them...
	**       .. and several other cases where packet has no
	**       positional data in it, but source callsign may
	**       have previous entry with data.
	*/

	/* NOTE2: We could use pb->srcname, and pb->srcname_len here,
	**        but then we would not know if this is a "kill-item"
	*/

	keybuf[CALLSIGNLEN_MAX] = 0;
	if (pb->packettype & T_OBJECT) {
		/* Pick object name  ";item  *" */
		memcpy( keybuf, pb->info_start+1, CALLSIGNLEN_MAX+1);
		keybuf[CALLSIGNLEN_MAX+1] = 0;
		s = strchr(keybuf, '*');
		if (s) *s = 0;
		else {
			s = strchr(keybuf, '_'); // kill an object!
			if (s) {
				*s = 0;
				isdead = 1;
			}
		}
		s = keybuf + strlen(keybuf);
		for ( ; s > keybuf; --s ) {  // tail space padded..
			if (*s == ' ') *s = ' ';
			else break;
		}

	} else if (pb->packettype & T_ITEM) {
		// Pick item name  ") . . . !"  or ") . . . _"
		memcpy( keybuf, pb->info_start+1, CALLSIGNLEN_MAX+1);
		keybuf[CALLSIGNLEN_MAX+1] = 0;
		s = strchr(keybuf, '!');
		if (s) *s = 0;
		else {
			s = strchr(keybuf, '_'); // kill an item!
			if (s) {
				*s = 0;
				isdead = 1;
			}
		}

	} else if (pb->packettype & T_MESSAGE) {
		// Pick originator callsign
		memcpy( keybuf, pb->data, CALLSIGNLEN_MAX) ;
		s = strchr(keybuf, '>');
		if (s) *s = 0;

	} else if (pb->packettype & T_POSITION) {
		// Pick originator callsign
		memcpy( keybuf, pb->data, CALLSIGNLEN_MAX) ;
		s = strchr(keybuf, '>');
		if (s) *s = 0;

	} else {
        	if (insertall) {
                	// Pick originator callsign
                	memcpy( keybuf, pb->data, CALLSIGNLEN_MAX) ;
                        s = strchr(keybuf, '>');
                        if (s) *s = 0;

                } else {

                	historydb_nointerest(); // debug thing -- a profiling counter
                        return NULL; // Not a packet with positional data, not interested in...
                }
	}
	keylen = strlen(keybuf);

	++db->historydb_inserts;

	h1 = keyhash(keybuf, keylen, 0);
	i  = foldhash(h1);
	if (debug > 1) printf(" key='%s' hash=%d", keybuf, i);

	cp = cp1 = NULL;
	hp = &db->hash[i];

	// scan the hash-bucket chain, and do incidential obsolete data discard
	while (( cp = *hp )) {
		if (timecmp(cp->arrivaltime, expirytime) < 0) {
			// OLD...
			*hp = cp->next;
			cp->next = NULL;
			historydb_free(cp);
			continue;
		}
		if ( (cp->hash1 == h1)) {
		       // Hash match, compare the key
		    historydb_hashmatch(); // debug thing -- a profiling counter
		    ++db->historydb_hashmatches;
		    if ( cp->keylen == keylen &&
			 (memcmp(cp->key, keybuf, keylen) == 0) ) {
		  	// Key match!
		    	historydb_keymatch(); // debug thing -- a profiling counter
			++db->historydb_keymatches;
			if (isdead) {
				// Remove this key..
				*hp = cp->next;
				cp->next = NULL;
				historydb_free(cp);
				continue;
			} else {
				historydb_dataupdate(); // debug thing -- a profiling counter
				// Update the data content
				cp1 = cp;
				if (pb->flags & F_HASPOS) {
				  // Update coordinate, if available
				  cp->lat         = pb->lat;
				  cp->coslat      = pb->cos_lat;
				  cp->lon         = pb->lng;
				  cp->positiontime = pb->t;
				}
				cp->packettype  = pb->packettype;
				cp->flags      |= pb->flags;

				cp->arrivaltime = pb->t;
				cp->flags       = pb->flags;
				cp->packetlen   = pb->packet_len;
				cp->last_heard[pb->source_if_group] = pb->t;
				if ( cp->packet != cp->packetbuf )
					free( cp->packet );

				cp->packet = cp->packetbuf; // default case
				if ( cp->packetlen > sizeof(cp->packetbuf) ) {
				  // Needs bigger buffer than pre-allocated one,
				  // thus it retrieves that one from heap.
				  cp->packet = malloc( cp->packetlen );
				}
				memcpy( cp->packet, pb->data, cp->packetlen );
			}
		    }
		} // .. else no match, advance hp..
		hp = &(cp -> next);
	}

	if (!cp1 && !isdead) {
		// Not found on this chain, append it!
		cp = historydb_alloc(db, pb->packet_len);
		cp->next = NULL;
		memcpy(cp->key, keybuf, keylen);
		cp->key[keylen] = 0; /* zero terminate */
		cp->keylen = keylen;
		cp->hash1 = h1;

		cp->lat         = pb->lat;
		cp->coslat      = pb->cos_lat;
		cp->lon         = pb->lng;
		cp->arrivaltime = pb->t;
		cp->packettype  = pb->packettype;
		cp->flags       = pb->flags;
		cp->last_heard[pb->source_if_group] = pb->t;
		if (pb->flags & F_HASPOS)
		  cp->positiontime = pb->t;

		cp->packetlen   = pb->packet_len;
		cp->packet      = cp->packetbuf; // default case
		if (cp->packetlen > sizeof(cp->packetbuf)) {
		  // Needs bigger buffer than pre-allocated one,
		  // thus it retrieves that one from heap.
		  cp->packet = malloc( cp->packetlen );
		}

                // Initial value is 32.0 tokens to permit
                // digipeat a packet source at the first
                // time it has been heard -- including to
                // possible multiple transmitters. Within
                // about 5 seconds this will be dropped
                // down to max burst rate of the srcratefilter
                // parameter. This code does not know how
                // many interfaces there are...
                cp->tokenbucket = 32.0;

		*hp = cp; 
	}

	return *hp;
}

history_cell_t *historydb_insert_heard(historydb_t *db, const struct pbuf_t *pb)
{
	int i;
	unsigned int h1;
	int keylen;
	struct history_cell_t **hp, *cp, *cp1;

	time_t expirytime   = tick.tv_sec - lastposition_storetime;

	char keybuf[CALLSIGNLEN_MAX+2];


	/* NOTE: Parser does set on MESSAGES the RECIPIENTS
	**       location if such is known! We do not want them...
	**       .. and several other cases where packet has no
	**       positional data in it, but source callsign may
	**       have previous entry with data.
	*/

	/* NOTE2: We could use pb->srcname, and pb->srcname_len here,
	**        but then we would not know if this is a "kill-item"
	*/

	keybuf[CALLSIGNLEN_MAX+1] = 0;

	if (pb->packettype & T_OBJECT) {
	  historydb_nointerest(); // debug thing -- a profiling counter
	  if (debug > 1) printf(" .. objects not interested\n");
	  return NULL; // Not interested in ";objects :"

	} else if (pb->packettype & T_ITEM) {
	  historydb_nointerest(); // debug thing -- a profiling counter
	  if (debug > 1) printf(" .. items not interested\n");
	  return NULL; // Not interested in ")items..."

	} else if (pb->packettype & T_MESSAGE) {
	  // Pick originator callsign
	  //memcpy( keybuf, pb->data, CALLSIGNLEN_MAX) ;
	  //s = strchr(keybuf, '>');
	  //if (s) *s = 0;
          memcpy(keybuf, pb->srcname, pb->srcname_len);
          keybuf[pb->srcname_len] = 0;

	} else if (pb->packettype & T_POSITION) {
	  // Something with a position (but not an item or an object)
	  //memcpy( keybuf, pb->data, CALLSIGNLEN_MAX) ;
	  //s = strchr(keybuf, '>');
	  //if (s) *s = 0;
          memcpy(keybuf, pb->srcname, pb->srcname_len);
          keybuf[pb->srcname_len] = 0;

	} else {
	  if (debug > 1) printf(" .. other not interested\n");
	  historydb_nointerest(); // debug thing -- a profiling counter
	  return NULL; // Not a packet with positional data, not interested in...
	}
	keylen = strlen(keybuf);

	++db->historydb_inserts;

	h1 = keyhash(keybuf, keylen, 0);
	i  = foldhash(h1);
	if (debug > 1) printf(" key='%s' hash=%d", keybuf, i);

	cp1 = NULL;
	hp = &db->hash[i];

	// scan the hash-bucket chain, and do incidential obsolete data discard
	while (( cp = *hp ) != NULL) {
        	if (timecmp(cp->arrivaltime, expirytime) < 0) {
			// OLD...
			if (debug > 1) printf(" .. dropping old record\n");
			*hp = cp->next;
			cp->next = NULL;
			historydb_free(cp);
			continue;
		}
		if ( (cp->hash1 == h1)) {
		       // Hash match, compare the key
		    historydb_hashmatch(); // debug thing -- a profiling counter
		    ++db->historydb_hashmatches;
		    if (debug > 1) printf(" .. found matching hash");
		    if ( cp->keylen == keylen &&
			 (memcmp(cp->key, keybuf, keylen) == 0) ) {
		  	// Key match!
		        if (debug > 1) printf(" .. found matching key!\n");

		    	historydb_keymatch(); // debug thing -- a profiling counter
			++db->historydb_keymatches;

			historydb_dataupdate(); // debug thing -- a profiling counter
			// Update the data content
			cp1 = cp;
			if (pb->flags & F_HASPOS) {
			  // Update coordinate, if available
			  cp->lat         = pb->lat;
			  cp->coslat      = pb->cos_lat;
			  cp->lon         = pb->lng;
			  cp->positiontime = pb->t;
			  cp->arrivaltime  = pb->t;
			}
			cp->flags      |= pb->flags;

			// Track packet source timestamps
			cp->last_heard[pb->source_if_group] = pb->t;

			// Don't save a message on top of positional packet
			if (!(pb->packettype & T_MESSAGE)) {
			  cp->packettype  = pb->packettype;
			  cp->arrivaltime = pb->t;
			  cp->flags       = pb->flags;
			  cp->packetlen   = pb->packet_len;
			  if ( cp->packet != cp->packetbuf )
			    free( cp->packet );
				  
			  cp->packet = cp->packetbuf; // default case
			  if ( cp->packetlen > sizeof(cp->packetbuf) ) {
			    // Needs bigger buffer than pre-allocated one,
			    // thus it retrieves that one from heap.
			    cp->packet = malloc( cp->packetlen );
			  }
			  memcpy( cp->packet, pb->data, cp->packetlen );
			}
		    }
		} // .. else no match, advance hp..
		hp = &(cp -> next);
	}

	if (!cp1) {
		if (debug > 1) printf(" .. inserting new history entry.\n");

		// Not found on this chain, append it!
		cp = historydb_alloc(db, pb->packet_len);
		cp->next = NULL;
		memcpy(cp->key, keybuf, keylen);
		cp->key[keylen] = 0; /* zero terminate */
		cp->keylen = keylen;
		cp->hash1 = h1;

		cp->lat         = pb->lat;
		cp->coslat      = pb->cos_lat;
		cp->lon         = pb->lng;
		cp->arrivaltime = pb->t;
		cp->packettype  = pb->packettype;
		cp->flags       = pb->flags;
		cp->last_heard[pb->source_if_group] = pb->t;
		if (pb->flags & F_HASPOS)
		  cp->positiontime = pb->t;

		cp->packetlen   = pb->packet_len;
		cp->packet      = cp->packetbuf; // default case
		if (cp->packetlen > sizeof(cp->packetbuf)) {
		  // Needs bigger buffer than pre-allocated one,
		  // thus it retrieves that one from heap.
		  cp->packet = malloc( cp->packetlen );
		}

		*hp = cp; 
	}
	else
	  return cp1; // != NULL

	return *hp;
}


/* lookup... */

history_cell_t *historydb_lookup(historydb_t *db, const char *keybuf, const int keylen)
{
	int i;
	unsigned int h1;
	struct history_cell_t *cp;

	// validity is 5 minutes shorter than expiration time..
	time_t validitytime   = tick.tv_sec - lastposition_storetime + 5*60;

	++db->historydb_lookups;

	h1 = keyhash(keybuf, keylen, 0);
	i  = foldhash(h1);

	cp = db->hash[i];

	if (debug > 1) printf("historydb_lookup('%.*s') -> i=%d", keylen, keybuf, i);

	for ( ; cp != NULL ; cp = cp->next ) {
	  if ( (cp->hash1 == h1) &&
	       // Hash match, compare the key
	       (cp->keylen == keylen) ) {
	    if (debug > 1) printf(" .. hash match");
	    if (memcmp(cp->key, keybuf, keylen) == 0) {
	      if (debug > 1) printf(" .. key match");
	      // Key match!
	      if (timecmp(cp->arrivaltime, validitytime) > 0) {
		if (debug > 1) printf(" .. and not too old\n");
		return cp;
	      }
	    }
	  }
	}
	if (debug > 1) printf(" .. no match\n");
	return NULL;
}



/*
 *	The  historydb_cleanup()  exists to purge too old data out of
 *	the database at regular intervals.  Call this about once a minute.
 */

static void historydb_cleanup(historydb_t *db)
{
	struct history_cell_t **hp, *cp;
	int i, cleancount = 0;

	if (debug > 1) printf("historydb_cleanup() ");

	time_t expirytime   = tick.tv_sec - lastposition_storetime;

	for (i = 0; i < HISTORYDB_HASH_MODULO; ++i) {
		hp = &db->hash[i];

		// multiple locks ? one for each bucket, or for a subset of buckets ?

		while (( cp = *hp )) {
                	if (timecmp(cp->arrivaltime, expirytime) < 0) {
				// OLD...
				*hp = cp->next;
				cp->next = NULL;
				historydb_free(cp);
				++cleancount;
				if (debug > 1) printf(" drop(%p) i=%d", cp, i);

			} else {
				/* No expiry, just advance the pointer */
				hp = &(cp -> next);
			}
		}
	}
	if (debug > 1) printf(" .. done.\n");
}


static time_t next_cleanup_time;

int  historydb_prepoll(struct aprxpolls *app)
{
	return 0;
}

int  historydb_postpoll(struct aprxpolls *app)
{
	int i;
        // Limit next cleanup to be at most 60 second in future
        // (just in case the system time jumped back)
        if (next_cleanup_time >= tick.tv_sec+61) {
          next_cleanup_time = tick.tv_sec + 60;
        }
	if (next_cleanup_time >= tick.tv_sec) return 0;
	next_cleanup_time = tick.tv_sec + 60; // A minute from now..

	for (i = 0; i < _dbs_count; ++i) {
	  historydb_cleanup(_dbs[i]);
	}

	return 0;
}

#endif
