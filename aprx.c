/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2013                            *
 *                                                                  *
 * **************************************************************** */
#include "aprx.h"

/* Bits used only in the main program.. */
#include <signal.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
# include <time.h>
#endif
#include <fcntl.h>

struct timeval now;			/* this is globally used */
int debug;
int verbout;
int erlangout;
const char *rflogfile;
const char *aprxlogfile;
const char *mycall;
float myloc_lat;
float myloc_coslat;
float myloc_lon;
const char *myloc_latstr;
const char *myloc_lonstr;

const char *tocall = "APRX28";
const uint8_t tocall25[7] = {'A'<<1,'P'<<1,'R'<<1,'X'<<1,'2'<<1,'8'<<1,0x60};

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
	if (debug)
	  printf("SIGNAL %d - DYING!\n", sig);
}

static void usage(void)
{
	printf("aprx: [-d[d[d]]][-e][-i][-v][-L][-l logfacility] [-f %s]\n",
	       CFGFILE);
	printf("    version: %s\n", swversion);
	printf("    -f %s:  where the configuration is\n", CFGFILE);
	printf("    -v:  Outputs textual format of received packets, and data on STDOUT.\n");
	printf("    -e:  Outputs raw ERLANG-report lines on SYSLOG.\n");
	printf("    -i:  Keep the program foreground without debugging printouts.\n");
	printf("    -l ...: sets syslog FACILITY code for erlang reports, default: LOG_DAEMON\n");
	printf("    -d:  turn debug printout on, use to verify config file!\n");
	printf("         twice: prints also interaction with aprs-is system..\n");
	printf("    -L:  Log also all of APRS-IS traffic on relevant log.\n");
	exit(64);		/* EX_USAGE */
}

static void versionprint()
{
	printf("aprx: %s\n", swversion);
        exit(1);
}

void fd_nonblockingmode(int fd)
{
	int __i = fcntl(fd, F_GETFL, 0);
	if (__i >= 0) {
		/* set up non-blocking I/O */
		__i |= O_NONBLOCK;
		__i = fcntl(fd, F_SETFL, __i);
	}
	// return __i;
}

int main(int argc, char *const argv[])
{
	int i;
	const char *cfgfile = "/etc/aprx.conf";
	const char *syslog_facility = "NONE";
	int foreground = 0;
        int millis;

	/* Init the poll(2) descriptor array */
	struct aprxpolls app = APRXPOLLS_INIT;

        gettimeofday(&now, NULL); // init global time reference

	setlinebuf(stdout);
	setlinebuf(stderr);

	while ((i = getopt(argc, argv, "def:hiLl:vV?")) != -1) {
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
		case 'i':
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
                case 'V':
                	versionprint();
                	break;
		default:
			break;
		}
	}

	interface_init(); // before any interface system and aprsis init !
	erlang_init(syslog_facility);
	ttyreader_init();
#ifdef PF_AX25			/* PF_AX25 exists -- highly likely a Linux system ! */
	netax25_init();
#endif
#ifdef ENABLE_AGWPE
	agwpe_init();
#endif
	dupecheck_init(); // before aprsis_init() !
#ifndef DISABLE_IGATE
	aprsis_init();
#endif
	filter_init();
	pbuf_init();

	i = readconfig(cfgfile);
	if (i) {
	  fflush(stdout);
	  fprintf(stderr, "Seen configuration errors. Aborting!\n");
	  fflush(stderr);
	  exit(1); // CONFIGURATION ERRORS SEEN! ABORT!
	}

	erlang_start(1);
#ifndef DISABLE_IGATE
	historydb_init();
#endif

	if (debug || verbout) {
	  if (!mycall
#ifndef DISABLE_IGATE
	      && !aprsis_login
#endif
	      ) {
		fflush(stdout);
		fprintf(stderr,
			"APRX: NO GLOBAL  MYCALL=  PARAMETER CONFIGURED, WILL NOT CONNECT APRS-IS\n(This is OK, if no connection to APRS-IS is needed.)\n");
	  } else if (!mycall
#ifndef DISABLE_IGATE
		     && !aprsis_login
#endif
		     ) {
		fflush(stdout);
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
					fflush(stdout);
					fprintf(stderr,
						"APRX: PIDFILE '%s' EXISTS, AND PROCESSID %d INDICATED THERE EXISTS TOO. FURTHER INSTANCES CAN ONLY BE RUN ON FOREGROUND!\n",
						pidfile, pid);
					fflush(stderr);
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
		poll((void*)&pid, 0, 500);

	}

	if (1) {
		/* Open the pidfile, if you can.. */

		FILE *pf = fopen(pidfile, "w");

		setsid();	/* Happens or not ... */

		if (!pf) {
			/* Could not open pidfile! */
			fflush(stdout);
			fprintf(stderr, "COULD NOT OPEN PIDFILE: '%s'\n",
				pidfile);
			pidfile = NULL;
		} else {
			int f = fileno(pf);
			if (flock(f, LOCK_EX|LOCK_NB) < 0) {
				if (errno == EWOULDBLOCK) {
					printf("Could not lock pid file file %s, another process has a lock on it. Another process running - bailing out.\n", pidfile);
				} else {
					printf("Failed to lock pid file %s: %s\n", pidfile, strerror(errno));
				}
				exit(1);
			}
			
			fprintf(pf, "%ld\n", (long) getpid());
			// Leave it open - flock will prevent double-activation
			dup(f); // don't care what the fd number is
			fclose(pf);
		}
	}


	erlang_start(2);	// reset PID, etc..

	// Do following as late as possible..

	// In all cases we close STDIN/FD=0..
	// .. and replace it with reading from /dev/null..
	i = open("/dev/null", O_RDONLY, 0);
	if (i >= 0) { dup2(i, 0); close(i); }
	
	// Leave STDOUT and STDERR open

	if (!foreground) {
	  // when daemoning, we close also stdout and stderr..
	  dup2(0, 1);
	  dup2(0, 2);
	}

	// .. but not latter than this.


	// Set default signal handling

	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGHUP, sig_handler);
	signal(SIGPIPE, SIG_IGN);



	// Must be after config reading ...
	netresolv_start();
#ifndef DISABLE_IGATE
	aprsis_start();
#endif
#ifdef PF_AX25			/* PF_AX25 exists -- highly likely a Linux system ! */
	netax25_start();
#endif
#ifdef ENABLE_AGWPE
	agwpe_start();
#endif
	telemetry_start();
#ifndef DISABLE_IGATE
	igate_start();
#endif

	// The main loop

	while (!die_now) {

                gettimeofday(&now, NULL);

		aprxpolls_reset(&app);
                tv_timeradd_millis( &app.next_timeout, &now, 30000 ); // 30 seconds

		i = ttyreader_prepoll(&app);
                // if (debug>3)printf("after ttyreader prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
#ifndef DISABLE_IGATE
		i = aprsis_prepoll(&app);
                // if (debug>3)printf("after aprsis prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
#endif
		i = beacon_prepoll(&app);
                // if (debug>3)printf("after beacon prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
#ifdef PF_AX25			/* PF_AX25 exists -- highly likely a Linux system ! */
		i = netax25_prepoll(&app);
                // if (debug>3)printf("after netax25 prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
#endif
#ifdef ENABLE_AGWPE
		i = agwpe_prepoll(&app);
                // if (debug>3)printf("after agwpe prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
#endif
		i = erlang_prepoll(&app);
                // if (debug>3)printf("after erlang prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
		i = telemetry_prepoll(&app);
                // if (debug>3)printf("after telemetry prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
		i = dupecheck_prepoll(&app);
                // if (debug>3)printf("after dupecheck prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
		i = digipeater_prepoll(&app);
                // if (debug>3)printf("after digipeater prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
#ifndef DISABLE_IGATE
		i = historydb_prepoll(&app);
                // if (debug>3)printf("after historydb prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
		i = dprsgw_prepoll(&app);
                // if (debug>3)printf("after dprsgw prepoll - timeout millis=%d\n",aprxpolls_millis(&app));
#endif

		// if (app.next_timeout <= now.tv_sec)
                // app.next_timeout = now.tv_sec + 1;	// Just to be on safe side..

                millis = aprxpolls_millis(&app);
                if (millis < 10)
                  millis = 10;

		i = poll(app.polls, app.pollcount, millis);
                gettimeofday(&now, NULL);


		i = beacon_postpoll(&app);
		i = ttyreader_postpoll(&app);
#ifdef PF_AX25			/* PF_AX25 exists -- highly likely a Linux system ! */
		i = netax25_postpoll(&app);
#endif
#ifdef ENABLE_AGWPE
		i = agwpe_postpoll(&app);
#endif
#ifndef DISABLE_IGATE
		i = aprsis_postpoll(&app);
#endif
		i = erlang_postpoll(&app);
		i = telemetry_postpoll(&app);
		i = dupecheck_postpoll(&app);
		i = digipeater_postpoll(&app);
#ifndef DISABLE_IGATE
		i = historydb_postpoll(&app);
		i = dprsgw_postpoll(&app);
#endif

	}
	aprxpolls_free(&app); // valgrind..

#ifndef DISABLE_IGATE
	aprsis_stop();
#endif
	netresolv_stop();

	if (pidfile) {
		unlink(pidfile);
	}

	exit(0);
}


void printtime(char *buf, int buflen)
{
	struct timeval tv;
	struct tm t;

	gettimeofday(&tv, NULL);
        gmtime_r(&tv.tv_sec, &t);
	// strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);
	sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
		t.tm_year+1900,t.tm_mon+1,t.tm_mday,
		t.tm_hour,t.tm_min,t.tm_sec,
		(int)(tv.tv_usec / 1000));
}

static struct syslog_facs {
	const char *name;
	int fac_code;
} syslog_facs[] = {
	{
	"NONE", -1}, {
	"LOG_DAEMON", LOG_DAEMON},
#ifdef LOG_FTP
	{
	"LOG_FTP", LOG_FTP},
#endif
#ifdef LOG_LPR
	{
	"LOG_LPR", LOG_LPR},
#endif
#ifdef LOG_MAIL
	{
	"LOG_MAIL", LOG_MAIL},
#endif
#ifdef LOG_USER
	{
	"LOG_USER", LOG_USER},
#endif
#ifdef LOG_UUCP
	{
	"LOG_UUCP", LOG_UUCP},
#endif
	{
	"LOG_LOCAL0", LOG_LOCAL0}, {
	"LOG_LOCAL1", LOG_LOCAL1}, {
	"LOG_LOCAL2", LOG_LOCAL2}, {
	"LOG_LOCAL3", LOG_LOCAL3}, {
	"LOG_LOCAL4", LOG_LOCAL4}, {
	"LOG_LOCAL5", LOG_LOCAL5}, {
	"LOG_LOCAL6", LOG_LOCAL6}, {
	"LOG_LOCAL7", LOG_LOCAL7}, {
	NULL, 0}
};

void aprx_syslog_init(const char *syslog_facility_name)
{
	static int done_once = 0;
	int syslog_fac = LOG_DAEMON, i;

	if (done_once) {
		closelog();	/* We reconfigure from config file! */
	} else
		++done_once;
	for (i = 0;; ++i) {
		if (syslog_facs[i].name == NULL) {
			fprintf(stderr,
				"Sorry, unknown erlang syslog facility code name: %s, not supported in this system.\n",
				syslog_facility_name);
			fprintf(stderr, "Accepted list is:");
			for (i = 0;; ++i) {
				if (syslog_facs[i].name == NULL)
					break;
				fprintf(stderr, " %s",
					syslog_facs[i].name);
			}
			fprintf(stderr, "\n");
			break;
		}
		if (strcasecmp(syslog_facs[i].name, syslog_facility_name)
		    == 0) {
			syslog_fac = syslog_facs[i].fac_code;
			break;
		}
	}

	if (syslog_fac >= 0) {
		erlangsyslog = 1;
		openlog("aprx", LOG_NDELAY | LOG_PID, syslog_fac);
	}
}

int tv_timerdelta_millis(struct timeval *_now, struct timeval *_target)
{
	int deltasec  = _target->tv_sec  - _now->tv_sec;
        int deltausec = _target->tv_usec - _now->tv_usec;
        while (deltausec < 0) {
        	deltausec += 1000000;
                --deltasec;
        }
        return deltasec * 1000 + deltausec / 1000;
}

void tv_timeradd_millis(struct timeval *res, struct timeval *a, int millis)
{
	if (res != a) {
          // Copy if different pointers..
          *res = *a;
        }
        int usec = (int)(res->tv_usec) + millis * 1000;
        if (usec >= 1000000) {
          int dsec = (usec / 1000000);
          res->tv_sec += dsec;
          usec %= 1000000;
          // if (debug>3) printf("tv_timeadd_millis() dsec=%d dusec=%d\n",dsec, usec);
        }
        res->tv_usec = usec;
}

void tv_timeradd_seconds(struct timeval *res, struct timeval *a, int seconds)
{
	if (res != a) {
          // Copy if different pointers..
          *res = *a;
        }
        res->tv_sec += seconds;
}

int tv_timercmp(struct timeval *a, struct timeval *b)
{
  // if (debug>3) {
  // int dt_sec  = a->tv_sec - b->tv_sec;
  // int dt_usec = a->tv_usec - b->tv_usec;
  // printf("tv_timercmp(%d.%06d <=> %d.%06d) dt=%d:%06d ret= ",
  // a->tv_sec, a->tv_usec, b->tv_sec, b->tv_usec, dt_sec, dt_usec);
  // }

	if (a->tv_sec < b->tv_sec) {
          // if (debug>3) printf("-1s\n");
          return -1;
        }
	if (a->tv_sec > b->tv_sec) {
          // if (debug>3) printf("1s\n");
          return 1;
        }
        if (a->tv_usec < b->tv_usec) {
          // if (debug>3) printf("-1u\n");
          return -1;
        }
        if (a->tv_usec > b->tv_usec) {
          // if (debug>3) printf("1u\n");
          return 1;
        }
        // if (debug>3) printf("0\n");
        return 0; // equals!
}
