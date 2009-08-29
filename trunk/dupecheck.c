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

static int          dupecheck_cellgauge;
static dupecheck_t *dupecheckers;
static int          dupefilter_storetime = 30; /* 30 seconds */


#ifndef _FOR_VALGRIND_
struct dupe_record_t *dupecheck_free;
cellarena_t *dupecheck_cells;
#endif


/*
 *	The cellmalloc does not need internal MUTEX, it is being used in single thread..
 */

void dupecheck_init(void)
{
#ifndef _FOR_VALGRIND_
	dupecheck_cells = cellinit( "dupecheck",
				    sizeof(struct dupe_record_t),
				    __alignof__(struct dupe_record_t),
				    CELLMALLOC_POLICY_LIFO | CELLMALLOC_POLICY_NOMUTEX,
				    256 /* 0.25 MB at the time */,
				    0 /* minfree */);
#endif
}

/*
 * new_dupecheck() creates a new instance of dupechecker
 *
 */
dupecheck_t *new_dupecheck(void) {
	dupecheck_t *dp = malloc(sizeof(dupecheck_t));
	memset(dp, 0, sizeof(*dp));
	dp->next = dupecheckers;
	dupecheckers = dp;

	return dp;
}



static struct dupe_record_t *dupecheck_db_alloc(int alen, int pktlen)
{
	struct dupe_record_t *dp;
#ifndef _FOR_VALGRIND_
	if (dupecheck_free) { /* pick from free chain */
		dp = dupecheck_free;
		dupecheck_free = dp->next;
	} else
		dp = cellmalloc(dupecheck_cells);
	if (!dp)
		return NULL;
#else
	dp = malloc(pktlen + sizeof(*dp));
#endif
	memset(dp, 0, sizeof(*dp));
	dp->alen = alen;
	dp->plen = pktlen;
	dp->packet = dp->packetbuf;
	if (pktlen > sizeof(dp->packetbuf))
		dp->packet = malloc(pktlen+1);

	++dupecheck_cellgauge;

	return dp;
}

static void dupecheck_db_free(struct dupe_record_t *dp)
{
#ifndef _FOR_VALGRIND_
	if (dp->packet != dp->packetbuf)
		free(dp->packet);
	dp->next = dupecheck_free;
	dupecheck_free = dp;
	// cellfree(dupecheck_cells, dp);
#else
	free(dp);
#endif
	--dupecheck_cellgauge;
}

/*	The  dupecheck_cleanup() is for regular database cleanups,
 *	Call this about once a minute.
 *
 *	Note: entry validity is possibly shorter time than the cleanup
 *	invocation interval!
 */
void dupecheck_cleanup(void)
{
	struct dupe_record_t *dp, **dpp;
	int cleancount = 0, i;

	struct dupecheck_t *dpc = dupecheckers;

	for ( ; dpc != NULL; dpc = dpc->next ) {

	    for (i = 0; i < DUPECHECK_DB_SIZE; ++i) {
		dpp = & (dpc->dupecheck_db[i]);
		while (( dp = *dpp )) {
		    if (dp->t < now) {
			/* Old..  discard. */
			*dpp = dp->next;
			dp->next = NULL;
			dupecheck_db_free(dp);
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
 *	check a single packet for duplicates
 */

int dupecheck(dupecheck_t *dpc,
	      const char *addr, const int alen,
	      const char *data, const int dlen)
{
	/* check a single packet */
	// pb->flags |= F_DUPE; /* this is a duplicate! */

	int i;
	int addrlen;  // length of the address part
	int datalen;  // length of the payload
	uint32_t hash, idx;
	struct dupe_record_t **dpp, *dp;

	// 1) collect canonic rep of the address
	for (addrlen = 0; addrlen < alen; ++ addrlen) {
		const char c = addr[addrlen];
		if (c == '-' || c == 0 || c == ',' || c == ':') {
			break;
		}
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
	idx ^= (idx >> 24); /* fold the hash bits.. */
	idx ^= (idx >> 12); /* fold the hash bits.. */
	idx ^= (idx >>  6); /* fold the hash bits.. */
	i = idx % DUPECHECK_DB_SIZE;
	dpp = &(dpc->dupecheck_db[i]);
	while (*dpp) {
		dp = *dpp;
		if (dp->t < now) {
			// Old ones are discarded when seen
			*dpp = dp->next;
			dp->next = NULL;
			dupecheck_db_free(dp);
			continue;
		}
		if (dp->hash == hash) {
			// HASH match!  And not too old!
			if (dp->alen == addrlen &&
			    dp->plen == datalen &&
			    memcmp(addr, dp->addresses, addrlen) == 0 &&
			    memcmp(data, dp->packet,    datalen) == 0) {
				// PACKET MATCH!
				return 1;
			}
			// no packet match.. check next
		}
		dpp = &dp->next;
	}
	// dpp points to pointer at the tail of the chain

	// 4) Add comparison copy of non-dupe into dupe-db

	dp = dupecheck_db_alloc(addrlen, datalen);
	if (!dp) return -1; // alloc error!

	*dpp = dp;
	memcpy(dp->addresses, addr, addrlen);
	memcpy(dp->packet,    data, datalen);
	dp->hash = hash;
	dp->t    = now + dupefilter_storetime;
	return 0;
}
