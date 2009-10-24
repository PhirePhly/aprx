/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * **************************************************************** */

/* This code works only with single  aprsis-server  instance! */

#include "aprx.h"
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>


/*
 * $aprsserver = "rotate.aprs.net:14580";
 *
 * re-resolve the $aprsserver at each connection setup!
 *
 * The APRS-IS system connection runs as separate sub-process, once it starts.
 * This way the main-loop is independent from uncertainties of DNS resolving
 * delay times in this part of the code.
 *
 */


struct aprsis_host {
	char *server_name;
	char *server_port;
	char *login;
	char *filterparam;
	int heartbeat_monitor_timeout;
};

struct aprsis {
	int server_socket;
	struct aprsis_host *H;
	time_t next_reconnect;
	time_t last_read;
	int wrbuf_len;
	int wrbuf_cur;
	int rdbuf_len;
	int rdbuf_cur;
	int rdlin_len;

	char wrbuf[16000];
	char rdbuf[3000];
	char rdline[500];
};

static struct aprsis *AprsIS;
static struct aprsis_host **AISh;
static int AIShcount;
static int AIShindex;
static int aprsis_sp;		/* up & down talking socket(pair),
				   parent: write talks down,
				   child: write talks up. */
static dupecheck_t *aprsis_rx_dupecheck;


extern int log_aprsis;

void aprsis_init(void)
{
	aprsis_sp = -1;
}

void enable_aprsis_rx_dupecheck(void) {
	aprsis_rx_dupecheck = dupecheck_new();
}

static void sig_handler(int sig)
{
	die_now = 1;
	signal(sig, sig_handler);
}



/*
 *Close APRS-IS server_socket, clean state..
 */

static void aprsis_close(struct aprsis *A, const char *why)
{
	if (A->server_socket >= 0)
		close(A->server_socket);	/* close, and flush write buffers */

	A->server_socket = -1;

	A->wrbuf_len = A->wrbuf_cur = 0;
	A->next_reconnect = now + 60;
	A->last_read = now;

	if (!A->H)
		return;		/* Not connected, nor defined.. */

	if (verbout)
		printf("%ld\tCLOSE APRSIS %s:%s %s\n", (long) now,
		       A->H->server_name, A->H->server_port,
		       why ? why : "");
	if (aprxlogfile) {
		FILE *fp = fopen(aprxlogfile, "a");
		if (fp) {
			char timebuf[60];
			struct tm *t = gmtime(&now);
			// strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);
			sprintf(timebuf, "%04d-%02d-%02d %02d:%02d:%02d",
				t->tm_year+1900,t->tm_mon+1,t->tm_mday,
				t->tm_hour,t->tm_min,t->tm_sec);

			fprintf(fp, "%s CLOSE APRSIS %s:%s %s\n", timebuf,
				A->H->server_name, A->H->server_port,
				why ? why : "");
			fclose(fp);
		}
	}
}


/*
 *  aprsis_queue_() - internal routine - queue data to specific APRS-IS instance
 */
static int aprsis_queue_(struct aprsis *A, const char *addr,
			 const char *gwcall, const char *text, int textlen)
{
	int i;
	char addrbuf[1000];
	int addrlen, len;

	/* Queue for sending to APRS-IS only when the socket is operational */
	if (A->server_socket < 0)
		return 1;

	/* Here the A->H->login is always set. */

	/*
	 * Append stuff on the writebuf, if it fits.
	 * If it does not fit, something is broken already
	 * and we just drop it..
	 *
	 * Just to make sure that the write pointer is not left
	 * rewound when all has been done...
	 */

	if (A->wrbuf_cur >= A->wrbuf_len && A->wrbuf_len > 0)
		A->wrbuf_cur = A->wrbuf_len = 0;

	addrlen = 0;
	if (addr)
		addrlen = sprintf(addrbuf, "%s,qAR,%s:", addr,
				  (gwcall
				   && *gwcall) ? gwcall : A->H->login);
	len = addrlen + textlen;


	/* Does it fit in ? */

	if ((sizeof(A->wrbuf) - 10) <= (A->wrbuf_len + len)) {
		/* The string does not fit in, perhaps it needs compacting ? */
		if (A->wrbuf_cur > 0) {	/* Compacting is possible ! */
			memcpy(A->wrbuf, A->wrbuf + A->wrbuf_cur,
			       A->wrbuf_len - A->wrbuf_cur);
			A->wrbuf_len -= A->wrbuf_cur;
			A->wrbuf_cur = 0;
		}

		/* Check again if it fits in.. */
		if ((sizeof(A->wrbuf) - 10) <= (A->wrbuf_len + len)) {
			/* NOT!  Too bad, drop it.. */
			return 2;
		}
	}


	/* Place it on our send buffer */

	if (addrlen > 0) {
		memcpy(A->wrbuf + A->wrbuf_len, addrbuf, addrlen);
		A->wrbuf_len += addrlen;
	}

	memcpy(A->wrbuf + A->wrbuf_len, text, textlen);
	A->wrbuf_len += textlen;	/* Always supplied with tail newline.. */

	/* -- debug --
	  fwrite(A->wrbuf,A->wrbuf_len,1,stdout);
	  return 0;
	*/

	/* Try writing it right away: */

	i = write(A->server_socket, A->wrbuf + A->wrbuf_cur,
		  A->wrbuf_len - A->wrbuf_cur);
	if (i > 0) {
		if (debug > 1) {
			printf("%ld\t<< %s:%s << ", now, A->H->server_name,
			       A->H->server_port);
			fwrite(A->wrbuf + A->wrbuf_cur,
			       (A->wrbuf_len - A->wrbuf_cur),
			       1, stdout);	/* Does end on  \r\n */
		}
		if (aprxlogfile && log_aprsis) {
			FILE *fp = fopen(aprxlogfile, "a");
			if (fp) {
				fprintf(fp, "%ld\t<< %s:%s << ",
					now, A->H->server_name,
					A->H->server_port);
				fwrite(A->wrbuf + A->wrbuf_cur,
				       (A->wrbuf_len - A->wrbuf_cur),
				       1, fp);	/* Does end on  \r\n */
				fclose(fp);
			}
		}

		A->wrbuf_cur += i;
		if (A->wrbuf_cur >= A->wrbuf_len) {	/* Wrote all ! */
			A->wrbuf_cur = A->wrbuf_len = 0;
		}
	}

	return 0;
}


/*
 * APRSpass requires that input callsign is in uppercase ASCII
 * characters (A-Z), or decimal digits.  Characters outside those
 * will terminate the calculation.
 */

static int aprspass(const char *login)
{
	int a = 0, h = 29666, c;

	for (; *login; ++login) {
		c = 0xFF & *login;
		if ('a' <= c && c <= 'z')
			c = c - ('a' - 'A');
		if (!(('0' <= c && c <= '9') || ('A' <= c && c <= 'Z')))
			break;
		h ^= ((0xFF & *login) * (a ? 1 : 256));
		a = !a;
	}
	return h;
}


/*
 *  THIS CONNECT ROUTINE WILL BLOCK THE WHOLE PROGRAM
 *
 *  On the other hand, it is no big deal in case of this
 *  programs principal reason for existence...
 */

static void aprsis_reconnect(struct aprsis *A)
{
	struct addrinfo req, *ai, *a, *ap[21];
	int i, n;
	char *s;
	char aprsislogincmd[3000];
	const char *errstr;

	aprsis_close(A, "reconnect");

	if (!A->H) {
		A->H = AISh[0];
	} else {
		++AIShindex;
		if (AIShindex >= AIShcount)
			AIShindex = 0;
		A->H = AISh[AIShindex];
	}

	if (!A->H->login) {
		if (verbout)
			printf("%ld\tFAIL - APRSIS-LOGIN not defined, no APRSIS connection!\n", (long) now);
		if (aprxlogfile) {
			FILE *fp = fopen(aprxlogfile, "a");
			if (fp) {
				char timebuf[60];
				struct tm *t = gmtime(&now);
				// strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S",t);
				sprintf(timebuf, "%04d-%02d-%02d %02d:%02d:%02d",
					t->tm_year+1900,t->tm_mon+1,t->tm_mday,
					t->tm_hour,t->tm_min,t->tm_sec);


				fprintf(fp,
					"%s FAIL - APRSIS-LOGIN not defined, no APRSIS connection!\n",
					timebuf);
				fclose(fp);
			}
		}

		return;		/* Will try to reconnect in about 60 seconds.. */
	}

	memset(&req, 0, sizeof(req));
	req.ai_socktype = SOCK_STREAM;
	req.ai_protocol = IPPROTO_TCP;
	req.ai_flags = 0;
#if 1
	req.ai_family = AF_UNSPEC;	/* IPv4 and IPv6 are both OK */
#else
	req.ai_family = AF_INET;	/* IPv4 only */
#endif
	ai = NULL;


	i = getaddrinfo(A->H->server_name, A->H->server_port, &req, &ai);
	errstr = "address resolution failure";

	if (i != 0) {

	      fail_out:;
		/* Discard stuff and redo latter.. */

		if (ai)
			freeaddrinfo(ai);

		aprsis_close(A, "fail on connect");

		if (verbout)
			printf("%ld\tFAIL - Connect to %s:%s failed: %s\n",
			       (long) now, A->H->server_name,
			       A->H->server_port, errstr);
		if (aprxlogfile) {
			FILE *fp = fopen(aprxlogfile, "a");
			if (fp) {
				char timebuf[60];
				struct tm *t = gmtime(&now);
				// strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S",t);
				sprintf(timebuf, "%04d-%02d-%02d %02d:%02d:%02d",
					t->tm_year+1900,t->tm_mon+1,t->tm_mday,
					t->tm_hour,t->tm_min,t->tm_sec);

				fprintf(fp,
					"%s FAIL - Connect to %s:%s failed: %s\n",
					timebuf, A->H->server_name,
					A->H->server_port, errstr);
				fclose(fp);
			}
		}
		return;
	}

	/* Count the addresses */
	for (n = 0, a = ai; a; a = a->ai_next, ++n) {
		if (n < 20)
			ap[n] = a;
		else
			break;
	}
	ap[n] = NULL;

	if (n > 1) {		/* more than one ?  choose one at random as the first address,
				   then go through the address list in new sequence. */
		n = rand() % n;
		if (n > 0) {
			a = ap[n];
			ap[n] = ap[0];
			ap[0] = a;
		}
	}

	for (n = 0; (a = ap[n]) && A->server_socket < 0; ++n) {

		A->server_socket =
			socket(a->ai_family, a->ai_socktype,
			       a->ai_protocol);

		errstr = "socket formation failed";
		if (A->server_socket < 0)
			continue;

		errstr = "connection failed";
		i = connect(A->server_socket, a->ai_addr, a->ai_addrlen);

		if (i < 0) {
			/* If connection fails, try next possible address */
			close(A->server_socket);
			A->server_socket = -1;
			continue;
		}
	}

	if (A->server_socket < 0)
		goto fail_out;

	freeaddrinfo(ai);
	ai = NULL;


	now = time(NULL);	/* unpredictable time since system did last poll.. */
	if (verbout)
		printf("%ld\tCONNECT APRSIS %s:%s\n", (long) now,
		       A->H->server_name, A->H->server_port);
	if (aprxlogfile) {
		FILE *fp = fopen(aprxlogfile, "a");
		if (fp) {
			char timebuf[60];
			struct tm *t = gmtime(&now);
			// strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);
			sprintf(timebuf, "%04d-%02d-%02d %02d:%02d:%02d",
				t->tm_year+1900,t->tm_mon+1,t->tm_mday,
				t->tm_hour,t->tm_min,t->tm_sec);

			fprintf(fp, "%s CONNECT APRSIS %s:%s\n", timebuf,
				A->H->server_name, A->H->server_port);
			fclose(fp);
		}
	}


	/* From now the socket will be non-blocking for its entire lifetime.. */
	fd_nonblockingmode(A->server_socket);

	/* We do at first sync writing of login, and such.. */
	s = aprsislogincmd;
	s += sprintf(s, "user %s pass %d vers %s %s", A->H->login,
		     aprspass(A->H->login), swname, swversion);
	if (A->H->filterparam)
		s += sprintf(s, " filter %s", A->H->filterparam);
	strcpy(s, "\r\n");

	A->last_read = now;

	aprsis_queue_(A, NULL, "", aprsislogincmd, strlen(aprsislogincmd));

	return;			/* just a place-holder */
}


static int aprsis_sockreadline(struct aprsis *A)
{
	int i, c;

	/* Reads multiple lines from buffer,
	   Last one is left into incomplete state */

	for (i = A->rdbuf_cur; i < A->rdbuf_len; ++i) {
	    c = 0xFF & (A->rdbuf[i]);
	    if (c == '\r' || c == '\n') {
		/* End of line, process.. */
		if (A->rdlin_len > 0) {
		    A->rdline[A->rdlin_len] = 0;
		    /* */
		    A->last_read = now;	/* Time stamp me ! */

		    if (debug > 1) {
		      printf("%ld\t<< %s:%s >> %s\n",
			     now, A->H->server_name,
			     A->H->server_port,
			     A->rdline);
		    } else {
			if (aprxlogfile && log_aprsis) {
			    FILE *fp = fopen(aprxlogfile, "a");
			    if (fp) {
				fprintf(fp, "%ld\t<< %s:%s >> ",
					now, A->H->server_name,
					A->H->server_port);
				fwrite(A->rdline, A->rdlin_len, 1, fp);
				fprintf(fp, "\n");
				fclose(fp);
			    }
			}
		    }

		    /* Send the A->rdline content to main program */
		    c = send(aprsis_sp, A->rdline,
			     strlen(A->rdline), 0);
		    /* This may fail with SIGPIPE.. */
		    if (c < 0 && (errno == EPIPE ||
				  errno == ECONNRESET ||
				  errno == ECONNREFUSED ||
				  errno == ENOTCONN)) {
		      /* death-sentence to us.. */
		      exit(1);
		    }
		}
		A->rdlin_len = 0;
		continue;
	    }
	    if (A->rdlin_len < sizeof(A->rdline) - 2) {
	      A->rdline[A->rdlin_len++] = c;
	    }
	}
	A->rdbuf_cur = 0;
	A->rdbuf_len = 0;	/* we ignore line reading */
	return 0;		/* .. this is placeholder.. */
}

static int aprsis_sockread(struct aprsis *A)
{
	int i;

	int rdspace = sizeof(A->rdbuf) - A->rdbuf_len;

	if (A->rdbuf_cur > 0) {
		/* Read-out cursor is not at block beginning,
		   is there unread data too ? */
		if (A->rdbuf_cur > A->rdbuf_len) {
			memcpy(A->rdbuf, A->rdbuf + A->rdbuf_cur,
			       A->rdbuf_len - A->rdbuf_cur);
			A->rdbuf_len -= A->rdbuf_cur;
		} else
			A->rdbuf_len = 0;	/* all processed, mark its size zero */
		A->rdbuf_cur = 0;

		/* recalculate */
		rdspace = sizeof(A->rdbuf) - A->rdbuf_len;
	}

	i = read(A->server_socket, A->rdbuf + A->rdbuf_len, rdspace);

	if (i > 0) {

		A->rdbuf_len += i;

		/* we just ignore the readback.. but do time-stamp the event */
		A->last_read = now;

		aprsis_sockreadline(A);
	}

	return i;
}

struct aprsis_tx_msg_head {
	time_t then;
	int addrlen;
	int gwlen;
	int textlen;
};

/*
 * Read frame from a socket in between main-program and
 * APRS-IS interface subprogram.  (At APRS-IS side.)
 * 
 */
static void aprsis_readsp(void)
{
	int i;
	char buf[10000];
	const char *addr;
	const char *gwcall;
	const char *text;
	int textlen;
	struct aprsis_tx_msg_head head;

	i = recv(aprsis_sp, buf, sizeof(buf), 0);
	if (i == 0) {		/* EOF ! */
		exit(0);
	}
	if (i < 0) {
		return;		/* Whatever was the reason.. */
	}
	buf[i] = 0;		/* String Termination NUL byte */

	memcpy(&head, buf, sizeof(head));
	if (head.then + 10 < now)
		return;		/* Too old, discard */
	addr = buf + sizeof(head);

	gwcall = addr + head.addrlen + 1;

	text = gwcall + head.gwlen + 1;

	textlen = head.textlen;
	if (textlen <= 2)
		return;		/* BAD! */

	/*
	  printf("addrlen=%d addr=%s\n",head.addrlen, addr);
	  printf("gwlen=%d  gwcall=%s\n",head.gwlen,gwcall);
	  printf("textlen=%d text=%s",head.textlen, text);
	  return;
	*/

	/* Now queue the thing! */

	if (AprsIS != NULL)
		aprsis_queue_(AprsIS, addr, gwcall, text, textlen);
}

int aprsis_queue(const char *addr, int addrlen, const char *gwcall, const char *text,
		 int textlen)
{
	static char *buf;	/* Dynamically allocated buffer... */
	static int buflen;
	int i, len, gwlen = strlen(gwcall);
	char *p;
	struct aprsis_tx_msg_head head;
	int newlen;
	dupe_record_t *dp;

	if (addrlen == 0)      /* should never be... */
		addrlen = strlen(addr);

	if (aprsis_rx_dupecheck != NULL) {
	  dp = dupecheck_aprs( aprsis_rx_dupecheck, 
			       addr, addrlen,
			       text, textlen );
	  if (dp != NULL) return 1; // Bad either as dupe, or due to alloc failure
	}

	newlen = sizeof(head) + addrlen + gwlen + textlen + 6;
	if (newlen > buflen) {
		buflen = newlen;
		buf = realloc(buf, buflen);
		memset(buf, 0, buflen); // (re)init it to silence valgrind
	}

	head.then    = now;
	head.addrlen = addrlen;
	head.gwlen   = gwlen;
	head.textlen = textlen + 2;	/* We add line terminating \r\n  pair. */

	memcpy(buf, &head, sizeof(head));
	p = buf + sizeof(head);

	memcpy(p, addr, addrlen);
	p += addrlen;
	*p++ = 0;		/* string terminating 0 byte */
	memcpy(p, gwcall, gwlen);
	p += gwlen;
	*p++ = 0;		/* string terminating 0 byte */
	memcpy(p, text, textlen);
	p += textlen;
	memcpy(p, "\r\n", 2);
	p += 2;
	len = p - buf;

#ifndef MSG_NOSIGNAL
# define MSG_NOSIGNAL 0 /* This exists only on Linux  */
#endif
	i = send(aprsis_sp, buf, len, MSG_NOSIGNAL);	/* No SIGPIPE if the
							   receiver is out,
							   or pipe is full
							   because it is doing
							   slow reconnection. */

	return (i != len);
	/* Return 0 if ANY of the queue operations was successfull
	   Return 1 if there was some error.. */
}


static int aprsis_prepoll_(struct aprxpolls *app)
{
	struct pollfd *pfd;
	struct aprsis *A = AprsIS;

	if (A->last_read == 0)
		A->last_read = now;	/* mark it non-zero.. */

	if (A->server_socket < 0)
		return -1;	/* Not open, do nothing */

	/* Not all aprs-is systems send "heartbeat", but when they do.. */
	if ((A->H->heartbeat_monitor_timeout > 0) &&
	    (A->last_read + A->H->heartbeat_monitor_timeout < now)) {

		/*
		 * More than 120 seconds (2 minutes) since last time
		 * that APRS-IS systems told us something on the connection.
		 * There is a heart-beat ticking every 20 or so seconds.
		 */

		aprsis_close(A, "heartbeat timeout");
	}

	/* FD is open, lets mark it for poll read.. */

	pfd = aprxpolls_new(app);

	pfd->fd = A->server_socket;
	pfd->events = POLLIN | POLLPRI | POLLERR | POLLHUP;
	pfd->revents = 0;

	/* Do we have something for writing ?  */
	if (A->wrbuf_len) {
		pfd->events |= POLLOUT;
	}

	return 0;
}

static int aprsis_postpoll_(struct aprxpolls *app)
{
	int i;
	struct pollfd *pfd = app->polls;
	struct aprsis *A = AprsIS;

	for (i = 0; i < app->pollcount; ++i, ++pfd) {
		if (pfd->fd == A->server_socket && pfd->fd >= 0) {
			/* This is APRS-IS socket, and we may have some results.. */

			if (pfd->revents & (POLLERR)) {	/* Errors ? */
				aprsis_close(A,"postpoll_ POLLERR");
				continue;
			}
			if (pfd->revents & (POLLHUP)) {	/* Errors ? */
				aprsis_close(A,"postpoll_ POLLHUP");
				continue;
			}

			if (pfd->revents & POLLIN) {	/* Ready for reading */
				for (;;) {
					i = aprsis_sockread(A);
					if (i == 0) {	/* EOF ! */
						aprsis_close(A,"postpoll_ EOF");
						continue;
					}
					if (i < 0)
						break;
				}
			}

			if (pfd->revents & POLLOUT) {	/* Ready for writing  */
				/* Normal queue write processing */

				if (A->wrbuf_len > 0 &&
				    A->wrbuf_cur < A->wrbuf_len) {
					i = write(A->server_socket,
						  A->wrbuf +
						  A->wrbuf_cur,
						  A->wrbuf_len -
						  A->wrbuf_cur);
					if (i < 0)
						continue;	/* Argh.. nothing */
					if (i == 0);	/* What ? */
					A->wrbuf_cur += i;
					if (A->wrbuf_cur >= A->wrbuf_len) {	/* Wrote all! */
						A->wrbuf_len = A->wrbuf_cur = 0;
					} else {
						/* partial write .. do nothing.. */
					}
				}
				/* .. normal queue */
			}	/* .. POLLOUT */
		}	/* .. if fd == server_socket */
	}			/* .. for .. nfds .. */
	return 1;		/* there was something we did, maybe.. */
}


static void aprsis_cond_reconnect(void)
{
	if (AprsIS &&	/* First time around it may trip.. */
	    AprsIS->server_socket < 0 && AprsIS->next_reconnect <= now) {
		aprsis_reconnect(AprsIS);
	}
}


/*
 * Main-loop of subprogram handling communication with
 * APRS-IS network servers.
 *
 * This starts only when we have at least one <aprsis> defined without errors.
 */
static void aprsis_main(void)
{
	int i;
	int ppid = getppid();
	struct aprxpolls app = { NULL, 0, 0, 0 };

	signal(SIGHUP, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	/* The main loop */
	while (!die_now) {
		struct pollfd *pfd;
		now = time(NULL);

		aprsis_cond_reconnect();

		now = time(NULL);	/* may take unpredictable time.. */

		i = getppid();
		if (i != ppid)
			break;	/* die now, my parent is gone.. */
		if (i == 1)
			break;	/* a safety fallback case.. */

		aprxpolls_reset(&app);
		app.next_timeout = now + 5;

		pfd = aprxpolls_new(&app);

		pfd->fd = aprsis_sp;
		pfd->events = POLLIN | POLLPRI | POLLERR | POLLHUP;
		pfd->revents = 0;

		i = aprsis_prepoll_(&app);

		if (app.next_timeout <= now)
			app.next_timeout = now + 1;	/* Just to be on safe side.. */

		i = poll(app.polls, app.pollcount,
			 (app.next_timeout - now) * 1000);
		now = time(NULL);

		if (app.polls[0].
		    revents & (POLLIN | POLLPRI | POLLERR | POLLHUP)) {
			/* messaging channel has something for us, if
			   the channel reports EOF, we exit there and then. */
			aprsis_readsp();
		}
		i = aprsis_postpoll_(&app);
	}
	/* Got "DIE NOW" signal... */
	exit(0);
}


/*
 *  aprsis_add_server() - old style configuration
 */

void aprsis_add_server(const char *server, const char *port)
{
	struct aprsis_host *H;

	if (AprsIS == NULL) {
		AprsIS = malloc(sizeof(*AprsIS));
	}

	H = malloc(sizeof(*H));
	AISh = realloc(AISh, sizeof(AISh[0]) * (AIShcount + 1));
	AISh[AIShcount] = H;

	++AIShcount;
	/* No inc on AprsIScount */


	H->server_name = strdup(server);
	H->server_port = strdup(port);
	H->heartbeat_monitor_timeout = 120; // Default timeout 120 seconds
	H->login       = strdup(aprsis_login);	// global aprsis_login
	if (H->login == NULL) H->login = strdup(mycall);

	AprsIS->server_socket = -1;
	AprsIS->next_reconnect = now;	/* perhaps somewhen latter.. */
}

// old style configuration
void aprsis_set_heartbeat_timeout(const int tout)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0)
		--i;
	H = AISh[i];

	H->heartbeat_monitor_timeout = tout;
}

// old style configuration
void aprsis_set_filter(const char *filter)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0)
		--i;
	H = AISh[i];

	H->filterparam = strdup(filter);
}

// old style configuration
void aprsis_set_login(const char *login)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0)
		--i;
	H = AISh[i];

	H->login = strdup(login);
}

void aprsis_start(void)
{
	int i;
	int pipes[2];

	if (AISh == NULL || AprsIS == NULL) {
	  fprintf(stderr,"***** NO APRSIS SERVER CONNECTION DEFINED *****");
	  return;
	}


	i = socketpair(AF_UNIX, SOCK_DGRAM, PF_UNSPEC, pipes);
	if (i != 0) {
		return;		/* FAIL ! */
	}

	i = fork();
	if (i < 0) {
		close(pipes[0]);
		close(pipes[1]);
		return;		/* FAIL ! */
	}

	if (i == 0) {
		/* Child -- the APRSIS talker */
		aprsis_sp = pipes[1];
		fd_nonblockingmode(pipes[1]);
		close(pipes[0]);
		aprsis_main();
	}

	/* Parent */
	close(pipes[1]);
	fd_nonblockingmode(pipes[0]);
	aprsis_sp = pipes[0];
}



/*
 * main-program side pre-poll
 *
 */
int aprsis_prepoll(struct aprxpolls *app)
{
	int idx = 0;		/* returns number of *fds filled.. */

	struct pollfd *pfd;

	pfd = aprxpolls_new(app);

	pfd->fd = aprsis_sp;	/* APRS-IS communicator server Sub-process */
	pfd->events = POLLIN | POLLPRI;
	pfd->revents = 0;

	/* We react only for reading, if write fails because the socket is
	   jammed,  that is just too bad... */

	++idx;

	return idx;

}

/*
 * main-program side reading of aprsis_sp
 *
 */
static int aprsis_comssockread(int fd)
{
	int i;
	char buf[10000];

	i = recv(fd, buf, sizeof(buf), 0);
	if (i == 0)
		return 0;

	/* TODO: do something with the data ?
	   A receive-only iGate does nothing, but Rx/Tx would do... */

	/* Send the frame to Tx-IGate function */
	if (i > 0)
		igate_from_aprsis(buf, i);

	return 1;
}


/*
 * main-program side post-poll
 *
 */
int aprsis_postpoll(struct aprxpolls *app)
{
	int i;
	struct pollfd *pfd = app->polls;

	for (i = 0; i < app->pollcount; ++i, ++pfd) {
		if (pfd->fd == aprsis_sp) {
			/* This is APRS-IS communicator subprocess socket,
			   and we may have some results.. */

			if (pfd->revents & (POLLERR | POLLHUP)) {	/* Errors ? */
				printf("APRS-IS coms subprocess socket failure from main program side!\n");
				continue;
			}

			if (pfd->revents & POLLIN) {	/* Ready for reading */
				i = aprsis_comssockread(pfd->fd);
				if (i == 0) {	/* EOF ! */
					printf("APRS-IS coms subprocess socket EOF from main program side!\n");

					continue;
				}
				if (i < 0)
					continue;
			}
		}
	}			/* .. for .. nfds .. */
	return 1;		/* there was something we did, maybe.. */
}


void aprsis_config(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int has_fault = 0;
	int line0 = cf->linenum;

	struct aprsis_host *AIH = malloc(sizeof(*AIH));
	memset(AIH, 0, sizeof(*AIH));
	AIH->login                     = strdup(mycall);
	AIH->heartbeat_monitor_timeout = 120;

	while (readconfigline(cf) != NULL) {
		if (configline_is_comment(cf))
			continue;	/* Comment line, or empty line */

		// It can be severely indented...
		str = config_SKIPSPACE(cf->buf);

		name = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);
		config_STRLOWER(name);

		param1 = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		if (strcmp(name, "</aprsis>") == 0) {
		  // End of this interface definition block
		  break;
		}

		// APRSIS parameters

		// login
		// server
		// filter
		// heartbeat-timeout

		if (strcmp(name, "login") == 0) {
		  config_STRUPPER(param1);
		  if (!validate_callsign_input(param1,0)) {
		    // bad input...
		  }
		  if (debug)
		    printf("%s:%d: LOGIN = '%s' '%s'\n",
			   cf->name, cf->linenum, param1, str);
		  AIH->login = strdup(param1);

		} else if (strcmp(name, "server") == 0) {

		  if (AIH->server_name != NULL) {
		  }
		  AIH->server_name = strdup(param1);

		  param1 = str;
		  str = config_SKIPTEXT(str, NULL);
		  str = config_SKIPSPACE(str);
		  if ('1' <= *param1 && *param1 <= '9') {
		    // fixme: more input analysis?
		    AIH->server_port = strdup(param1);
		  } else if (*param1 == 0) {
		    // Default silently!
		    AIH->server_port = strdup("14580");
		  } else {
		    AIH->server_port = strdup("14580");
		    printf("%s:%d SERVER = '%s' port='%s' is not supplying valid TCP port number, defaulting to '14580'\n",
			   cf->name, cf->linenum, AIH->server_name, param1);
		  }

		  if (debug)
		    printf("%s:%d: SERVER = '%s':'%s'\n",
			   cf->name, cf->linenum, AIH->server_name, AIH->server_port);

		} else if (strcmp(name, "heartbeat-timeout") == 0) {
		  int i = 0;
		  if (config_parse_interval(param1, &i)) {
		    // FIXME: Report parameter failure ...
		    printf("%s:%d: HEARTBEAT-TIMEOUT = '%s'  - bad parameter'\n",
			   cf->name, cf->linenum, param1);
		    has_fault = 1;
		  }
		  if (i < 0) {	/* param failure ? */
		    i = 0;	/* no timeout */
		    printf("%s:%d: HEARTBEAT-TIMEOUT = '%s'  - bad parameter'\n",
			   cf->name, cf->linenum, param1);
		    has_fault = 1;
		  }
		  AIH->heartbeat_monitor_timeout = i;
		  
		  if (debug)
		    printf("%s:%d: HEARTBEAT-TIMEOUT = '%d' '%s'\n",
			   cf->name, cf->linenum, i, str);

		} else if (strcmp(name, "filter") == 0) {
		  int l1 = (AIH->filterparam != NULL) ? strlen(AIH->filterparam) : 0;
		  int l2 = strlen(param1);

		  AIH->filterparam = realloc( AIH->filterparam, l1 + l2 +2 );

		  if (l1 > 0) {
		    AIH->filterparam[l1] = ' ';
		    memcpy(&(AIH->filterparam[l1+1]), param1, l2+1);
		  } else {
		    memcpy(&(AIH->filterparam[0]), param1, l2+1);
		  }

		  if (debug)
		    printf("%s:%d: FILTER = '%s' -->  '%s'\n",
			   cf->name, cf->linenum, param1, AIH->filterparam);

		} else  {
		  printf("%s:%d: Unknown configuration keyword: '%s'\n",
			 cf->name, cf->linenum, param1);
		}
	}
	if (AIH->server_name == NULL) {
	  printf("%s:%d This <aprsis> block does not define server!\n",
		 cf->name, line0);
	  has_fault = 1;
	}
	if (has_fault) {
		if (AIH->server_name != NULL) free(AIH->server_name);
		if (AIH->server_port != NULL) free(AIH->server_port);
		if (AIH->filterparam != NULL) free(AIH->filterparam);
		if (AIH->login       != NULL) free(AIH->login);
		free(AIH);
		return;

	} else {
		if (AprsIS == NULL) {
			AprsIS = malloc(sizeof(*AprsIS));
			memset(AprsIS, 0, sizeof(*AprsIS));
			AprsIS->server_socket = -1;
			AprsIS->next_reconnect = now;	/* perhaps somewhen latter.. */
		}

		AISh = realloc(AISh, sizeof(AISh[0]) * (AIShcount + 1));
		AISh[AIShcount] = AIH;
	}
}
