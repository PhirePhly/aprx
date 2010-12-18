/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2010                            *
 *                                                                  *
 * **************************************************************** */
#include "aprx.h"

/***  AGWPE interface description from Xastir + AGWPE documents.
 ***  As those documents are unclear, I am using Xastir to supply
 ***  clue as to what really needs to be done.


// How to Start your application and register with AGW Packet Engine

// First of all create a stream socket and connect with
// AGW Packet Engine ip address at port 8000.
// If your application is running at the same machine with
// AGWPE then as ip address use localhost (127.0.0.1).
//
// After connecting you need to register your application's
// callsign if your application need to establish AX.25 connections.
// If your application just do monitoring and sends Unproto frames,
// no need for callsign registration.
// You can register as many as 100 different callsigns.
// If you register a callsign then AGW Packet Engine accepts AX.25
// connections for this call.
// You then ask AGWPE to send you the radioport information.
// How many radioports are with their description.
// After that you can create your windows or anything else.
//
// Now you are ready.
// If you wish to do monitoring then you must enable monitoring.


// Transmit a special frame in Raw AX25 format
//
// The frame must be in RAW AX25 format as should be txed by
//  the modem (no need for bit stuffing etc)
// You can use this function to tx for instance an unproto
// frame with a special PID or any other frame different from normal AX25 Format.
// 
// Port field is the port where we want the data to tx
// DataKind field = MAKELONG('K',0); The ASCII value of letter K
// CallFrom empty (NULL)
// CallTo empty (NULL)
// DataLen is the length of the data that follow
// USER is Undefined
//
// the whole frame with the header is
//
// [ HEADER                                   ]
// [port][DataKind][CallFrom][CallTo ][DataLen][USER][Data         ]
//  4bytes  4bytes     10bytes  10bytes   4bytes      4bytes  DataLen Bytes
//


// ASK RadioPorts Number and descriptions
//
// Port field must be 0
// DataKind field =MAKELONG('G',0); The ASCII value of letter G
// CallFrom is empty (NULL)
// CallTo is empty(NULL)
// DataLen must be 0
// USER is undefined
// No data field must exists
// [ HEADER                                   ]
// [port][DataKind][CallFrom][CallTo ][DataLen][USER]
//  4bytes  4bytes     10bytes  10bytes   4bytes     4bytes
//

// ASK To start receiving AX25 RAW Frames
//
// Sending again thos command will disable this service.
// You can start and stop this service any times you needed
//
// Port field no needed set it to 0
// DataKind field =MAKELONG('k',0); The ASCII value of lower case letter k
// CallFrom is empty no needed
// CallTo is empty no needed
// DataLen must be 0
// USER is undefined
// No data field must be present
//
// the whole frame with the header is
//
// [ HEADER                                   ]
// [port][DataKind][CallFrom][CallTo ][DataLen][USER]
//  4bytes  4bytes     10bytes  10bytes   4bytes     4bytes


// Raw AX25  Frames
//
// You can receive RAW AX25 frames if you enable this service.
// Those frames are all the packet valid frames received from
// any radioport. The frame is exactly the same as the pure
// AX25 frame with the follow additions.
// 
// The first byte always contains the radioport number 0 for 1st radioport.
// There is no FCS field at the end of the frame.
// There is no bit stuffing.
// The LOWORD Port field is the port which heard the frame
// The LOWORD DataKind field ='K'; The ASCII value of letter K
// CallFrom the callsign of the station who TX the frame(Source station)
// CallTo The callsign of the destination station
// DataLen is the length of the DATA field(the length of the frame
// USER is undefined.
// the whole frame with the header is
//   [port][DataKind][CallFrom][CallTo ][DataLen][USER][DATA   ]
//    4bytes  4bytes     10bytes  10bytes   4bytes      4bytes    DataLen Bytes


// 1.UNPROTO monitor frame
// 
// The LOWORD Port field is the port which heard the frame
// The LOWORD DataKind field ='U'; The ASCII value of letter U
// CallFrom= is the call from the station we heard the Packet
// CallTo =is the destination call (CQ,BEACON etc)
// DataLen= is the length of the data that follow
// the whole frame with the header is
// [port][DataKind][CallFrom][CallTo ][DataLen][USER][Data         ]
//  4bytes  4bytes     10bytes  10bytes   4bytes      4bytes  DataLen Bytes


// 4.RadioPort information
// 
// The LOWORD Port field is always 0
// The LOWORD DataKind field ='G'; The ASCII value of letter G
// CallFrom  empty(NULL)
// CallTo    empty(NULL)
// DataLen is the length of the data that follow
// USER is undefined
// the whole frame with the header is
// [port][DataKind][CallFrom][CallTo ][DataLen][USER][Data         ]
//  4bytes  4bytes     10bytes  10bytes   4bytes     4bytes    DataLen Bytes
// the data field format is as follow in plain text
// howmany ports ;1st radioport description;2nd radioport;....;last radioport describtion
// like
// 2;TNC2 on serialport 1;OE5DXL on serialport2;
// We have here 2 radioports. The separator is the ';'

// 10. Reply to a 'G' command. This frame returns the radioport number
//     and description
//
// Port field is always 0
// LOWORD DataKind field ='G'. The ASCII value of letter G
// CallFrom is empty (NULL)
// CallTo is empty(NULL)
// DataLen =The number of bytes of DATA field
// USER is undefined
// DATA field conatins the radioport description
// [ HEADER                                   ]
// [port][DataKind][CallFrom][CallTo ][DataLen][USER] [DATA]
//  4bytes  4bytes     10bytes  10bytes   4bytes     4bytes    Datalen bytes.
//
// The DATA field is organised as follow and is in plain ASCII.
//
// Number of Radioports;First radioport description(Friendlyname);Second radioport description(Friendly name)...........
//
// Number of radioports=a Decimal Value. A value of 3 means 3 radioports
// Radioport description= A string that describes the radioport.
// The separator between fields is the letter ';'.
// Just parse the whole DATA field and use as separator the ';'



//
// Xastir parses integer data like this:  That is, it is LITTLE ENDIAN
// 
// Fetch the length of the data portion of the packet
//    data_length = (unsigned char)(input_string[31]);
//    data_length = (data_length << 8) + (unsigned char)(input_string[30]);
//    data_length = (data_length << 8) + (unsigned char)(input_string[29]);
//    data_length = (data_length << 8) + (unsigned char)(input_string[28]);
//

***/

// Socket communication packet header
struct agwpeheader {
	uint32_t	radioPort;	// 0..3
	uint32_t	dataType;	// 4..7
	uint8_t		fromCall[10];	// 8..17
	uint8_t		toCall[10];	// 18..27
	uint32_t	dataLength;	// 28..31
	uint32_t	userField;	// 32..35
};


struct agwpesocket; // forward declarator

// One agwpecom per connection to AGWPE
struct agwpecom {
	int		fd;
	time_t		wait_until;

	struct netresolver *netaddr;

	int		socketscount;
	struct agwpesocket *sockets;

	int		wrlen;
	int		wrcursor;

	int		rdneed;  // this much in rdbuf before decision
	int		rdlen;
	int		rdcursor;

	uint8_t		wrbuf[4196];
	uint8_t		rdbuf[4196];
};

// One agwpesocket per interface
struct agwpesocket {
	int		portnum;
	struct interface *iface;
	struct agwpecom  *com;
};


static struct agwpecom *pecom;
static int              pecomcount;


static uint32_t fetch_le32(uint8_t *u) {
	return (u[3] << 24 |
		u[2] << 16 |
		u[1] <<  8 |
		u[0]);
}

static void set_le32(uint8_t *u, uint32_t value) {
	u[0] = (uint8_t)value;
	value >>= 8;
	u[1] = (uint8_t)value;
	value >>= 8;
	u[2] = (uint8_t)value;
	value >>= 8;
	u[3] = (uint8_t)value;
}


/*
 *  agwpe_flush()  -- write out buffered data - at least partially
 */
static void agwpe_flush(struct agwpecom *com)
{
	int i, len;

	if (com->fd < 0) return; // nothing to do!

	if ((com->wrlen == 0) || (com->wrlen > 0 && com->wrcursor >= com->wrlen)) {
	  com->wrlen = com->wrcursor = 0;	/* already all written */
	  return;
	}

	/* Now there is some data in between wrcursor and wrlen */

	len = com->wrlen - com->wrcursor;
	if (len > 0)
	  i = write(com->fd, com->wrbuf + com->wrcursor, len);
	else
	  i = 0;
	if (i > 0) {		/* wrote something */
		com->wrcursor += i;
		len = com->wrlen - com->wrcursor;
		if (len == 0) {
			com->wrcursor = com->wrlen = 0;	/* wrote all ! */
		} else {
			/* compact the buffer a bit */
			memcpy(com->wrbuf, com->wrbuf + com->wrcursor, len);
			com->wrcursor = 0;
			com->wrlen = len;
		}
	}
}


void agwpe_sendto(const void *_ap, const uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen) {

	struct agwpesocket *agwpe = (struct agwpesocket*)_ap;
	struct agwpecom *com = agwpe->com;
	int space = sizeof(com->wrbuf) - com->wrlen;
	struct agwpeheader hdr;

	if (debug) {
	  // printf("agwpe_sendto(->%s, axlen=%d)", S->ttycallsign[tncid], ax25rawlen);
	}
	if (com->fd < 0) {
	  if (debug)
	    printf("NOTE: Write to non-open AGWPE socket discarded.");
	  return;
	}

	agwpe_flush(com); // write out buffered data, if any

	if (space < (sizeof(struct agwpeheader) + axaddrlen + axdatalen)) {
	  // Uh, no space at all!
	  if (debug)
	    printf("ERROR: No buffer space to send data to AGWPE socket");
	  return;
	}

	memset(&hdr, 0, sizeof(hdr));
	set_le32((uint8_t*)(&hdr.radioPort), agwpe->portnum);
	set_le32((uint8_t*)(&hdr.dataType), 'K');
	set_le32((uint8_t*)(&hdr.dataLength), axaddrlen + axdatalen);

	memcpy(com->wrbuf + com->wrlen, &hdr, sizeof(hdr));
	com->wrlen += sizeof(hdr);
	memcpy(com->wrbuf + com->wrlen, axaddr, axaddrlen);
	com->wrlen += axaddrlen;
	memcpy(com->wrbuf + com->wrlen, axdata, axdatalen);
	com->wrlen += axdatalen;

	agwpe_flush(com); // write out buffered data
}



static int agwpe_controlwrite(struct agwpecom *com, const uint32_t oper) {

	int space = sizeof(com->wrbuf) - com->wrlen;
	struct agwpeheader hdr;

	if (debug) {
	  printf("agwpe_controlwrite(->%s, oper=%d)", "x", oper);
	}
	if (com->fd < 0) {
	  if (debug)
	    printf("NOTE: Write to non-open AGWPE socket discarded.");
	  return -1;
	}

	agwpe_flush(com); // write out buffered data, if any

	if (space < sizeof(hdr)) {
	  // No room :-(
	  return -1;
	}

	memset(&hdr, 0, sizeof(hdr));
	set_le32((uint8_t*)(&hdr.dataType), oper);
	
	memcpy(com->wrbuf + com->wrlen, &hdr, sizeof(hdr));
	com->wrlen += sizeof(hdr);

	agwpe_flush(com); // write out buffered data
	return 0;
}


// close the AGWPE communication socket, retry its call at some point latter
static void agwpe_reset(struct agwpecom *com) {
	if (com->fd < 0) {
	  // Should not happen..
	  return;
	}

	close(com->fd);
	com->fd = -1;
}


static void agwpe_parse_raw_ax25(struct agwpecom *com,
				 struct agwpeheader *hdr, const uint8_t *buf)
{
	int i;

#warning "WRITEME: AGWPE Raw AX.25 reception"	
}


static void agwpe_parsereceived(struct agwpecom *com,
				struct agwpeheader *hdr, const uint8_t *buf)
{

	uint8_t frameType = hdr->dataType;

	switch (frameType) {
	case 'K': // Raw AX.25 frame received
		agwpe_parse_raw_ax25(com, hdr, buf);
		break;

	default:  // Everything else: discard
		break;
	}
}



static void agwpe_read(struct agwpecom *com) {

	int rcvspace = sizeof(com->rdbuf) - com->rdlen;
	int rcvlen;
	struct agwpeheader hdr;

	if (com->fd < 0) {
	  // Should not happen..
	  return;
	}

	if (com->rdlen > com->rdcursor) {
	  memcpy(com->rdbuf, com->rdbuf + com->rdcursor,
		 com->rdlen - com->rdcursor);
	  com->rdlen -= com->rdcursor;
	}
	com->rdcursor = 0;


	rcvlen = read(com->fd, com->rdbuf + com->rdlen, rcvspace);
	if (rcvlen > 0)
	  com->rdlen += rcvlen;
	if (com->rdlen < com->rdneed) {
	  // insufficient amount received, continue with it latter
	  return;
	}

	while (com->rdlen >= com->rdneed) {

	  hdr.radioPort = fetch_le32(com->rdbuf + 0);
	  hdr.dataType  = fetch_le32(com->rdbuf + 4);
	  memcpy(hdr.fromCall, com->rdbuf + 8, 10);
	  memcpy(hdr.toCall,   com->rdbuf + 18, 10);
	  hdr.dataLength = fetch_le32(com->rdbuf + 28);
	  hdr.userField  = fetch_le32(com->rdbuf + 32);

	  if (com->rdneed < (sizeof(hdr) + hdr.dataLength)) {
	    // recalculate needed data size
	    com->rdneed = sizeof(hdr) + hdr.dataLength;
	  }
	  if (com->rdneed > sizeof(com->rdbuf)) {
	    // line noise or something...
	    agwpe_reset(com);
	    return;
	  }
	  if (com->rdlen < com->rdneed) {
	    // insufficient amount received..
	    break;
	  }
	  
	  // Process received frame
	  agwpe_parsereceived(com, &hdr, com->rdbuf + sizeof(hdr));

	  com->rdcursor += sizeof(hdr) + hdr.dataLength;
	  if (com->rdlen > com->rdcursor) {
	    memcpy(com->rdbuf, com->rdbuf + com->rdcursor,
		   com->rdlen - com->rdcursor);
	    com->rdlen -= com->rdcursor;
	  }
	  com->rdcursor = 0;
	  com->rdneed = sizeof(hdr);
	}
}


static void agwpe_connect(struct agwpecom *com) {
	int i;

	com->fd = socket(com->netaddr->ai.ai_family, com->netaddr->ai.ai_socktype,
			 com->netaddr->ai.ai_protocol);
	if (com->fd < 0) {
	  com->wait_until = now + 30; // redo in 30 seconds or so
	  if (debug)
	    printf("ERROR at AGWPE socket creation: errno=%d %s\n",errno,strerror(errno));
	  return;
	}
	fd_nonblockingmode(com->fd);

	i = connect(com->fd, com->netaddr->ai.ai_addr, com->netaddr->ai.ai_addrlen);
	// Should result "EINPROGRESS"
	if (i < 0 && (errno != EINPROGRESS)) {
	  // Unexpected fault!
	  if (debug)
	    printf("ERROR on non-blocking connect(): errno=%d (%s)\n", errno, strerror(errno));
	  close(com->fd);
	  com->fd = -1;
	  com->wait_until = now + 30; // redo in 30 seconds or so
	}
	agwpe_controlwrite(com, 'k'); // Ask for raw AX.25 frames
	agwpe_controlwrite(com, 'm'); // Ask for full monitoring of all interfaces
}


/*
 *  agwpe_init()
 */

void agwpe_init(void)
{
	/* nothing.. */
}

/*
 *  agwpe_start()
 */

void agwpe_start(void)
{
	/* nothing.. */
}



/*
 *  agwpe_prepoll()  --  prepare system for next round of polling
 */

int agwpe_prepoll(struct aprxpolls *app)
{
	int idx = 0;		/* returns number of *fds filled.. */
	int i;
	struct agwpecom *S;
	struct pollfd *pfd;

	for (i = 0; i < pecomcount; ++i) {
		S = &pecom[i];
		if (S->fd < 0) {
			/* Not an open TTY, but perhaps waiting ? */
			if ((S->wait_until != 0) && (S->wait_until > now)) {
				/* .. waiting for future! */
				if (app->next_timeout > S->wait_until)
					app->next_timeout = S->wait_until;
				/* .. but only until our timeout,
				   if it is sooner than global one. */
				continue;	/* Waiting on this one.. */
			}

			/* Waiting or not, FD is not open, and deadline is past.
			   Lets try to open! */

			agwpe_connect(S);

		}
		/* .. No open FD */
		/* Still no open FD ? */
		if (S->fd < 0)
			continue;

		// FD is open, lets mark it for poll read..
		pfd = aprxpolls_new(app);
		pfd->fd = S->fd;
		pfd->events = POLLIN | POLLPRI;
		pfd->revents = 0;
		// .. and if needed, poll write.
		if (S->wrlen > S->wrcursor)
			pfd->events |= POLLOUT;

		++idx;
	}
	return idx;
}

/*
 *  agwpe_postpoll()  -- Done polling, what happened ?
 */

int agwpe_postpoll(struct aprxpolls *app)
{
	int idx, i;

	struct agwpecom *S;
	struct pollfd *P;
	for (idx = 0, P = app->polls; idx < app->pollcount; ++idx, ++P) {

		if (!(P->revents & (POLLIN | POLLPRI | POLLERR | POLLHUP)))
			continue;	/* No read event we are interested in... */

		for (i = 0; i < pecomcount; ++i) {
			S = &pecom[i];
			if (S->fd != P->fd)
				continue;	/* Not this one ? */
			/* It is this one! */

			if (P->revents & POLLOUT)
				agwpe_flush(S);

			agwpe_read(S);
		}
	}

	return 0;
}
