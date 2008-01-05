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

static char **ax25rxports;
static int    ax25rxportscount;

void netax25_addport(const char *portname)
{
	ax25rxports = realloc(ax25rxports, sizeof(void*)*(ax25rxportscount+1));
	ax25rxports[ax25rxportscount] = strdup(portname);
	++ax25rxportscount;
}

void netax25_init(void)
{
	int i;

	rx_protocol = ETH_P_AX25; /* Choosing ETH_P_ALL would pick also outbound
				     packets, but also all of the ethernet traffic..
				     ETH_P_AX25 picks only inbound-at-ax25-devices
				     ..packets.  */

	rx_socket = socket(PF_PACKET, SOCK_PACKET, htons(rx_protocol));

	if (rx_socket < 0)
	  rx_socket = socket(PF_PACKET, SOCK_PACKET, htons(rx_protocol));

	if (rx_socket < 0) {
	  i = errno;
	  /* D'uh..  could not open it, report and leave it at that. */
	  if (debug)
	    fprintf(stderr,"aprx: Could not open socket(PF_PACKET,SOCK_PACKET,ETH_P_AX25) for listening.  Errno=%d (%s)\n",
		    i, strerror(i));
	  return;
	}

	if (rx_socket >= 0)
	  fd_nonblockingmode(rx_socket);

}

int netax25_prepoll(struct aprxpolls *app)
{
	struct pollfd *pfd;

	if (rx_socket < 0) return 0;

	/* FD is open, lets mark it for poll read.. */
	pfd = aprxpolls_new(app);
	pfd->fd = rx_socket;
	pfd->events = POLLIN|POLLPRI;
	pfd->revents = 0;

	return 1;
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

	if (rx_socket < 0) return 0;

	for (i = 0; i < app->pollcount; ++i, ++pfd) {
	  while ((pfd->fd == rx_socket) &&
		 (pfd->revents & (POLLIN|POLLPRI))) {
	    /* something coming in.. */
	    asize = sizeof(sa);
	    rcvlen = recvfrom(rx_socket, rxbuf, sizeof(rxbuf), 0, &sa, &asize);
	    if (rcvlen < 0) {
	      return -1; /* No more at this time.. */
	    }

	    if (ax25rxports) {
	      /* We have a list of AX.25 ports where we limit the reception from! */
	      int j, ok = 0;
	      for (j = 0; j < ax25rxportscount; ++j) {
		if (strcmp(sa.sa_data, ax25rxports[j]) == 0) {
		  ok = 1; /* Found match ! */
		  break;
		}
	      }
	      if (!ok) return -1; /* No match :-(  */
	    }

	    if (sa.sa_family == AF_AX25) {
	      ;
	    } else if (rx_protocol == ETH_P_ALL) { /* promiscuous mode ? */
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

	    /*
	     * "+10" is a magic constant for trying to estimate channel
	     * occupation overhead
	     */
	    erlang_add(NULL, sa.sa_data, ERLANG_RX, rcvlen+10, 1);

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

