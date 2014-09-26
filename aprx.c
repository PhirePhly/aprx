/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation APRS iGate and digi with                 *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
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
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

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
        aprxlog("aprx ending (SIG %d) - %s",sig,swversion);
	if (debug) {
          // Avoid stdio FILE* interlocks within signal handler
          char buf[64];
	  sprintf(buf, "SIGNAL %d - DYING!\n", sig);
          write(1, buf, strlen(buf));
        }
}

static void sig_child(int sig)
{
	int status;
        int pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        	beacon_childexit(pid);
        }
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

int time_reset = 1;             // observed time jump, initially as "reset is happening!"
static struct timeval old_tick; // monotonic
// static struct timeval old_now;  // wall-clock

static int timetick_count;

void timetick(void)
{
	++timetick_count;
        old_tick  = tick;
        //old_now = now;

	// Monotonic (or as near as possible) clock..
	// .. which is NOT wall clock time.
#ifdef HAVE_CLOCK_GETTIME
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	tick.tv_usec = ts.tv_nsec/1000;
	tick.tv_sec  = ts.tv_sec;
        // if (debug) printf("newtick: %d.%6d\n", tick.tv_sec, tick.tv_usec);
#else
	gettimeofday(&tick, NULL); // fallback when no clock_gettime() is available
#endif
        // Wall clock time
        // gettimeofday(&tick, NULL);

        // Main program clears this when appropriate
        int delta = 0;
        if (old_tick.tv_sec != 0) {
          delta = tv_timerdelta_millis(&old_tick, &tick);
          if (delta < -1) { // Up to 0.99999 seconds backwards for a leap second
            if (debug) {
              printf("MONOTONIC TIME JUMPED BACK BY %g SECONDS. ttcallcount=%d\n", delta/1000.0, timetick_count);
            }
            time_reset = 1;
          } else if (delta > 32000) { // 30.0 + leap second + margin
            if (debug) {
              printf("MONOTONIC TIME JUMPED FORWARD BY %g SECONDS. ttcallcount=%d mypid=%d\n", delta/1000.0, timetick_count, getpid());
            }
            time_reset = 1;
          } else {
            // Time is OK.
            // time_reset = 0;
          }
        } else {
          time_reset = 1;
          // This happens before argv is parsed, thus debug is never set.
          // But if it sets happens afterwards...
          if (debug) printf("Initializing MONOTONIC time\n");
        }
        // if (debug>1) printf("TIMETICK %ld:%6d  %d delta=%d ms\n", tick.tv_sec, tick.tv_usec, timetick_count, delta);
}

int main(int argc, char *const argv[])
{
	int i;
	const char *cfgfile = "/etc/aprx.conf";
	const char *syslog_facility = "NONE";
	int foreground = 0;
        int millis;
        int can_clear_timereset;

	/* Init the poll(2) descriptor array */
	struct aprxpolls app = APRXPOLLS_INIT;

        timetick(); // init global time references

        setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
        setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

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
	  exit(1); // CONFIION ERRORS SEEN! ABORT!
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
	signal(SIGINT,  sig_handler);
	signal(SIGHUP,  sig_handler);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, sig_child);

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

        aprxlog("aprx start - %s",swversion);

	// The main loop

        can_clear_timereset = 0;

	while (!die_now) {

        	timetick(); // pre-poll

		aprxpolls_reset(&app);
                tv_timeradd_millis( &app.next_timeout, &tick, 30000 ); // 30 seconds

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

                // All pre-polls are done
                if (can_clear_timereset) {
                  // if (time_reset) {
                  //   printf("Clearing time_reset.\n");
                  // }
                  time_reset = 0;
                } else {
                  can_clear_timereset = 1;
                }

		// if (app.next_timeout <= now.tv_sec)
                // app.next_timeout = now.tv_sec + 1;	// Just to be on safe side..

                millis = aprxpolls_millis(&app);
                if (millis < 10)
                  millis = 10;

		i = poll(app.polls, app.pollcount, millis);
                timetick(); // post-poll


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

        // Wall lock time for printouts
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

#ifdef HAVE_STDARG_H
#ifdef __STDC__
void aprxlog(const char *fmt, ...)
#else
void aprxlog(fmt)
#endif
#else
/* VARARGS */
void aprxlog(va_list)
va_dcl
#endif
{
	va_list ap;
	char timebuf[60];

        printtime(timebuf, sizeof(timebuf));
	if (verbout) {
#ifdef 	HAVE_STDARG_H
          va_start(ap, fmt);
#else
          const char *fmt;
          va_start(ap);
          fmt    = va_arg(ap, const char *);
#endif

	  fprintf(stdout, "%s ", timebuf);
	  vfprintf(stdout, fmt, ap);
          (void)fprintf(stdout, "\n");

#ifdef 	HAVE_STDARG_H
          va_end(ap);
#endif
	}

        if (aprxlogfile) {
          FILE *fp;

#ifdef 	HAVE_STDARG_H
          va_start(ap, fmt);
#else
          const char *fmt;
          va_start(ap);
          fmt    = va_arg(ap, const char *);
#endif


#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
          pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
#endif
          fp = fopen(aprxlogfile, "a");
          if (fp != NULL) {
            setlinebuf(fp);
            fprintf(fp, "%s ", timebuf);
            vfprintf(fp, fmt, ap);
            (void)fprintf(fp, "\n");
            fclose(fp);
          }
#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
          pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
#endif

#ifdef 	HAVE_STDARG_H
          va_end(ap);
#endif
        }
}


/* ---------------------------------------------------------- */

void rfloghex(const char *portname, char direction, int discard, const uint8_t *buf, int buflen)
{
}

void rflog(const char *portname, char direction, int discard, const char *tnc2buf, int tnc2len)
{
	if (rflogfile) {
#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
        	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
#endif

		FILE *fp = NULL;
		if (strcmp("-",rflogfile)==0) {
		  if (debug < 2) return;
		  fp = stdout;
		} else {
		  fp = fopen(rflogfile, "a");
		}
		
		if (fp) {
		  char timebuf[60];
		  printtime(timebuf, sizeof(timebuf));
	  
		  (void)fprintf(fp, "%s %-9s ", timebuf, portname);
		  (void)fprintf(fp, "%c ", direction);

		  if (discard < 0) {
		    fprintf(fp, "*");
		  }
		  if (discard > 0) {
		    fprintf(fp, "#");
		  }
		  (void)fwrite( tnc2buf, tnc2len, 1, fp);
		  (void)fprintf( fp, "\n" );

		  if (fp != stdout)
		    fclose(fp);
		}
#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
#endif
	}
}
