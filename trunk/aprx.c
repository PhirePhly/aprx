/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007,2008                            *
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

#ifndef CFGFILE
#define CFGFILE "/etc/aprx.conf"
#endif

char *pidfile = VARRUN "/aprx.pid";

int die_now;

const char *version = APRXVERSION;


static void sig_handler(int sig)
{
  die_now = 1;
}

static void usage(void)
{
	printf("aprx: [-d][-d][-e][-v][-l logfacility] [-f %s]\n", CFGFILE);
	printf("    version: %s\n", version);
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
	const char *cfgfile = "/etc/aprx.conf";
	const char *syslog_facility = "NONE";
	int foreground = 0;
	struct aprxpolls app = { NULL, 0, 0 };

	now = time(NULL);

	setlinebuf(stdout);
	setlinebuf(stderr);

	/* Init the poll(2) descriptor array */

	signal(SIGTERM, sig_handler);
	signal(SIGINT,  sig_handler);
	signal(SIGHUP,  sig_handler);

	while ((i = getopt(argc, argv, "def:hl:v?")) != -1) {
	  switch (i) {
	  case '?': case 'h':
	    usage();
	    break;
	  case 'd':
	    ++debug;
	    ++foreground;
	    break;
	  case 'e':
	    ++erlangout;
	    ++foreground;
	    break;
	  case 'l':
	    syslog_facility = optarg;
	    break;
	  case 'v':
	    ++verbout;
	    ++foreground;
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

	if (!foreground) {
	  int pid = fork();
	  if (pid > 0) {
	    /* This is parent */
	    exit(0);
	  }
	  if (pid == 0) {
	    /* This is child ! */
	    FILE *pf = fopen(pidfile,"w");

	    setsid(); /* Happens or not ... */

	    if (!pf) {
	      /* Could not open pidfile! */
	      fprintf(stderr,"COULD NOT OPEN PIDFILE: '%s'\n", pidfile);
	      pidfile = NULL;
	    } else {
	      fprintf(pf,"%ld\n",(long)getpid());
	      fclose(pf);
	    }
	  }
	}

	erlang_start(2); /* reset PID, etc.. */

	/* Must be after config reading ... */
	aprsis_start();

	/* The main loop */

	while (! die_now ) {

	  now = time(NULL);

	  aprxpolls_reset(&app);
	  app.next_timeout = now + 30;

	  i = ttyreader_prepoll(&app);
	  i = aprsis_prepoll(&app);
	  i = beacon_prepoll(&app);
	  i = netax25_prepoll(&app);
	  i = erlang_prepoll(&app);

	  if (app.next_timeout <= now)
	    app.next_timeout = now + 1; /* Just to be on safe side.. */

	  i = poll(app.polls, app.pollcount, (app.next_timeout - now) * 1000);
	  now = time(NULL);


	  i = beacon_postpoll(&app);
	  i = ttyreader_postpoll(&app);
	  i = netax25_postpoll(&app);
	  i = aprsis_postpoll(&app);
	  i = erlang_postpoll(&app);

	}

	if (pidfile) {
	  unlink(pidfile);
	}

	exit (0);
}

