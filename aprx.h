/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007,2008                            *
 *                                                                  *
 * **************************************************************** */

#include "config.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>


#if 0
 #define static /*ignore statics during debug */
#endif

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
extern const char *mycall;
extern int die_now;

extern int fd_nonblockingmode(int fd);

extern const char *swname;
extern const char *swversion;

extern time_t now;
extern int    debug;
extern int    verbout;
extern int    erlangout;
extern const char *rflogfile;
extern const char *aprxlogfile;
extern const char *erlanglogfile;
extern const char *pidfile;


/* ttyreader.c */
extern int  ttyreader_prepoll (struct aprxpolls *);
extern int  ttyreader_postpoll (struct aprxpolls *);
extern void ttyreader_init (void);
extern const char *ttyreader_serialcfg(char *param1, char *str);
extern int kissencoder(void *, int, const void *, int, int, int);

/* ax25.c */
extern void  tnc2_rxgate(const char *portname, int tncid, char *tnc2buf, int discard);
extern void  ax25_to_tnc2(const char *portname, int tncid, int cmdbyte, const unsigned char *frame, const int framelen);
extern void  ax25_filter_add(const char *p1, const char *p2);

extern int parse_ax25addr(unsigned char ax25[7], const char *text, int ssidflags);


/* aprsis.c */
extern void aprsis_add_server(const char *server, const char *port);
extern void aprsis_set_heartbeat_timeout(const int tout);
extern void aprsis_set_filter(const char *filter);
extern void aprsis_set_mycall(const char *filter);

extern int  aprsis_queue(const char *addr, const char *gwcall, const char *text, int textlen);
extern int  aprsis_prepoll(struct aprxpolls *app);
extern int  aprsis_postpoll(struct aprxpolls *app);
extern void aprsis_init(void);
extern void aprsis_start(void);

/* beacon.c */
extern void beacon_set(const char *s, char *);
extern void beacon_reset(void);
extern int  beacon_prepoll(struct aprxpolls *app);
extern int  beacon_postpoll(struct aprxpolls *app);

/* netax25.c */
extern void netax25_init(void);
extern int  netax25_prepoll(struct aprxpolls *);
extern int  netax25_postpoll(struct aprxpolls *);
extern void netax25_addport(const char *portname, char *str);
extern void netax25_sendax25(const void *ax25, int ax25len);
extern void netax25_sendax25_tnc2(const void *tnc2, int tnc2len, int is3rdparty);

/* config.c */
extern void  readconfig(const char *cfgfile);
extern char *config_SKIPSPACE(char *Y);
extern char *config_SKIPTEXT(char *Y);
extern void  config_STRLOWER(char *Y);
extern void  config_STRUPPER(char *Y);

/* erlang.c */
extern void erlang_init(const char *syslog_facility_name);
extern void erlang_start(int do_create);
extern int  erlang_prepoll(struct aprxpolls *app);
extern int  erlang_postpoll(struct aprxpolls *app);
#define ERLANG_RX 0
#define ERLANG_TX 1
extern void erlang_add(const void *refp, const char *portname, int subport, int rx_or_tx, int bytes, int packets);
extern void erlang_set(const void *refp, const char *portname, int subport, int bytes_per_minute);
extern int erlangsyslog;
extern int erlanglog1min;
extern const char *erlang_backingstore;

/* The   struct erlangline  is shared in between the aprx, and
   erlang reporter application: aprx-erl */

struct erlang_rxtxbytepkt {
	long	packets_rx, packets_tx;
	long	bytes_rx, bytes_tx;
	time_t	update;
};

struct erlangline {
	const void *refp;
	int	index;
	char    name[31];
	unsigned char subport;
	time_t	last_update;

	int     erlang_capa;	/* bytes, 1 minute			*/

	struct erlang_rxtxbytepkt SNMP; /* SNMPish counters		*/

	struct erlang_rxtxbytepkt erl1m;  /*  1 minute erlang period	*/
	struct erlang_rxtxbytepkt erl10m; /* 10 minute erlang period	*/
	struct erlang_rxtxbytepkt erl60m; /* 60 minute erlang period	*/

	int	e1_cursor,  e1_max;	/* next store point + max cursor index */
	int	e10_cursor, e10_max;
	int	e60_cursor, e60_max;

#define APRXERL_1M_COUNT   (60*24)
#define APRXERL_10M_COUNT  (60*24*7)
#define APRXERL_60M_COUNT  (24*31*3)

	struct erlang_rxtxbytepkt e1[APRXERL_1M_COUNT];   /* 1 minute RR, 24 hours */
	struct erlang_rxtxbytepkt e10[APRXERL_10M_COUNT]; /* 10 min RR, 7 days     */
	struct erlang_rxtxbytepkt e60[APRXERL_60M_COUNT]; /* 1 hour RR, 3 months  */
};

struct erlanghead {
	char	title[32];
	int	version;	/* format version			*/
	int	linecount;
	time_t	last_update;

	pid_t	server_pid;
	time_t	start_time;

	char	mycall[16];

	double	align_filler;
};

#define ERLANGLINE_STRUCT_VERSION ((sizeof(struct erlanghead)<<16)+sizeof(struct erlangline))

extern struct erlanghead  *ErlangHead;
extern struct erlangline **ErlangLines;
extern int                 ErlangLinesCount;
