/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2012                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

/*
 *	Some parts of this code are copied from:
 */
/*
 *	aprsc
 *
 *	(c) Heikki Hannikainen, OH7LZB <hessu@hes.iki.fi>
 *
 *	This program is licensed under the BSD license, which can be found
 *	in the file LICENSE.
 *	
 */

/*
 *	dupecheck.c: the dupe-checkers
 */

static int           dupecheck_cellgauge;
static int           dupecheckers_count;
static dupecheck_t **dupecheckers;


#ifndef _FOR_VALGRIND_
cellarena_t *dupecheck_cells;
#endif

const int duperecord_size  = sizeof(struct dupe_record_t);
const int duperecord_align = __alignof__(struct dupe_record_t);

/*
 *	The cellmalloc does not need internal MUTEX, it is being used in single thread..
 */

void dupecheck_init(void)
{
#ifndef _FOR_VALGRIND_
	dupecheck_cells = cellinit( "dupecheck",
				    duperecord_size,
				    duperecord_align,
				    CELLMALLOC_POLICY_LIFO | CELLMALLOC_POLICY_NOMUTEX,
				    4 /* 4 kB at the time */,
				    0 /* minfree */);
#endif
}

/*
 * dupecheck_new() creates a new instance of dupechecker
 *
 */
dupecheck_t *dupecheck_new(const int storetime) {
	dupecheck_t *dp = malloc(sizeof(dupecheck_t));
	memset(dp, 0, sizeof(*dp));

	++dupecheckers_count;
	dupecheckers = realloc(dupecheckers,
			       sizeof(dupecheck_t *) * dupecheckers_count);
	dupecheckers[ dupecheckers_count -1 ] = dp;

        dp->storetime = storetime;

	return dp;
}


static dupe_record_t *dupecheck_db_alloc(int alen, int pktlen)
{
	dupe_record_t *dp;
#ifndef _FOR_VALGRIND_
//	if (debug) printf("DUPECHECK db alloc(alen=%d,dlen=%d) %s",
//			  alen,pktlen, dupecheck_free ? "FreeChain":"CellMalloc");

	dp = cellmalloc(dupecheck_cells);
//	if (debug) printf(" dp=%p\n",dp);
	if (dp == NULL)
		return NULL;
	// cellmalloc() block may need separate pktbuf
	memset(dp, 0, sizeof(*dp));
	dp->packet = dp->packetbuf;
	if (pktlen > sizeof(dp->packetbuf))
		dp->packet = malloc(pktlen+1);
#else
	// directly malloced block is fine as is
	dp = malloc(pktlen + sizeof(*dp));
	memset(dp, 0, sizeof(*dp));
	dp->packet = dp->packetbuf; // always suitable size
#endif
	dp->alen = alen;
	dp->plen = pktlen;

	++dupecheck_cellgauge;

	dupecheck_get(dp); // increment refcount

	// if(debug)printf("DUPECHECK db alloc() returning dp=%p\n",dp);
	return dp;
}

static void dupecheck_db_free(dupe_record_t *dp)
{
	if (dp->pbuf != NULL) { // If a pbuf is referred, release it
		pbuf_put(dp->pbuf); // decrements refcount - and frees at zero.
		dp->pbuf = NULL;
	}
#ifndef _FOR_VALGRIND_
	if (dp->packet != dp->packetbuf)
		free(dp->packet);
	cellfree(dupecheck_cells, dp);
#else
	free(dp);
#endif
	--dupecheck_cellgauge;
}

// Increment refcount
dupe_record_t *dupecheck_get(dupe_record_t *dp)
{
	dp->refcount += 1;
	return dp;
}

// Decrement refcount, when zero, call free
void dupecheck_put(dupe_record_t *dp)
{
	dp->refcount -= 1;
	if (dp->refcount <= 0) {
		dupecheck_db_free(dp);
	}
}

/*	The  dupecheck_cleanup() is for regular database cleanups,
 *	Call this about once a minute.
 *
 *	Note: entry validity is possibly shorter time than the cleanup
 *	invocation interval!
 */
static void dupecheck_cleanup(void)
{
	dupe_record_t *dp, **dpp;
	int cleancount = 0, i, d;

	// All dupecheckers..
	for (d = 0; d < dupecheckers_count; ++d) {

	  // Within this dupechecker...
	  struct dupecheck_t *dpc = dupecheckers[d];
	  for (i = 0; i < DUPECHECK_DB_SIZE; ++i) {
	    dpp = & (dpc->dupecheck_db[i]);
	    while (( dp = *dpp )) {
	      if (dp->t_exp < now) {
		/* Old..  discard. */
		*dpp = dp->next;
		dp->next = NULL;
		dupecheck_put(dp);
		++cleancount;
		continue;
	      }
	      /* No expiry, just advance the pointer */
	      dpp = &dp->next;
	    }
	  }
	}
	// hlog( LOG_DEBUG, "dupecheck_cleanup() removed %d entries, count now %ld",
	//       cleancount, dupecheck_cellgauge );
}

/*
 *	Check a single packet for duplicates in APRS sense
 *	The addr/alen must be in TNC2 monitor format, data/dlen
 *      are expected to be APRS payload as well.
 */

dupe_record_t *dupecheck_aprs(dupecheck_t *dpc,
			      const char *addr, const int alen,
			      const char *data, const int dlen)
{
	/* check a single packet */
	// pb->flags |= F_DUPE; /* this is a duplicate! */

	int i;
	int addrlen;  // length of the address part
	int datalen;  // length of the payload
	uint32_t hash, idx;
	dupe_record_t **dpp, *dp;

	// 1) collect canonic rep of the address (SRC,DEST, no VIAs)
	i = 1;
	for (addrlen = 0; addrlen < alen; ++ addrlen) {
		const char c = addr[addrlen];
		if (c == 0 || c == ',' || c == ':') {
			break;
		}
		if (c == '-' && i) {
			i = 0;
		}
	}

        // code to prevent segmentation fault
        if (addrlen > 18) {
          if (debug>1) printf("  addrlen=\"%d\" > 18, discard packet\n",addrlen);
          return NULL;
        }

	// Canonic tail has no SPACEs in data portion!
	// TODO: how to treat 0 bytes ???
	datalen = dlen;
	while (datalen > 0 && data[datalen-1] == ' ')
		--datalen;

	// there are no 3rd-party frames in APRS-IS ...

	// 2) calculate checksum (from disjoint memory areas)

	hash = keyhash(addr, addrlen, 0);
	hash = keyhash(data, datalen, hash);
	idx  = hash;

	// 3) lookup if same checksum is in some hash bucket chain
	//  3b) compare packet...
	//    3b1) flag as F_DUPE if so
	// DUPECHECK_DB_SIZE == 16 -> 4 bits index
	idx ^= (idx >> 16); /* fold the hash bits.. */
	idx ^= (idx >>  8); /* fold the hash bits.. */
	idx ^= (idx >>  4); /* fold the hash bits.. */
	i = idx % DUPECHECK_DB_SIZE;
	dpp = &(dpc->dupecheck_db[i]);
	while (*dpp) {
		dp = *dpp;
		if (dp->t_exp < now) {
			// Old ones are discarded when seen
			*dpp = dp->next;
			dp->next = NULL;
			dupecheck_put(dp);
			continue;
		}
		if (dp->hash == hash) {
			// HASH match!  And not too old!
			if (dp->alen == addrlen &&
			    dp->plen == datalen &&
			    memcmp(addr, dp->addresses, addrlen) == 0 &&
			    memcmp(data, dp->packet,    datalen) == 0) {
				// PACKET MATCH!
				dp->seen += 1;
				return dp;
			}
			// no packet match.. check next
		}
		dpp = &dp->next;
	}
	// dpp points to pointer at the tail of the chain

	// 4) Add comparison copy of non-dupe into dupe-db

	dp = dupecheck_db_alloc(addrlen, datalen);
	if (dp == NULL) return NULL; // alloc error!
	*dpp = dp; // Put it on tail of existing chain


	memcpy(dp->addresses, addr, addrlen);
	memcpy(dp->packet,    data, datalen);

	dp->seen  = 1;  // First observation gets number 1
	dp->hash  = hash;
	dp->t     = now;
	dp->t_exp = now + dpc->storetime;
	return NULL;
}


/*
 *  dupecheck_pbuf() returns pointer to dupe record, if pbuf is
 *  a duplicate.  Otherwise it return a NULL.
 */
dupe_record_t *dupecheck_pbuf(dupecheck_t *dpc, struct pbuf_t *pb, const int viscous_delay)
{
	int i;
	uint32_t hash, idx;
	dupe_record_t **dpp, *dp;
	const char *addr = pb->data;
	int   alen = pb->dstcall_end - addr;

	const char *dataend = pb->data + pb->packet_len;
	const char *data    = pb->info_start;
	int   dlen = dataend - data;

	int addrlen = alen;
	int datalen = dlen;
	char *p;

	/* if (debug && pb->is_aprs) {
	  printf("dupecheck[1] addr='");
	  fwrite(addr, alen, 1, stdout);
	  printf("' data='");
	  fwrite(data, dlen, 1, stdout);
	  printf("'\n");
	} */


	// Canonic tail has no SPACEs in data portion!
	// TODO: how to treat 0 bytes ???
	
	if (!pb->is_aprs) {
		// data and dlen are raw AX.25 section pointers
		data    = (const char*) pb->ax25data;
		datalen = pb->ax25datalen;

	} else {  // Do with APRS rules
	    for (;;) {

		// 1) collect canonic rep of the address
		i = 1;
		for (addrlen = 0; addrlen < alen; ++ addrlen) {
			const char c = addr[addrlen];
			if (c == 0 || c == ',' || c == ':') {
				break;
			}
			if (c == '-' && i) {
				i = 0;
			}
		}
		while (datalen > 0 && data[datalen-1] == ' ')
			--datalen;

		if (data[0] == '}') {
			// 3rd party frame!
			addr = data+1;
			p = memchr(addr,':',datalen-1);
			if (p == NULL)
				break; // Invalid 3rd party frame, no ":" in it
			alen = p - addr;
			data = p+1;
			datalen = dataend - data;

			/* if (debug && pb->is_aprs) {
			  printf("dupecheck[2] 3rd-party: addr='");
			  fwrite(addr, alen, 1, stdout);
			  printf("' data='");
			  fwrite(data, datalen, 1, stdout);
			  printf("'\n");
			} */

			continue;  // repeat the processing!
		}
		break; // No repeat necessary in general case
	    }
	}

	// 2) calculate checksum (from disjoint memory areas)

	/* if (debug && pb->is_aprs) {
	  printf("dupecheck[3] addr='");
	  fwrite(addr, addrlen, 1, stdout);
	  printf("' data='");
	  fwrite(data, datalen, 1, stdout);
	  printf("'\n");
	} */

	hash = keyhash(addr, addrlen, 0);
	hash = keyhash(data, datalen, hash);
	idx  = hash;

	/* if (debug>1) {
	     printf("DUPECHECK: Addr='");
	     fwrite(addr, 1, addrlen, stdout);
	     printf("' Data='");
	     fwrite(data, 1, datalen, stdout);
	     printf("'  hash=%x\n", hash);
	   }
	*/

	// 3) lookup if same checksum is in some hash bucket chain
	//  3b) compare packet...
	//    3b1) flag as F_DUPE if so
	// DUPECHECK_DB_SIZE == 16 -> 4 bits index
	idx ^= (idx >> 16); /* fold the hash bits.. */
	idx ^= (idx >>  8); /* fold the hash bits.. */
	idx ^= (idx >>  4); /* fold the hash bits.. */
	i = idx % DUPECHECK_DB_SIZE;
	dpp = &(dpc->dupecheck_db[i]);
	while (*dpp) {
		dp = *dpp;
		if (dp->t_exp < now) {
			// Old ones are discarded when seen
			*dpp = dp->next;
			dp->next = NULL;
			dupecheck_put(dp);
			continue;
		}
		if (dp->hash == hash) {
			// HASH match!  And not too old!
			if (dp->alen == addrlen &&
			    dp->plen == datalen &&
			    memcmp(addr, dp->addresses, addrlen) == 0 &&
			    memcmp(data, dp->packet,    datalen) == 0) {
				// PACKET MATCH!
				if (viscous_delay > 0)
				  dp->delayed_seen += 1;
				else
				  dp->seen += 1;
				return dp;
			}
			// no packet match.. check next
		}
		dpp = &dp->next;
	}
	// dpp points to pointer at the tail of the chain

	// 4) Add comparison copy of non-dupe into dupe-db

	dp = dupecheck_db_alloc(addrlen, datalen);
	if (dp == NULL) {
	  if (debug) printf("DUPECHECK ALLOC ERROR!\n");
	  return NULL; // alloc error!
	}
	*dpp = dp; // Put it on tail of existing chain

	memcpy(dp->addresses, addr, addrlen);
	memcpy(dp->packet,    data, datalen);

	dp->pbuf  = pbuf_get(pb); // increments refcount
	if (viscous_delay > 0) {  // First observation gets number 1
	  dp->seen         = 0;
	  dp->delayed_seen = 1;
	  dp->pbuf         = pb;
	} else {
	  dp->seen         = 1;
	  dp->delayed_seen = 0;
	}

	dp->hash  = hash;
	dp->t     = now;
	dp->t_exp = now + dpc->storetime;

	return dp;
}

/*
 * dupechecker aprx poll integration, timed tasks control
 *
 */

static time_t dupecheck_cleanup_nexttime;

int dupecheck_prepoll(struct aprxpolls *app)
{
	if (dupecheck_cleanup_nexttime < app->next_timeout)
		app->next_timeout = dupecheck_cleanup_nexttime;

	return 0;		/* No poll descriptors, only time.. */
}


int dupecheck_postpoll(struct aprxpolls *app)
{
	if (dupecheck_cleanup_nexttime > now)
		return 0;	/* Too early.. */

	dupecheck_cleanup_nexttime = now + 30; // tick every 30 second or so

	dupecheck_cleanup();

	return 0;
}
