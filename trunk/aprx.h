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
#define __need_size_t
#define __need_NULL
#include <stddef.h>
#ifdef _FOR_VALGRIND_
#define strdup  aprx_strdup
#define strcmp  aprx_strcmp
#define strncmp aprx_strncmp
#define memcmp  aprx_memcmp
#define memcpy  aprx_memcpy
#define memchr  aprx_memchr
#define strlen  aprx_strlen
#define strcpy  aprx_strcpy
#define strncpy aprx_strncpy
#define strchr  aprx_strchr

// Single char at the time naive implementations for valgrind runs
extern int     memcmp(const char *p1, const char *p2, size_t n);
extern void   *memcpy(void *dest, const void *src, size_t n);
extern size_t  strlen(const char *p);
extern char   *strdup(const char *s);
extern int     strcmp(const char *s1, const char *s2);
extern int     strncmp(const char *s1, const char *s2, size_t n);
extern char   *strcpy(char *dest, const char *src);
extern char   *strncpy(char *dest, const char *src, size_t n);
extern void   *memchr(const void *s, int c, size_t n);
extern char   *strchr(const char *s, int c);

// extern declarators for standard functions
extern void *memset(void *s, int c, size_t n);
extern char *strerror(const int n);
extern void *memmove(void *dest, const void *src, size_t n);
extern char *strtok(char *str, const char *delim);
extern int   strcasecmp(const char *s1, const char *s2);

#else
#include <string.h>
#endif

#include <termios.h>
#include <errno.h>
#include <syslog.h>
#include <regex.h>
#include <alloca.h>

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
	int	linenum_i; // internal linenum
	int	linenum;   // externally presented, first line of folded multilines
	char	buf[8010];
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
extern const uint8_t tocall25[7];

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

extern void printtime(char *buf, int buflen);

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


	uint8_t rdbuf[2000];	/* buffering area for raw stream read */
	int rdlen, rdcursor;	/* rdlen = last byte in buffer,
				   rdcursor = next to read.
				   When rdlen == 0, buffer is empty.    */
	uint8_t rdline[2000];	/* processed into lines/records       */
	int rdlinelen;		/* length of this record                */

	uint8_t wrbuf[4000];	/* buffering area for raw stream read */
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
extern void ttyreader_kisswrite(struct serialport *S, const int tncid, const uint8_t *ax25raw, const int ax25rawlen);


extern void aprx_cfmakeraw(struct termios *, int f);

/* ax25.c */
extern int  ax25_to_tnc2_fmtaddress(char *dest, const uint8_t *src,
				    int markflag);
extern int  ax25_to_tnc2(const struct aprx_interface *aif, const char *portname,
			 const int tncid, const int cmdbyte,
			 const uint8_t *frame, const int framelen);
extern void ax25_filter_add(const char *p1, const char *p2);
extern int  ax25_format_to_tnc(const uint8_t *frame, const int framelen,
			       char *tnc2buf, const int tnc2buflen,
			       int *frameaddrlen, int *tnc2addrlen,
			       int *is_aprs, int *ui_pid);
extern int  parse_ax25addr(uint8_t ax25[7], const char *text,
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

/* beacon.c */
extern int  beacon_prepoll(struct aprxpolls *app);
extern int  beacon_postpoll(struct aprxpolls *app);
extern void beacon_config(struct configfile *cf);

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
extern void igate_to_aprsis(const char *portname, const int tncid, const char *tnc2buf, int tnc2addrlen, int tnc2len, const int discard);
extern void enable_tx_igate(const char *, const char *);
extern void rflog(const char *portname, int istx, int discard, const char *tnc2buf, int tnc2len);

/* netax25.c */
extern void        netax25_init(void);
extern void        netax25_start(void);
extern const void* netax25_open(const char *ifcallsign);
extern int         netax25_prepoll(struct aprxpolls *);
extern int         netax25_postpoll(struct aprxpolls *);
extern void      * netax25_addrxport(const char *callsign, const struct aprx_interface *aif);
extern void        netax25_sendax25(const void *nax25, const void *ax25, int ax25len);
extern void        netax25_sendto(const void *nax25, const uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen);

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
   erlang reporter application: aprx-stat */

struct erlang_rxtxbytepkt {
	long packets_rx, packets_rxdrop, packets_tx ;
	long bytes_rx,   bytes_rxdrop,   bytes_tx ;
	time_t update;
};

struct erlangline {
	const void *refp;
	int index;
	char name[31];
	uint8_t __subport;
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

#define APRXERL_1M_COUNT   (30)	      // 30 minutes of 1 minute data
#define APRXERL_10M_COUNT  (3)        // 30 minutes of 10 minute data
#define APRXERL_60M_COUNT  (2)        // 2 hours of 60 minute data

#else

#define APRXERL_1M_COUNT   (60*24)    // 1 day of 1 minute data
#define APRXERL_10M_COUNT  (60*24*7)  // 1 week of 10 minute data
#define APRXERL_60M_COUNT  (24*31*3)  // 3 months of hourly data
#endif
	struct erlang_rxtxbytepkt e1[APRXERL_1M_COUNT];
	struct erlang_rxtxbytepkt e10[APRXERL_10M_COUNT];
	struct erlang_rxtxbytepkt e60[APRXERL_60M_COUNT];
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


typedef struct dupe_record_t {
	struct dupe_record_t *next;
	uint32_t hash;
	time_t	 t;	// creation time
	time_t	 t_exp;	// expiration time

	struct pbuf_t *pbuf;	// To send packet out of delayed processing,
				// this pointer must be non-NULL.
        int16_t  seen;          // Count of times this packet has been seen
                                // on non-delayed processing.  First one will
                                // be sent when pbuf is != NULL.
        int16_t  delayed_seen;  // Count of times this packet has been seen
                                // on delayed processing.  The packet may get
                                // sent, if "seen" count is zero at delay end.
	int16_t  seen_on_transmitter; // Source of where it was seen is same
				// as this digipeater transmitter.
	int16_t  refcount; // number of references on this entry
	
	int16_t	 alen;	// Address length
	int16_t	 plen;	// Payload length

	char	 addresses[20];
	char	*packet;
	char	 packetbuf[200]; /* 99.9+ % of time this is enough.. */
} dupe_record_t;

#define DUPECHECK_DB_SIZE 16     /* Hash index table size - per dupechecker */

typedef struct dupecheck_t {
	struct dupe_record_t *dupecheck_db[DUPECHECK_DB_SIZE]; /* Hash index table */
} dupecheck_t;

extern void           dupecheck_init(void); /* Inits the dupechecker subsystem */
extern dupecheck_t   *dupecheck_new(void);  /* Makes a new dupechecker  */
extern dupe_record_t *dupecheck_get(dupe_record_t *dp); // increment refcount
extern void           dupecheck_put(dupe_record_t *dp); // decrement refcount
extern dupe_record_t *dupecheck_aprs(dupecheck_t *dp, const char *addr, const int alen, const char *data, const int dlen);     /* aprs checker */
extern dupe_record_t *dupecheck_pbuf(dupecheck_t *dp, struct pbuf_t *pb, const int viscous_delay); /* pbuf checker */
extern int            dupecheck_prepoll(struct aprxpolls *app);
extern int            dupecheck_postpoll(struct aprxpolls *app);


/* kiss.c */

/* KISS protocol encoder/decoder specials */

#define KISS_FEND  (0xC0)
#define KISS_FESC  (0xDB)
#define KISS_TFEND (0xDC)
#define KISS_TFESC (0xDD)

extern int crc16_calc(uint8_t *buf, int n); /* SMACK's CRC16 */
extern int kissencoder(void *, int, const void *, int, int);



/* digipeater.c */
typedef enum {
	DIGIRELAY_UNSET,
	DIGIRELAY_DIGIPEAT,
	DIGIRELAY_THIRDPARTY
} digi_relaytype;

struct filter_t;       // Forward declarator
struct digipeater;     // Forward declarator

struct tracewide {
	int    maxreq;
	int    maxdone;
	int    is_trace;

	int    nkeys;
	char **keys;
	int   *keylens;
};

struct digipeater_source {
	struct digipeater     *parent;
	digi_relaytype	       src_relaytype;
	struct aprx_interface *src_if;
	struct filter_t       *src_filters;
	struct tracewide      *src_trace;
	struct tracewide      *src_wide;

	// Viscous queue is at <source>, but used dupechecker
	// is <digipeater> -wide, common to all sources in that
	// digipeater.
	int                    viscous_delay;
	int	               viscous_queue_size;
	int	               viscous_queue_space;
	struct dupe_record_t **viscous_queue;

	int sourceregscount;
	regex_t **sourceregs;

	int destinationregscount;
	regex_t **destinationregs;

	int viaregscount;
	regex_t **viaregs;

	int dataregscount;
	regex_t **dataregs;
};

struct digipeater {
	struct aprx_interface *transmitter;
	int		       ratelimit;

	dupecheck_t           *dupechecker;

	const struct tracewide *trace;
	const struct tracewide *wide;

	int                        sourcecount;
	struct digipeater_source **sources;
};

extern int  digipeater_prepoll(struct aprxpolls *app);
extern int  digipeater_postpoll(struct aprxpolls *app);
extern void digipeater_config(struct configfile *cf);
extern void digipeater_receive(struct digipeater_source *src, struct pbuf_t *pb, const int do_3rdparty);
extern dupecheck_t *digipeater_find_dupecheck(const struct aprx_interface *aif);

/* interface.c */

typedef enum {
	IFTYPE_UNSET,
	IFTYPE_AX25,
	IFTYPE_SERIAL,
	IFTYPE_TCPIP,
	IFTYPE_APRSIS
} iftype_e;

struct aprx_interface {
	iftype_e    iftype;
	int	    timeout;

	char       *callsign;      // Callsign of this interface
	uint8_t     ax25call[7];   // AX.25 address field format callsign

	int	    aliascount;
	char	  **aliases;	   // Alias callsigns for this interface

	int	    subif;	   // Sub-interface index - for KISS uses
	int         txok;	   // This is Tx interface
	int	    txrefcount;    // Number of digipeaters using this as Tx
	int	    initlength;
	char	   *initstring;

	const void        *nax25p; // used on IFTYPE_AX25
	struct serialport *tty;    // used on IFTYPE_SERIAL, IFTYPE_TCPIP

	int	                   digisourcecount;
	struct digipeater_source **digisources;
};

extern struct aprx_interface aprsis_interface;

extern int                     all_interfaces_count;
extern struct aprx_interface **all_interfaces;

extern void interface_init(void);
extern void interface_config(struct configfile *cf);
extern struct aprx_interface *find_interface_by_callsign(const char *callsign);


extern void interface_receive_ax25( const struct aprx_interface *aif, const char *ifaddress, const int is_aprs, const int ui_pid, const uint8_t *axbuf, const int axaddrlen, const int axlen, const char *tnc2buf, const int tnc2addrlen, const int tnc2len);
extern void interface_transmit_ax25(const struct aprx_interface *aif, uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen);
extern void interface_receive_3rdparty(const struct aprx_interface *aif, const char *fromcall, const char *origtocall, const char *igatecall, const char *tnc2data, const int tnc2datalen);
extern int  interface_transmit_beacon(const struct aprx_interface *aif, const char *src, const char *dest, const char *via, const char *tncbuf, const int tnclen);


/* pbuf.c */
extern struct pbuf_t *pbuf_get(struct pbuf_t *pb);
extern void           pbuf_put(struct pbuf_t *pb);
extern struct pbuf_t *pbuf_new(const int is_aprs, const int digi_like_aprs, const int axdatalen, const int tnc2len);


/* parse_aprs.c */
extern int parse_aprs(struct pbuf_t *pb, int look_into_3rd_party );



/* filter.c */
struct filter_t;  // Forward declarator
struct client_t;  // Forward declarator
struct worker_t;  // Forward declarator

extern void filter_init(void);
extern int  filter_parse(struct filter_t **ffp, const char *filt);
extern void filter_free(struct filter_t *c);
extern int  filter_process(struct pbuf_t *pb, struct filter_t *f);

extern void filter_preprocess_dupefilter(struct pbuf_t *pb);
extern void filter_postprocess_dupefilter(struct pbuf_t *pb);

extern float filter_lat2rad(float lat);
extern float filter_lon2rad(float lon);
