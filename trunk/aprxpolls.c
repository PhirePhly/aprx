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


/* aprxpolls libary functions.. */


void aprxpolls_reset(struct aprxpolls *app)
{
	app->pollcount = 0;
}

struct pollfd *aprxpolls_new(struct aprxpolls *app)
{
	struct pollfd *p;
	app->pollcount += 1;
	if (app->pollcount >= app->pollsize) {
		app->pollsize += 8;
		app->polls = realloc(app->polls,
				     sizeof(struct pollfd) * app->pollsize);
	}
	
	p = &(app->polls[app->pollcount - 1]);
	memset(p, 0, sizeof(struct pollfd));
	return p;
}
