/* **************************************************************** *
 *								    *
 *  APRX -- 2nd generation receive-only APRS-i-gate with	    *
 *	    minimal requirement of esoteric facilities or	    *
 *	    libraries of any kind beyond UNIX system libc.	    *
 *								    *
 * (c) Matti Aarnio - OH2MQK,  2007-2014			    *
 *								    *
 * **************************************************************** */

/* This code works only with single  aprsis-server  instance! */

#include "aprx.h"

#ifndef DISABLE_IGATE

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>

#ifdef HAVE_NETINET_SCTP_H
#include <netinet/sctp.h>
#endif

#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
#include <pthread.h>
pthread_t aprsis_thread;
pthread_attr_t pthr_attrs;
#endif

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

enum aprsis_mode {
	MODE_TCP,
	MODE_SSL,
	MODE_SCTP,
	MODE_DTLS
};

static char default_passcode[] = "-1";

struct aprsis_host {
	char *server_name;
	char *server_port;
	char *login;
	char *pass;
	char *filterparam;
	int heartbeat_monitor_timeout;
	enum aprsis_mode mode;
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

char * const aprsis_loginid;
static struct aprsis *AprsIS;
static struct aprsis_host **AISh;
static int AIShcount;
static int AIShindex;
static int aprsis_up = -1;	/* up & down talking socket(pair),
				   The aprsis talker (thread/child)
				   uses this socket. */
static int aprsis_down = -1;	/* down talking socket(pair),
				   The aprx main loop uses this socket */
//static dupecheck_t *aprsis_rx_dupecheck;

//int  aprsis_dupecheck_storetime = 30;


extern int log_aprsis;
extern int die_now;

void aprsis_init(void)
{
	aprsis_up   = -1;
	aprsis_down = -1;
}

//void enable_aprsis_rx_dupecheck(void) {
//	aprsis_rx_dupecheck = dupecheck_new(aprsis_dupecheck_storetime);
//}
#if !(defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD))
static void sig_handler(int sig)
{
	die_now = 1;
	signal(sig, sig_handler);
}
#endif

/*
 *Close APRS-IS server_socket, clean state..
 */
// APRS-IS communicator
static void aprsis_close(struct aprsis *A, const char *why)
{
	if (A->server_socket >= 0)
		close(A->server_socket);	/* close, and flush write buffers */

	A->server_socket = -1;

	A->wrbuf_len = A->wrbuf_cur = 0;
	A->next_reconnect = tick.tv_sec + 10;
	A->last_read = tick.tv_sec;

	if (!A->H)
		return;		/* Not connected, nor defined.. */

	aprxlog("CLOSE APRSIS %s:%s %s", 
		A->H->server_name, A->H->server_port,
		why ? why : "");
}


/*
 *  aprsis_queue_() - internal routine - queue data to specific APRS-IS instance
 */
// APRS-IS communicator
static int aprsis_queue_(struct aprsis *A, const char * const addr, const char qtype,
			 const char *gwcall, const char * const text, int textlen)
{
	int i;
	char addrbuf[1000];
	int addrlen, len;
	char * p;

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
		addrlen = sprintf(addrbuf, "%s,qA%c,%s:", addr, qtype,
				  (gwcall
				   && *gwcall) ? gwcall : A->H->login);
	aprsis_login = A->H->login;

	len = addrlen + textlen;


	/* Does it fit in ? */

	if ((sizeof(A->wrbuf) - 10) <= (A->wrbuf_len + len)) {
		/* The string does not fit in, perhaps it needs compacting ? */
		if (A->wrbuf_cur > 0) { /* Compacting is possible ! */
			memcpy(A->wrbuf, A->wrbuf + A->wrbuf_cur,
			       A->wrbuf_len - A->wrbuf_cur);
			A->wrbuf_len -= A->wrbuf_cur;
			A->wrbuf_cur = 0;
		}

		/* Check again if it fits in.. */
		if ((sizeof(A->wrbuf) - 10) <= (A->wrbuf_len + len)) {
			/* NOT!	 Too bad, drop it.. */
			return 2;
		}
	}


	/* Place it on our send buffer */

	if (addrlen > 0) {
		memcpy(A->wrbuf + A->wrbuf_len, addrbuf, addrlen);
		A->wrbuf_len += addrlen;
	}

	/* If there is CR or LF within the packet, terminate packet at it.. */
	p = memchr(text, '\r', textlen);
	if (p != NULL) {
	  textlen = p - text;
	}
	p = memchr(text, '\n', textlen);
	if (p != NULL) {
	  textlen = p - text;
	}

	/* Append CR+LF at the end of the packet */
	p = (char*)(text + textlen);
	*p++ = '\r';
	*p++ = '\n';
	textlen += 2;

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
		// the buffer's last character is \n, don't write it
		if (log_aprsis)
		  aprxlog(A->wrbuf + A->wrbuf_cur, (A->wrbuf_len - A->wrbuf_cur) -1,
			  "<< %s:%s << ", A->H->server_name, A->H->server_port);

		A->wrbuf_cur += i;
		if (A->wrbuf_cur >= A->wrbuf_len) {	/* Wrote all ! */
			A->wrbuf_cur = A->wrbuf_len = 0;
		}
	}

	return 0;
}


/*
 *  THIS CONNECT ROUTINE WILL BLOCK  (At DNS resolving)
 *  
 *  This is why APRSIS communication is run at either
 *  a fork()ed child, or separate pthread from main loop.
 */

// APRS-IS communicator
static void aprsis_reconnect(struct aprsis *A)
{
	struct addrinfo req, *ai, *a, *ap[21];
	int i, n;
	char *s;
	char aprsislogincmd[3000];
	const char *errstr;
	int errcode;

	memset(aprsislogincmd, 0, sizeof(aprsislogincmd)); // please valgrind

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
		if (log_aprsis)
		  aprxlog("FAIL - APRSIS-LOGIN not defined, no APRSIS connection!");

		return;		/* Will try to reconnect in about 60 seconds.. */
	}
	aprsis_login = A->H->login;

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
	errcode = errno;

	if (i != 0) {

	      fail_out:;
		/* Discard stuff and redo latter.. */

		if (ai)
			freeaddrinfo(ai);

		aprsis_close(A, "fail on connect");

		aprxlog("FAIL - Connect to %s:%s failed: %s - errno=%d - %s",
			A->H->server_name, A->H->server_port, errstr, errno, strerror(errcode));
		return;
	}

	/* Count the addresses */
	memset(ap, 0, sizeof(ap));
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

		errstr = "socket formation failed";

		A->server_socket =
			socket(a->ai_family, a->ai_socktype,
			       a->ai_protocol);
		errcode = errno;

		if (A->server_socket < 0)
			continue;

		errstr = "connection failed";
		i = connect(A->server_socket, a->ai_addr, a->ai_addrlen);
		errcode = errno;

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


	timetick(); // unpredictable time since system did last poll..

        if (time_reset) {
          if (debug) printf("In time_reset mode, no touching yet!\n");
          A->next_reconnect = tick.tv_sec + 10;
          return;
        }

	aprxlog("CONNECT APRSIS %s:%s",
		A->H->server_name, A->H->server_port);

	/* From now the socket will be non-blocking for its entire lifetime.. */
	fd_nonblockingmode(A->server_socket);

	/* We do at first sync writing of login, and such.. */
	s = aprsislogincmd;
	s += sprintf(s, "user %s pass %s vers %s %s", A->H->login,
		    A->H->pass, swname, swversion);
	if (A->H->filterparam)
		s += sprintf(s, " filter %s", A->H->filterparam);

	A->last_read = tick.tv_sec;

	aprsis_queue_(A, NULL, qTYPE_LOCALGEN, "", aprsislogincmd, strlen(aprsislogincmd));

	return;			/* just a place-holder */
}


// APRS-IS communicator
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
		    A->last_read = tick.tv_sec; /* Time stamp me ! */

		    if (log_aprsis)
		      aprxlog(A->rdline, A->rdlin_len,
			      ">> %s:%s >> ", A->H->server_name, A->H->server_port);

		    /* Send the A->rdline content to main program */
		    c = send(aprsis_up, A->rdline, A->rdlin_len, 0);
		    /* This may fail with SIGPIPE.. */
		    if (c < 0 && (errno == EPIPE ||
				  errno == ECONNRESET ||
				  errno == ECONNREFUSED ||
				  errno == ENOTCONN)) {
		      die_now = 1; // upstream socket send failed
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

// APRS-IS communicator
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
		A->last_read = tick.tv_sec;

		aprsis_sockreadline(A);
	}

	return i;
}

struct aprsis_tx_msg_head {
	time_t then;
	int addrlen;
	int gwlen;
	int textlen;
	char qtype;
};

/*
 * Read frame from a socket in between main-program and
 * APRS-IS interface subprogram.  (At APRS-IS side.)
 * 
 */
// APRS-IS communicator
static void aprsis_readup(void)
{
	int i;
	char buf[10000];
	const char *addr;
	const char *gwcall;
	const char *text;
	int textlen;
	struct aprsis_tx_msg_head head;

	i = recv(aprsis_up, buf, sizeof(buf), 0);
	if (i == 0) {		/* EOF ! */
	  if (debug>1) printf("Upstream fd read resulted eof status.\n");
	  die_now = 1;
	  return;
	}
	if (i < 0) {
		return;		/* Whatever was the reason.. */
	}
	buf[i] = 0;		/* String Termination NUL byte */

	memcpy(&head, buf, sizeof(head));
	addr = buf + sizeof(head);
	gwcall = addr + head.addrlen + 1;
	text = gwcall + head.gwlen + 1;
	textlen = head.textlen;

	if (head.then + 10 < tick.tv_sec) {
          return;		/* Too old, discard */
          // rflog();
        }
	if (textlen <= 2) {
	  return;		// BAD!
        }
	if ((text + textlen) > (buf + i)) {
	  return;		// BAD!
	}

	/*
	  printf("addrlen=%d addr=%s\n",head.addrlen, addr);
	  printf("gwlen=%d  gwcall=%s\n",head.gwlen,gwcall);
	  printf("textlen=%d text=%s",head.textlen, text);
	  return;
	*/

	/* Now queue the thing! */

	if (AprsIS != NULL)
		aprsis_queue_(AprsIS, addr, head.qtype, gwcall, text, textlen);
}


// main program side
int aprsis_queue(const char *addr, int addrlen, const char qtype, const char *gwcall, const char *text,	 int textlen)
{
	static char *buf;	/* Dynamically allocated buffer... */
	static int buflen;
	int i, len, gwlen = strlen(gwcall);
	char *p;
	struct aprsis_tx_msg_head head;
	int newlen;
//	dupe_record_t *dp;

	if (aprsis_down < 0) return -1; // No socket!

	if (addrlen == 0)      /* should never be... */
		addrlen = strlen(addr);

//	if (aprsis_rx_dupecheck != NULL) {
//	  dp = dupecheck_aprs( aprsis_rx_dupecheck, 
//			       addr, addrlen,
//			       text, textlen );
//	  if (dp != NULL) return 1; // Bad either as dupe, or due to alloc failure
//	}

	newlen = sizeof(head) + addrlen + gwlen + textlen + 6;
	if (newlen > buflen) {
		buflen = newlen;
		buf = realloc(buf, buflen);
		memset(buf, 0, buflen); // (re)init it to silence valgrind
	}

	memset(&head, 0, sizeof(head));
	head.then    = tick.tv_sec;
	head.addrlen = addrlen;
	head.gwlen   = gwlen;
	head.textlen = textlen;
	head.qtype   = qtype;

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
	len = p - buf;
	*p++ = 0;

#ifndef MSG_NOSIGNAL
# define MSG_NOSIGNAL 0 /* This exists only on Linux  */
#endif
	i = send(aprsis_down, buf, len, MSG_NOSIGNAL);	/* No SIGPIPE if the
							   receiver is out,
							   or pipe is full
							   because it is doing
							   slow reconnection. */

	return (i != len);
	/* Return 0 if ANY of the queue operations was successfull
	   Return 1 if there was some error.. */
}


// APRS-IS communicator
static int aprsis_prepoll_(struct aprxpolls *app)
{
	struct pollfd *pfd;
	struct aprsis *A = AprsIS;

	if (A->last_read == 0)
		A->last_read = tick.tv_sec;	/* mark it non-zero.. */

	if (A->server_socket < 0)
		return -1;	/* Not open, do nothing */

	if (debug>3) printf("aprsis_prepoll_()\n");

	if (time_reset) {
		aprsis_close(A, "time_reset!");
	}


	/* Not all aprs-is systems send "heartbeat", but when they do.. */
	if ((A->H->heartbeat_monitor_timeout > 0) &&
	    ((A->last_read + A->H->heartbeat_monitor_timeout - tick.tv_sec) < 0)) {

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

// APRS-IS communicator
static int aprsis_postpoll_(struct aprxpolls *app)
{
	int i;
	struct pollfd *pfd = app->polls;
	struct aprsis *A = AprsIS;

	if (debug>3) printf("aprsis_postpoll_() cnt=%d\n", app->pollcount);

	for (i = 0; i < app->pollcount; ++i, ++pfd) {
		if (pfd->fd == A->server_socket && pfd->fd >= 0) {
			/* This is APRS-IS socket, and we may have some results.. */

			if (pfd->revents & (POLLERR)) { /* Errors ? */
				aprsis_close(A,"postpoll_ POLLERR");
				continue;
			}
			if (pfd->revents & (POLLHUP)) { /* Errors ? */
				aprsis_close(A,"postpoll_ POLLHUP");
				continue;
			}

			if (pfd->revents & (POLLIN | POLLPRI)) { /* Ready for reading */
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
					if (debug>2)
					  printf("%ld << %s:%s << write() rc= %d\n",
						 tick.tv_sec, A->H->server_name, A->H->server_port, i);

					if (i < 0)
						continue;	/* Argh.. nothing */
					// if (i == 0); /* What ? */

					if (log_aprsis)
					  aprxlog(A->wrbuf + A->wrbuf_cur,
						  (A->wrbuf_len - A->wrbuf_cur) -1,
						  "<< %s:%s << ", A->H->server_name,
						  A->H->server_port);

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


// APRS-IS communicator
static void aprsis_cond_reconnect(void)
{
	if (AprsIS &&	/* First time around it may trip.. */
	    AprsIS->server_socket < 0 && (AprsIS->next_reconnect - tick.tv_sec) <= 0) {
		aprsis_reconnect(AprsIS);
	}
}


/*
 * Main-loop of subprogram handling communication with
 * APRS-IS network servers.
 *
 * This starts only when we have at least one <aprsis> defined without errors.
 */
// APRS-IS communicator
static void aprsis_main(void)
{
#if !(defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD))
	int ppid = getppid();
#endif
	struct aprxpolls app = APRXPOLLS_INIT;


#if !(defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD))
	signal(SIGHUP, sig_handler);
	signal(SIGPIPE, SIG_IGN);
#endif

	/* The main loop */
	while (!die_now) {
		struct pollfd *pfd;

		timetick();

		aprsis_cond_reconnect(); // may take unpredictable time..

		timetick();

#if !(defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD))
		// Parent-pid makes no sense in threaded setup
		int i;
		i = getppid();
		if (i != ppid)
			break;	/* die now, my parent is gone.. */
		if (i == 1)
			break;	/* a safety fallback case.. */
#endif

		aprxpolls_reset(&app);
		tv_timeradd_seconds( &app.next_timeout, &tick, 5 );

		if (aprsis_up >= 0) {
			pfd = aprxpolls_new(&app);

			pfd->fd = aprsis_up;
			pfd->events = POLLIN | POLLPRI | POLLERR | POLLHUP;
			pfd->revents = 0;
		}

		aprsis_prepoll_(&app);

		// Prepolls are done
		time_reset = 0;

		if (tv_timercmp(&app.next_timeout, &tick) <= 0) {
			tv_timeradd_seconds( &app.next_timeout, &tick, 1 ); // Just to be on safe side..
		}

		poll(app.polls, app.pollcount, aprxpolls_millis(&app));

		timetick();

		assert(app.polls != NULL);
		if (app.polls[0].
		    revents & (POLLIN | POLLPRI | POLLERR | POLLHUP)) {
			/* messaging channel has something for us, if
			   the channel reports EOF, we exit there and then. */
			aprsis_readup();
		}
		aprsis_postpoll_(&app);
	}
	aprxpolls_free(&app); // valgrind..
	/* Got "DIE NOW" signal... */
	// exit(0);
}


/*
 *  aprsis_add_server() - old style configuration
 */

int aprsis_add_server(const char *server, const char *port)
{
	struct aprsis_host *H;

	if (AprsIS == NULL) {
		AprsIS = calloc(1,sizeof(*AprsIS));
	}

	H = calloc(1,sizeof(*H));
	AISh = realloc(AISh, sizeof(AISh[0]) * (AIShcount + 1));
	AISh[AIShcount] = H;

	++AIShcount;
	/* No inc on AprsIScount */


	H->server_name = strdup(server);
	H->server_port = strdup(port);
	H->heartbeat_monitor_timeout = 120; // Default timeout 120 seconds
	H->login       = strdup(aprsis_login);	// global aprsis_login
	H->pass	       = default_passcode;
	if (H->login == NULL) H->login = strdup(mycall);

	AprsIS->server_socket = -1;
	AprsIS->next_reconnect = tick.tv_sec +10;	/* perhaps somewhen latter.. */

	return 0;
}

// old style configuration
int aprsis_set_heartbeat_timeout(const int tout)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0)
		--i;
	H = AISh[i];

	H->heartbeat_monitor_timeout = tout;

	return 0;
}

// old style configuration
int aprsis_set_filter(const char *filter)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0)
		--i;
	H = AISh[i];

	H->filterparam = strdup(filter);

	return 0;
}

// old style configuration
int aprsis_set_login(const char *login)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0)
		--i;
	H = AISh[i];

	H->login = strdup(login);

	return 0;
}

#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
static void aprsis_runthread(void)
{
	sigset_t sigs_to_block;

	sigemptyset(&sigs_to_block);
	sigaddset(&sigs_to_block, SIGALRM);
	sigaddset(&sigs_to_block, SIGINT);
	sigaddset(&sigs_to_block, SIGTERM);
	sigaddset(&sigs_to_block, SIGQUIT);
	sigaddset(&sigs_to_block, SIGHUP);
	sigaddset(&sigs_to_block, SIGURG);
	sigaddset(&sigs_to_block, SIGPIPE);
	sigaddset(&sigs_to_block, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &sigs_to_block, NULL);

	// generally the cancelability is enabled
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	if (debug) printf("aprsis_runthread()\n");

	aprsis_main();
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

	fd_nonblockingmode(pipes[0]);
	fd_nonblockingmode(pipes[1]);
	aprsis_down = pipes[0];
	aprsis_up   = pipes[1];

	if (debug)printf("aprsis_start() PTHREAD  socketpair(up=%d,down=%d)\n", aprsis_up, aprsis_down);

	pthread_attr_init(&pthr_attrs);
	/* 64 kB stack is enough for this thread (I hope!)
	   default of 2 MB is way too much...*/
	pthread_attr_setstacksize(&pthr_attrs, 64*1024);

	i = pthread_create(&aprsis_thread, &pthr_attrs, (void*)aprsis_runthread, NULL);
	if (i == 0) {
	  if (debug) printf("APRSIS pthread_create() OK!\n");
	} else {  // FAIL!
		close(pipes[0]);
		close(pipes[1]);
		aprsis_down = -1;
		aprsis_up   = -1;
	}
}

// Shutdown the aprsis thread
void aprsis_stop(void)
{
	die_now = 1;
	pthread_cancel(aprsis_thread);
	pthread_join(aprsis_thread, NULL);
}


#else  // No pthread(3p)
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
		aprsis_up = pipes[1];
		fd_nonblockingmode(pipes[1]);
		close(pipes[0]);
		aprsis_main();
		exit(0);
	}


	/* Parent */
	close(pipes[1]);
	fd_nonblockingmode(pipes[0]);
	aprsis_down = pipes[0];
}


void aprsis_stop(void)
{
}
#endif


/*
 * main-program side pre-poll
 */
int aprsis_prepoll(struct aprxpolls *app)
{
	int idx = 0;		/* returns number of *fds filled.. */

	struct pollfd *pfd;

	// if (debug>3) printf("aprsis_prepoll()\n");

	pfd = aprxpolls_new(app);

	pfd->fd = aprsis_down;	/* APRS-IS communicator server Sub-process */
	pfd->events = POLLIN | POLLPRI;
	pfd->revents = 0;

	/* We react only for reading, if write fails because the socket is
	   jammed,  that is just too bad... */

	++idx;

	return idx;

}

/*
 * main-program side reading of aprsis_down
 */
static int aprsis_comssockread(int fd)
{
	int i;
	char buf[10000];

	i = recv(fd, buf, sizeof(buf), 0);
	if (debug>3) printf("aprsis_comsockread(fd=%d) -> i = %d\n", fd, i);
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
 */
int aprsis_postpoll(struct aprxpolls *app)
{
	int i;
	struct pollfd *pfd = app->polls;


	// if (debug>3) printf("aprsis_postpoll()\n");

	for (i = 0; i < app->pollcount; ++i, ++pfd) {
		if (pfd->fd == aprsis_down) {
			/* This is APRS-IS communicator subprocess socket,
			   and we may have some results.. */

			if (pfd->revents) {	/* Ready for reading */
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


// main program side
int aprsis_config(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int has_fault = 0;
	int line0 = cf->linenum;

	struct aprsis_host *AIH = calloc(1,sizeof(*AIH));
	AIH->login		= strdup(mycall);
	AIH->pass		= default_passcode;
	AIH->heartbeat_monitor_timeout = 120;
	AIH->mode = MODE_TCP; // default mode

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
		// mode

		if (strcmp(name, "login") == 0) {
		  if (strcasecmp("$mycall",param1) != 0) {
		    // If not "$mycall" ..
		    config_STRUPPER(param1);
		    if (!validate_callsign_input(param1,0)) {
		      // bad input...
		    }
		    if (debug)
		      printf("%s:%d: INFO: LOGIN = '%s' '%s'\n",
			     cf->name, cf->linenum, param1, str);
		    if (AIH->login) free(AIH->login);
		    AIH->login = strdup(param1);
		  }

		} else if (strcmp(name, "passcode") == 0) {
		    if (debug)
		      printf("%s:%d: INFO: PASSCODE = '%s' '%s'\n",
			     cf->name, cf->linenum, param1, str);
		    AIH->pass = strdup(param1);

		} else if (strcmp(name, "server") == 0) {

		  if (AIH->server_name) free(AIH->server_name);
		  AIH->server_name = strdup(param1);

		  param1 = str;
		  str = config_SKIPTEXT(str, NULL);
		  // coverity[returned_pointer]
		  str = config_SKIPSPACE(str);
		  if ('1' <= *param1 && *param1 <= '9') {
		    // fixme: more input analysis?
		    int port = atoi(param1);
		    if (port < 1 || port > 65535) {
		      printf("%s:%d INFO: SERVER = '%s' port='%s' is not supplying valid TCP port number, defaulting to '14580'\n",
			     cf->name, cf->linenum, AIH->server_name, param1);
		      param1 = "14580";
		    }
		    AIH->server_port = strdup(param1);
		  } else if (*param1 == 0) {
		    // Default silently!
		    AIH->server_port = strdup("14580");
		  } else {
		    AIH->server_port = strdup("14580");
		    printf("%s:%d INFO: SERVER = '%s' port='%s' is not supplying valid TCP port number, defaulting to '14580'\n",
			   cf->name, cf->linenum, AIH->server_name, param1);
		  }

		  if (debug)
		    printf("%s:%d: INFO: SERVER = '%s':'%s'\n",
			   cf->name, cf->linenum, AIH->server_name, AIH->server_port);

		} else if (strcmp(name, "heartbeat-timeout") == 0) {
		  int i = 0;
		  if (config_parse_interval(param1, &i)) {
		    // FIXME: Report parameter failure ...
		    printf("%s:%d: ERROR: HEARTBEAT-TIMEOUT = '%s'  - bad parameter'\n",
			   cf->name, cf->linenum, param1);
		    has_fault = 1;
		  }
		  if (i < 0) {	/* param failure ? */
		    i = 0;	/* no timeout */
		    printf("%s:%d: ERROR: HEARTBEAT-TIMEOUT = '%s'  - bad parameter'\n",
			   cf->name, cf->linenum, param1);
		    has_fault = 1;
		  }
		  AIH->heartbeat_monitor_timeout = i;
		  
		  if (debug)
		    printf("%s:%d: INFO: HEARTBEAT-TIMEOUT = '%d' '%s'\n",
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
		    printf("%s:%d: INFO: FILTER = '%s' -->  '%s'\n",
			   cf->name, cf->linenum, param1, AIH->filterparam);

		} else if (strcmp(name, "mode") == 0) {
		  if (strcmp(param1,"tcp") == 0) {
		    AIH->mode = MODE_TCP;
		  } else if (strcmp(param1,"ssl") == 0) {
		    AIH->mode = MODE_SSL;
		  } else if (strcmp(param1,"sctp") == 0) {
		    AIH->mode = MODE_SCTP;
		  } else if (strcmp(param1,"dtls") == 0) {
		    AIH->mode = MODE_DTLS;
		  } else {
		    printf("%s:%d: ERROR: Unknown mode keyword in <aprsis> block: '%s'\n",
			 cf->name, cf->linenum, param1);
		    has_fault = 1;
		  }

		} else	{
		  printf("%s:%d: ERROR: Unknown configuration keyword in <aprsis> block: '%s'\n",
			 cf->name, cf->linenum, name);
		  has_fault = 1;
		}
	}
	if (AIH->server_name == NULL) {
	  printf("%s:%d ERROR: This <aprsis> block does not define server!\n",
		 cf->name, line0);
	  has_fault = 1;
	}
	if (has_fault) {
		if (AIH->server_name != NULL) free(AIH->server_name);
		if (AIH->server_port != NULL) free(AIH->server_port);
		if (AIH->filterparam != NULL) free(AIH->filterparam);
		if (AIH->login	     != NULL) free(AIH->login);
		free(AIH);

	} else {
		if (AprsIS == NULL) {
			AprsIS = calloc(1, sizeof(*AprsIS));
			AprsIS->server_socket = -1;
			AprsIS->next_reconnect = tick.tv_sec +10;
		}
                if (AIH->pass == default_passcode) {
                  printf("%s:%d WARNING: This <aprsis> block does not define passcode!\n",
                         cf->name, line0);
                  printf("%s:%d WARNING: Your beacons and RF received will not make it to APRS-IS.\n",
                         cf->name, line0);
                }

		AISh = realloc(AISh, sizeof(AISh[0]) * (AIShcount + 1));
		AISh[AIShcount] = AIH;
	}
	return has_fault;
}

#endif
