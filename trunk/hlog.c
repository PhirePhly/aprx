/*
 *	aprsc
 *
 *	(c) Heikki Hannikainen, OH7LZB <hessu@hes.iki.fi>
 *
 *	This program is licensed under the BSD license, which can be found
 *	in the file LICENSE.
 *
 */

/*
 *	log.c
 *
 *	logging facility with configurable log levels and
 *	logging destinations
 */

#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <errno.h>

#include "hlog.h"
#include "hmalloc.h"
#include "rwlock.h"

int log_dest = L_DEFDEST;	/* Logging destination */
int log_level = LOG_INFO;	/* Logging level */
int log_facility = LOG_DAEMON;	/* Logging facility */
char *log_name = NULL;		/* Logging name */

char log_basename[] = "aprsc.log";
char *log_dir = NULL;		/* Access log directory */
char *log_fname = NULL;		/* Access log file name */
int log_file = -1;		/* If logging to a file, the file name */
rwlock_t log_file_lock = RWL_INITIALIZER;

char accesslog_basename[] = "aprsc.access.log";
char *accesslog_dir = NULL;	/* Access log directory */
char *accesslog_fname = NULL;	/* Access log file name */
int accesslog_file = -1;	/* Access log fd */
rwlock_t accesslog_lock = RWL_INITIALIZER;

int log_rotate_size = 0;	/* Rotate log when it reaches a given size */
int log_rotate_num = 5;		/* How many logs to keep around */

char *log_levelnames[] = {
	"EMERG",
	"ALERT",
	"CRIT",
	"ERROR",
	"WARNING",
	"NOTICE",
	"INFO",
	"DEBUG",
	NULL
};

char *log_destnames[] = {
	"none",
	"stderr",
	"syslog",
	"file",
	NULL
};

/*
 *	Quote a string, C-style. dst will be null-terminated, always.
 */

static int str_quote(char *dst, int dst_len, const char *src, int src_len)
{
	int si;
	int di = 0;
	int dst_use_len = dst_len - 2; /* leave space for terminating NUL and escaping an escape  */
	unsigned char c;
	
	for (si = 0; si < src_len; si++) {
		if (di >= dst_use_len)
			break;
		
		c = (unsigned char) src[si];
		
		/* printable ASCII */
		if (c >= 0x20 && c < 0x7f) {
			/* escape the escape (space reserved already) */
			if (c == '\\')
				dst[di++] = '\\';
			
			dst[di++] = c;
			continue;
		}
		
		/* hex escape, is going to take more space */
		if (di >= dst_use_len - 4)
			break;
		
		dst[di++] = '\\';
		dst[di++] = 'x';
		di += snprintf(dst + di, 3, "%.2X", c);
	}
	
	dst[di++] = 0;
	
	return di;
}

/*
 *	Append a formatted string to a dynamically allocated string
 */

char *str_append(char *s, const char *fmt, ...)
{
	va_list args;
	char buf[LOG_LEN];
	int len;
	char *ret;
	
	va_start(args, fmt);
	vsnprintf(buf, LOG_LEN, fmt, args);
	va_end(args);
	buf[LOG_LEN-1] = 0;
	
	len = strlen(s);
	ret = hrealloc(s, len + strlen(buf) + 1);
	strcpy(ret + len, buf);
	
	return ret;
}

/*
 *	Pick a log level
 */

int pick_loglevel(char *s, char **names)
{
	int i;
	
	for (i = 0; (names[i]); i++)
		if (!strcasecmp(s, names[i]))
			return i;
			
	return -1;
}

/*
 *	Open log
 */
 
int open_log(char *name, int reopen)
{
	if (!reopen)
		rwl_wrlock(&log_file_lock);
		
	if (log_name)
		hfree(log_name);
		
	if (!(log_name = hstrdup(name))) {
		fprintf(stderr, "aprsc logger: out of memory!\n");
		exit(1);
	}
	
	if (log_dest == L_SYSLOG)
		openlog(name, LOG_NDELAY|LOG_PID, log_facility);
	
	if (log_dest == L_FILE) {
		if (log_fname)
			hfree(log_fname);
		
		log_fname = hmalloc(strlen(log_dir) + strlen(log_basename) + 2);
		sprintf(log_fname, "%s/%s", log_dir, log_basename);
		
		log_file = open(log_fname, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP);
		if (log_file < 0) {
			fprintf(stderr, "aprsc logger: Could not open %s: %s\n", log_fname, strerror(errno));
			exit(1);
		}
	}
	
	rwl_wrunlock(&log_file_lock);
	
	if (log_dest == L_FILE)
		hlog(LOG_DEBUG, "Log file %s %sopened on fd %d", log_fname, (reopen) ? "re" : "", log_file);
	
	return 0;
}

/*
 *	Close log
 */
 
int close_log(int reopen)
{
	hlog(LOG_DEBUG, "close_log");
	
	char *s = NULL;
	if (log_name)
		s = hstrdup(log_name);
	
	rwl_wrlock(&log_file_lock);
	
	if (log_name) {
		hfree(log_name);
		log_name = NULL;
	}
	
	if (log_dest == L_SYSLOG) {
		closelog();
	} else if (log_dest == L_FILE) {
		if (log_file >= 0) {
			if (close(log_file))
				fprintf(stderr, "aprsc logger: Could not close log file %s: %s\n", log_fname, strerror(errno));
			log_file = -1;
		}
		if (log_fname) {
			hfree(log_fname);
			log_fname = NULL;
		}
	}
	
	if (reopen && s)
		open_log(s, 1);
	
	if (!reopen)
		rwl_wrunlock(&log_file_lock);
	
	if (s)
		hfree(s);
	
	return 0;
}

/*
 *	Rotate the log file
 */

int rotate_log(void)
{
	char *tmp;
	int i;
	char *r1, *r2;
	
	if (rwl_trywrlock(&log_file_lock)) {
		fprintf(stderr, "failed to wrlock log_file_lock for rotation\n");
		return 0;
	}
	
	// check if still oversize and not rotated by another thread
	off_t l = lseek(log_file, 0, SEEK_CUR);
	if (l < log_rotate_size) {
		rwl_wrunlock(&log_file_lock);
		return 0;
	}
	
	// rename
	tmp = hmalloc(strlen(log_fname) + 6);
	sprintf(tmp, "%s.tmp", log_fname);
	if (rename(log_fname, tmp) != 0) {
		fprintf(stderr, "aprsc logger: Failed to rename %s to %s: %s\n", log_fname, tmp, strerror(errno));
		// continue anyway, try to reopen
	}
	
	// reopen
	if (close(log_file))
		fprintf(stderr, "aprsc logger: Could not close log file %s: %s\n", log_fname, strerror(errno));
	
	log_file = open(log_fname, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP);
	if (log_file < 0) {
		fprintf(stderr, "aprsc logger: Could not open %s: %s\n", log_fname, strerror(errno));
		log_file = -1;
	}
	
	rwl_wrunlock(&log_file_lock);
	
	// do the rest of the rotation
	r1 = hmalloc(strlen(log_fname) + 16);
	r2 = hmalloc(strlen(log_fname) + 16);
	
	for (i = log_rotate_num-1; i > 0; i--) {
		sprintf(r1, "%s.%d", log_fname, i-1);
		sprintf(r2, "%s.%d", log_fname, i);
		if (rename(r1, r2) != 0 && errno != ENOENT) {
			fprintf(stderr, "rename %s => %s failed:%s\n", r1, r2, strerror(errno));
		}
	}
	
	if (rename(tmp, r1) != 0) {
		fprintf(stderr, "aprsc logger: Failed to rename %s to %s: %s\n", tmp, r1, strerror(errno));
	}
	
	hfree(tmp);
	hfree(r1);
	hfree(r2);
	
	return 0;
}

static int hlog_write(int priority, const char *s)
{
	struct tm lt;
	struct timeval tv;
	char wb[LOG_LEN];
	int len, w;
	
	gettimeofday(&tv, NULL);
	gmtime_r(&tv.tv_sec, &lt);
	
	if (log_dest & L_STDERR) {
		rwl_rdlock(&log_file_lock);
		fprintf(stderr, "%4d/%02d/%02d %02d:%02d:%02d.%06d %s[%d:%lx] %s: %s\n",
			lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, (int)tv.tv_usec,
			(log_name) ? log_name : "aprsc", (int)getpid(), (unsigned long int)pthread_self(), log_levelnames[priority], s);
		rwl_rdunlock(&log_file_lock);
		
	}
	
	if ((log_dest & L_FILE) && (log_file >= 0)) {
		len = snprintf(wb, LOG_LEN, "%4d/%02d/%02d %02d:%02d:%02d.%06d %s[%d:%lx] %s: %s\n",
			       lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, (int)tv.tv_usec,
			       (log_name) ? log_name : "aprsc", (int)getpid(), (unsigned long int)pthread_self(), log_levelnames[priority], s);
		wb[LOG_LEN-1] = 0;
		rwl_rdlock(&log_file_lock);
		if ((w = write(log_file, wb, len)) != len)
			fprintf(stderr, "aprsc logger: Could not write to %s (fd %d): %s\n", log_fname, log_file, strerror(errno));
		rwl_rdunlock(&log_file_lock);
		
		if (log_rotate_size) {
			off_t l = lseek(log_file, 0, SEEK_CUR);
			if (l >= log_rotate_size) {
				rotate_log();
			}
		}
		
	}
	
	if (log_dest & L_SYSLOG) {
		rwl_rdlock(&log_file_lock);
		syslog(priority, "%s: %s", log_levelnames[priority], s);
		rwl_rdunlock(&log_file_lock);
	}
	
	return 1;
}

/*
 *	Log a message with a packet (will be quoted)
 */

int hlog(int priority, const char *fmt, ...)
{
	va_list args;
	char s[LOG_LEN];
	
	if (priority > 7)
		priority = 7;
	else if (priority < 0)
		priority = 0;
	
	if (priority > log_level)
		return 0;
	
	va_start(args, fmt);
	vsnprintf(s, LOG_LEN, fmt, args);
	va_end(args);
	
	return hlog_write(priority, s);
}


/*
 *	Log a message, with a packet in the end.
 *	Packet will be quoted.
 */

int hlog_packet(int priority, const char *packet, int packetlen, const char *fmt, ...)
{
	va_list args;
	char s[LOG_LEN];
	int l;
	
	if (priority > 7)
		priority = 7;
	else if (priority < 0)
		priority = 0;
	
	if (priority > log_level)
		return 0;
	
	va_start(args, fmt);
	l = vsnprintf(s, LOG_LEN, fmt, args);
	va_end(args);
	
	str_quote(s + l, LOG_LEN - l, packet, packetlen);
	
	return hlog_write(priority, s);
}

/*
 *	Open access log
 */

int accesslog_open(char *logd, int reopen)
{
	if (!reopen)
		rwl_wrlock(&accesslog_lock);
	
	if (accesslog_fname)
		hfree(accesslog_fname);
		
	if (accesslog_dir)
		hfree(accesslog_dir);
		
	accesslog_dir = hstrdup(logd);
	accesslog_fname = hmalloc(strlen(accesslog_dir) + strlen(accesslog_basename) + 2);
	sprintf(accesslog_fname, "%s/%s", accesslog_dir, accesslog_basename);
	
	accesslog_file = open(accesslog_fname, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP);
	if (accesslog_file < 0)
		hlog(LOG_CRIT, "Could not open %s: %s", accesslog_fname, strerror(errno));
	
	rwl_wrunlock(&accesslog_lock);
	
	return accesslog_file;
}

/*
 *	Close access log
 */
 
int accesslog_close(char *reopenpath)
{
	hlog(LOG_DEBUG, "Closing access log...");
	rwl_wrlock(&accesslog_lock);
	hlog(LOG_DEBUG, "Closing access log, got lock");
	
	if (close(accesslog_file))
		hlog(LOG_CRIT, "Could not close %s: %s", accesslog_fname, strerror(errno));
	hfree(accesslog_fname);
	hfree(accesslog_dir);
	accesslog_fname = accesslog_dir = NULL;
	accesslog_file = -1;
	
	if (reopenpath) {
		return accesslog_open(reopenpath, 1);
	} else {
		rwl_wrunlock(&accesslog_lock);
		return 0;
	}
}

/*
 *	Log an access log message
 */

int accesslog(const char *fmt, ...)
{
	va_list args;
	char s[LOG_LEN], wb[LOG_LEN];
	time_t t;
	struct tm lt;
	int len;
	ssize_t w;
	
	va_start(args, fmt);
	vsnprintf(s, LOG_LEN, fmt, args);
	va_end(args);
	s[LOG_LEN-1] = 0;
	
	time(&t);
	gmtime_r(&t, &lt);
	
	len = snprintf(wb, LOG_LEN, "[%4.4d/%2.2d/%2.2d %2.2d:%2.2d:%2.2d] %s\n",
		lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec, s);
	wb[LOG_LEN-1] = 0;
	
	rwl_rdlock(&accesslog_lock);
	if (accesslog_file >= 0) {
		if ((w = write(accesslog_file, wb, len)) != len)
			hlog(LOG_CRIT, "Could not write to %s (fd %d): %s", accesslog_fname, accesslog_file, strerror(errno));
	} else {
		if (accesslog_file != -666) {
			hlog(LOG_ERR, "Access log not open, log lines are lost!");
			accesslog_file = -666;
		}
	}
	rwl_rdunlock(&accesslog_lock);
	
	return 1;
}

/*
 *	Write my PID to file, after locking the pid file.
 *	Leaves the file descriptor open so that the lock will be held
 *	as long as the process is running.
 */

int pidfile_fd = -1;

int writepid(char *name)
{
	int f;
	char s[32];
	int l;
	
	f = open(name, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (f < 0) {
		hlog(LOG_CRIT, "Could not open %s for writing: %s",
			name, strerror(errno));
		return 0;
	}
	
	pidfile_fd = f;
	
	if (flock(f, LOCK_EX|LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK) {
			hlog(LOG_CRIT, "Could not lock pid file file %s, another process has a lock on it. Another process running - bailing out.", name);
		} else {
			hlog(LOG_CRIT, "Failed to lock pid file %s: %s", name, strerror(errno));
		}
		return 0;
	}
	
	l = snprintf(s, 32, "%ld\n", (long)getpid());
	
	if (ftruncate(f, 0) < 0) {
		hlog(LOG_CRIT, "Could not truncate pid file %s: %s",
			name, strerror(errno));
		return 0;
	}
	
	if (write(f, s, l) != l) {
		hlog(LOG_CRIT, "Could not write pid to %s: %s",
			name, strerror(errno));
		return 0;
	}
	
	return 1;
}

int closepid(void)
{
	if (pidfile_fd >= 0) {
		if (close(pidfile_fd) != 0) {
			hlog(LOG_CRIT, "Could not close pid file: %s", strerror(errno));
			return -1;
		}
		pidfile_fd = -1;
	}
	
	return 0;
}
