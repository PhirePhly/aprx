/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */


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

/* Max number of things to pre-allocate several arrays */
#define MAXTTYS 16
#define MAXAPRSIS 4


#define MAXPOLLS 24 /* No really all that much, MAXTTYS + MAXAPRSIS,
		       plus some slack just in case... */


extern const char *mycall;
extern int die_now;

extern int  ttyreader_prepoll (int, struct pollfd **, time_t *);
extern int  ttyreader_postpoll (int, struct pollfd *);
extern void ttyreader_init (void);
extern const char *ttyreader_serialcfg(char *param1, char *param2, char *str);

extern void  ax25_to_tnc2(int cmdbyte, const unsigned char *frame, const int framelen);

extern void aprsis_add_server(const char *server, const char *port);
extern void aprsis_set_heartbeat_timeout(const int tout);
extern void aprsis_set_filter(const char *filter);
extern void aprsis_set_mycall(const char *filter);

extern int  aprsis_queue(const char *addr, const char *text, int textlen);
extern int  aprsis_prepoll(int nfds, struct pollfd **fdsp, time_t *tout);
extern int  aprsis_postpoll(int nfds, struct pollfd *fds);
extern void aprsis_init(void);
extern void aprsis_start(void);

extern int fd_nonblockingmode(int fd);

extern const char *version;

extern time_t now;
extern int    debug;
extern int    verbout;
extern int    erlangout;

extern void beacon_set(const char *s);
extern void beacon_reset(void);
extern int  beacon_prepoll(int nfds, struct pollfd **fdsp, time_t *tout);
extern int  beacon_postpoll(int nfds, struct pollfd *fds);


extern void netax25_init (void);
extern int  netax25_prepoll (int, struct pollfd **, time_t *);
extern int  netax25_postpoll (int, struct pollfd *);

extern void  readconfig(const char *cfgfile);
extern char *config_SKIPSPACE(char *Y);
extern char *config_SKIPTEXT(char *Y);
extern void  config_STRLOWER(char *Y);

extern void erlang_init(const char *syslog_facility_name);
extern void erlang_start(void);
extern int  erlang_prepoll(int nfds, struct pollfd **fdsp, time_t *tout);
extern int  erlang_postpoll(int nfds, struct pollfd *fds);
#define ERLANG_RX 0
#define ERLANG_TX 1
extern void erlang_add(const void *refp, const char *portname, int rx_or_tx, int bytes, int packets);
extern void erlang_set(const void *refp, const char *portname, int bytes_per_minute);
extern int erlangsyslog;
extern int erlanglog1min;


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
	char name[32];
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
	int	version;	/* format version			*/
	int	linecount;
	time_t	last_update;

	double	align_filler;
};

#define ERLANGLINE_STRUCT_VERSION ((1<<16)|sizeof(struct erlangline))
