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
#include <sys/socket.h>
#include <netdb.h>

      int   aprsis_heartbeat_monitor_timeout;
const char *aprsis_server_name;
const char *aprsis_server_port; /* numeric text, not an interger */

/*
 * $aprsserver = "rotate.aprs.net:14580";
 *
 * re-resolve the $aprsserver at each connection setup!
 */ 

static int sockaprsis;
static time_t next_aprsis_reconnect;
static time_t aprsis_last_read;

static char aprsis_wrbuf[16000];
static char aprsis_rdbuf[ 3000];

static int  aprsis_wrbuf_len;
static int  aprsis_wrbuf_cur;


void aprsis_init(void)
{
	sockaprsis = -1;
}

int fd_nonblockingmode(int fd)
{
	int __i, __i2;
	__i2 = __i = fcntl(fd, F_GETFL, 0);
	if (__i >= 0) {
	  /* set up non-blocking I/O */
	  __i |= O_NONBLOCK;
	  __i = fcntl(fd, F_SETFL, __i);
	}
	return __i;
}


/*
 *Close APRS-IS socket, clean state..
 */

static void aprsis_close(void)
{
	if (sockaprsis >= 0)
	  close(sockaprsis);  /* close, and flush write buffers */

	sockaprsis = -1;

	aprsis_wrbuf_len = aprsis_wrbuf_cur = 0;
	next_aprsis_reconnect = now + 60;
	aprsis_last_read = 0;


now = time(NULL);
printf("%ld\tCLOSE APRSIS\n",(long)now);

}



/*
 * APRSpass requires that input callsign is in uppercase ASCII
 * characters (A-Z), or decimal digits.  Characters outside those
 * will terminate the calculation.
 */

static int aprspass(const char *mycall)
{
	int a = 0, h = 29666, c;

	for ( ; *mycall ; ++mycall ) {
	  c = 0xFF & *mycall;
	  if (!(( '0' <= c && c <= '9' ) ||
		( 'A' <= c && c <= 'Z' )))
	    break;
	  h ^= ((0xFF & *mycall) * (a ? 1 : 256));
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

static void aprsis_reconnect(void)
{
	struct addrinfo req, *ai, *a2;
	int i, n, asize;
	const char *s;
	int  len;
	char aprsislogincmd[300];

	aprsis_close();

	memset(&req, 0, sizeof(req));
	req.ai_socktype = SOCK_STREAM;
	req.ai_protocol = IPPROTO_TCP;
	req.ai_flags    = 0;
#if 0
	req.ai_family   = AF_UNSPEC;  /* IPv4 and IPv6 are both OK */
#else
	req.ai_family   = AF_INET;    /* IPv4 only */
#endif
	ai = NULL;


	i = getaddrinfo(aprsis_server_name, aprsis_server_port, &req, &ai);

	if (i != 0) {

	fail_out:;
	  /* Discard stuff and redo latter.. */

	  if (ai) freeaddrinfo(ai);

	  aprsis_close();
	  return;
	}
	
	/* Count the addresses */
	for (n = 0, a2 = ai; a2; a2 = a2->ai_next, ++n)
	  ;

	a2 = ai;
	if (n > 1) {  /* more than one ?  choose one at random.. */
	  n = rand() % n;
	  for ( ; n > 0; a2 = a2->ai_next, --n )
	    ;
	}

	sockaprsis = socket(a2->ai_family, SOCK_STREAM, 0);
	if (sockaprsis < 0)
	  goto fail_out;

	i = connect(sockaprsis, a2->ai_addr, a2->ai_addrlen);
	if (i < 0)
	  goto fail_out;

	freeaddrinfo(ai);
	ai = NULL;

	now = time(NULL);
printf("%ld\tCONNECT APRSIS\n",(long)now);


	/* From now the socket will be non-blocking for its entire lifetime..*/
	fd_nonblockingmode(sockaprsis);

	/* We do at first sync writing of login, and such.. */
	sprintf(aprsislogincmd, "user %s pass %d vers %s\r\n", mycall, aprspass(mycall), version);

	aprsis_queue(aprsislogincmd, strlen(aprsislogincmd));
	beacon_reset();
}


int aprsis_queue(const char *s, int len)
{
	int i;

	/* Queue for sending to APRS-IS only when the socket is operational */
	if (sockaprsis < 0) return 1;

	/*
	 * Append stuff on the writebuf, if it fits.
	 * If it does not fit, something is broken already
	 * and we just drop it..
	 *
	 * Just to make sure that the write pointer is not left
	 * rewound when all has been done...
	 */

	if (aprsis_wrbuf_cur >= aprsis_wrbuf_len && aprsis_wrbuf_len > 0)
	  aprsis_wrbuf_cur = aprsis_wrbuf_len = 0;

	/* Does it fit in ? */

	if ((sizeof(aprsis_wrbuf)-10) <= (aprsis_wrbuf_len + len)) {
	  /* The string does not fit in, perhaps it needs compacting ? */
	  if (aprsis_wrbuf_cur > 0) { /* Compacting is possible ! */
	    memmove(aprsis_wrbuf, aprsis_wrbuf + aprsis_wrbuf_cur, aprsis_wrbuf_len - aprsis_wrbuf_cur);
	    aprsis_wrbuf_len -= aprsis_wrbuf_cur;
	    aprsis_wrbuf_cur = 0;
	  }

	  /* Check again if it fits in..*/
	  if ((sizeof(aprsis_wrbuf)-10) <= (aprsis_wrbuf_len + len)) {
	    /* NOT!  Too bad, drop it.. */
	    return 2;
	  }
	}


	/* Place it on our send buffer */

	memcpy(aprsis_wrbuf + aprsis_wrbuf_len, s, len);
	aprsis_wrbuf_len += len; /* Always supplied with tail newline.. */

	/* Try writing it right away: */

	i = write(sockaprsis, aprsis_wrbuf + aprsis_wrbuf_cur, aprsis_wrbuf_len - aprsis_wrbuf_cur);
	if (i > 0) {
	  aprsis_wrbuf_cur += i;
	  if (aprsis_wrbuf_cur >= aprsis_wrbuf_len) {  /* Wrote all ! */
	    aprsis_wrbuf_cur = aprsis_wrbuf_len = 0;
	  }
	}

	return 0;
}



int aprsis_prepoll(int nfds, struct pollfd **fdsp, time_t *tout)
{
	int idx = 0; /* returns number of *fds filled.. */
	int i;

	struct pollfd *fds = *fdsp;

	if (aprsis_last_read == 0) aprsis_last_read = now;

	/* Not all aprs-is systems send "heartbeat", but when they do.. */
	if ((aprsis_heartbeat_monitor_timeout > 0) &&
	    (aprsis_last_read + aprsis_heartbeat_monitor_timeout < now)) {

	  /*
	   * More than 120 seconds (2 minutes) since last time
	   * that APRS-IS systems told us something on the connection.
	   * There is a heart-beat ticking every 20 or so seconds.
	   */

	  aprsis_close();
	}

	if (sockaprsis < 0) return 0; /* Not open, do nothing */

	/* FD is open, lets mark it for poll read.. */
	fds->fd = sockaprsis;
	fds->events = POLLIN|POLLPRI;
	fds->revents = 0;

	/* Do we have something for writing ?  */
	if (aprsis_wrbuf_len)
	  fds->events |= POLLOUT;

	++fds;

	*fdsp = fds;

	return 1;

}

int aprsis_postpoll(int nfds, struct pollfd *fds)
{
	int i;

	for (i = 0; i < nfds; ++i, ++fds) {
	  if (fds->fd == sockaprsis && sockaprsis >= 0) {
	    /* This is APRS-IS socket, and we may have some results.. */

	    if (fds->revents & (POLLERR | POLLHUP)) { /* Errors ? */
	    close_sockaprsis:;
	      aprsis_close();
	      return -1;
	    }

	    if (fds->revents & POLLIN) {  /* Ready for reading */
	      for(;;) {
		i = read(sockaprsis, aprsis_rdbuf, sizeof(aprsis_rdbuf));
		if (i == 0) /* EOF ! */
		  goto close_sockaprsis;
		if (i < 0) break;
		/* we just ignore the readback.. */
		aprsis_last_read = time(NULL);
	      }
	    }

	    if (fds->revents & POLLOUT) { /* Ready for writing  */
	      /* Normal queue write processing */

	      if (aprsis_wrbuf_len > 0 && aprsis_wrbuf_cur < aprsis_wrbuf_len) {
		i = write(sockaprsis, aprsis_wrbuf + aprsis_wrbuf_cur, aprsis_wrbuf_len - aprsis_wrbuf_cur);
		if (i < 0) return; /* Argh.. nothing */
		if (i == 0) ; /* What ? */
		if (i == (aprsis_wrbuf_len - aprsis_wrbuf_cur)) { /* Wrote all! */
		  aprsis_wrbuf_len = aprsis_wrbuf_cur = 0;
		} else { /* Partial write */
		  aprsis_wrbuf_cur += i;
		}

	      } /* .. normal queue */

	    } /* .. POLLOUT */

	    return 1; /* there was something we did, maybe.. */
	  }
	}
}


void aprsis_cond_reconnect(void)
{
	if (sockaprsis < 0 &&
	    next_aprsis_reconnect <= now) {

	  aprsis_reconnect();
	}
}
