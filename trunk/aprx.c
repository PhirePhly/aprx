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

/* Bits used only in the main program.. */
#include <signal.h>

time_t now;			/* this is globally used */
int debug;
int verbout;
int erlangout;
const char *rflogfile;
const char *aprxlogfile;
const char *mycall;

const char *tocall = "APRX19";
const uint8_t tocall25[7] = {'A'<<1,'P'<<1,'R'<<1,'X'<<1,'1'<<1,'9'<<1,0x60};

#ifndef CFGFILE
#define CFGFILE "/etc/aprx.conf"
#endif

const char *pidfile = VARRUN "/aprx.pid";

int die_now;
int log_aprsis;

const char *swname = "aprx";
const char *swversion = APRXVERSION;


static void sig_handler(int sig)
{
	die_now = 1;
	signal(sig, sig_handler);
}

static void usage(void)
{
	printf("aprx: [-d][-d][-e][-v][-L][-l logfacility] [-f %s]\n",
	       CFGFILE);
	printf("    version: %s\n", swversion);
	printf("    -f %s:  where the configuration is\n", CFGFILE);
	printf("    -v:  Outputs textual format of received packets, and data on STDOUT.\n");
	printf("    -e:  Outputs raw ERLANG-report lines on SYSLOG.\n");
	printf("    -l ...: sets syslog FACILITY code for erlang reports, default: LOG_DAEMON\n");
	printf("    -d:  turn debug printout on, use to verify config file!\n");
	printf("         twice: prints also interaction with aprs-is system..\n");
	printf("    -L:  Log also all of APRS-IS traffic on relevant log.\n");
	exit(64);		/* EX_USAGE */
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

int main(int argc, char *const argv[])
{
	int i;
	const char *cfgfile = "/etc/aprx.conf";
	const char *syslog_facility = "NONE";
	int foreground = 0;
	struct aprxpolls app = { NULL, 0, 0, 0 };

	now = time(NULL);

	setlinebuf(stdout);
	setlinebuf(stderr);

	/* Init the poll(2) descriptor array */

	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	while ((i = getopt(argc, argv, "def:hLl:v?")) != -1) {
		switch (i) {
		case '?':
		case 'h':
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
		case 'L':
			log_aprsis = 1;
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


	interface_init(); // before any interface system and aprsis init !
	erlang_init(syslog_facility);
	ttyreader_init();
	netax25_init();
	dupecheck_init(); // before aprsis_init() !
	aprsis_init();

	erlang_start(1);
	readconfig(cfgfile);	/* TODO: real parametrized cfg file location.. */

	if (debug || verbout) {
	  if (!mycall && !aprsis_login) {
		fprintf(stderr,
			"APRX: NO GLOBAL  MYCALL=  PARAMETER CONFIGURED, WILL NOT CONNECT APRS-IS\n(This is OK, if no connection to APRS-IS is needed.)\n");
	  } else if (!mycall && !aprsis_login) {
		fprintf(stderr,
			"APRX: NO GLOBAL  APRSIS-LOGIN=  PARAMETER CONFIGURED, WILL NOT CONNECT APRS-IS\n(This is OK, if no connection to APRS-IS is needed.)\n");
	  }
	}

	if (!foreground) {
		/* See if pidfile exists ? */
		FILE *pf = fopen(pidfile, "r");
		if (pf) {	/* See if the pid exists ? */
			int rc, er;
			int pid = -1;
			fscanf(pf, "%d", &pid);
			fclose(pf);

			if (pid > 0) {
				rc = kill(pid, 0);
				er = errno;

				if ((rc == 0) || (er == EPERM)) {
					fprintf(stderr,
						"APRX: PIDFILE '%s' EXISTS, AND PROCESSID %d INDICATED THERE EXISTS TOO. FURTHER INSTANCES CAN ONLY BE RUN ON FOREGROUND!\n",
						pidfile, pid);
					exit(1);
				}
			}
		}
	}


	if (!foreground) {
		int pid = fork();
		if (pid > 0) {
			/* This is parent */
			exit(0);
		}
		/* child and error cases continue on main program.. */
	}

	if (1) {
		/* Open the pidfile, if you can.. */

		FILE *pf = fopen(pidfile, "w");

		setsid();	/* Happens or not ... */

		if (!pf) {
			/* Could not open pidfile! */
			fprintf(stderr, "COULD NOT OPEN PIDFILE: '%s'\n",
				pidfile);
			pidfile = NULL;
		} else {
			fprintf(pf, "%ld\n", (long) getpid());
			fclose(pf);
		}
	}


	erlang_start(2);	/* reset PID, etc.. */

	/* Do following as late as possible.. */

	/* In all cases we close STDIN/FD=0.. */
	close(0);
	/* .. and replace it with reading from /dev/null.. */
	open("/dev/null", O_RDONLY, 0);
	/* Leave STDOUT and STDERR open */

	if (!foreground) {
		/* when daemoning, we close also stdout and stderr.. */
		close(1);
		close(2);
		/* .. and replace them with writing to /dev/null.. */
		open("/dev/null", O_WRONLY, 0);
		open("/dev/null", O_WRONLY, 0);
	}

	/* .. but not latter than this. */


	/* Must be after config reading ... */
	aprsis_start();
	netax25_start();
	telemetry_start();
	igate_start();

	/* The main loop */

	while (!die_now) {

		now = time(NULL);

		aprxpolls_reset(&app);
		app.next_timeout = now + 30;

		i = ttyreader_prepoll(&app);
		i = aprsis_prepoll(&app);
		i = beacon_prepoll(&app);
		i = netax25_prepoll(&app);
		i = erlang_prepoll(&app);
		i = telemetry_prepoll(&app);
		i = dupecheck_prepoll(&app);
		i = digipeater_prepoll(&app);

		if (app.next_timeout <= now)
			app.next_timeout = now + 1;	/* Just to be on safe side.. */

		i = poll(app.polls, app.pollcount,
			 (app.next_timeout - now) * 1000);
		now = time(NULL);


		i = beacon_postpoll(&app);
		i = ttyreader_postpoll(&app);
		i = netax25_postpoll(&app);
		i = aprsis_postpoll(&app);
		i = erlang_postpoll(&app);
		i = telemetry_postpoll(&app);
		i = dupecheck_postpoll(&app);
		i = digipeater_postpoll(&app);

	}

	if (pidfile) {
		unlink(pidfile);
	}

	exit(0);
}
