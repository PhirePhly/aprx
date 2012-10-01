/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2012                            *
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

const int pbufcell_size  = sizeof(struct pbuf_t) + 2150;
const int pbufcell_align = __alignof__(struct pbuf_t);

void pbuf_init(void)
{
#ifndef _FOR_VALGRIND_
	/* A _few_... */

	pbuf_cells = cellinit( "filter",
			       pbufcell_size,
			       pbufcell_align,
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
	int pblen = sizeof(struct pbuf_t) + axlen + tnc2len + 2;

#ifndef _FOR_VALGRIND_
	// Picks suitably sized pbuf, and pre-cleans it
	// before passing to user

	struct pbuf_t *pb;
	if (pblen > 2150) {
	  // Outch!
	  return NULL;
	}
	pb = cellmalloc(pbuf_cells);
#else
	// No size limits with valgrind..
	struct pbuf_t *pb = malloc( pblen );
#endif

	if (debug > 1) printf("pbuf_alloc(%d,%d) -> %p\n",axlen,tnc2len,pb);

	memset(pb, 0, pblen );

	pb->packet_len = tnc2len;
	pb->buf_len    = tnc2len;
	pb->data[tnc2len] = 0;
        pb->ax25addr = (uint8_t*)pb->data + tnc2len + 1;


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


static struct pbuf_t *_pbuf_new(const int is_aprs, const int digi_like_aprs, const int axlen, const int tnc2len);
static struct pbuf_t *_pbuf_new(const int is_aprs, const int digi_like_aprs, const int axlen, const int tnc2len)
{
	struct pbuf_t *pb = pbuf_alloc( axlen, tnc2len );
	if (pb == NULL) return NULL;

	pbuf_get(pb);

	pb->is_aprs        = is_aprs;
	pb->digi_like_aprs = digi_like_aprs;
	pb->t              = now.tv_sec;      // Arrival time

	return pb;
}


// Do the pbuf filling in single location, processes the TNC2 header data
struct pbuf_t * pbuf_new( const int is_aprs, const int digi_like_aprs,
                          const int tnc2addrlen, const char *tnc2buf, const int tnc2len,
                          const int ax25addrlen, const void *ax25buf, const int ax25len )
{
	char *p;

	char *src_end; /* pointer to the > after srccall */
	char *path_start; /* pointer to the start of the path */
	const char *path_end; /* pointer to the : after the path */
	const char *packet_end; /* pointer to the end of the packet */
	const char *info_start; /* pointer to the beginning of the info */
	const char *info_end; /* end of the info */
	char *dstcall_end_or_ssid; /* end of dstcall, before SSID ([-:,]) */
	char *dstcall_end; /* end of dstcall including SSID ([:,]) */
	char *via_start; /* start of the digipeater path (after dstcall,) */
	// const char *data;	  /* points to original incoming path/payload separating ':' character */
	// int datalen;		  /* length of the data block excluding tail \r\n */
	int pathlen;		  /* length of the path  ==  data-s  */
        struct pbuf_t *pb;

	/* a packet looks like:
	 * SRCCALL>DSTCALL,PATH,PATH:INFO\r\n
	 * (we have normalized the \r\n by now)
         *
         * The tnc2addrlen is index of the first ':'.
	 */

        path_end = tnc2buf + tnc2addrlen;
        pathlen  = tnc2addrlen;
	// data     = path_end;            // Begins with ":"
	// datalen  = tnc2len - pathlen;   // Not including line end \r\n

	packet_end = tnc2buf + tnc2len; // Just to compare against far end..

	/* look for the '>' */
	src_end = memchr(tnc2buf, '>', pathlen < CALLSIGNLEN_MAX+1 ? pathlen : CALLSIGNLEN_MAX+1);
	if (!src_end) {
		return NULL;	// No ">" in packet start..
        }
	
	path_start = src_end+1;
	if (path_start >= packet_end) {	// We're already at the path end
		return NULL;
        }
	
	if (src_end - tnc2buf > CALLSIGNLEN_MAX || src_end - tnc2buf < CALLSIGNLEN_MIN) {
		return NULL; /* too long source callsign */
        }
	
	info_start = path_end+1;	// @":"+1 - first char of the payload
	if (info_start >= packet_end) {
		return NULL;
        }
	
	/* see that there is at least some data in the packet */
	info_end = packet_end;
	if (info_end <= info_start) {
		return NULL;
        }
	
	/* look up end of dstcall (excluding SSID - this is the way dupecheck and
	 * mic-e parser wants it)
	 */

	dstcall_end = path_start;
	while (dstcall_end < path_end && *dstcall_end != '-' && *dstcall_end != ',' && *dstcall_end != ':')
		dstcall_end++;
	dstcall_end_or_ssid = dstcall_end; // OK, SSID is here (or the dstcall end), go for the real end
	while (dstcall_end < path_end && *dstcall_end != ',' && *dstcall_end != ':')
		dstcall_end++;
	
	if (dstcall_end - path_start > CALLSIGNLEN_MAX) {
		return NULL; /* too long for destination callsign */
        }
	
	/* where does the digipeater path start? */
	via_start = dstcall_end;
	while (via_start < path_end && (*via_start != ',' && *via_start != ':')) {
		via_start++;
        }
	
        pb = _pbuf_new( is_aprs, digi_like_aprs, ax25len, tnc2len );
	if (!pb) {
          // This should never happen...
          return NULL;
	}
	
        // copy TNC2 data to its area
        p = pb->data;
        memcpy(p, tnc2buf, tnc2len);
        p += tnc2len;

        // Copy AX.25 data to its area..
	memcpy(pb->ax25addr, ax25buf, ax25len);
	pb->ax25addrlen = ax25addrlen;
	pb->ax25data    = pb->ax25addr + ax25addrlen;
	pb->ax25datalen = ax25len - ax25addrlen;
	
	// How much there really is data?
	pb->packet_len = tnc2len;
	
	packet_end = p; /* for easier overflow checking expressions */
	/* fill necessary info for parsing and dupe checking in the packet buffer */
	pb->srcname = pb->data;
	pb->srcname_len = src_end - tnc2buf;
	pb->srccall_end = pb->data + (src_end - tnc2buf); // "srccall>.." <-- @'>'
	pb->dstcall_end_or_ssid = pb->data + (dstcall_end_or_ssid - tnc2buf);
	pb->dstcall_end = pb->data + (dstcall_end - tnc2buf);
	pb->dstcall_len = via_start - src_end - 1;
	pb->info_start  = pb->data + tnc2addrlen + 1;

        return pb;
}
