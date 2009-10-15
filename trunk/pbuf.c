/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * **************************************************************** */

#define _SVID_SOURCE 1

#include "aprx.h"

/*
 * - Allocate pbuf
 * - Free pbuf
 * - Handle refcount  (get/put)
 */


static void pbuf_free(struct pbuf_t *pb)
{
	free(pb);
}

struct pbuf_t *pbuf_alloc( const int axlen,
			   const int tnc2len )
{
	// Picks suitably sized pbuf, and pre-cleans it
	// before passing to user

	int pblen = sizeof(struct pbuf_t) + axlen + tnc2len;
	struct pbuf_t *pb = malloc( pblen );

	memset(pb, 0, pblen );

	pb->packet_len = tnc2len;
	pb->buf_len    = tnc2len;

	pb->ax25addr = (unsigned char*)pb->data;
	pb->destcall = pb->data + axlen;

	return pb;
}

void pbuf_get( struct pbuf_t *pb )
{
	// Increments refcount
	pb->refcount += 1;
}

void pbuf_put( struct pbuf_t *pb )
{
	// Decrements refcount, if 0 -> free()!
	pb->refcount -= 1;
	if (pb->refcount == 0)
		pbuf_free(pb);
}


struct pbuf_t *pbuf_new(const int is_aprs, const int axlen, const int tnc2len)
{
	struct pbuf_t *pb = pbuf_alloc( axlen, tnc2len );
	pbuf_get(pb);

	pb->is_aprs = is_aprs;
	pb->t       = now;      // Arrival time

	return pb;
}
