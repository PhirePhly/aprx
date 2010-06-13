/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2010                            *
 *                                                                  *
 * **************************************************************** */

#define _SVID_SOURCE 1

#include "aprx.h"

/*
 * - Allocate pbuf
 * - Free pbuf
 * - Handle refcount  (get/put)
 */

#ifndef _FOR_VALGRIND_
static cellarena_t *pbuf_cells;
#endif

// int pbuf_size = sizeof(struct pbuf_t); // 152 bytes on i386
// int pbuf_alignment = __alignof__(struct pbuf_t); // 8 on i386

// 2150 byte pbuf takes in an AX.25 packet of about 1kB in size,
// and in APRS use there never should be larger than about 512 bytes.
// A 16 kB arena fits in 7 of these humongous pbufs.

void pbuf_init(void)
{
#ifndef _FOR_VALGRIND_
	/* A _few_... */

	pbuf_cells = cellinit( "filter",
			       sizeof(struct pbuf_t) + 2150,
			       __alignof__(struct pbuf_t),
			       CELLMALLOC_POLICY_LIFO,
			       16, // 16 kB at the time
			       0   // minfree
			       );
#endif
}

static void pbuf_free(struct pbuf_t *pb)
{
#ifndef _FOR_VALGRIND_
	cellfree(pbuf_cells, pb);
#else
	free(pb);
#endif
	if (debug > 1) printf("pbuf_free(%p)\n",pb);
}

struct pbuf_t *pbuf_alloc( const int axlen,
			   const int tnc2len )
{
#ifndef _FOR_VALGRIND_

	// Picks suitably sized pbuf, and pre-cleans it
	// before passing to user

	struct pbuf_t *pb;
	int pblen = sizeof(struct pbuf_t) + axlen + tnc2len + 2;
	if (pblen > 2150) {
	  // Outch!
	  return NULL;
	}
	pb = cellmalloc(pbuf_cells);
#else
	int pblen = sizeof(struct pbuf_t) + axlen + tnc2len + 2;
	// No size limits with valgrind..
	struct pbuf_t *pb = malloc( pblen );
#endif

	if (debug > 1) printf("pbuf_alloc(%d,%d) -> %p\n",axlen,tnc2len,pb);

	memset(pb, 0, pblen );

	pb->packet_len = tnc2len;
	pb->buf_len    = tnc2len;
	pb->data[tnc2len] = 0;

	// pb->destcall = pb->data + axlen;
	if (axlen > 0)
		pb->ax25addr = (uint8_t*)pb->data + tnc2len+1;

	return pb;
}

struct pbuf_t *pbuf_get( struct pbuf_t *pb )
{
	// Increments refcount
	pb->refcount += 1;
	return pb;
}

void pbuf_put( struct pbuf_t *pb )
{
	// Decrements refcount, if 0 -> free()!
	pb->refcount -= 1;
	if (pb->refcount == 0)
		pbuf_free(pb);
}


struct pbuf_t *pbuf_new(const int is_aprs, const int digi_like_aprs, const int axlen, const int tnc2len)
{
	struct pbuf_t *pb = pbuf_alloc( axlen, tnc2len );
	if (pb == NULL) return NULL;

	pbuf_get(pb);

	pb->is_aprs        = is_aprs;
	pb->digi_like_aprs = digi_like_aprs;
	pb->t              = now;      // Arrival time

	return pb;
}
