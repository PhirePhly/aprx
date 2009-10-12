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



struct pbuf_t pbuf_alloc( const int ax25hdrlen,
			  const int payloadlength,
			  const int tnc2hdrlen )
{
}

