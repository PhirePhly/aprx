/* **************************************************************** *
 *                                                                  *
 *  APRSG-NG -- 2nd generation receive-only APRS-i-gate with        *
 *              minimal requirement of esoteric facilities or       *
 *              libraries of any kind beyond UNIX system libc.      *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * NETAX25:  Listen on (Linux) AX.25 socket and pick all AX.25      *
 *           data packets                                           *
 *                                                                  *
 * **************************************************************** */

#include "aprsg.h"

// TODO: Requires autoconfig sensing that the system really does have
//       AX.25 headers available ?   Or does it ?

#include <sys/socket.h>

#ifdef PF_AX25  /* PF_AX25 exists -- highly likely a Linux system ! */

#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <sys/ioctl.h>

static int rx_socket;
static int rx_protocol = ETH_P_ALL;

void netax25_init(void)
{
	int i;

	rx_protocol = ETH_P_AX25;

	rx_socket = socket(PF_PACKET, SOCK_PACKET, htons(rx_protocol));

	if (rx_socket < 0)
	  rx_socket = socket(PF_PACKET, SOCK_PACKET, htons(rx_protocol));

	if (rx_socket < 0) {
	  i = errno;
	  /* D'uh..  could not open it, report and leave it at that. */
	  if (debug)
	    fprintf(stderr,"aprsg-ng: Could not open socket(PF_PACKET,SOCK_PACKET,ETH_P_AX25) for listening.  Errno=%d (%s)\n",
		    i, strerror(i));
	  return;
	}

	if (rx_socket >= 0)
	  fd_nonblockingmode(rx_socket);

}

int netax25_prepoll(int n, struct pollfd **fdsp, time_t *tp)
{
	int i;
	struct pollfd *fds = *fdsp;

	if (rx_socket < 0) return 0;

	/* FD is open, lets mark it for poll read.. */
	fds->fd = rx_socket;
	fds->events = POLLIN|POLLPRI;
	fds->revents = 0;

	++fds;
	*fdsp = fds;

	return 1;
}

int netax25_postpoll(int nfds, struct pollfd *fds)
{
	struct sockaddr sa;
	struct ifreq ifr;
	socklen_t asize;
	unsigned char rxbuf[3000];
	int rcvlen;
	int i;

	if (rx_socket < 0) return 0;

	for (i = 0; i < nfds; ++i, ++fds) {
	  while ((fds->fd == rx_socket) &&
		 (fds->revents & (POLLIN|POLLPRI))) {
	    /* something coming in.. */
	    asize = sizeof(sa);
	    rcvlen = recvfrom(rx_socket, rxbuf, sizeof(rxbuf), 0, &sa, &asize);
	    if (rcvlen < 0) {
	      return -1; /* No more at this time.. */
	    }

	    if (rx_protocol == ETH_P_ALL) { /* promiscuous mode ? */
	      strcpy(ifr.ifr_name, sa.sa_data);
	      if (ioctl(rx_socket, SIOCGIFHWADDR, &ifr) < 0
		  || ifr.ifr_hwaddr.sa_family != AF_AX25) {
		/* not AX25 so ignore this packet .. */
		continue; /* there may be more on this socket */
	      }
	    }

	    /* TODO: POSSIBLY: Limit the list of interfaces we accept
	       the packets from ! */

	    /* Now: actual AX.25 frame reception,
	       and transmit via ax25_to_tnc2() ! */

	    ax25_to_tnc2(rxbuf[0], rxbuf+1, rcvlen-1);

	  }
	}

	return 0;
}

#else  /* Not Linux with PF_AX25 ?  Dummy routines.. */

void netax25_init(void)
{
}

int netax25_prepoll(int n, struct pollfd **fdsp, time_t *tp)
{
	return 0;
}

int netax25_postpoll(int n, struct pollfd *fds)
{
	return 0;
}
#endif

