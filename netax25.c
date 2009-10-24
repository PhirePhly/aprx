/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * NETAX25:  Listen on (Linux) AX.25 socket and pick all AX.25      *
 *           data packets     ...    actually don't pick those      *
 *           that are going outwards.  All incoming ones do pick.   *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

#include <sys/socket.h>

#ifdef PF_AX25			/* PF_AX25 exists -- highly likely a Linux system ! */

#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <netax25/ax25.h>


#if defined(HAVE_OPENPTY)
#ifdef HAVE_PTY_H
#include <pty.h>
#endif

/*
 *  Talking to Linux kernel 2.6.x, using SMACK type frames
 *  on each configured serial port callsign -> ptymux 
 *  writer channel.  If system does not write correct SMACK
 *  frame on that KISS port for any number of reasons,
 *  including writing incompletely buffered data, then
 *  kernel will be able to notice that frame it received
 *  is not valid, and discard it.  (Maybe... P = 2^-16 to
 *  accepting of error frame in spite of these controls.)
 */

struct netax25_pty {
	int                          fd;
	const char                  *callsign;
	const struct aprx_interface *interface;
	struct sockaddr_ax25         ax25addr;
};

static void netax25_addttyport(const char *callsign,
			       const int masterfd, const int slavefd);

static const void* netax25_openpty(const char *mycall)
{
	int rc;
	int disc;
	struct termios tio;
	char devname[64];
	uint8_t ax25call[7];
	struct ifreq ifr;
	int fd = -1;
	struct netax25_pty *nax25;
	int pty_master, pty_slave;

	if (!mycall)
		return NULL;		/* No mycall, no ptys! */

	if (parse_ax25addr(ax25call, mycall, 0x60)) {
		// Not valid per AX.25 rules
	  if (debug)
	    printf(" netax25_openpty('%s') failed to parse the parameter string as valid AX.25 callsign. Not opening kernel pty.\n", mycall);
		return NULL;
	}

	rc = openpty(&pty_master, &pty_slave, devname, NULL, NULL);

	if (debug)
		printf("openpty() rc=%d name='%s' master=%d slave=%d\n",
		       rc, devname, pty_master, pty_slave);

	if (rc != 0 || pty_slave < 0) {
	      error_exit:;
		if (pty_master >= 0)
			close(pty_master);
		pty_master = -1;
		if (pty_slave >= 0)
			close(pty_slave);
		pty_slave = -1;
		if (fd >= 0)
			close(fd);
		if (debug)
		  printf("netax25_openpty() error exit.\n");
		return NULL;		/* D'uh.. */
	}

	nax25 = calloc( 1,sizeof(*nax25) );
	nax25->fd       = pty_master;
	nax25->callsign = mycall;

	nax25->ax25addr.sax25_family = PF_AX25;
	nax25->ax25addr.sax25_ndigis = 0;
	memcpy(&nax25->ax25addr.sax25_call, ax25call, sizeof(ax25call));

	/* setup termios parameters for this line.. */
	aprx_cfmakeraw(&tio, 0);
	tio.c_cc[VMIN] = 1;	/* pick at least one char .. */
	tio.c_cc[VTIME] = 3;	/* 0.3 seconds timeout - 36 chars @ 1200 baud */
	tio.c_cflag |= (CREAD | CLOCAL);
	cfsetispeed(&tio, B38400);	/* Pseudo-tty -- pseudo speed */
	cfsetospeed(&tio, B38400);
	rc = tcsetattr(pty_slave, TCSANOW, &tio);
	if (rc < 0)
		goto error_exit;

	/* The pty_slave will get N_AX25 discipline attached on itself.. */
	disc = N_AX25;
	rc = ioctl(pty_slave, TIOCSETD, &disc);
	if (rc < 0)
		goto error_exit;

	rc = ioctl(pty_slave, SIOCGIFNAME, devname);
	if (rc < 0)
		goto error_exit;

	/* Convert mycall[] to AX.25 format callsign */
	rc = ioctl(pty_slave, SIOCSIFHWADDR, ax25call);
	if (rc < 0)
		goto error_exit;

	/* Now set encapsulation.. */
	disc = 4;
	rc = ioctl(pty_slave, SIOCSIFENCAP, &disc);
	if (rc < 0)
		goto error_exit;

	/* Then final tricks to start the interface... */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (rc < 0)
		goto error_exit;

	strcpy(ifr.ifr_name, devname);

	ifr.ifr_mtu = 512;
	rc = ioctl(fd, SIOCSIFMTU, &ifr);
	if (rc < 0)
		goto error_exit;

	ifr.ifr_flags = IFF_UP | IFF_RUNNING | IFF_NOARP;
	rc = ioctl(fd, SIOCSIFFLAGS, &ifr);
	if (rc < 0)
		goto error_exit;

	close(fd);

	/* OK, we write and read on pty_master, the pty_slave is now
	   attached on kernel side AX.25 interface with call: mycall */

	netax25_addttyport( mycall, pty_master, pty_slave );

	return (void*) nax25;
}

void netax25_sendax25(const void *nax25p, const void *ax25, int ax25len)
{
	int rc, p;
	uint8_t ax25buf[2100];
	const struct netax25_pty *nax25 = nax25p;

	/* kissencoder() takes AX.25 frame, and adds framing + cmd-byte */
	rc = kissencoder(ax25buf, sizeof(ax25buf), ax25, ax25len, 0x80);
	if (rc < 0)
		return;
	ax25len = rc;

	/* Try to write it to the PTY */
	p = 0;
	rc = write(nax25->fd, ax25buf + p, ax25len - p);
	if (rc < 0) rc = 0; // error hickup..
	p += rc; rc = 0;
	if (p < ax25len) { // something left unwritten
		rc = write(nax25->fd, ax25buf + p, ax25len - p);
		if (rc < 0) rc = 0; // error hickup..
	}
	p += rc; rc = 0;
	if (p < ax25len) { // something left unwritten
		rc = write(nax25->fd, ax25buf + p, ax25len - p);
		if (rc < 0) rc = 0; // error hickup..
	}
	p += rc; rc = 0;
	// Now it either succeeded, or it failed.
	// in both cases we give up on this frame.
	if (p < ax25len) {
	  if (aprxlogfile) {
	    FILE *fp = fopen(aprxlogfile, "a");
	    if (fp) {
	      char timebuf[60];
	      struct tm *t = gmtime(&now);
	      strftime(timebuf, 60, "%Y-%m-%d %H:%M:%S", t);
	      fprintf(fp, "%s netax25_sendax25(%s,len=%d) wrote %d bytes\n", timebuf, nax25->callsign, ax25len, p);
	      fclose(fp);
	    }
	  }
	}
}

#else
static const void* netax25_openpty(const char *mycall)
{
	return null;
}

void netax25_sendax25(const void *nax25, const void *ax25, int ax25len)
{
}
#endif				/* HAVE_OPENPTY */

static int rx_socket = -1;

static struct netax25_pty **ax25rxports;
static int                  ax25rxportscount;

static char **ax25ttyports;
static int   *ax25ttyfds;
static int    ax25ttyportscount;

/* config interface:  ax25-rxport: callsign */
void *netax25_addrxport(const char *callsign, const struct aprx_interface *interface)
{
	struct netax25_pty *nax25p = malloc(sizeof(*nax25p));
	memset(nax25p, 0, sizeof(*nax25p));

	nax25p->fd        = -1;
	nax25p->interface = interface;
	nax25p->ax25addr.sax25_family = PF_AX25;
	nax25p->ax25addr.sax25_ndigis = 0;

	if (interface == NULL) {  // Old config style
	  if (parse_ax25addr((uint8_t*)&nax25p->ax25addr.sax25_call, callsign, 0x60)) {
	    // Not valid per AX.25 rules
	    return NULL;
	  }
	  nax25p->callsign  = strdup(callsign);
	} else {  // new config fule
	  memcpy(&nax25p->ax25addr.sax25_call, interface->ax25call, sizeof(interface->ax25call));
	  nax25p->callsign  = interface->callsign;
	}

	ax25rxports = realloc(ax25rxports,
			      sizeof(struct netax25_pty*) * (ax25rxportscount + 1));
	ax25rxports[ax25rxportscount++] = nax25p;

	return nax25p;
}

static void netax25_addttyport(const char *callsign,
			       const int masterfd, const int slavefd)
{
	ax25ttyports = realloc(ax25ttyports,
			       sizeof(void *) * (ax25ttyportscount + 1));
	ax25ttyfds   = realloc(ax25ttyfds,
			       sizeof(int) * (ax25ttyportscount + 1));
	ax25ttyports[ax25ttyportscount] = strdup(callsign);
	ax25ttyfds  [ax25ttyportscount] = masterfd; /* slavefd forgotten */
	++ax25ttyportscount;
}

static int is_ax25ttyport(const char *callsign)
{
	int i;
	for (i = 0; i < ax25ttyportscount; ++i) {
		if (strcmp(callsign,ax25ttyports[i]) == 0)
			return 1; // Have match
	}
	return 0; // No match
}

/* Nothing much in early init */
void netax25_init(void)
{
}

/* .. but all things in late start.. */
void netax25_start(void)
{
	int i;
	int rx_protocol;

	rx_socket = -1;			/* Initialize for early bail-out  */

	if (!ax25rxports) return;	/* No configured receiver ports.
					   No receiver socket creation. */


	rx_protocol = ETH_P_AX25;	/* Choosing ETH_P_ALL would pick also
					   outbound packets, but also all of
					   the ethernet traffic..  ETH_P_AX25
					   picks only inbound-at-ax25-devices
					   ..packets.  */

	rx_socket = socket(PF_PACKET, SOCK_PACKET, htons(rx_protocol));

	if (rx_socket < 0)
		rx_socket =
			socket(PF_PACKET, SOCK_PACKET, htons(rx_protocol));

	if (rx_socket < 0) {
		i = errno;
		/* D'uh..  could not open it, report and leave it at that. */
		if (debug)
			fprintf(stderr,
				"aprx: Could not open socket(PF_PACKET,SOCK_PACKET,ETH_P_AX25) for listening.  Errno=%d (%s)"
				" -- not a big deal unless you want to receive via AX.25 sockets.\n",
				i, strerror(i));
		return;
	}

	if (rx_socket >= 0)
		fd_nonblockingmode(rx_socket);
}


/* .. but all things in late start.. */
const void* netax25_open(const char *ifcallsign)
{
	return netax25_openpty(ifcallsign);
}

int netax25_prepoll(struct aprxpolls *app)
{
	struct pollfd *pfd;
	int i;

	if (rx_socket >= 0) {
		/* FD is open, lets mark it for poll read.. */
		pfd = aprxpolls_new(app);
		pfd->fd = rx_socket;
		pfd->events = POLLIN | POLLPRI;
		pfd->revents = 0;
	}

	/* read from PTY masters */
	for (i = 0; i < ax25ttyportscount; ++i) {
		if (ax25ttyfds[i] >= 0) {
		  pfd = aprxpolls_new(app);
		  pfd->fd = ax25ttyfds[i];
		  pfd->events = POLLIN | POLLPRI;
		  pfd->revents = 0;
		}
	}

	return 1;
}

static int ax25_fmtaddress(char *dest, const uint8_t *src)
{
	int i, c;

	/* We really should verify that  */

	/* 6 bytes of station callsigns in shifted ASCII format.. */
	for (i = 0; i < 6; ++i, ++src) {
		c = *src;
		if (c & 1)
			return (-(int) (c));	/* Bad address-end flag ? */

		/* Don't copy spaces or 0 bytes */
		if (c != 0 && c != 0x40)
			*dest++ = c >> 1;
	}
	/* 7th byte carries SSID et.al. bits */
	c = *src;
	if ((c >> 1) % 16) {	/* don't print SSID==0 value */
		dest += sprintf(dest, "-%d", (c >> 1) % 16);
	}

	*dest = 0;

	return c;
}


static int rxsock_read( const int fd )
{
	// struct msghdr msgh;
	// union {
	  struct sockaddr sa;
	//  struct full_sockaddr_ax25 sax;
	//  uint8_t sab[200];
	// } sa;
	// struct iovec    iov[1];
	// uint8_t msgbuf[1000];
	uint8_t rxbuf[3000];

	struct ifreq ifr;
	socklen_t asize;
	int rcvlen;
	char ifaddress[12]; /* max size: 6+1+2 chars */

	const struct aprx_interface *aif = NULL;

	/*
	msgh.msg_name       = & sa;
	msgh.msg_namelen    = sizeof(sa);
	msgh.msg_iov        = iov;
	msgh.msg_iovlen     = 1;
	msgh.msg_control    = msgbuf;
	msgh.msg_controllen = sizeof(msgbuf);
	msgh.msg_flags      = 0;
	iov[0].iov_base        = rxbuf;
	iov[0].iov_len         = sizeof(rxbuf);
	*/

	// memset(&sa, 0, sizeof(sa));

	asize = sizeof(sa);
	rcvlen = recvfrom(fd, rxbuf, sizeof(rxbuf), 0, &sa, &asize);


	// rcvlen = recvmsg(fd, &msgh, MSG_DONTWAIT);

	if (rcvlen < 0) {
		return 0;	/* No more at this time.. */
	}

	if (debug) {
	  // printf("netax25rx packet from %s length %d family=%d\n", &sa.sax.fsa_ax25.sax25_call, rcvlen, sa.sax.fsa_ax25.sax25_family);
	  printf("netax25rx packet from rx_socket; device %s data length %d address family=%d\n", sa.sa_data, rcvlen, sa.sa_family);
	}

	/* Query AX.25 for the address from whence
	   this came in.. */
	// memcpy(ifr.ifr_name, &sa.sax.fsa_ax25.sax25_call, sizeof(ifr.ifr_name));
	memcpy(ifr.ifr_name, &sa.sa_data, sizeof(ifr.ifr_name));
	if (ioctl(rx_socket, SIOCGIFHWADDR, &ifr) < 0
	    || ifr.ifr_hwaddr.sa_family != AF_AX25) {
		/* not AX.25 so ignore this packet .. */
		return 1;	/* there may be more on this socket */
	}
	/* OK, AX.25 address.  Print it out in text. */
	ax25_fmtaddress(ifaddress, (uint8_t*)ifr.ifr_hwaddr.sa_data);

	if (debug > 1)
		printf("Received frame from '%s' len %d\n",
		       ifaddress, rcvlen);

	if (is_ax25ttyport(ifaddress)) {
		if (debug > 1) printf("%s is ttyport which we serve.\n",ifaddress);
		return 1;	/* We drop our own packets,
				   if we ever see them */
	}

	if (ax25rxports) {
		/* We have a list of AX.25 ports
		   (callsigns) where we limit
		   the reception from! */
		int j, ok = 0;
		for (j = 0; j < ax25rxportscount; ++j) {
			if (strcmp(ifaddress,ax25rxports[j]->callsign) == 0) {
				aif = ax25rxports[j]->interface;
				ok = 1;	/* Found match ! */
				break;
			}
		}
		if (!ok) {
			if (debug > 1) printf("%s is not known on  ax25-rxport definitions.\n",ifaddress);
			return 1;	/* No match :-(  */
		}
	}

	/* Now: actual AX.25 frame reception,
	   and transmit via ax25_to_tnc2() ! */

	/*
	 * "+10" is a magic constant for trying
	 * to estimate channel occupation overhead
	 */
	erlang_add(ifaddress, ERLANG_RX, rcvlen + 10, 1); // rxsock_read()

	// Send it to Rx-IGate, validates also AX.25 header bits,
	// and returns non-zero only when things are OK for processing.
	// Will internally also send to interface layer, if OK.
	ax25_to_tnc2(aif, ifaddress, 0, rxbuf[0], rxbuf + 1, rcvlen - 1);

	return 1;
}

static void discard_read_fd( const int fd )
{
	int i;
	char buf[2000];

	i = read(fd, buf, sizeof(buf));
}


int netax25_postpoll(struct aprxpolls *app)
{
	int i, j;
	struct pollfd *pfd = app->polls;
	// char ifaddress[10];

	if (rx_socket < 0)
		return 0;

	for (i = 0; i < app->pollcount; ++i, ++pfd) {
	  if ((pfd->fd == rx_socket) &&
	      (pfd->revents & (POLLIN | POLLPRI))) {
	    /* something coming in.. */
	    rxsock_read( rx_socket );
	  }
	  for (j = 0; j < ax25ttyportscount; ++j) {
	    if ((pfd->revents & (POLLIN | POLLPRI)) &&
		(ax25ttyfds[j] == pfd->fd)) {
	      discard_read_fd(ax25ttyfds[j]);
	    }
	  }
	}
	
	return 0;
}



/*
 *	Compare two ax.25 addresses
 */
static int ax25_compare(const ax25_address *a, const ax25_address *b)
{
	if ((a->ax25_call[0] & 0xFE) != (b->ax25_call[0] & 0xFE))
		return 1;

	if ((a->ax25_call[1] & 0xFE) != (b->ax25_call[1] & 0xFE))
		return 1;

	if ((a->ax25_call[2] & 0xFE) != (b->ax25_call[2] & 0xFE))
		return 1;

	if ((a->ax25_call[3] & 0xFE) != (b->ax25_call[3] & 0xFE))
		return 1;

	if ((a->ax25_call[4] & 0xFE) != (b->ax25_call[4] & 0xFE))
		return 1;

	if ((a->ax25_call[5] & 0xFE) != (b->ax25_call[5] & 0xFE))
		return 1;

 	if ((a->ax25_call[6] & 0x1E) != (b->ax25_call[6] & 0x1E))	/* SSID without control bit */
 		return 2;

 	return 0;
}

void netax25_sendto(const void *nax25p, uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen)
{
	const struct netax25_pty *nax25 = nax25p;
	struct full_sockaddr_ax25 srcsax;
	struct full_sockaddr_ax25 dstsax;
	int tx_socket = socket(PF_AX25,   SOCK_DGRAM, 0);
	int digilen = axaddrlen - 14;
	int srclen, dstlen;

	axaddr[6] &= 0xFE;
	axaddr[13] &= 0xFE;
	
	srclen = sizeof(srcsax.fsa_ax25);
	memset(&srcsax, 0, sizeof(srcsax));
	srcsax.fsa_ax25.sax25_family = PF_AX25;
	srcsax.fsa_ax25.sax25_ndigis = 0;
	memcpy(srcsax.fsa_ax25.sax25_call.ax25_call, axaddr+7, 7);
	if (ax25_compare(&srcsax.fsa_ax25.sax25_call,
			 (const ax25_address *)nax25->interface->ax25call)) {
	  // source call
	  srclen = sizeof(srcsax);
	  srcsax.fsa_ax25.sax25_ndigis = 1;
	  memcpy(srcsax.fsa_digipeater, nax25->interface->ax25call, 7);
	}

	dstlen = sizeof(dstsax.fsa_ax25);
	memset(&dstsax, 0, sizeof(dstsax));
	dstsax.fsa_ax25.sax25_family = PF_AX25;
	memcpy(dstsax.fsa_ax25.sax25_call.ax25_call, axaddr, 7);
	if (digilen > 0) {
	  dstsax.fsa_ax25.sax25_ndigis = digilen / 7;
	  axaddr[14-1] &= 0xFE;
	  memcpy(&dstsax.fsa_digipeater, axaddr + 14, digilen);
	  dstlen = sizeof(dstsax);
	}
	
	bind(tx_socket, (struct sockaddr *)&srcsax, sizeof(srcsax));

	sendto(tx_socket, axdata, axdatalen, 0, // That NOSIGNAL is Linux specific, but so is AX.25 too...
	       (struct sockaddr *)&dstsax, sizeof(dstsax));

	close(tx_socket);

	erlang_add(nax25->callsign, ERLANG_TX, axaddrlen+axdatalen + 10, 1);  // netax25_sendto()
}


#else				/* Not Linux with PF_AX25 ?  Dummy routines.. */

void netax25_init(void)
{
}

int netax25_prepoll(struct aprxpolls *app)
{
	return 0;
}

int netax25_postpoll(struct aprxpolls *app)
{
	return 0;
}

void netax25_start(void)
{
}

const void* netax25_open(const char *ifcallsign)
{
}

void netax25_addrxport(const char *portname, char *str)
{
}

void netax25_sendax25(const void *nax25, const void *ax25, int ax25len)
{
}

void netax25_sendax25_tnc2(const void *tnc2, int tnc2len)
{
}

void netax25_sendto(const void *nax25, const uint8_t *txbuf, const int txlen)
{
}
#endif
