/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2010                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

/*
 *  The DPRS RX Gateway
 *
 *  Receive data from DPRS.
 *  Convert to 3rd-party frame.
 *  Send out to APRSIS and Digipeaters.
 *
 */


typedef struct dprsgw_receiver {
	int i;
} dprsgw_receiver_t;


/*
 * ??
 */

void *dprsgw_new( void )
{
}


void dprslog( const char *buf ) {
  FILE *fp = fopen("/tmp/dprslog.txt","a");

  fprintf(fp, "%ld\t%s\n", now, buf);

  fclose(fp);
}



/*
 *  Receive one text line from serial port
 *  It will end with 0x00 byte, and not contain \r nor \n.
 *
 *  It MAY have junk at the start.
 *
 *


 */

void dprsgw_receive( struct serialport *S )
{
	int i;

	dprslog(S->rdline);
	
}
