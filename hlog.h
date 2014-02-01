/*
 *	aprsc
 *
 *	(c) Heikki Hannikainen, OH7LZB <hessu@hes.iki.fi>
 *
 *     This program is licensed under the BSD license, which can be found
 *     in the file LICENSE.
 *
 */

#ifndef LOG_H
#define LOG_H

#define LOG_LEN	2048

#define L_STDERR        1		/* Log to stderror */
#define L_SYSLOG        (1 << 1)	/* Log to syslog */
#define L_FILE		(1 << 2)	/* Log to a file */

#ifdef __CYGWIN__
#define L_DEFDEST	L_FILE
#else
#define L_DEFDEST	L_STDERR
#endif

#define LOG_LEVELS "emerg alert crit err warning notice info debug"
#define LOG_DESTS "syslog stderr file"

#include <syslog.h>

extern char *log_levelnames[];
extern char *log_destnames[];

extern int log_dest;    /* Logging destination */
extern int log_level;	/* Logging level */
extern char *log_dir;	/* Log directory */

extern int log_rotate_size;	/* Rotate log when it reaches a given size */
extern int log_rotate_num;	/* How many logs to keep around */


extern char *str_append(char *s, const char *fmt, ...);

extern int pick_loglevel(char *s, char **names);
extern int open_log(char *name, int reopen);
extern int close_log(int reopen);
extern int hlog(int priority, const char *fmt, ...);
extern int hlog_packet(int priority, const char *packet, int packetlen, const char *fmt, ...);

extern int accesslog_open(char *logd, int reopen);
extern int accesslog_close(char *reopenpath);
extern int accesslog(const char *fmt, ...);

extern int writepid(char *name);
extern int closepid(void);

#endif
