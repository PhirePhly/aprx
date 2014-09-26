/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation APRS iGate and digi with                 *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 * **************************************************************** */

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <assert.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
# include <time.h>
#endif
#include <unistd.h>
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#define __need_size_t
#define __need_NULL
#ifdef HAVE_STDDEF_H
# include <stddef.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
#include <pthread.h>
pthread_t aprsis_thread;
pthread_attr_t pthr_attrs;
#endif

#ifdef _FOR_VALGRIND_
#define strdup  aprx_strdup
#define strcmp  aprx_strcmp
#define strncmp aprx_strncmp
#define memcmp  aprx_memcmp
#define memcpy  aprx_memcpy
#define memchr  aprx_memchr
#define memrchr aprx_memrchr
#define strlen  aprx_strlen
#define strcpy  aprx_strcpy
#define strncpy aprx_strncpy
#define strchr  aprx_strchr

// Single char at the time naive implementations for valgrind runs
extern int     memcmp(const void *p1, const void *p2, size_t n);
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
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#endif

extern void   *memrchr(const void *s, int c, size_t n);

#include <termios.h>
#include <errno.h>
#include <syslog.h>
#include <regex.h>
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#endif

#include <ctype.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <math.h>

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

/* Radio interface groups on igate receiption history tracking.
 * Value range:  1 to MAX_IF_GROUP-1.
 * Value 0 is reserved for APRSIS.
 */
#define MAX_IF_GROUP 4

#define CALLSIGNLEN_MAX 9

struct aprxpolls; // forward declarator

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
	struct timeval next_timeout;
};
#define APRXPOLLS_INIT { NULL, 0, 0, {0,0} }

extern int  aprxpolls_millis(struct aprxpolls *app);
extern void aprxpolls_reset(struct aprxpolls *app);
extern struct pollfd *aprxpolls_new(struct aprxpolls *app);
extern void aprxpolls_free(struct aprxpolls *app);

/* aprx.c */
#ifndef DISABLE_IGATE
extern const char *aprsis_login;
#endif
extern int die_now;
extern const char *mycall;
extern const char *tocall;
extern const uint8_t tocall25[7];
extern float myloc_lat;
extern float myloc_coslat;
extern float myloc_lon;
extern const char *myloc_latstr;
extern const char *myloc_lonstr;

extern void fd_nonblockingmode(int fd);

extern const char *swname;
extern const char *swversion;

extern void timetick(void);
extern struct timeval now; // Public wall lock time that can jump around
extern struct timeval tick;  // Monotonic clock, progresses regularly from boot. NOT wall clock time.
extern int time_reset;      // Set during ONE call cycle of prepolls
extern int debug;
extern int verbout;
extern int erlangout;
extern const char *rflogfile;
extern const char *aprxlogfile;
extern const char *dprslogfile;
extern const char *erlanglogfile;
extern const char *pidfile;

extern void printtime(char *buf, int buflen);
extern void aprx_syslog_init(const char *syslog_fac);

#ifdef HAVE_STDARG_H
#ifdef __STDC__
extern void aprxlog(const char *fmt, ...);
#endif
#else
/* VARARGS */
extern void aprxlog(va_list);
#endif
extern void rflog(const char *portname, char direction, int discard, const char *tnc2buf, int tnc2len);
extern void rfloghex(const char *portname, char direction, int discard, const uint8_t *buf, int buflen);

/* netresolver.c */
extern void netresolv_start(void); // separate thread working on this!
extern void netresolv_stop(void);

struct netresolver {
	char const	*hostname;
	char const	*port;
	time_t	re_resolve_time;
	struct addrinfo ai;
	struct sockaddr sa;
};

extern struct netresolver *netresolv_add(const char *hostname, const char *port);

/* ttyreader.c */
typedef enum {
	LINETYPE_KISS,		/* all KISS variants without CRC on line */
	LINETYPE_KISSSMACK,	/* KISS/SMACK variants with CRC on line */
	LINETYPE_KISSFLEXNET,	/* KISS/FLEXNET with CRC on line */
	LINETYPE_KISSBPQCRC,	/* BPQCRC - really XOR sum of data bytes,
				   also "AEACRC"                        */
	LINETYPE_TNC2,		/* text line from TNC2 in monitor mode  */
	LINETYPE_AEA,		/* not implemented...                   */

	LINETYPE_DPRSGW		/* Special DPRS RX GW mode              */
} LineType;

typedef enum {
	KISSSTATE_SYNCHUNT = 0,
	KISSSTATE_COLLECTING,
	KISSSTATE_KISSFESC
} KissState;

struct serialport {
	int fd;			/* UNIX fd of the port                  */

	struct timeval wait_until;
	time_t last_read_something;	/* Used by serial port functionality
					   watchdog */
	int read_timeout;	/* seconds                              */
	int poll_millis;        /* milliseconds (0 = none.)             */

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

	time_t  rdline_time;	/* last time something was added there  */
	uint8_t rdline[2000];	/* processed into lines/records         */
	int rdlinelen;		/* length of this record                */

	uint8_t wrbuf[4000];	/* buffering area for raw stream read */
	int wrlen, wrcursor;	/* wrlen = last byte in buffer,
				   wrcursor = next to write.
				   When wrlen == 0, buffer is empty.    */

	void *dprsgw;		/* opaque DPRS GW data */
};


extern int  ttyreader_prepoll(struct aprxpolls *);
extern int  ttyreader_postpoll(struct aprxpolls *);
extern void ttyreader_init(void);
// Old style init: ttyreader_serialcfg()
extern const char *ttyreader_serialcfg(struct configfile *cf, char *param1, char *str);
// New style init: ttyreader_new()
extern struct serialport *ttyreader_new(void);
extern void ttyreader_register(struct serialport *tty);
extern int  ttyreader_getc(struct serialport *tty);
// extern void               ttyreader_setlineparam(struct serialport *tty, const char *ttyname, const int baud, int const kisstype);
// extern void               ttyreader_setkissparams(struct serialport *tty, const int tncid, const char *callsign, const int timeout);
extern int  ttyreader_parse_ttyparams(struct configfile *cf, struct serialport *tty, char *str);
extern void ttyreader_linewrite(struct serialport *S);
extern int  ttyreader_parse_nullparams(struct configfile *cf, struct serialport *tty, char *str);

extern void hexdumpfp(FILE *fp, const uint8_t *buf, const int len, int axaddr);
extern void aprx_cfmakeraw(struct termios *, int f);

extern void tv_timerbounds(const char *, struct timeval *tv, const int margin, void (*resetfunc)(void*), void *resetarg );
extern void tv_timeradd_millis(struct timeval *res, struct timeval * const a, const int millis);
extern void tv_timeradd_seconds(struct timeval *res, struct timeval * const a, const int seconds);
extern int  tv_timerdelta_millis(struct timeval * const _now, struct timeval * const _target);
extern int  tv_timercmp(struct timeval * const a, struct timeval * const b);
extern int  timecmp(time_t a, time_t b);


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


#ifndef DISABLE_IGATE
/* aprsis.c */
extern int  aprsis_add_server(const char *server, const char *port);
extern int  aprsis_set_heartbeat_timeout(const int tout);
extern int  aprsis_set_filter(const char *filter);
extern int  aprsis_set_login(const char *login);
#define qTYPE_IGATED   'R'
#define qTYPE_LOCALGEN 'S'
extern int  aprsis_queue(const char *addr, int addrlen,
			 const char qtype, const char *gwcall,
			 const char *text, int textlen);
extern int  aprsis_prepoll(struct aprxpolls *app);
extern int  aprsis_postpoll(struct aprxpolls *app);
extern void aprsis_init(void);
extern void aprsis_start(void);
extern void aprsis_stop(void);
extern int  aprsis_config(struct configfile *cf);
extern char * const aprsis_loginid;
#endif

/* beacon.c */
extern int  beacon_prepoll(struct aprxpolls *app);
extern int  beacon_postpoll(struct aprxpolls *app);
extern int  beacon_config(struct configfile *cf);
extern void beacon_childexit(int pid);

/* config.c */
extern void *readconfigline(struct configfile *cf);
extern int   configline_is_comment(struct configfile *cf);
extern int   readconfig(const char *cfgfile);
extern char *config_SKIPSPACE(char *Y);
extern char *config_SKIPTEXT(char *Y, int *lenp);
extern void  config_STRLOWER(char *Y);
extern void  config_STRUPPER(char *Y);
extern int   validate_callsign_input(char *callsign, int strict); // this modifies callsign string!
extern int   config_parse_interval(const char *par, int *resultp);
extern int   config_parse_boolean(const char *par, int *resultp);
extern const char *scan_int(const char *p, int len, int*val, int*seen_space);
extern int   validate_degmin_input(const char *s, int maxdeg);


/* dprsgw.c */
extern int  dprsgw_pulldprs(struct serialport *S);
extern int  dprsgw_prepoll(struct aprxpolls *app);
extern int  dprsgw_postpoll(struct aprxpolls *app);


/* erlang.c */
extern void erlang_init(const char *syslog_facility_name);
extern void erlang_start(int do_create);
extern int  erlang_prepoll(struct aprxpolls *app);
extern int  erlang_postpoll(struct aprxpolls *app);

/* igate.c */
#ifndef DISABLE_IGATE
extern void igate_start(void);
extern void igate_from_aprsis(const char *ax25, int ax25len);
extern void igate_to_aprsis(const char *portname, const int tncid, const char *tnc2buf, int tnc2addrlen, int tnc2len, const int discard, const int strictax25);
extern void enable_tx_igate(const char *, const char *);
#endif
extern const char *tnc2_verify_callsign_format(const char *t, int starok, int strictax25, const char *e);

/* netax25.c */
#ifdef PF_AX25			/* PF_AX25 exists -- highly likely a Linux system ! */
extern void        netax25_init(void);
extern void        netax25_start(void);
extern const void* netax25_open(const char *ifcallsign);
extern int         netax25_prepoll(struct aprxpolls *);
extern int         netax25_postpoll(struct aprxpolls *);
extern void      * netax25_addrxport(const char *callsign, const struct aprx_interface *aif);
extern void        netax25_sendax25(const void *nax25, const void *ax25, int ax25len);
extern void        netax25_sendto(const void *nax25, const uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen);
#endif

/* telemetry.c */

#define USE_ONE_MINUTE_DATA 0

extern void telemetry_start(void);
extern int  telemetry_prepoll(struct aprxpolls *app);
extern int  telemetry_postpoll(struct aprxpolls *app);
extern int  telemetry_config(struct configfile *cf);


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

#ifdef ERLANGSTORAGE
	struct erlang_rxtxbytepkt erl1m;	/*  1 minute erlang period    */
	struct erlang_rxtxbytepkt erl10m;	/* 10 minute erlang period    */
	struct erlang_rxtxbytepkt erl60m;	/* 60 minute erlang period    */
#else
#if (USE_ONE_MINUTE_DATA == 1)
	struct erlang_rxtxbytepkt erl1m;	/*  1 minute erlang period    */
#else
	struct erlang_rxtxbytepkt erl10m;	/* 10 minute erlang period    */
#endif
#endif

#ifdef ERLANGSTORAGE
	int e1_cursor, e1_max;	/* next store point + max cursor index */
	int e10_cursor, e10_max;
	int e60_cursor, e60_max;
#else
#if (USE_ONE_MINUTE_DATA == 1)
	int e1_cursor, e1_max;	/* next store point + max cursor index */
#else
	int e10_cursor, e10_max;
#endif
#endif

#ifdef ERLANGSTORAGE

#define APRXERL_1M_COUNT   (60*24)    // 1 day of 1 minute data
#define APRXERL_10M_COUNT  (60*24*7)  // 1 week of 10 minute data
#define APRXERL_60M_COUNT  (24*31*3)  // 3 months of hourly data
	struct erlang_rxtxbytepkt e1[APRXERL_1M_COUNT];
	struct erlang_rxtxbytepkt e10[APRXERL_10M_COUNT];
	struct erlang_rxtxbytepkt e60[APRXERL_60M_COUNT];
#else /* EMBEDDED */		/* When making very small memory footprint,
				   like embedding on Linksys WRT54GL ... */

#define APRXERL_1M_COUNT   (22)	      // 22 minutes of 1 minute data
#define APRXERL_10M_COUNT  (3)	      // 30 minutes of 10 minute data
#if (USE_ONE_MINUTE_DATA == 1)
	struct erlang_rxtxbytepkt e1[APRXERL_1M_COUNT];
#else
	struct erlang_rxtxbytepkt e10[APRXERL_10M_COUNT];
#endif
#endif
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
	int	storetime;
	struct dupe_record_t *dupecheck_db[DUPECHECK_DB_SIZE]; /* Hash index table */
} dupecheck_t;

extern void           dupecheck_init(void); /* Inits the dupechecker subsystem */
extern dupecheck_t   *dupecheck_new(const int storetime);  /* Makes a new dupechecker  */
extern dupe_record_t *dupecheck_get(dupe_record_t *dp); // increment refcount
extern void           dupecheck_put(dupe_record_t *dp); // decrement refcount
extern dupe_record_t *dupecheck_aprs(dupecheck_t *dp, const char *addr, const int alen, const char *data, const int dlen);     /* aprs checker */
extern dupe_record_t *dupecheck_pbuf(dupecheck_t *dp, struct pbuf_t *pb, const int viscous_delay); /* pbuf checker */
extern int            dupecheck_prepoll(struct aprxpolls *app);
extern int            dupecheck_postpoll(struct aprxpolls *app);


/* crc.c */

// kissencoder() needs direct access to CRC tables..
extern const uint16_t crc16_table[256];
extern const uint16_t crc_flex_table[256];

extern uint16_t calc_crc_16(const uint8_t *buf, int n);    /* SMACK's CRC-16 */
extern uint16_t calc_crc_flex(const uint8_t *buf, int n);  /* FLEXNET's CRC */
extern uint16_t calc_crc_ccitt(uint16_t crc, const uint8_t *buf, int len); // X.25's FCS a.k.a. CRC-CCITT a.k.a. CCITT-CRC
extern int      check_crc_16(const uint8_t *buf, int n);   /* SMACK's CRC-16 */
extern int      check_crc_flex(const uint8_t *buf, int n); /* FLEXNET's CRC */
extern int      check_crc_ccitt(const uint8_t *buf, int n);

/* KISS protocol encoder/decoder specials */

#define KISS_FEND  (0xC0)
#define KISS_FESC  (0xDB)
#define KISS_TFEND (0xDC)
#define KISS_TFESC (0xDD)

extern int  kissencoder(void *, int, LineType, const void *, int, int);
extern void kiss_kisswrite(struct serialport *S, const int tncid, const uint8_t *ax25raw, const int ax25rawlen);
extern int  kiss_pullkiss(struct serialport *S);
extern void kiss_poll(struct serialport *S);


/* digipeater.c */
typedef enum {
	DIGIRELAY_UNSET,
	DIGIRELAY_DIGIPEAT,
	DIGIRELAY_DIGIPEAT_DIRECTONLY,
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
#ifndef DISABLE_IGATE
	char		      *via_path; // for APRSIS only
	char		      *msg_path; // for APRSIS only
	uint8_t		       ax25viapath[7]; // APRSIS
	uint8_t		       msgviapath[7];  // APRSIS
#endif

	float		       tokenbucket;
	float		       tbf_increment;
	float		       tbf_limit;

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
	float		       tokenbucket;  // Per transmitter TokenBucket filter
	float		       tbf_increment;
	float		       tbf_limit;
	float		       src_tbf_increment; // Source call specific TokenBucket rules
        float                  src_tbf_limit;

	dupecheck_t           *dupechecker; // Per transmitter dupecheck
#ifndef DISABLE_IGATE
	historydb_t	      *historydb;   // Per transmitter HistoryDB
#endif

	const struct tracewide *trace;
	const struct tracewide *wide;

	int                        sourcecount;
	struct digipeater_source **sources;
};

extern int  digipeater_prepoll(struct aprxpolls *app);
extern int  digipeater_postpoll(struct aprxpolls *app);
extern int  digipeater_config(struct configfile *cf);
extern void digipeater_receive(struct digipeater_source *src, struct pbuf_t *pb);
extern int  digipeater_receive_filter(struct digipeater_source *src, struct pbuf_t *pb);
extern dupecheck_t *digipeater_find_dupecheck(const struct aprx_interface *aif);

/* interface.c */

typedef enum {
	IFTYPE_UNSET,
	IFTYPE_AX25,
	IFTYPE_SERIAL,
	IFTYPE_TCPIP,
	IFTYPE_AGWPE,
	IFTYPE_NULL,
	IFTYPE_APRSIS
} iftype_e;


struct aprx_interface {
	iftype_e    iftype;
	int	    timeout;
	int16_t	    ifindex;       // Absolute index on this interface
	int16_t	    ifgroup;	   // Group definition on this interface

	char       *callsign;      // Callsign of this interface
	uint8_t     ax25call[7];   // AX.25 address field format callsign

	int	    aliascount;
	char	  **aliases;	   // Alias callsigns for this interface

	int8_t	    subif;	   // Sub-interface index - for KISS uses
	uint8_t	    txrefcount;    // Number of digipeaters using this as Tx
	unsigned    tx_ok:1;	   // This is Tx interface
	unsigned    telemeter_to_is:1; // Telemeter this to APRS-IS
	unsigned    telemeter_to_rf:1; // Telemeter this to this radio port
	unsigned    telemeter_newformat:1; // Telemeter in "new format"

	int	    initlength;
	char	   *initstring;

	const void        *nax25p; // used on IFTYPE_AX25
#ifdef ENABLE_AGWPE
	const void	  *agwpe;  // used on IFTYPE_AGWPE
#endif
	struct serialport *tty;    // used on IFTYPE_SERIAL, IFTYPE_TCPIP

	int	                   digisourcecount;
	struct digipeater_source **digisources;
};

extern struct aprx_interface aprsis_interface;

extern int                     top_interfaces_group;
extern int                     all_interfaces_count;
extern struct aprx_interface **all_interfaces;

extern void interface_init(void);
extern int  interface_config(struct configfile *cf);
extern struct aprx_interface *find_interface_by_callsign(const char *callsign);

extern int interface_is_beaconable( const struct aprx_interface *iface );
extern int interface_is_telemetrable(const struct aprx_interface *iface );

extern void interface_receive_ax25( const struct aprx_interface *aif, const char *ifaddress, const int is_aprs, const int ui_pid, const uint8_t *axbuf, const int axaddrlen, const int axlen, const char *tnc2buf, const int tnc2addrlen, const int tnc2len);
extern void interface_transmit_ax25(const struct aprx_interface *aif, uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen);
extern void interface_receive_3rdparty(const struct aprx_interface *aif, char **heads, const int headscount,  const char *gwtype, const char *tnc2data, const int tnc2datalen);
extern int  interface_transmit_beacon(const struct aprx_interface *aif, const char *src, const char *dest, const char *via, const char *tncbuf, const int tnclen);
extern int process_message_to_myself(const struct aprx_interface*const srcif, const struct pbuf_t*const pb);


/* pbuf.c */
extern void           pbuf_init(void);
extern struct pbuf_t *pbuf_get(struct pbuf_t *pb);
extern void           pbuf_put(struct pbuf_t *pb);
extern struct pbuf_t *pbuf_new(const int is_aprs, const int digi_like_aprs, const int tnc2addrlen, const char *tnc2buf, const int tnc2len, const int ax25addrlen, const void *ax25buf, const int ax25len );


/* parse_aprs.c */
extern int parse_aprs(struct pbuf_t*const pb, historydb_t*const historydb);

struct aprs_message_t {
        const char *body;          /* message body */
        const char *msgid;
        
        int body_len;
        int msgid_len;
        int is_ack;
        int is_rej;
};

extern int parse_aprs_message(const struct pbuf_t*const pb, struct aprs_message_t*const am);


/* filter.c */
struct filter_t;  // Forward declarator
struct client_t;  // Forward declarator
struct worker_t;  // Forward declarator

extern void filter_init(void);
extern int  filter_parse(struct filter_t **ffp, const char *filt);
extern void filter_free(struct filter_t *c);
extern int  filter_process(struct pbuf_t *pb, struct filter_t *f, historydb_t *historydb);

extern void filter_preprocess_dupefilter(struct pbuf_t *pb);
extern void filter_postprocess_dupefilter(struct pbuf_t *pb, historydb_t *historydb);

extern float filter_lat2rad(float lat);
extern float filter_lon2rad(float lon);

#ifdef ENABLE_AGWPE
/* agwpesocket.c */
extern void *agwpe_addport(const char *hostname, const char *hostport, const char *agwpeport, const struct aprx_interface *interface);
extern void agwpe_sendto(const void *_ap, const uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen);

extern int  agwpe_prepoll(struct aprxpolls *);
extern int  agwpe_postpoll(struct aprxpolls *);
extern void agwpe_init(void);
extern void agwpe_start(void);
#endif
