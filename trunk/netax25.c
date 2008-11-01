/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007,2008                            *
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


#if defined(HAVE_OPENPTY)
#ifdef HAVE_PTY_H
#include <pty.h>
#endif

static int pty_master = -1;
static int pty_slave = -1;

static void netax25_openpty(void)
{
	int rc;
	int disc;
	struct termios tio;
	char devname[64];
	unsigned char ax25call[7];
	struct ifreq ifr;
	int fd = -1;

	if (!mycall)
		return;		/* No mycall, no ptys! */

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
		return;		/* D'uh.. */
	}

	/* setup termios parameters for this line.. */
	cfmakeraw(&tio);
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
	parse_ax25addr(ax25call, mycall, 0x60);
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

	return;
}

void netax25_sendax25(const void *ax25, int ax25len)
{
	int rc;
	unsigned char ax25buf[1000];

	/* kissencoder() takes AX.25 frame, and adds framing + cmd-byte */
	rc = kissencoder(ax25buf, sizeof(ax25buf), ax25, ax25len, 0);
	if (rc < 0)
		return;
	ax25len = rc;

	rc = write(pty_master, ax25buf, ax25len);
}

/* Have to convert incoming TNC2 format messge to AX.25.. */
void netax25_sendax25_tnc2(const void *ax25, int ax25len, int is3rdparty)
{
}

#else
static void netax25_openpty(void)
{
}

void netax25_sendax25(const void *ax25, int ax25len)
{
}
void netax25_sendax25_tnc2(const void *ax25, int ax25len)
{
}
#endif				/* HAVE_OPENPTY */

static int rx_socket;

static char **ax25rxports;
static int ax25rxportscount;

void netax25_addport(const char *portname, char *str)
{
	ax25rxports =
		realloc(ax25rxports,
			sizeof(void *) * (ax25rxportscount + 1));
	ax25rxports[ax25rxportscount] = strdup(portname);
	++ax25rxportscount;
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

	netax25_openpty();

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

int netax25_prepoll(struct aprxpolls *app)
{
	struct pollfd *pfd;

	if (rx_socket < 0)
		return 0;

	/* FD is open, lets mark it for poll read.. */
	pfd = aprxpolls_new(app);
	pfd->fd = rx_socket;
	pfd->events = POLLIN | POLLPRI;
	pfd->revents = 0;

	return 1;
}

static int ax25_fmtaddress(char *dest, const unsigned char *src)
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


int netax25_postpoll(struct aprxpolls *app)
{
	struct sockaddr sa;
	struct ifreq ifr;
	socklen_t asize;
	unsigned char rxbuf[3000];
	int rcvlen;
	int i;
	struct pollfd *pfd = app->polls;
	char ifaddress[10];

	if (rx_socket < 0)
		return 0;

	for (i = 0; i < app->pollcount; ++i, ++pfd) {
		while ((pfd->fd == rx_socket) &&
		       (pfd->revents & (POLLIN | POLLPRI))) {
			/* something coming in.. */
			asize = sizeof(sa);
			rcvlen = recvfrom(rx_socket, rxbuf, sizeof(rxbuf),
					  0, &sa, &asize);
			if (rcvlen < 0) {
				return -1;	/* No more at this time.. */
			}

			/* Query AX.25 for the address from whence this came in.. */
			strcpy(ifr.ifr_name, sa.sa_data);
			if (ioctl(rx_socket, SIOCGIFHWADDR, &ifr) < 0
			    || ifr.ifr_hwaddr.sa_family != AF_AX25) {
				/* not AX.25 so ignore this packet .. */
				continue;	/* there may be more on this socket */
			}
			/* OK, AX.25 address.  Print it out in text. */
			ax25_fmtaddress(ifaddress, ifr.ifr_hwaddr.sa_data);

			if (debug > 1)
				printf("Received frame from '%s' len %d\n",
				       ifaddress, rcvlen);

			if (strcmp(ifaddress, mycall) == 0)
				continue;	/* We drop our own packets */


			if (ax25rxports) {
				/* We have a list of AX.25 ports (callsigns) where we limit
				   the reception from! */
				int j, ok = 0;
				for (j = 0; j < ax25rxportscount; ++j) {
					if (strcmp
					    (ifaddress,
					     ax25rxports[j]) == 0) {
						ok = 1;	/* Found match ! */
						break;
					}
				}
				if (!ok)
					continue;	/* No match :-(  */
			}

			/* Now: actual AX.25 frame reception,
			   and transmit via ax25_to_tnc2() ! */

			/*
			 * "+10" is a magic constant for trying to estimate channel
			 * occupation overhead
			 */
			erlang_add(NULL, ifaddress, 0, ERLANG_RX,
				   rcvlen + 10, 1);

			ax25_to_tnc2(ifaddress, 0, rxbuf[0], rxbuf + 1,
				     rcvlen - 1);

		}
	}

	return 0;
}

#else				/* Not Linux with PF_AX25 ?  Dummy routines.. */

void netax25_init(void)
{
}

int netax25_prepoll(int n, struct pollfd **fdsp, time_t * tp)
{
	return 0;
}

int netax25_postpoll(int n, struct pollfd *fds)
{
	return 0;
}
#endif
