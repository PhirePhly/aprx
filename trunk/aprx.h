/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * **************************************************************** */

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <syslog.h>


#define CALLSIGNLEN_MAX 9

#include "cellmalloc.h"
#include "historydb.h"
#include "keyhash.h"
#include "pbuf.h"

#if 0
#define static			/*ignore statics during debug */
#endif

struct aprx_interface; // Forward declarator


struct configfile {
	const char *name;
	FILE	*fp;
	int	linenum;
	char	buf[1024];
};

/* aprxpolls.c */
struct aprxpolls {
	struct pollfd *polls;
	int pollcount;
	int pollsize;
	time_t next_timeout;
};

extern void aprxpolls_reset(struct aprxpolls *app);
extern struct pollfd *aprxpolls_new(struct aprxpolls *app);

/* aprx.c */
extern const char *aprsis_login;
extern int die_now;
extern const char *mycall;
extern const char *tocall;

extern int fd_nonblockingmode(int fd);

extern const char *swname;
extern const char *swversion;

extern time_t now;
extern int debug;
extern int verbout;
extern int erlangout;
extern const char *rflogfile;
extern const char *aprxlogfile;
extern const char *erlanglogfile;
extern const char *pidfile;


/* ttyreader.c */
typedef enum {
	LINETYPE_KISS,		/* all KISS variants without CRC on line */
	LINETYPE_KISSSMACK,	/* KISS/SMACK variants with CRC on line */
	LINETYPE_KISSBPQCRC,	/* BPQCRC - really XOR sum of data bytes,
				   also "AEACRC"                        */
	LINETYPE_TNC2,		/* text line from TNC2 in monitor mode  */
	LINETYPE_AEA		/* not implemented...                   */
} LineType;

typedef enum {
	KISSSTATE_SYNCHUNT = 0,
	KISSSTATE_COLLECTING,
	KISSSTATE_KISSFESC
} KissState;


struct serialport {
	int fd;			/* UNIX fd of the port                  */

	time_t wait_until;
	time_t last_read_something;	/* Used by serial port functionality
					   watchdog */
	int read_timeout;	/* seconds                              */

	LineType linetype;

	KissState kissstate;	/* state for KISS frame reader,
				   also for line collector              */

	/* NOTE: The smack_probe is separate on all
	**       sub-tnc:s on SMACK loop
	*/
	time_t smack_probe[8];	/* if need to send SMACK probe, use this
				   to limit their transmit frequency.	*/
	int    smack_subids;    /* bitset; 0..7; could use char...	*/


	struct termios tio;	/* tcsetattr(fd, TCSAFLUSH, &tio)       */
	/*  stty speed 19200 sane clocal pass8 min 1 time 5 -hupcl ignbrk -echo -ixon -ixoff -icanon  */

	const char *ttyname;	/* "/dev/ttyUSB1234-bar22-xyz7" --
				   Linux TTY-names can be long..        */
	const char *ttycallsign[16]; /* callsign                             */
	const void *netax25[16];

	char *initstring[16];	/* optional init-string to be sent to
				   the TNC, NULL OK                     */
	int initlen[16];	/* .. as it can have even NUL-bytes,
				   length is important!                 */

	struct aprx_interface	*interface[16];


	unsigned char rdbuf[2000];	/* buffering area for raw stream read */
	int rdlen, rdcursor;	/* rdlen = last byte in buffer,
				   rdcursor = next to read.
				   When rdlen == 0, buffer is empty.    */
	unsigned char rdline[2000];	/* processed into lines/records       */
	int rdlinelen;		/* length of this record                */

	unsigned char wrbuf[4000];	/* buffering area for raw stream read */
	int wrlen, wrcursor;	/* wrlen = last byte in buffer,
				   wrcursor = next to write.
				   When wrlen == 0, buffer is empty.    */
};


extern int  ttyreader_prepoll(struct aprxpolls *);
extern int  ttyreader_postpoll(struct aprxpolls *);
extern void ttyreader_init(void);
// Old style init: ttyreader_serialcfg()
extern const char *ttyreader_serialcfg(struct configfile *cf, char *param1, char *str);
// New style init: ttyreader_new()
extern struct serialport *ttyreader_new(void);
extern void ttyreader_register(struct serialport *tty);
// extern void               ttyreader_setlineparam(struct serialport *tty, const char *ttyname, const int baud, int const kisstype);
// extern void               ttyreader_setkissparams(struct serialport *tty, const int tncid, const char *callsign, const int timeout);
extern void ttyreader_parse_ttyparams(struct configfile *cf, struct serialport *tty, char *str);
extern void ttyreader_kisswrite(struct serialport *S, const int tncid, const unsigned char *ax25raw, const int ax25rawlen);


extern void aprx_cfmakeraw(struct termios *, int f);

/* ax25.c */
extern int  ax25_to_tnc2(const struct aprx_interface *aif, const char *portname,
			 const int tncid, const int cmdbyte,
			 const unsigned char *frame, const int framelen);
extern void ax25_filter_add(const char *p1, const char *p2);

extern int  parse_ax25addr(unsigned char ax25[7], const char *text,
			   int ssidflags);


/* aprsis.c */
extern void aprsis_add_server(const char *server, const char *port);
extern void aprsis_set_heartbeat_timeout(const int tout);
extern void aprsis_set_filter(const char *filter);
extern void aprsis_set_login(const char *login);

extern int  aprsis_queue(const char *addr, int addrlen,
			 const char *gwcall,
			 const char *text, int textlen);
extern int  aprsis_prepoll(struct aprxpolls *app);
extern int  aprsis_postpoll(struct aprxpolls *app);
extern void aprsis_init(void);
extern void aprsis_start(void);
extern void aprsis_config(struct configfile *cf);

/* netbeacon.c */
extern int  validate_degmin_input(const char *s, int maxdeg);

extern void netbeacon_set(const char *s, char *);
extern void netbeacon_reset(void);
extern int  netbeacon_prepoll(struct aprxpolls *app);
extern int  netbeacon_postpoll(struct aprxpolls *app);
extern void netbeacon_config(struct configfile *cf);

/* rfbeacon.c */
extern int  rfbeacon_prepoll(struct aprxpolls *app);
extern int  rfbeacon_postpoll(struct aprxpolls *app);
extern void rfbeacon_config(struct configfile *cf, const int netonly);

/* config.c */
extern void *readconfigline(struct configfile *cf);
extern int   configline_is_comment(struct configfile *cf);
extern void  readconfig(const char *cfgfile);
extern char *config_SKIPSPACE(char *Y);
extern char *config_SKIPTEXT(char *Y, int *lenp);
extern void  config_STRLOWER(char *Y);
extern void  config_STRUPPER(char *Y);
extern int   validate_callsign_input(char *callsign, int strict);
extern int   config_parse_interval(const char *par, int *resultp);
extern int   config_parse_boolean(const char *par, int *resultp);

/* erlang.c */
extern void erlang_init(const char *syslog_facility_name);
extern void erlang_start(int do_create);
extern int  erlang_prepoll(struct aprxpolls *app);
extern int  erlang_postpoll(struct aprxpolls *app);

/* igate.c */
extern void igate_start(void);
extern void igate_from_aprsis(const char *ax25, int ax25len);
extern void igate_to_aprsis(const char *portname, int tncid, char *tnc2buf,
			    int tnc2len, int discard);
extern void enable_tx_igate(const char *, const char *);

/* netax25.c */
extern void        netax25_init(void);
extern void        netax25_start(void);
extern const void* netax25_open(const char *ifcallsign);
extern int         netax25_prepoll(struct aprxpolls *);
extern int         netax25_postpoll(struct aprxpolls *);
extern void        netax25_addrxport(const char *callsign, char *str, const struct aprx_interface *aif);
extern void        netax25_sendax25(const void *nax25, const void *ax25, int ax25len);
extern void        netax25_sendto(const void *nax25, const unsigned char *txbuf, const int txlen);

/* telemetry.c */
extern void telemetry_start(void);
extern int  telemetry_prepoll(struct aprxpolls *app);
extern int  telemetry_postpoll(struct aprxpolls *app);

typedef enum {
	ERLANG_RX,
	ERLANG_DROP,
	ERLANG_TX
} ErlangMode;

extern void erlang_add(const char *portname, ErlangMode erl, int bytes, int packets);
extern void erlang_set(const char *portname, int bytes_per_minute);

extern int erlangsyslog;
extern int erlanglog1min;
extern const char *erlang_backingstore;

/* The   struct erlangline  is shared in between the aprx, and
   erlang reporter application: aprx-erl */

struct erlang_rxtxbytepkt {
	long packets_rx, packets_rxdrop /* , packets_tx */ ;
	long bytes_rx,   bytes_rxdrop   /* , bytes_tx   */ ;
	time_t update;
};

struct erlangline {
	const void *refp;
	int index;
	char name[31];
	unsigned char __subport;
	time_t last_update;

	int erlang_capa;	/* bytes, 1 minute                      */

	struct erlang_rxtxbytepkt SNMP;	/* SNMPish counters             */

	struct erlang_rxtxbytepkt erl1m;	/*  1 minute erlang period    */
	struct erlang_rxtxbytepkt erl10m;	/* 10 minute erlang period    */
	struct erlang_rxtxbytepkt erl60m;	/* 60 minute erlang period    */

	int e1_cursor, e1_max;	/* next store point + max cursor index */
	int e10_cursor, e10_max;
	int e60_cursor, e60_max;

#ifdef EMBEDDED			/* When making very small memory footprint,
				   like embedding on Linksys WRT54GL ... */
#define APRXERL_1M_COUNT   (30)
#define APRXERL_10M_COUNT  (3)
#define APRXERL_60M_COUNT  (2)
#else
#define APRXERL_1M_COUNT   (60*24)
#define APRXERL_10M_COUNT  (60*24*7)
#define APRXERL_60M_COUNT  (24*31*3)
#endif
	struct erlang_rxtxbytepkt e1[APRXERL_1M_COUNT];	/* 1 minute RR, 24 hours */
	struct erlang_rxtxbytepkt e10[APRXERL_10M_COUNT];	/* 10 min RR, 7 days     */
	struct erlang_rxtxbytepkt e60[APRXERL_60M_COUNT];	/* 1 hour RR, 3 months  */
};

struct erlanghead {
	char title[32];
	int version;		/* format version                       */
	int linecount;
	time_t last_update;

	pid_t server_pid;
	time_t start_time;

	char mycall[16];

	double align_filler;
};

#define ERLANGLINE_STRUCT_VERSION ((sizeof(struct erlanghead)<<16)+sizeof(struct erlangline))

extern struct erlanghead *ErlangHead;
extern struct erlangline **ErlangLines;
extern int ErlangLinesCount;


/* dupecheck.c */


struct dupe_record_t {
	struct dupe_record_t *next;
	uint32_t hash;
	time_t	 t;
	int	 alen;	// Address length
	int	 plen;	// Payload length
	char	 addresses[20];
	char	*packet;
	char	 packetbuf[200]; /* 99.9+ % of time this is enough.. */
};

#define DUPECHECK_DB_SIZE 64        /* Hash index table size - per dupechecker */

typedef struct dupecheck_t {
	struct dupecheck_t *next;
	struct dupe_record_t *dupecheck_db[DUPECHECK_DB_SIZE]; /* Hash index table */
	

} dupecheck_t;

extern void         dupecheck_init(void);	/* Inits the dupechecker subsystem */
extern dupecheck_t *new_dupecheck(void);	/* Makes a new dupechecker  */
extern int	    dupecheck(dupecheck_t *dp, const char *addr, const int alen, const char *data, const int dlen); /* the checker */
extern int          dupecheck_prepoll(struct aprxpolls *app);
extern int          dupecheck_postpoll(struct aprxpolls *app);


/* kiss.c */

/* KISS protocol encoder/decoder specials */

#define KISS_FEND  (0xC0)
#define KISS_FESC  (0xDB)
#define KISS_TFEND (0xDC)
#define KISS_TFESC (0xDD)

extern int crc16_calc(unsigned char *buf, int n); /* SMACK's CRC16 */
extern int kissencoder(void *, int, const void *, int, int);



/* digipeater.c */
typedef enum {
	DIGIRELAY_UNSET,
	DIGIRELAY_DIGIPEAT,
	DIGIRELAY_THIRDPARTY
} digi_relaytype;

struct aprx_filter;    // Forward declarator
struct digipeater;     // Forward declarator

struct digipeater_source {
	struct digipeater     *parent;
	digi_relaytype	       src_relaytype;
	struct aprx_interface *src_if;
	struct aprx_filter    *src_filters;
};

struct tracewide {
	int   maxreq;
	int   maxdone;

	int   nkeys;
	char *keys[];
};

struct digipeater {
	struct aprx_interface *transmitter;
	int		       ratelimit;

	struct tracewide      *trace;
	struct tracewide      *wide;

	int                        sourcecount;
	struct digipeater_source **sources;

	int		       viscous_delay;
	// viscous queue ?
	void * viscous_queue; // FIXME: What ???
};

extern int  digipeater_prepoll(struct aprxpolls *app);
extern int  digipeater_postpoll(struct aprxpolls *app);

extern void digipeater_config(struct configfile *cf);
extern void digipeater_receive(struct digipeater_source *src, struct pbuf_t *pb);


/* interface.c */

typedef enum {
	IFTYPE_UNSET,
	IFTYPE_SERIAL,
	IFTYPE_AX25,
	IFTYPE_TCPIP,
	IFTYPE_APRSIS
} iftype_e;

struct aprx_interface {
	iftype_e    iftype;
	int	    timeout;

	char       *callsign;
	int	    subif;
	int         txok;
	int	    initlength;
	char	   *initstring;

	const void        *nax25p; // fix this
	struct serialport *tty;

	int	                   digicount;
	struct digipeater_source **digipeaters;
};

extern struct aprx_interface aprsis_interface;

extern int                     all_interfaces_count;
extern struct aprx_interface **all_interfaces;

extern void interface_init(void);
extern void interface_config(struct configfile *cf);
extern struct aprx_interface *find_interface_by_callsign(const char *callsign);


extern void interface_receive_ax25( const struct aprx_interface *aif, const char *ifaddress, const int is_aprs, const unsigned char *axbuf, const int axaddrlen, const int axlen, const char *tnc2buf, const int tnc2addrlen, const int tnc2len);
extern void interface_transmit_ax25(const struct aprx_interface *aif, const unsigned char *axbuf, const int axlen);
extern void interface_receive_tnc2( const struct aprx_interface *aif, const char *ifaddress, const char *rxbuf, const int rcvlen);
extern void interface_transmit_tnc2(const struct aprx_interface *aif, const char *rxbuf, const int rcvlen);


/* pbuf.c */

extern void           pbuf_get(struct pbuf_t *pb);
extern void           pbuf_put(struct pbuf_t *pb);
extern struct pbuf_t *pbuf_new(const int is_aprs, const int axdatalen, const int tnc2len);


