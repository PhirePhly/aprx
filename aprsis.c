/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */

/* This code works only with single  aprsis-server  instance! */

#include "aprx.h"
#include <sys/socket.h>
#include <netdb.h>


/*
 * $aprsserver = "rotate.aprs.net:14580";
 *
 * re-resolve the $aprsserver at each connection setup!
 *
 * The APRS-IS system connection runs as separate sub-process, once it starts.
 * This way the main-loop is independent from uncertainties of DNS resolving
 * in this part of the code.
 *
 */ 


struct aprsis_host {
	const char *server_name;
	const char *server_port;
	const char *filterparam;
	const char *mycall;
	int	heartbeat_monitor_timeout;
};

struct aprsis {
	int	server_socket;
	struct aprsis_host *H;	
	time_t	next_reconnect;
	time_t	last_read;
	int	wrbuf_len;
	int	wrbuf_cur;
	int	rdbuf_len;
	int	rdbuf_cur;
	int	rdlin_len;

	char	wrbuf[16000];
	char	rdbuf[3000];
	char	rdline[500];
};


static int AprsIScount;
static int AprsISindex;
static struct aprsis AprsIS[MAXAPRSIS];
static struct aprsis_host AISh[MAXAPRSIS];
static int AIShcount;
static int AIShindex;
static int aprsis_multiconnect;
static int aprsis_sp; /* up & down talking socket(pair),
			 parent: write talks down,
			 child: write talks up. */

void aprsis_init(void)
{
	int i;
	for (i = 0; i < MAXAPRSIS; ++i)
	  AprsIS[i].server_socket = -1;
	aprsis_sp = -1;
}


/*
 *Close APRS-IS server_socket, clean state..
 */

static void aprsis_close(struct aprsis *A)
{
	if (A->server_socket >= 0)
	  close(A->server_socket);  /* close, and flush write buffers */

	A->server_socket = -1;

	A->wrbuf_len = A->wrbuf_cur = 0;
	A->next_reconnect = now + 60;
	A->last_read = now;

	if (!A->H) return; /* Not connected, nor defined.. */

	if (verbout)
	  printf("%ld\tCLOSE APRSIS %s:%s\n",(long)now, A->H->server_name,A->H->server_port);
	if (aprxlogfile) {
	  FILE *fp = fopen(aprxlogfile,"a");
	  if (fp) {
	    char timebuf[60];
	    struct tm *t = gmtime(&now);
	    strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);

	    fprintf(fp,"%s CLOSE APRSIS %s:%s\n", timebuf, A->H->server_name,A->H->server_port);
	  }
	}
}


/*
 *  aprsis_queue_() - internal routine - queue data to specific APRS-IS instance
 */
static int aprsis_queue_(struct aprsis *A, const char *addr, const char *text, int textlen)
{
	int i;
	char addrbuf[1000];
	int addrlen, len;

	/* Queue for sending to APRS-IS only when the socket is operational */
	if (A->server_socket < 0) return 1;

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
	  addrlen = sprintf(addrbuf, "%s,%s,I:", addr, A->H->mycall);
	len = addrlen + textlen;


	/* Does it fit in ? */

	if ((sizeof(A->wrbuf)-10) <= (A->wrbuf_len + len)) {
	  /* The string does not fit in, perhaps it needs compacting ? */
	  if (A->wrbuf_cur > 0) { /* Compacting is possible ! */
	    memmove(A->wrbuf, A->wrbuf + A->wrbuf_cur, A->wrbuf_len - A->wrbuf_cur);
	    A->wrbuf_len -= A->wrbuf_cur;
	    A->wrbuf_cur = 0;
	  }

	  /* Check again if it fits in..*/
	  if ((sizeof(A->wrbuf)-10) <= (A->wrbuf_len + len)) {
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
	A->wrbuf_len += textlen; /* Always supplied with tail newline.. */

	/* Try writing it right away: */

	i = write(A->server_socket, A->wrbuf + A->wrbuf_cur, A->wrbuf_len - A->wrbuf_cur);
	if (i > 0) {
	  if (debug > 1) {
	    printf("%ld\t<< %s:%s << ", now, A->H->server_name, A->H->server_port);
	    fwrite(A->wrbuf + A->wrbuf_cur, (A->wrbuf_len - A->wrbuf_cur), 1, stdout); /* Does end on  \r\n */
	  }

	  A->wrbuf_cur += i;
	  if (A->wrbuf_cur >= A->wrbuf_len) {  /* Wrote all ! */
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

static void aprsis_reconnect(struct aprsis *A)
{
	struct addrinfo req, *ai, *a2;
	int i, n;
	char *s;
	char aprsislogincmd[3000];

	aprsis_close(A);

	if (!aprsis_multiconnect) {
	  if (!A->H) {
	    A->H = & AISh[0];
	  } else {
	    ++AIShindex;
	    if (AIShindex >= AIShcount)
	      AIShindex = 0;
	    A->H = &AISh[AIShindex];
	  }
	}

	memset(&req, 0, sizeof(req));
	req.ai_socktype = SOCK_STREAM;
	req.ai_protocol = IPPROTO_TCP;
	req.ai_flags    = 0;
#if 1
	req.ai_family   = AF_UNSPEC;  /* IPv4 and IPv6 are both OK */
#else
	req.ai_family   = AF_INET;    /* IPv4 only */
#endif
	ai = NULL;


	i = getaddrinfo(A->H->server_name, A->H->server_port, &req, &ai);

	if (i != 0) {

	fail_out:;
	  /* Discard stuff and redo latter.. */

	  if (ai) freeaddrinfo(ai);

	  aprsis_close(A);
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

	A->server_socket = socket(a2->ai_family, SOCK_STREAM, 0);
	if (A->server_socket < 0)
	  goto fail_out;

	i = connect(A->server_socket, a2->ai_addr, a2->ai_addrlen);
	if (i < 0)
	  goto fail_out;

	freeaddrinfo(ai);
	ai = NULL;

	now = time(NULL); /* unpredictable time since system did last poll.. */
	if (verbout)
	  printf("%ld\tCONNECT APRSIS %s:%s\n",(long)now,A->H->server_name,A->H->server_port);
	if (aprxlogfile) {
	  FILE *fp = fopen(aprxlogfile,"a");
	  if (fp) {
	    char timebuf[60];
	    struct tm *t = gmtime(&now);
	    strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);

	    fprintf(fp,"%s CONNECT APRSIS %s:%s\n", timebuf, A->H->server_name, A->H->server_port);
	  }
	}


	/* From now the socket will be non-blocking for its entire lifetime..*/
	fd_nonblockingmode(A->server_socket);

	/* We do at first sync writing of login, and such.. */
	s = aprsislogincmd;
	s += sprintf(s, "user %s pass %d vers %s", A->H->mycall, aprspass(A->H->mycall), version);
	if (A->H->filterparam)
	  s+= sprintf(s, "filter %s", A->H->filterparam);
	strcpy(s,"\r\n");

	A->last_read = now;

	aprsis_queue_(A, NULL, aprsislogincmd, strlen(aprsislogincmd));
	beacon_reset();
}


static int aprsis_sockreadline(struct aprsis *A)
{
	int i, c;

	/* reads multiple lines from buffer, last one is left into incomplete state */

	for (i = A->rdbuf_cur; i < A->rdbuf_len; ++i) {
	  c = 0xFF & (A->rdbuf[i]);
	  if (c == '\r' || c == '\n') {
	    /* End of line, process.. */
	    if (A->rdlin_len > 0) {
	      A->rdline[A->rdlin_len] = 0;
	      /* */
	      if (A->rdline[0] != '#') /* Not only a comment! */
		A->last_read = now;  /* Time stamp me ! */
	      
	      if (debug > 1)
		printf("%ld\t<< %s:%s >> %s\n", now,
		       A->H->server_name, A->H->server_port, A->rdline);

	      /* Send the A->rdline content to main program */
	      send(aprsis_sp, A->rdline, strlen(A->rdline), MSG_NOSIGNAL);
	    }
	    A->rdlin_len = 0;
	    continue;
	  }
	  if (A->rdlin_len < sizeof(A->rdline)-2) {
	    A->rdline[A->rdlin_len++] = c;
	  }
	}
	A->rdbuf_cur = 0;
	A->rdbuf_len = 0;  /* we ignore line reading */
	return 0;	   /* .. this is placeholder.. */
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
	    A->rdbuf_len = 0; /* all processed, mark its size zero */
	  A->rdbuf_cur = 0;

	  /* recalculate */
	  rdspace = sizeof(A->rdbuf) - A->rdbuf_len;
	}

	i = read(A->server_socket, A->rdbuf + A->rdbuf_len, rdspace);

	if (i > 0) {

	  A->rdbuf_len += i;

	  /* we just ignore the readback.. but do time-stamp the event */
	  if (! A->H->filterparam)
	    A->last_read = now;

	  aprsis_sockreadline(A);
	}

	return i;
}

static void aprsis_readsp(void)
{
	int i;
	char buf[10000];
	const char *addr;
	const char *text;
	int textlen;
	time_t then;

	i = recv(aprsis_sp, buf, sizeof(buf), 0);
	if (i == 0) { /* EOF ! */
	  exit(0);
	}
	if (i < 0) {
	  return; /* Whatever was the reason.. */
	}
	buf[i] = 0; /* String Termination NUL byte */

	memcpy(&then, buf, sizeof(then));
	if (then + 10 < now) return; /* Too old, discard */
	addr = buf+sizeof(then);
	text = addr;
	while (*text && text < (buf+sizeof(buf))) ++text;
	++text; /* skip over the 0 byte in between addr and message text */
	/* now we have text content.. */
	textlen = i - (text - buf);
	if (textlen < 0) return; /* BAD! */
	
	/* Now queue the thing! */

	for (i = 0; i < AprsIScount; ++i)
	  aprsis_queue_(& AprsIS[i], addr, text, textlen);
}

int aprsis_queue(const char *addr, const char *text, int textlen)
{
	static char *buf; /* Dynamically allocated buffer... */
	static int buflen;
	int len, i;
	char *p;

	if (textlen + strlen(addr) + 30 > buflen) {
	  buflen = textlen + strlen(addr) + 30;
	  buf = realloc(buf, buflen);
	}

	memcpy(buf, &now, sizeof(now));
	p = buf+sizeof(now);
	p += sprintf(p, "%s", addr);
	++p; /* string terminating 0 byte */
	p += sprintf(p, "%s\r\n", text);
	len = p - buf + 2;

	i = send(aprsis_sp, buf, len, MSG_NOSIGNAL); /* No SIGPIPE if the
							receiver is out,
							or pipe is full
							because it is doing
							slow reconnection. */

	return (i != len);
	/* Return 0 if ANY of the queue operations was successfull
	   Return 1 if there was some error.. */
}




static int aprsis_prepoll_(int nfds, struct pollfd **fdsp, time_t *tout)
{
	int idx = 0; /* returns number of *fds filled.. */
	int i;

	struct pollfd *fds = *fdsp;

#if 0
	if (AprsIScount > 1 && !aprsis_multiconnect)
	  AprsIScount = 1; /* There can be only one... */
#endif

	for (i = 0; i < AprsIScount; ++i) {
	  struct aprsis *A = & AprsIS[i];

	  if (A->last_read == 0) A->last_read = now; /* mark it non-zero.. */

	  if (A->server_socket < 0) continue; /* Not open, do nothing */

	  /* Not all aprs-is systems send "heartbeat", but when they do.. */
	  if ((A->H->heartbeat_monitor_timeout > 0) &&
	      (A->last_read + A->H->heartbeat_monitor_timeout < now)) {

	    /*
	     * More than 120 seconds (2 minutes) since last time
	     * that APRS-IS systems told us something on the connection.
	     * There is a heart-beat ticking every 20 or so seconds.
	     */

	    aprsis_close(A);
	  }


	  /* FD is open, lets mark it for poll read.. */

	  if (nfds <= 0) continue; /* If we have room! */

	  fds->fd = A->server_socket;
	  fds->events = POLLIN|POLLPRI;
	  fds->revents = 0;

	  /* Do we have something for writing ?  */
	  if (A->wrbuf_len)
	    fds->events |= POLLOUT;

	  --nfds;
	  ++fds;
	  ++idx;
	}

	*fdsp = fds;

	return idx;

}

static int aprsis_postpoll_(int nfds, struct pollfd *fds)
{
	int i, a;

	for (i = 0; i < nfds; ++i, ++fds) {
	  for (a = 0; a < AprsIScount; ++a) {
	    struct aprsis *A = & AprsIS[a];
	    if (fds->fd == A->server_socket && A->server_socket >= 0) {
	      /* This is APRS-IS socket, and we may have some results.. */

	      if (fds->revents & (POLLERR | POLLHUP)) { /* Errors ? */
	      close_sockaprsis:;
		aprsis_close(A);
		continue;
	      }

	      if (fds->revents & POLLIN) {  /* Ready for reading */
		for(;;) {
		  i = aprsis_sockread(A);
		  if (i == 0) /* EOF ! */
		    goto close_sockaprsis;
		  if (i < 0) break;
		}
	      }

	      if (fds->revents & POLLOUT) { /* Ready for writing  */
		/* Normal queue write processing */

		if (A->wrbuf_len > 0 && A->wrbuf_cur < A->wrbuf_len) {
		  i = write(A->server_socket, A->wrbuf + A->wrbuf_cur, A->wrbuf_len - A->wrbuf_cur);
		  if (i < 0) continue; /* Argh.. nothing */
		  if (i == 0) ; /* What ? */
		  A->wrbuf_cur += i;
		  if (A->wrbuf_cur >= A->wrbuf_len) { /* Wrote all! */
		    A->wrbuf_len = A->wrbuf_cur = 0;
		  } else {
		    /* partial write .. do nothing.. */
		  }
		} /* .. normal queue */

	      } /* .. POLLOUT */
	    } /* .. if fd == server_socket */
	  } /* .. MAXPARSIS .. */
	} /* .. for .. nfds .. */
	return 1; /* there was something we did, maybe.. */
}


static void aprsis_cond_reconnect(void)
{
	if (aprsis_multiconnect) {
	  int i;
	  for (i = 0; i < AprsIScount; ++i) {
	    struct aprsis *A = & AprsIS[i];
	    if (A->server_socket < 0 &&
		A->next_reconnect <= now) {
	      aprsis_reconnect(A);
	    }
	  }
	} else {
	  struct aprsis *AIS = & AprsIS[AprsISindex];
	  if (AIS->server_socket < 0 &&
	      AIS->next_reconnect <= now) {
	    aprsis_reconnect(AIS);
	  }
	}
}



extern struct pollfd polls[MAXPOLLS];

static void aprsis_main(void)
{
	int i;
	struct pollfd *fds;
	int nfds, nfds2;

	/* The main loop */
	while (! die_now) {
	  time_t next_timeout;
	  now = time(NULL);
	  next_timeout = now + 30;

	  aprsis_cond_reconnect();

	  fds = polls;

	  fds[0].fd = aprsis_sp;
	  fds[0].events = POLLIN | POLLPRI;
	  fds[0].revents = 0;
	  ++fds;

	  nfds = MAXPOLLS-1;
	  nfds2 = 1;


	  i = aprsis_prepoll_(nfds, &fds, &next_timeout);
	  nfds2  += i;
	  nfds   -= i;


	  if (next_timeout <= now)
	    next_timeout = now + 1; /* Just to be on safe side.. */

	  i = poll(polls, nfds2, (next_timeout - now) * 1000);
	  now = time(NULL);

	  if (polls[0].revents & (POLLIN|POLLPRI|POLLERR|POLLHUP)) {
	    /* messaging channel has something for us, if
	       the channel reports EOF, we exit there and then. */
	    aprsis_readsp();
	  }
	  i = aprsis_postpoll_(nfds2, polls);
	}
	/* Got "DIE NOW" signal... */
	exit(0);
}


/*
 *  aprsis_add_server() - 
 */

void aprsis_add_server(const char *server, const char *port)
{
	struct aprsis *A;
	struct aprsis_host *H;

	if (aprsis_multiconnect) {

	  if (AprsIScount >= MAXAPRSIS) return; /* Too many, no room.. */

	  A = & AprsIS[AprsIScount];
	  H = &AISh[AIShcount];
	  A->H = H;

	  ++AprsIScount;
	  ++AIShcount;

	} else { /* not multiconnect */

	  if (AprsIScount == 0) AprsIScount = 1;

	  if (AIShcount >= MAXAPRSIS) return; /* Too many, no room.. */

	  A = & AprsIS[AprsIScount];
	  H = &AISh[AIShcount];

	  ++AIShcount;
	  /* No inc on AprsIScount */

	}


	H->server_name   = strdup(server);
	H->server_port   = strdup(port);
	H->mycall        = mycall;  /* global mycall */

	A->server_socket = -1;
	A->next_reconnect = now; /* perhaps somewhen latter.. */
}

void aprsis_set_heartbeat_timeout(const int tout)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0) --i;
	H = & AISh[i];

	H->heartbeat_monitor_timeout = tout;
}

void aprsis_set_multiconnect(void)
{
	aprsis_multiconnect = 1; /* not really implemented.. */
}

void aprsis_set_filter(const char *filter)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0) --i;
	H = & AISh[i];

	H->filterparam = strdup(filter);
}

void aprsis_set_mycall(const char *mycall)
{
	int i = AIShcount;
	struct aprsis_host *H;

	if (i > 0) --i;
	H = & AISh[i];

	H->mycall = strdup(mycall);
}

void aprsis_start(void)
{
	int i;
	int pipes[2];

	i = socketpair(AF_UNIX, SOCK_DGRAM, PF_UNSPEC, pipes);
	if (i != 0) {
	  return; /* FAIL ! */
	}

	i = fork();
	if (i < 0) {
	  close(pipes[0]);close(pipes[1]);
	  return; /* FAIL ! */
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




int aprsis_prepoll(int nfds, struct pollfd **fdsp, time_t *tout)
{
	int idx = 0; /* returns number of *fds filled.. */

	struct pollfd *fds = *fdsp;


	fds->fd = aprsis_sp; /* APRS-IS communicator server Sub-process */
	fds->events = POLLIN|POLLPRI;
	fds->revents = 0;
	
	/* We react only for reading, if write fails because the socket is
	   jammed,  that is just too bad... */

	--nfds;
	++fds;
	++idx;

	*fdsp = fds;

	return idx;

}

static int aprsis_comssockread(int fd)
{
	int i;
	char buf[10000];

	i = recv(fd, buf, sizeof(buf), 0);
	if (i == 0) return 0;

	/* TODO: do something with the data ?
	   A receive-only iGate does nothing, but Rx/Tx would do... */

	return 1;
}


int aprsis_postpoll(int nfds, struct pollfd *fds)
{
	int i;

	for (i = 0; i < nfds; ++i, ++fds) {
	  if (fds->fd == aprsis_sp) {
	    /* This is APRS-IS communicator subprocess socket,
	       and we may have some results.. */
	    
	    if (fds->revents & (POLLERR | POLLHUP)) { /* Errors ? */
	    close_sockaprsis:;
	      printf("APRS-IS coms subprocess socket failure from main program side!\n");
	      continue;
	    }

	    if (fds->revents & POLLIN) {  /* Ready for reading */
	      i = aprsis_comssockread(fds->fd);
	      if (i == 0) /* EOF ! */
		goto close_sockaprsis;
	      if (i < 0) continue;
	    }
	  }
	} /* .. for .. nfds .. */
	return 1; /* there was something we did, maybe.. */
}
