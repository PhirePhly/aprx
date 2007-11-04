/* **************************************************************** *
 *                                                                  *
 *  APRSG-NG -- 2nd generation receive-only APRS-i-gate with        *
 *              minimal requirement of esoteric facilities or       *
 *              libraries of any kind beyond UNIX system libc.      *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */

#include "aprsg.h"

/* Bits used only in the main program.. */
#include <signal.h>

time_t now; /* this is globally used */

#define MAXPOLLS 20 /* No really all that much, 16 serial ports
		       one network socket to APRS-IS, something
		       for latter inventions.. */

struct pollfd polls[MAXPOLLS];

int die_now;

const char *version = "aprsg-ng-v0.01";


static void sig_handler(int sig)
{
  die_now = 1;
}


int main(int argc, char * const argv[]) 
{
	int i;
	struct pollfd *fds;
	int nfds, nfds2;
	const char *cfgfile = "/etc/aprsg-ng.conf";

	/* Init the poll(2) descriptor array */
	memset(polls, 0, sizeof(polls));
	for (i = 0; i < MAXPOLLS; ++i)
	  polls[i].fd = -1;
	memset(ttys, 0, sizeof(ttys));
	for (i = 0; i < MAXTTYS; ++i)
	  ttys[i].fd = -1;

	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);

	now = time(NULL);

	readconfig(cfgfile); /* TODO: real parametrized cfg file location.. */

	aprsis_init();


	/* The main loop */

	while (! die_now ) {

	  time_t next_timeout;
	  now = time(NULL);
	  next_timeout = now + 30;

	  aprsis_cond_reconnect();

	  fds = polls;
	  nfds = MAXPOLLS;

	  for (i = 0; i < MAXPOLLS; ++i)   polls[i].fd = -1;

	  i = ttyreader_prepoll(nfds, &fds, &next_timeout);
	  nfds -= i;
	  nfds2 = i;
	  i = aprsis_prepoll(nfds, &fds, &next_timeout);
	  nfds2 += i;
	  nfds  -= i;
	  i = beacon_prepoll(nfds, &fds, &next_timeout);
	  nfds2 += i;
	  nfds  -= i;


	  now = time(NULL);
	  if (next_timeout <= now)
	    next_timeout = now + 1; /* Just to be on safe side.. */

	  i = poll(polls, nfds2, (next_timeout - now) * 1000);
	  now = time(NULL);


	  i = beacon_postpoll(nfds2, polls);
	  i = aprsis_postpoll(nfds2, polls);
	  i = ttyreader_postpoll(nfds2, polls);

	}

	exit (0);
}

