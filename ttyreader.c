/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2012                            *
 *                                                                  *
 * **************************************************************** */

#define _SVID_SOURCE 1

#include "aprx.h"
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

extern void tv_timeradd_millis(struct timeval *res, struct timeval *a, int millis);
extern int  tv_timerdelta_millis(struct timeval *_now, struct timeval *_target);
extern int  tv_timercmp(struct timeval *a, struct timeval *b);



/* The ttyreader does read TTY ports into a big buffer, and then from there
   to packet frames depending on what is attached...  */


static struct serialport **ttys;
static int ttycount;		/* How many are defined ? */

#define TTY_OPEN_RETRY_DELAY_SECS 30

static int poll_millis;         /* milliseconds (0 = none.)             */
static struct timeval poll_millis_tv;


void hexdumpfp(FILE *fp, const uint8_t *buf, const int len, int axaddr)
{
	int i, j;
	for (i = 0, j=1; i < len; ++i,++j) {
	  int c = buf[i] & 0xFF;
	  fprintf(fp, "%02x", c);
	  if (j < 8)
	    fputc(' ',fp);
	  else {
	    fputc('|',fp);
	    j = 0;
	  }
	}
	fprintf(fp, " = ");
	for (i = 0, j = 1; i < len; ++i,++j) {
	  int c = buf[i] & 0xFF;
	  if ((c & 0x81) == 0x80 && (i < 8)) {
	    // Auto-trigger AX.25 address plaintext converting
	    axaddr = 1;
	  }
	  if (axaddr && ((c & 0x01) == 1) && i > 3) {
	    // Definitely not AX.25 address anymore..
	    axaddr = 0;
	  }
	  if (axaddr) {
	    // Shifted AX.25 address byte?
	    c >>= 1;
	  }
	  if (c < 0x20 || c > 0x7E)
	    c = '.';
	  fputc(c, fp);
	  if (j >= 8) {
	    fputc('|',fp);
	    j = 0;
	  }
	}
}


/*
 *  ttyreader_getc()  -- pick one char ( >= 0 ) out of input buffer, or -1 if out of buffer
 */
int ttyreader_getc(struct serialport *S)
{
	if (S->rdcursor >= S->rdlen) {	/* Out of data ? */
		if (S->rdcursor)
			S->rdcursor = S->rdlen = 0;
		/* printf("-\n"); */
		return -1;
	}

	/* printf(" %02X", 0xFF & S->rdbuf[S->rdcursor++]); */

	return (0xFF & S->rdbuf[S->rdcursor++]);
}



/*
 *  ttyreader_pulltnc2()  --  process a line of text by calling
 *				TNC2 UI Monitor analyzer
 */

static int ttyreader_pulltnc2(struct serialport *S)
{
	const uint8_t *p;
	int addrlen = 0;
	p = memchr(S->rdline, ':', S->rdlinelen);
	if (p != NULL)
	  addrlen = (int)(p - S->rdline);

	erlang_add(S->ttycallsign[0], ERLANG_RX, S->rdlinelen, 1);	/* Account one packet */

	/* Send the frame to internal AX.25 network */
	/* netax25_sendax25_tnc2(S->rdline, S->rdlinelen); */

#ifndef DISABLE_IGATE
	/* S->rdline[] has text line without line ending CR/LF chars   */
	igate_to_aprsis(S->ttycallsign[0], 0, (char *) (S->rdline), addrlen, S->rdlinelen, 0, 1);
#endif

	return 0;
}

#if 0
/*
 * ttyreader_pullaea()  --  process a line of text by calling
 * 			    AEA MONITOR 1 analyzer
 */

static int ttyreader_pullaea(struct serialport *S)
{
	int i;

	if (S->rdline[S->rdlinelen - 1] == ':') {
		/* Could this be the AX25 header ? */
		char *s = strchr(S->rdline, '>');
		if (s) {
			/* Ah yes, it well could be.. */
			strcpy(S->rdline2, S->rdline);
			return;
		}
	}

	/* FIXME: re-arrange the  S->rdline2  contained AX25 address tokens 
	   and flags..

	   perl code:
	   @addrs = split('>', $rdline2);
	   $out = shift @addrs; # pop first token in sequence
	   $out .= '>';
	   $out .= pop @addrs;  # pop last token in sequence
	   foreach $a (@addrs) { # rest of the tokens in sequence, if any
	   $out .= ',' . $a;
	   }
	   # now $out has address data in TNC2 sequence.
	 */

	/* printf("%s%s\n", S->rdline2, S->rdline); fflush(stdout); */

	return 0;
}
#endif


/*
 *  ttyreader_pulltext()  -- process a line of text from the serial port..
 */

static int ttyreader_pulltext(struct serialport *S)
{
	int c;

	time_t rdtime = S->rdline_time;
	if (rdtime+2 < now.tv_sec) {
		// A timeout has happen? Either data is added constantly, or
		// nothing was received from TEXT datastream for couple seconds!
		S->rdlinelen = 0;
		// S->kissstate = KISSSTATE_SYNCHUNT;
	}
	S->rdline_time = now.tv_sec;

	for (;;) {

		c = ttyreader_getc(S);
		if (c < 0)
			return c;	/* Out of input.. */

		/* S->kissstate != 0: read data into S->rdline,
		   == 0: discard data until CR|LF.
		   Zero-size read line is discarded as well
		   (only CR|LF on input frame)  */

		if (S->kissstate == KISSSTATE_SYNCHUNT) {
			/* Looking for CR or LF.. */
			if (c == '\n' || c == '\r')
				S->kissstate = KISSSTATE_COLLECTING;

			S->rdlinelen = 0;
			continue;
		}

		/* Now: (S->kissstate != KISSSTATE_SYNCHUNT)  */

		if (c == '\n' || c == '\r') {
			/* End of line seen! */
			if (S->rdlinelen > 0) {

				/* Non-zero-size string, put terminating 0 byte on it. */
				S->rdline[S->rdlinelen] = 0;

				/* .. and process it depending ..  */

				if (S->linetype == LINETYPE_TNC2) {
					ttyreader_pulltnc2(S);
#if 0
				} else {	/* .. it is LINETYPE_AEA ? */
					ttyreader_pullaea(S);
#endif
				}
			}
			S->rdlinelen = 0;
			continue;
		}

		/* Now place the char in the linebuffer, if there is space.. */
		if (S->rdlinelen >= (sizeof(S->rdline) - 3)) {	/* Too long !  Way too long ! */
			S->kissstate = KISSSTATE_SYNCHUNT;	/* Sigh.. discard it. */
			S->rdlinelen = 0;
			continue;
		}

		/* Put it on line store: */
		S->rdline[S->rdlinelen++] = c;

	}			/* .. input loop */

	return 0;		/* not reached */
}



/*
 *  ttyreader_linewrite()  -- write out buffered data
 */
void ttyreader_linewrite(struct serialport *S)
{
	int i, len;

	if ((S->wrlen == 0) || (S->wrlen > 0 && S->wrcursor >= S->wrlen)) {
		S->wrlen = S->wrcursor = 0;	/* already all written */
		return;
	}

	/* Now there is some data in between wrcursor and wrlen */

	len = S->wrlen - S->wrcursor;
	if (len > 0)
	  i = write(S->fd, S->wrbuf + S->wrcursor, len);
	else
	  i = 0;
	if (i > 0) {		/* wrote something */
		S->wrcursor += i;
		len = S->wrlen - S->wrcursor;
		if (len == 0) {
			S->wrcursor = S->wrlen = 0;	/* wrote all ! */
		} else {
			/* compact the buffer a bit */
			memcpy(S->wrbuf, S->wrbuf + S->wrcursor, len);
			S->wrcursor = 0;
			S->wrlen = len;
		}
	}
}


/*
 *  ttyreader_lineread()  --  read what there is into our buffer,
 *			      and process the buffer..
 */

static void ttyreader_lineread(struct serialport *S)
{
	int i;

	int rdspace = sizeof(S->rdbuf) - S->rdlen;

	if (S->rdcursor > 0) {
		/* Read-out cursor is not at block beginning,
		   is there unread data too ?  */
		if (S->rdlen > S->rdcursor) {
			/* Uh..  lets move buffer down a bit,
			   to make room for more to the end.. */
			memcpy(S->rdbuf, S->rdbuf + S->rdcursor,
			       S->rdlen - S->rdcursor);
			S->rdlen = S->rdlen - S->rdcursor;
		} else
			S->rdlen = 0;	/* all processed, mark its size zero */
		/* Cursor to zero, rdspace recalculated */
		S->rdcursor = 0;

		/* recalculate */
		rdspace = sizeof(S->rdbuf) - S->rdlen;
	}

	if (rdspace > 0) {	/* We have room to read into.. */
		i = read(S->fd, S->rdbuf + S->rdlen, rdspace);
		if (i == 0) {	/* EOF ?  USB unplugged ? */
			close(S->fd);
			S->fd = -1;
			S->wait_until = now.tv_sec + TTY_OPEN_RETRY_DELAY_SECS;
			if (debug)
				printf("%ld\tTTY %s EOF - CLOSED, WAITING %d SECS\n", now.tv_sec, S->ttyname, TTY_OPEN_RETRY_DELAY_SECS);
			return;
		}
		if (i < 0)	/* EAGAIN or whatever.. */
			return;

		/* Some data has been accumulated ! */
		if (debug > 2) {
		  printf("%ld\tTTY %s: read() frame: ", now.tv_sec, S->ttyname);
		  hexdumpfp(stdout, S->rdbuf+S->rdlen, i, 1);
		  printf("\n");
		}
                
		S->rdlen += i;
		S->last_read_something = now.tv_sec;
	}

	/* Done reading, maybe.  Now processing.
	   The pullXX does read up all input, and does
	   however many frames there are in, and pauses
	   when there is no enough input data for a full
	   frame/line/whatever.
	 */

	if (S->linetype == LINETYPE_KISS ||
	    S->linetype == LINETYPE_KISSFLEXNET ||
	    S->linetype == LINETYPE_KISSBPQCRC ||
	    S->linetype == LINETYPE_KISSSMACK) {

		kiss_pullkiss(S);


#ifndef DISABLE_IGATE
	} else if (S->linetype == LINETYPE_DPRSGW) {

		dprsgw_pulldprs(S);

#endif
	} else if (S->linetype == LINETYPE_TNC2
#if 0
		   || S->linetype == LINETYPE_AEA
#endif
		   ) {

		ttyreader_pulltext(S);

	} else {
		close(S->fd);	/* Urgh ?? Bad linetype value ?? */
		S->fd = -1;
		S->wait_until = now.tv_sec + TTY_OPEN_RETRY_DELAY_SECS;
	}

	/* Consumed something, and our read cursor is not in the beginning ? */
	if (S->rdcursor > 0 && S->rdcursor < S->rdlen) {
		/* Compact the input buffer! */
		memcpy(S->rdbuf, S->rdbuf + S->rdcursor,
		       S->rdlen - S->rdcursor);
	}
	S->rdlen -= S->rdcursor;
	S->rdcursor = 0;
}


/*
 * ttyreader_linesetup()  --  open and configure the serial port
 */

static void ttyreader_linesetup(struct serialport *S)
{
	int i;

	S->wait_until = 0;	/* Zero it just to be safe */

	S->wrlen = S->wrcursor = 0;	/* init them at first */

	if (memcmp(S->ttyname, "tcp!", 4) != 0) {

		S->fd = open(S->ttyname, O_RDWR | O_NOCTTY, 0);

		if (debug)
			printf("%ld\tTTY %s OPEN - fd=%d - ",
			       now.tv_sec, S->ttyname, S->fd);
		if (S->fd < 0) {	/* Urgh.. an error.. */
			S->wait_until = now.tv_sec + TTY_OPEN_RETRY_DELAY_SECS;
			if (debug)
				printf("FAILED, WAITING %d SECS\n",
				       TTY_OPEN_RETRY_DELAY_SECS);
			return;
		}
		if (debug)
			printf("OK\n");

		/* Set attributes */
		aprx_cfmakeraw(&S->tio, 1); /* hw-flow on */
		i = tcsetattr(S->fd, TCSAFLUSH, &S->tio);

		if (i < 0) {
			if (debug)
			  printf("%ld\tERROR: TCSETATTR failed; errno=%d\n",
				 now.tv_sec, errno);
			close(S->fd);
			S->fd = -1;
			S->wait_until = now.tv_sec + TTY_OPEN_RETRY_DELAY_SECS;
			return;
		}
		/* FIXME: ??  Set baud-rates ?
		   Used system (Linux) has them in   'struct termios'  so they
		   are now set, but other systems may have different ways..
		 */

		/* Flush buffers once again. */
		i = tcflush(S->fd, TCIOFLUSH);

		/* change the file handle to non-blocking */
		fd_nonblockingmode(S->fd);

		for (i = 0; i < 16; ++i) {
		  if (S->initstring[i] != NULL) {
		    memcpy(S->wrbuf + S->wrlen, S->initstring[i], S->initlen[i]);
		    S->wrlen += S->initlen[i];
		  }
		}

		/* Flush it out..  and if not successfull,
		   poll(2) will take care of it soon enough.. */
		ttyreader_linewrite(S);

	} else {		/* socket connection to remote TTY.. */
		/*   "tcp!hostname-or-ip!port!opt-parameters" */
		char *par = strdup(S->ttyname);
		char *host = NULL, *port = NULL, *opts = NULL;
		struct addrinfo req, *ai;
		int i;

		if (debug)
			printf("socket connect() preparing: %s\n", par);

		for (;;) {
			host = strchr(par, '!');
			if (host)
				++host;
			else
				break;	/* Found no '!' ! */
			port = strchr(host, '!');
			if (port)
				*port++ = 0;
			else
				break;	/* Found no '!' ! */
			opts = strchr(port, '!');
			if (opts)
				*opts++ = 0;
			break;
		}

		if (!port) {
			/* Still error condition.. no port data */
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

		i = getaddrinfo(host, port, &req, &ai);

		if (ai) {
			S->fd = socket(ai->ai_family, SOCK_STREAM, 0);
			if (S->fd >= 0) {

				fd_nonblockingmode(S->fd);

				i = connect(S->fd, ai->ai_addr,
					    ai->ai_addrlen);
				if ((i != 0) && (errno != EINPROGRESS)) {
					/* non-blocking connect() yields EINPROGRESS,
					   anything else and we fail entirely...      */
					if (debug)
						printf("ttyreader socket connect call failed: %d : %s\n", errno, strerror(errno));
					close(S->fd);
					S->fd = -1;
				}
			}

			freeaddrinfo(ai);
                }
		free(par);
	}

	S->last_read_something = now.tv_sec;	/* mark the timeout for future.. */

	S->rdlen = S->rdcursor = S->rdlinelen = 0;
	S->kissstate = 0;	/* Zero it, whatever protocol we actually use
				   will consider it as 'hunt for sync' state. */

	memset( S->smack_probe, 0, sizeof(S->smack_probe) );
	S->smack_subids = 0;
}

/*
 *  ttyreader_init()
 */

void ttyreader_init(void)
{
	/* nothing.. */
}



/*
 *  ttyreader_prepoll()  --  prepare system for next round of polling
 */

int ttyreader_prepoll(struct aprxpolls *app)
{
	int idx = 0;		/* returns number of *fds filled.. */
	int i;
	struct serialport *S;
	struct pollfd *pfd;

        // if (debug) printf("ttyreader_prepoll() %d\n", poll_millis);
	for (i = 0; i < ttycount; ++i) {
		S = ttys[i];
		if (!S->ttyname)
			continue;	/* No name, no look... */


#if 0 // occasional debug mode without real hardware at hand
                if (poll_millis > 0) {
			int deltams = tv_timerdelta_millis(&now, &poll_millis_tv);
                	app->next_timeout = now.tv_sec;
                        if (deltams > 0) {
                          app->next_timeout_millisecs = deltams;
                        } else {
                          app->next_timeout_millisecs = 0;
                        }
                        if (debug) printf("%d.%06d .. defining %d ms KISS POLL\n", now.tv_sec, now.tv_usec, poll_millis);
                }
#endif

		if (S->fd < 0) {
			/* Not an open TTY, but perhaps waiting ? */
			if ((S->wait_until != 0) && (S->wait_until > now.tv_sec)) {
				/* .. waiting for future! */
				if (app->next_timeout > S->wait_until)
					app->next_timeout = S->wait_until;
				/* .. but only until our timeout,
				   if it is sooner than global one. */
				continue;	/* Waiting on this one.. */
			}

			/* Waiting or not, FD is not open, and deadline is past.
			   Lets try to open! */

			ttyreader_linesetup(S);

		}
		/* .. No open FD */
		/* Still no open FD ? */
		if (S->fd < 0)
			continue;

		/* FD is open, check read/idle timeout ... */
		if ((S->read_timeout > 0) &&
		    (now.tv_sec > (S->last_read_something + S->read_timeout))) {
			if (debug)
			  printf("%ld\tRead timeout on %s; %d seconds w/o input. fd=%d\n",
				 now.tv_sec, S->ttyname, S->read_timeout, S->fd);
			close(S->fd);	/* Close and mark for re-open */
			S->fd = -1;
			S->wait_until = now.tv_sec + TTY_OPEN_RETRY_DELAY_SECS;
			continue;
		}


                if (poll_millis > 0) {
			int deltams = tv_timerdelta_millis(&now, &poll_millis_tv);
                	app->next_timeout = now.tv_sec;
                        if (deltams > 0) {
                          app->next_timeout_millisecs = deltams;
                        } else {
                          app->next_timeout_millisecs = 0;
                          if (poll_millis_tv.tv_sec == 0) {
                            poll_millis_tv = now;
                          }
                          tv_timeradd_millis(&poll_millis_tv, &poll_millis_tv, poll_millis);
                        }
                        if (debug) printf("%d.%06d .. defining %d ms KISS POLL\n", now.tv_sec, now.tv_usec, poll_millis);
                }

		/* FD is open, lets mark it for poll read.. */
		pfd = aprxpolls_new(app);
		pfd->fd = S->fd;
		pfd->events = POLLIN | POLLPRI;
		pfd->revents = 0;
		if (S->wrlen > 0 && S->wrlen > S->wrcursor)
			pfd->events |= POLLOUT;

		++idx;
	}
	return idx;
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
	*res = *a;
        int usec = (int)(res->tv_usec) + millis * 1000;
        if (usec >= 1000000) {
          int dsec = (usec / 1000000);
          res->tv_sec += dsec;
          usec %= 1000000;
          if (debug>3) printf("tv_timeadd_millis() dsec=%d dusec=%d\n",dsec, usec);
        }
        res->tv_usec = usec;
}

int tv_timercmp(struct timeval *a, struct timeval *b)
{
	if (a->tv_sec < b->tv_sec) {
          return -1;
        }
	if (a->tv_sec > b->tv_sec) {
          return 1;
        }
        if (a->tv_usec < b->tv_usec) {
          return -1;
        }
        if (a->tv_usec > b->tv_usec) {
          return 1;
        }
        return 0; // equals!
}

/*
 *  ttyreader_postpoll()  -- Done polling, what happened ?
 */

int ttyreader_postpoll(struct aprxpolls *app)
{
	int idx, i;

	struct serialport *S;
	struct pollfd *P;

        // if (debug) printf("ttyreader_postpoll()\n");

	for (idx = 0, P = app->polls; idx < app->pollcount; ++idx, ++P) {

        	// Are we operating in active KISS polling mode?
        	if (poll_millis > 0) {
			for (i = 0; i < ttycount; ++i) {
                               	S = ttys[i];

#if 0  // occasional debug mode without real hardware at hand
                                if (tv_timercmp(&poll_millis_tv, &now) <= 0) {
	                                // Poll interval gone, time for next active POLL request!
                                        kiss_poll(S);
                                        if (poll_millis_tv.tv_sec == 0) {
                                          poll_millis_tv = now;
                                        }
                                        tv_timeradd_millis(&poll_millis_tv, &poll_millis_tv, poll_millis);
                                }
#endif

                                if (S->fd != P->fd)
                                	continue;	/* Not this one ? */
                                if (S->fd < 0)
                                	continue;	/* Not this one ? */

                                if (!(S->linetype == LINETYPE_KISS ||
                                      S->linetype == LINETYPE_KISSFLEXNET ||
                                      S->linetype == LINETYPE_KISSBPQCRC ||
                                      S->linetype == LINETYPE_KISSSMACK)) {
                                        // Not a KISS line..
                                        continue;
                                }
                                if (tv_timercmp(&poll_millis_tv, &now) <= 0) {
	                                // Poll interval gone, time for next active POLL request!
                                        kiss_poll(S);
                                        if (poll_millis_tv.tv_sec == 0) {
                                          poll_millis_tv = now;
                                        }
                                        tv_timeradd_millis(&poll_millis_tv, &poll_millis_tv, poll_millis);
                                }
                        }
                }

		if (!(P->revents & (POLLIN | POLLPRI | POLLERR | POLLHUP)))
			continue;	/* No read event we are interested in... */

		for (i = 0; i < ttycount; ++i) {
			S = ttys[i];
			if (S->fd != P->fd)
				continue;	/* Not this one ? */
			/* It is this one! */

			if (P->revents & POLLOUT)
				ttyreader_linewrite(S);

			ttyreader_lineread(S);
		}
	}

	return 0;
}

/*
 * Make a pre-existing termios structure into "raw" mode: character-at-a-time
 * mode with no characters interpreted, 8-bit data path.
 */
void
aprx_cfmakeraw(t, f)
	struct termios *t;
{

	t->c_iflag &= ~(IMAXBEL|IXOFF|INPCK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON|IGNPAR);
	t->c_iflag |= IGNBRK;

	t->c_oflag &= ~OPOST;
	if (f) {
	  t->c_oflag |= CRTSCTS;
	} else {
	  t->c_oflag &= ~CRTSCTS;
	}

	t->c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL|ICANON|ISIG|IEXTEN|NOFLSH|TOSTOP|PENDIN);
	t->c_cflag &= ~(CSIZE|PARENB);
	t->c_cflag |= CS8|CREAD;
	t->c_cc[VMIN] = 80;
	t->c_cc[VTIME] = 3;
}

struct serialport *ttyreader_new(void)
{
	struct serialport *tty = malloc(sizeof(*tty));
	int baud = B1200;

	memset(tty, 0, sizeof(*tty));

	tty->fd = -1;
	tty->wait_until = now.tv_sec - 1;	/* begin opening immediately */
	tty->last_read_something = now.tv_sec;	/* well, not really.. */
	tty->linetype  = LINETYPE_KISS;	/* default */
	tty->kissstate = KISSSTATE_SYNCHUNT;

	tty->ttyname = NULL;


	/* setup termios parameters for this line.. */
	aprx_cfmakeraw(&tty->tio, 0);
	tty->tio.c_cc[VMIN] = 80;	/* pick at least one char .. */
	tty->tio.c_cc[VTIME] = 3;	/* 0.3 seconds timeout - 36 chars @ 1200 baud */
	tty->tio.c_cflag |= (CREAD | CLOCAL);

	cfsetispeed(&tty->tio, baud);
	cfsetospeed(&tty->tio, baud);

	return tty;
}

/*
 * Parse tty related parameters, return 0 for OK, 1 for error
 */
int ttyreader_parse_ttyparams(struct configfile *cf, struct serialport *tty, char *str)
{
	int i;
	speed_t baud;
	int tncid   = 0;
	char *param1 = 0;
        int has_fault = 0;

	/* FIXME: analyze correct serial port data and parity format settings,
	   now hardwired to 8-n-1 -- does not work without for KISS anyway.. */
	
	config_STRLOWER(str);	/* until end of line */

	/* Optional parameters */
	while (*str != 0) {
		param1 = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		if (debug)
		  printf(" .. param='%s'",param1);

		/* See if it is baud-rate ? */
		i = atol(param1);	/* serial port speed - baud rate */
		baud = B1200;
		switch (i) {
		case 1200:
			baud = B1200;
			break;
#ifdef B1800
		case 1800:
			baud = B1800;
			break;
#endif
		case 2400:
			baud = B2400;
			break;
		case 4800:
			baud = B4800;
			break;
		case 9600:
			baud = B9600;
			break;
#ifdef B19200
		case 19200:
			baud = B19200;
			break;
#endif
#ifdef B38400
		case 38400:
			baud = B38400;
			break;
#endif
#ifdef B57600
		case 57600:
			baud = B57600;
			break;
#endif
#ifdef B115200
		case 115200:
			baud = B115200;
			break;
#endif
#ifdef B230400
		case B230400:
			baud = B230400;
			break;
#endif
#ifdef B460800
		case 460800:
			baud = B460800;
			break;
#endif
#ifdef B500000
		case 500000:
			baud = B500000;
			break;
#endif
#ifdef B576000
		case 576000:
			baud = B576000;
			break;
#endif
		default:
			i = -1;
			break;
		}
		if (baud != B1200) {
			cfsetispeed(&tty->tio, baud);
			cfsetospeed(&tty->tio, baud);
		}

		/* Note:  param1  is now lower-case string */

		if (i > 0) {
			;
		} else if (strcmp(param1, "8n1") == 0) {
			/* default behaviour, ignore */
		} else if (strcmp(param1, "kiss") == 0) {
			tty->linetype = LINETYPE_KISS;	/* plain basic KISS */

		} else if (strcmp(param1, "xorsum") == 0) {
			tty->linetype = LINETYPE_KISSBPQCRC;	/* KISS with BPQ "CRC" */
		} else if (strcmp(param1, "xkiss") == 0) {
			tty->linetype = LINETYPE_KISSBPQCRC;	/* KISS with BPQ "CRC" */
		} else if (strcmp(param1, "bpqcrc") == 0) {
			tty->linetype = LINETYPE_KISSBPQCRC;	/* KISS with BPQ "CRC" */

		} else if (strcmp(param1, "flexnet") == 0) {
			tty->linetype = LINETYPE_KISSFLEXNET;	/* KISS with FLEXNET's CRC16 */
		} else if (strcmp(param1, "smack") == 0) {
			tty->linetype = LINETYPE_KISSSMACK;	/* KISS with SMACK / CRC16 */
		} else if (strcmp(param1, "crc16") == 0) {
			tty->linetype = LINETYPE_KISSSMACK;	/* KISS with SMACK / CRC16 */

		} else if (strcmp(param1, "poll") == 0) {
			/* FIXME: Some systems want polling... */

		} else if (strcmp(param1, "callsign") == 0 ||
			   strcmp(param1, "alias") == 0) {
			param1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
			config_STRUPPER(param1);
			tty->ttycallsign[tncid] = strdup(param1);

#ifdef PF_AX25	/* PF_AX25 exists -- highly likely a Linux system ! */
			tty->netax25[tncid] = netax25_open(param1);
#endif

			/* Use side-effect: this defines the tty into
			   erlang accounting */

			erlang_set(param1, /* Heuristic constant for max channel capa.. */ (int) ((1200.0 * 60) / 8.2));

		} else if (strcmp(param1, "timeout") == 0) {
			param1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
			tty->read_timeout = atol(param1);

		} else if (strcmp(param1, "tncid") == 0) {
			param1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
			tncid = atoi(param1);
			if (tncid < 0 || tncid > 15) {
				tncid = 0;
                                printf("%s:%d TNCID value not in sanity range of 0 to 15: '%s'", cf->name, cf->linenum, param1);
                                has_fault = 1;
                        }

		} else if (strcmp(param1, "pollmillis") == 0) {
			param1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
			tty->poll_millis = atol(param1); // milliseconds
                        if (poll_millis == 0)
                          poll_millis = tty->poll_millis;
                        if (tty->poll_millis < poll_millis)
                          poll_millis = tty->poll_millis;
                        if (poll_millis < 1 || poll_millis > 10000) {
                          has_fault = 1;
                          printf("%s:%d POLLMILLIS value not in sanity range of 1 to 10 000: '%s'", cf->name, cf->linenum, param1);
                        } else {
                          if (debug)
                            printf(" .. pollmillis %d  -- polling interval\n", tty->poll_millis);
                        }

#ifndef DISABLE_IGATE
		} else if (strcmp(param1, "tnc2") == 0) {
			tty->linetype = LINETYPE_TNC2;	/* TNC2 monitor */

		} else if (strcmp(param1, "dprs") == 0) {
			tty->linetype = LINETYPE_DPRSGW;
#endif

		} else if (strcmp(param1, "initstring") == 0) {
			int parlen;
			param1 = str;
			str = config_SKIPTEXT(str, &parlen);
			str = config_SKIPSPACE(str);
			tty->initlen[tncid]    = parlen;
			tty->initstring[tncid] = malloc(parlen);
			memcpy(tty->initstring[tncid], param1, parlen);

			if (debug)
			  printf("initstring len=%d\n",parlen);
		} else {
		  printf("%s:%d ERROR: Unknown sub-keyword on a serial/tcp device configuration: '%s'\n",
			 cf->name, cf->linenum, param1);
                  has_fault = 1;
		}
	}
	if (debug) printf("\n");
	return has_fault;
}

void ttyreader_register(struct serialport *tty)
{
	/* Grow the array as is needed.. - this is array of pointers,
	   not array of blocks so that memory allocation does not
	   grow into way too big chunks. */
	ttys = realloc(ttys, sizeof(void *) * (ttycount + 1));
	ttys[ttycount++] = tty;
}

const char *ttyreader_serialcfg(struct configfile *cf, char *param1, char *str)
{				/* serialport /dev/ttyUSB123   19200  8n1   {KISS|TNC2|AEA|..}  */
	struct serialport *tty;

	/*
	   radio serial /dev/ttyUSB123  [19200 [8n1]]  KISS
	   radio tcp 12.34.56.78 4001 KISS

	 */

	if (*param1 == 0)
		return "Bad mode keyword";
	if (*str == 0)
		return "Bad tty-name/param";

	tty = ttyreader_new();
	ttyreader_register(tty);

	if (strcmp(param1, "serial") == 0) {
		/* New style! */
		free((char *) (tty->ttyname));

		param1 = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		tty->ttyname = strdup(param1);

		if (debug)
			printf(".. new style serial:  '%s' '%s'..\n",
			       tty->ttyname, str);

	} else if (strcmp(param1, "tcp") == 0) {
		/* New style! */
		int len;
		char *host, *port;

		free((char *) (tty->ttyname));

		host = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		port = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		if (debug)
			printf(".. new style tcp!:  '%s' '%s' '%s'..\n",
			       host, port, str);

		len = strlen(host) + strlen(port) + 8;

		tty->ttyname = malloc(len);
		sprintf((char *) (tty->ttyname), "tcp!%s!%s!", host, port);

	}

	if (ttyreader_parse_ttyparams( cf, tty, str))
	  return "Bad ttyparameters";

	return NULL; // All OK
}

