/* **************************************************************** *
 *                                                                  *
 *  APRSG-NG -- 2nd generation receive-only APRS-i-gate with        *
 *              minimal requirement of esoteric facilities or       *
 *              libraries of any kind beyond UNIX system libc.      *
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


/* #define static /*ignore statics during debug */

#define CFGFILE "aprsg.conf"

struct serialport {
	char	fd;		/* UNIX fd of the port					*/

	time_t	wait_until;

	int	linetype;	/* 0: KISS,  2: TNC2 monitor, 3: AEA TNC monitor	*/
#define LINETYPE_KISS 0		/* all KISS variants without CRC on line		*/
#define LINETYPE_KISSSMACK 1	/* KISS/SMACK variants with CRC on line			*/
#define LINETYPE_KISSBPQCRC 2	/* BPQCRC - really XOR sum of data bytes, also "AEACRC"	*/

#define LINETYPE_TNC2 4		/* text line from TNC2 in monitor mode -- incomplete	*/
#define LINETYPE_AEA  5		/* text line from AEA PK96 in monitor mode 1 - incomplete */


	struct termios tio;	/* tcsetattr(fd, TCSAFLUSH, &tio)			*/
  /*  stty speed 19200 sane clocal pass8 min 1 time 5 -hupcl ignbrk -echo -ixon -ixoff -icanon  */


	char	ttyname[32];	/* "/dev/ttyUSB1234-bar22-xyz7" -- Linux TTY-names can be long.. */
	char	*initstring;	/* optional init-string to be sent to the TNC, NULL ok	*/
	int	initlen;	/* .. as it can have even NUL-bytes, length is important! */

	char	rdbuf[1000];	/* buffering area for raw stream read			*/
	int	rdlen, rdcursor; /* rdlen = last byte in buffer, rdcursor = next to read.
				    When rdlen == 0, buffer is empty.			*/
	char	rdline[330];    /* processed into lines/records				*/
	int	rdlinelen;	/* length of this record				*/

	char	wrbuf[1000];	/* buffering area for raw stream read			*/
	int	wrlen, wrcursor; /* wrlen = last byte in buffer, wrcursor = next to write.
				    When wrlen == 0, buffer is empty.			*/

	int	kissstate;	/* state for KISS frame reader, also for line collector */
#define KISSSTATE_SYNCHUNT 0
#define KISSSTATE_COLLECTING 1
#define KISSSTATE_KISSFESC  2

	char	rdline2[330];	/* only in AEA PK96 format..				*/
};

#define MAXTTYS 16

extern struct serialport ttys[MAXTTYS];
extern int serialcount; /* How many are defined ? */

extern const char *mycall;
extern const char *aprsis_server_name;
extern const char *aprsis_server_port; /* numeric text, not an integer */
extern       int   aprsis_heartbeat_monitor_timeout;

#define TTY_OPEN_RETRY_DELAY_SECS 30

extern int  ttyreader_prepoll (int, struct pollfd **, time_t *);
extern int  ttyreader_postpoll (int, struct pollfd *);

extern void  ax25_to_tnc2(int cmdbyte, const char *frame, const int framelen);

extern int  aprsis_queue(const char *s, int len);
extern int  aprsis_prepoll(int nfds, struct pollfd **fdsp, time_t *tout);
extern int  aprsis_postpoll(int nfds, struct pollfd *fds);
extern void aprsis_cond_reconnect(void);
extern void aprsis_init(void);

extern const char *version;

extern time_t now;


extern void beacon_set(const char *s);
extern int  beacon_prepoll(int nfds, struct pollfd **fdsp, time_t *tout);
extern int  beacon_postpoll(int nfds, struct pollfd *fds);
