/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

/* Bits used only in the main program.. */
#include <signal.h>

time_t now; /* this is globally used */
int debug;
int verbout;
int erlangout;
char *rflogfile;
char *aprxlogfile;

#define CFGFILE "/etc/aprx.conf"


struct pollfd polls[MAXPOLLS];

int die_now;

const char *version = "aprx-v0.12";


static void sig_handler(int sig)
{
  die_now = 1;
}

static void usage(void)
{
	printf("aprx: [-d][-d][-e][-v][-l logfacility] [-f %s]\n", CFGFILE);
	printf("    -f %s:  where the configuration is\n", CFGFILE);
	printf("    -v:  Outputs textual format of received packets, and data on STDOUT.\n");
	printf("    -e:  Outputs raw ERLANG-report lines on SYSLOG.\n");
	printf("    -l ...: sets syslog FACILITY code for erlang reports, default: LOG_DAEMON\n");
	printf("    -d:  turn debug printout on, use to verify config file!\n");
	printf("         twice: prints also interaction with aprs-is system..\n");
	exit(64); /* EX_USAGE */
}

int fd_nonblockingmode(int fd)
{
	int __i = fcntl(fd, F_GETFL, 0);
	if (__i >= 0) {
	  /* set up non-blocking I/O */
	  __i |= O_NONBLOCK;
	  __i = fcntl(fd, F_SETFL, __i);
	}
	return __i;
}

int main(int argc, char * const argv[]) 
{
	int i;
	struct pollfd *fds;
	int nfds, nfds2;
	const char *cfgfile = "/etc/aprx.conf";
	const char *syslog_facility = "NONE";

	now = time(NULL);

	setlinebuf(stdout);
	setlinebuf(stderr);

	/* Init the poll(2) descriptor array */
	memset(polls, 0, sizeof(polls));
	for (i = 0; i < MAXPOLLS; ++i)
	  polls[i].fd = -1;

	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGHUP, sig_handler);

	while ((i = getopt(argc, argv, "def:hl:v?")) != -1) {
	  switch (i) {
	  case '?': case 'h':
	    usage();
	    break;
	  case 'd':
	    ++debug;
	    break;
	  case 'e':
	    ++erlangout;
	    break;
	  case 'l':
	    syslog_facility = optarg;
	    break;
	  case 'v':
	    ++verbout;
	    break;
	  case 'f':
	    cfgfile = optarg;
	    break;
	  default:
	    break;
	  }
	}


	erlang_init(syslog_facility);
	ttyreader_init();
	netax25_init();
	aprsis_init();

	erlang_start(1);
	readconfig(cfgfile); /* TODO: real parametrized cfg file location.. */

	if ((debug || verbout) && !mycall) {
	  fprintf(stderr,"NO GLOBAL  MYCALL=  PARAMETER CONFIGURED, WILL NOT CONNECT APRS-IS\n(This is OK, if no connection to APRS-IS is needed.)\n");
	}


	/* Must be after config reading ... */
	aprsis_start();

	/* The main loop */

	while (! die_now ) {

	  time_t next_timeout;
	  now = time(NULL);
	  next_timeout = now + 30;

	  fds = polls;
	  nfds = MAXPOLLS;

	  i = ttyreader_prepoll(nfds, &fds, &next_timeout);
	  nfds -= i;
	  nfds2 = i;
	  i = aprsis_prepoll(nfds, &fds, &next_timeout);
	  nfds2 += i;
	  nfds  -= i;
	  i = beacon_prepoll(nfds, &fds, &next_timeout);
	  nfds2 += i;
	  nfds  -= i;
	  i = netax25_prepoll(nfds, &fds, &next_timeout);
	  nfds2 += i;
	  nfds  -= i;
	  i = erlang_prepoll(nfds, &fds, &next_timeout);
	  nfds2 += i;
	  nfds  -= i;

	  if (next_timeout <= now)
	    next_timeout = now + 1; /* Just to be on safe side.. */

	  i = poll(polls, nfds2, (next_timeout - now) * 1000);
	  now = time(NULL);


	  i = beacon_postpoll(nfds2, polls);
	  i = ttyreader_postpoll(nfds2, polls);
	  i = netax25_postpoll(nfds2, polls);
	  i = aprsis_postpoll(nfds2, polls);
	  i = erlang_postpoll(nfds2, polls);

	}

	exit (0);
}

