/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation APRS iGate and digi with                 *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 * NETAX25:  Listen on (Linux) AX.25 socket and pick all AX.25      *
 *           data packets     ...    actually don't pick those      *
 *           that are going outwards.  All incoming ones do pick.   *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"


#ifdef PF_AX25	/* PF_AX25 exists -- highly likely a Linux system ! */

#include <sys/ioctl.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <netinet/if_ether.h>

#include <netinet/in.h>

#include <netax25/ax25.h>


/*
 * Link-level device access
 *
 * s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_AX25));
 *
 */

/*
struct sockaddr_ll
  {
    unsigned short int sll_family;
    unsigned short int sll_protocol;
    int sll_ifindex;
    unsigned short int sll_hatype;
    unsigned char sll_pkttype;
    unsigned char sll_halen;
    unsigned char sll_addr[8];
  };

SOCK_RAW Sending uses  sll_ifindex  and  sll_protocol

*/

struct netax25_dev {
	int		ifindex;
	int16_t		protocol;
	uint8_t		ax25addr[7];
	uint8_t		rxok;
	//uint8_t	txok;
	uint8_t         scan;
	char		devname[IFNAMSIZ];
	char		callsign[10];
	const struct aprx_interface *interface;
};


static struct netax25_dev **netax25_devs;
static int                  netax25_devcount;



/*
 *  Talking to Linux kernel 2.6.x, using SMACK type frames
 *  on each configured serial port callsign -> ptymux 
 *  writer channel.  If system does not write correct SMACK
 *  frame on that KISS port for any number of reasons,
 *  including writing incompletely buffered data, then
 *  kernel will be able to notice that frame it received
 *  is not valid, and discards it.  (Maybe... P = 2^-16 to
 *  accepting of error frame in spite of these controls.)
 */

struct netax25_pty {
	int                          fd;
	int			     ifindex;
	const char                  *callsign;
	const struct aprx_interface *interface;
	struct sockaddr_ax25         ax25addr;
};


static int rx_socket = -1;
static int tx_socket = -1;

static struct netax25_pty **ax25rxports;
static int                  ax25rxportscount;

static char **ax25ttyports;
static int   *ax25ttyfds;
static int    ax25ttyportscount;





#if defined(HAVE_OPENPTY)
#ifdef HAVE_PTY_H
#include <pty.h>
#endif

static void netax25_addttyport(const char *callsign,
			       const int masterfd, const int slavefd);

static const void* netax25_openpty(const char *mycall)
{
	int rc;
	int disc;
	struct termios tio;
	char devname[64];
	uint8_t ax25call[64]; // overlarge for AX.25 - which needs only 7 bytes, but valgrind whines..
	struct ifreq ifr;
	int fd = -1;
	struct netax25_pty *nax25 = NULL;
	int pty_master, pty_slave;

	if (!mycall)
		return NULL;		/* No mycall, no ptys! */

	memset(ax25call, 0, sizeof(ax25call)); // valgrind
	if (parse_ax25addr(ax25call, mycall, 0x60)) {
		// Not valid per AX.25 rules
	  if (debug)
	    printf(" netax25_openpty('%s') failed to parse the parameter string as valid AX.25 callsign. Not opening kernel pty.\n", mycall);
		return NULL;
	}

	memset(devname, 0, sizeof(devname)); // valgrind
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


                if (nax25 != NULL) free(nax25);

		return NULL;		/* D'uh.. */
	}

	nax25 = calloc( 1,sizeof(*nax25) );
	nax25->fd       = pty_master;
	nax25->ifindex  = -1;
	nax25->callsign = mycall;

	nax25->ax25addr.sax25_family = PF_AX25;
	nax25->ax25addr.sax25_ndigis = 0;
	memcpy(&nax25->ax25addr.sax25_call, ax25call, 7);

	/* setup termios parameters for this line.. */
	memset(&tio, 0, sizeof(tio)); // please valgrind
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
	if (fd < 0)
		goto error_exit;

	memset(&ifr, 0, sizeof(ifr)); // please valgrind
	strncpy(ifr.ifr_name, devname, sizeof(ifr.ifr_name));
        ifr.ifr_name[sizeof(ifr.ifr_name)-1] = 0;

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
	rc = kissencoder(ax25buf, sizeof(ax25buf), LINETYPE_KISSSMACK,
			 ax25, ax25len, 0x80);
	if (rc < 0)
		return;
	ax25len = rc;

	if (debug>2) {
	  printf("netax25_sendax25() len=%d ",ax25len);
	  hexdumpfp(stdout, ax25, ax25len, 1);
	  printf("\n");
	}


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
            aprxlog("netax25_sendax25(%s,len=%d) wrote %d bytes\n", nax25->callsign, ax25len, p);
	  }
	}
}

#else /* !HAVE_OPENPTY */

static const void* netax25_openpty(const char *mycall)
{
	return NULL;
}

void netax25_sendax25(const void *nax25, const void *ax25, int ax25len)
{
}
#endif				/* HAVE_OPENPTY */

static int is_ax25ttyport(const char *callsign)
{
	int i;
	for (i = 0; i < ax25ttyportscount; ++i) {
		if (strcmp(callsign,ax25ttyports[i]) == 0)
			return 1; // Have match
	}
	return 0; // No match
}


static int scan_linux_devices(void) {
	FILE *fp;
	struct ifreq ifr;
	char buffer[512], *s;
	int fd;
	struct netax25_dev ax25dev, *d;
	int i;

	// Mark all devices ready for scanning
	for (i = 0; i < netax25_devcount; ++i)
	  netax25_devs[i]->scan = 0;

	fd = socket(PF_FILE, SOCK_DGRAM, 0);
	if (fd < 0) {
	  // ... error
	  if (debug)printf("Can not create socket(PF_FILE,SOCK_DGRAM,0); errno=%d\n", errno);
	  return -1;
	}
	fp = fopen("/proc/net/dev", "r");
	if (fp == NULL) {
	  if (debug)printf("Can not open /proc/net/dev for reading; errno=%d\n", errno);
	  close(fd);
	  // ... error
	  return -1;
	}
	// Two header lines
	s = fgets(buffer, sizeof(buffer), fp);
	s = fgets(buffer, sizeof(buffer), fp);
	// Then network interface names
	while (!feof(fp)) {
	  if (!fgets(buffer, sizeof(buffer), fp))
	    break; // EOF
	  s = strchr(buffer, ':');
	  if (s) *s = 0;
	  s = buffer;
	  while (*s == ' '||*s == '\t') ++s;
	  memset(&ifr, 0, sizeof(ifr)); // please valgrind
	  strncpy(ifr.ifr_name, s, IFNAMSIZ-1);
	  ifr.ifr_name[IFNAMSIZ-1] = 0;

	  // Is it active?
	  if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
	    // error
	    continue;
	  }
	  if (!(ifr.ifr_flags & IFF_UP))
	    continue;  // not active, try next
	  
	  // Does it have AX.25 HW address ?
	  if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
	    // Error
	    continue;
	  }
	  if (ifr.ifr_hwaddr.sa_family != ARPHRD_AX25)
	    continue;  // Not AX.25 HW address, try next

	  memset(&ax25dev, 0, sizeof(ax25dev));
	  memcpy(ax25dev.devname,  ifr.ifr_name, IFNAMSIZ);
	  memcpy(ax25dev.ax25addr, ifr.ifr_hwaddr.sa_data, 7); // AX.25 address
	  ax25_to_tnc2_fmtaddress(ax25dev.callsign, ax25dev.ax25addr, 0); // in text

	  if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
	    // Error
	    continue;
	  }
	  ax25dev.ifindex = ifr.ifr_ifindex;

	  // Store/Update internal kernel interface index list

	  d = NULL;
	  for (i = 0; i < netax25_devcount; ++i) {
	    d = netax25_devs[i];
	    if (d->ifindex == ax25dev.ifindex) {
	      d->scan = 1;  // The ifindex does not change during interface lifetime
	      break;
	    }
	    d = NULL;
	  }
	  if (d == NULL) {
	    // Not in known interfaces, add a new one..
	    d = malloc(sizeof(*d));
	    ++netax25_devcount;
	    netax25_devs = realloc( netax25_devs,
				    sizeof(void*) * netax25_devcount );
	    netax25_devs[netax25_devcount-1] = d;
	    memcpy(d, &ax25dev, sizeof(*d));
	    d->scan = 1;
	    d->rxok = !is_ax25ttyport(d->callsign);
	  }

	}
	fclose(fp);
	close(fd);
	// Remove devices no longer known
	for (i = 0; i < netax25_devcount; ++i) {
	  if (netax25_devs[i]->scan == 0) {
	    int j;
	    if (debug>1)printf("Compating netax25_devs[] i=%d callsign=%s\n",
			       i, netax25_devs[i]->callsign);
	    free(netax25_devs[i]);
	    for (j = i+1; j < netax25_devcount; ++j) {
	      netax25_devs[j-1] = netax25_devs[j];
	    }
	    --netax25_devcount;
	  }
	}

	// Link interfaces
	for (i = 0; i < netax25_devcount; ++i) {
	  int j;
	  struct netax25_dev *d = netax25_devs[i];
	  for (j = 0; j < ax25rxportscount; ++j) {
	    if (strcmp(ax25rxports[j]->callsign,d->callsign) == 0) {
	      d->interface = ax25rxports[j]->interface;
	      ax25rxports[j]->ifindex = d->ifindex;
	    }
	  }
	}

	return 0;
}




/* config interface:  ax25-rxport: callsign */
void *netax25_addrxport(const char *callsign, const struct aprx_interface *interface)
{
	struct netax25_pty *nax25p = calloc(1, sizeof(*nax25p));

	nax25p->fd        = -1;
	nax25p->interface = interface;
	nax25p->ax25addr.sax25_family = PF_AX25;
	nax25p->ax25addr.sax25_ndigis = 0;

	if (interface == NULL) {  // Old config style
	  if (parse_ax25addr((uint8_t*)&nax25p->ax25addr.sax25_call, callsign, 0x60)) {
	    // Not valid per AX.25 rules
            free(nax25p);
	    return NULL;
	  }
	  nax25p->callsign  = strdup(callsign);
	} else {  // new config file
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
	tx_socket = -1;

	if (!ax25rxports) return;	/* No configured receiver ports.
					   No receiver socket creation. */

	rx_protocol = ETH_P_AX25;	/* Choosing ETH_P_ALL would pick also
					   outbound packets, but also all of
					   the ethernet traffic..  ETH_P_AX25
					   picks only inbound-at-ax25-devices
					   ..packets.  */

	rx_socket = socket(PF_PACKET, SOCK_RAW, htons(rx_protocol));
	tx_socket = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_AX25));

	if (rx_socket < 0) {
		i = errno;
		/* D'uh..  could not open it, report and leave it at that. */
		fprintf(stderr,
			"aprx: Could not open socket(PF_PACKET,SOCK_RAW,ETH_P_AX25) for listening.  Errno=%d (%s)"
			" -- not a big deal unless you want to receive via AX.25 sockets.\n",
			i, strerror(i));
		return;
	}

	if (tx_socket < 0) {
		i = errno;
		/* D'uh..  could not open it, report and leave it at that. */
		fprintf(stderr,
			"aprx: Could not open socket(PF_PACKET,SOCK_RAW,ETH_P_AX25) for sending.  Errno=%d (%s)"
			" -- not a big deal unless you want to send via AX.25 sockets.\n",
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

static struct timeval next_scantime;

static void netax25_resettimer(void*arg)
{
	struct timeval *tv = (struct timeval *)arg;
	tv_timeradd_seconds(tv, &tick, 60);
        scan_linux_devices();
}

int netax25_prepoll(struct aprxpolls *app)
{
	struct pollfd *pfd;
	int i;

        if (next_scantime.tv_sec == 0) next_scantime = tick;

        if (time_reset) {
        	netax25_resettimer(&next_scantime);
        }

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

static int rxsock_read( const int fd )
{
	struct sockaddr_ll sll;
	socklen_t sllsize;
	int rcvlen, ifindex, i;
	struct netax25_dev *netdev;
	uint8_t rxbuf[3000];

	sllsize = sizeof(sll);
	rcvlen = recvfrom(fd, rxbuf, sizeof(rxbuf), 0, (struct sockaddr*)&sll, &sllsize);

	if (rcvlen < 0) {
		return 0;	/* No more at this time.. */
	}

/*
struct sockaddr_ll
  {
    unsigned short int sll_family;	= PF_PACKET
    unsigned short int sll_protocol;	= 200 ?
    int sll_ifindex;			= 4
    unsigned short int sll_hatype;	= 3 = SOCK_RAW ?
    unsigned char sll_pkttype;		= 0
    unsigned char sll_halen;		= 0
    unsigned char sll_addr[8];		= random
  };

netax25rx packet len=54 from rx_socket;  family=17 protocol=200 ifindex=4 hatype=3
					 pkttype=0 halen=0  addr=84:f9:ca:bf:d7:04:f3:b7

Data:  00 82 a0 aa 64 6a 9c e0 9e 90 70 9a b0 94 60 ae 92 88 8a 64 40 61 03 f0 3d 36 33 35 33 2e ...
Text:  00 82 a0 aa  d  j 9c e0 9e 90  p 9a b0 94  ` ae 92 88 8a  d  @  a 03 f0  =  6  3  5  3  . ...
AX25:  00  A  P  U  2  5  N  p  O  H  8  M  X  J  0  W  I  D  E  2     0 01  x 1e 1b 19 1a 19 17 ...

Leads with 00 byte, then AX.25 address..

*/

	if (sll.sll_family   != PF_PACKET         ||
	    sll.sll_protocol != htons(ETH_P_AX25) ||
	    sll.sll_hatype   != SOCK_RAW          ||
	    sll.sll_pkttype  != 0                 ||
	    sll.sll_halen    != 0                 ||
	    rxbuf[0]         != 0 ) {
	  return 1; // Not of our interest
	}
	ifindex = sll.sll_ifindex;

	if (debug>1) {
	  printf("netax25rx packet len=%d from rx_socket;  family=%d protocol=%x ifindex=%d hatype=%d pkttype=%d halen=%d\n",
		 rcvlen, sll.sll_family, sll.sll_protocol, sll.sll_ifindex, sll.sll_hatype, sll.sll_pkttype, sll.sll_halen);
/*
	  printf(" addr=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		 sll.sll_addr[0],sll.sll_addr[1],sll.sll_addr[2],sll.sll_addr[3],
		 sll.sll_addr[4],sll.sll_addr[5],sll.sll_addr[6],sll.sll_addr[7]);

		  int i;
		  printf("Data: ");
		  for (i = 0; i < rcvlen; ++i)
		    printf(" %02x", rxbuf[i]);
		  printf("\n");
		  printf("Text: ");
		  for (i = 0; i < rcvlen; ++i) {
		    uint8_t c = rxbuf[i];
		    if (32 <= c && c <= 126)
		      printf("  %c", c);
		    else
		      printf(" %02x", c);
		  }
		  printf("\n");
		  printf("AX25: ");
		  for (i = 0; i < rcvlen; ++i) {
		    uint8_t c = rxbuf[i] >> 1;
		    if (32 <= c && c <= 126)
		      printf("  %c", c);
		    else
		      printf(" %02x", c);
		  }
		  printf("\n");
*/
 	}

	netdev = NULL;
	for (i = 0; i < netax25_devcount; ++i) {
	  if (netax25_devs[i]->ifindex == ifindex) {
	    netdev = netax25_devs[i];
	    break;
	  }
	}
	if (netdev == NULL) {
	  // Not found from Ax.25 devices
	  if (debug>1) printf(".. not from known AX.25 device\n");
	  return 1;
	}
        if (netdev->interface == NULL) {
	  if (debug>1) printf(".. not from AX.25 device configured for receiving.\n");
          return 1;
        }

	if (debug) printf("Received frame of %d bytes from %s: %s\n",
			  rcvlen, netdev->devname, netdev->callsign);

	// if (is_ax25ttyport(netdev->callsign)) {
	if (!netdev->rxok) {
		if (debug > 1) {
		  printf("%s is ttyport which we serve.\n",netdev->callsign);
		}
		return 1; // We drop our own packets, if we ever see them
	}

	/// Now: actual AX.25 frame reception,
	//   and transmit via ax25_to_tnc2() !

	/*
	 * "+10" is a magic constant for trying
	 * to estimate channel occupation overhead
	 */
	erlang_add(netdev->callsign, ERLANG_RX, rcvlen + 10, 1); // rxsock_read()

	// Send it to Rx-IGate, validates also AX.25 header bits,
	// and returns non-zero only when things are OK for processing.
	// Will internally also send to interface layer, if OK.
	if (ax25_to_tnc2(netdev->interface, netdev->callsign, 0, rxbuf[0], rxbuf + 1, rcvlen - 1)) {
	  // The packet is valid per AX.25 header bit rules.
	  // ax25_to_tnc2() did send the packet to rx-igate
	  ;
	} else {
	  // The packet is not valid per AX.25 header bit rules
          rfloghex(netdev->callsign, 'D', 1, rxbuf, rcvlen);
	  erlang_add(netdev->callsign, ERLANG_DROP, rcvlen+10, 1);	/* Account one packet */

	  if (aprxlogfile) {
	    FILE *fp = fopen(aprxlogfile, "a");
	    if (fp) {
	      char timebuf[60];
	      printtime(timebuf, sizeof(timebuf));

	      fprintf(fp, "%s ax25_to_tnc2(%s,len=%d) rejected the message: ", timebuf, netdev->callsign, rcvlen);
	      hexdumpfp(fp, rxbuf, rcvlen, 1);
	      fprintf(fp, "\n");
	      fclose(fp);
	    }
	  }
	}

	return 1;
}

static void discard_read_fd( const int fd )
{
	char buf[2000];
	(void)read(fd, buf, sizeof(buf));
}


int netax25_postpoll(struct aprxpolls *app)
{
	int i, j;
	struct pollfd *pfd;
	// char ifaddress[10];

        assert(app->polls != NULL);

        if (tv_timercmp(&tick, &next_scantime) > 0) {
        	scan_linux_devices();
                // Rescan every 60 seconds, on the dot.
                tv_timeradd_seconds(&next_scantime, &next_scantime, 60);
        }

	pfd = app->polls;

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



void netax25_sendto(const void *nax25p, const uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen)
{
	const struct netax25_pty *nax25 = nax25p;
	struct sockaddr_ll sll;
	char c0[1];
	struct iovec iovec[3];
	struct msghdr mh;
	int i, len;

	if (tx_socket < 0) {
	  if (debug>1) printf("netax25_sendto() tx_socket = -1, can not do..\n");
	  return; // D'uh..
	}
	if (nax25->ifindex < 0) {
	  if (debug>1) printf("netax25_sendto() ifindex < 0, can not do..\n");
	  return; // D'uh..
	}

	if (debug>2) {
	  printf("netax25_sendto() len=%d,%d ",axaddrlen,axdatalen);
	  hexdumpfp(stdout, axaddr, axaddrlen, 1);
	  printf(" // ");
	  hexdumpfp(stdout, (uint8_t*)axdata, axdatalen, 0);
	  printf("\n");
	}

	memset(&sll, 0, sizeof(sll));
	sll.sll_family   = PF_PACKET;
	sll.sll_ifindex  = nax25->ifindex;
	sll.sll_protocol = htons(ETH_P_AX25);
	sll.sll_hatype   = SOCK_RAW;

	c0[0] = 0;
	iovec[0].iov_base = c0;
	iovec[0].iov_len  = 1;
	iovec[1].iov_base = (void*)axaddr; // silence the compiler
	iovec[1].iov_len  = axaddrlen;
	iovec[2].iov_base = (void*)axdata; // silence the compiler
	iovec[2].iov_len  = axdatalen;
	len = 1+axaddrlen+axdatalen; // for debugging

	memset(&mh, 0, sizeof(mh));
	mh.msg_name    = &sll;
	mh.msg_namelen = sizeof(sll);
	mh.msg_iov     = iovec;
	mh.msg_iovlen  = 3;

	errno = 0;
	i = sendmsg(tx_socket, &mh, 0);
	if (debug>1)printf("netax25_sendto() the sendmsg len=%d rc=%d errno=%d\n", len, i, errno);

	erlang_add(nax25->callsign, ERLANG_TX, axaddrlen+axdatalen + 10, 1);  // netax25_sendto()
}
#endif
