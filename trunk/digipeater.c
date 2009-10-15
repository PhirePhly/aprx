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

static int digi_count;
static struct digipeater *digis;


int  digipeater_prepoll(struct aprxpolls *app) {
	return 0;
}
int  digipeater_postpoll(struct aprxpolls *app) {
	return 0;
}


void digipeater_receive(struct digipeater_source *src, struct pbuf_t *pb)
{
}



void digipeater_config(struct configfile *cf)
{
}
