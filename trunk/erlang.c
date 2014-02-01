/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2013                            *
 *                                                                  *
 * **************************************************************** */


#include "aprx.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>


/* The erlang module accounts data reception per 1m/10m/60m
   intervals, and reports them on verbout.. */


/* #define USE_ONE_MINUTE_INTERVAL 1 */


static struct timeval erlang_time_end_1min;
static float erlang_time_ival_1min = 1.0;

static struct timeval erlang_time_end_10min;
static float erlang_time_ival_10min = 1.0;

#ifdef ERLANGSTORAGE
static struct timeval erlang_time_end_60min;
static float erlang_time_ival_60min = 1.0;
#endif

#ifdef ERLANGSTORAGE
static const char *erlangtitle = "APRX SNMP + Erlang dataset\n";
#endif

int erlangsyslog;		/* if set, will log via syslog(3)  */
int erlanglog1min;		/* if set, will log also "ERLANG1" interval  */

const char *erlanglogfile;
const char *erlang_backingstore = VARRUN "/aprx.state";

#ifdef ERLANGSTORAGE
static int erlang_file_fd = -1;
static int erlang_mmap_size;
#endif

static void *erlang_mmap;

struct erlanghead *ErlangHead;
struct erlangline **ErlangLines;
int ErlangLinesCount;
int erlang_data_is_nonshared;	/* In embedded target.. */

struct erlang_file {
	struct erlanghead head;
	struct erlangline lines[1];
};

static void erlang_backingstore_startops(void)
{
	ErlangHead->server_pid = getpid();
	ErlangHead->start_time = time(NULL);

	if (!mycall)
		strncpy(ErlangHead->mycall, "N0CALL",
			sizeof(ErlangHead->mycall));
	else
		strncpy(ErlangHead->mycall, mycall,
			sizeof(ErlangHead->mycall));
	ErlangHead->mycall[sizeof(ErlangHead->mycall) - 1] = 0;	/* NUL terminate */
}

static int erlang_backingstore_grow(int do_create, int add_count)
{
	struct erlang_file *EF;
	int i;
#ifdef ERLANGSTORAGE
	struct stat st;
	char buf[256];
	int new_size, pagesize = sysconf(_SC_PAGE_SIZE);
	int doing_init = 0;

	if (erlang_data_is_nonshared)
		goto embedded_only;

	if (erlang_file_fd < 0) {
		goto embedded_only;
	}

	fstat(erlang_file_fd, &st);
	lseek(erlang_file_fd, 0, SEEK_END);

	new_size = st.st_size;

	if (new_size % pagesize) {
		new_size /= pagesize;
		++new_size;
		new_size *= pagesize;
	}
	if (new_size == 0) {
		new_size = pagesize;
		doing_init = 1;
	}
	/* new_size expanded to be exact page size multiple.  */

	/* If the new size is larger than the file size..
	   .. and at least one page size (e.g. 4 kB) .. */

	if (new_size > st.st_size) {
		/* .. then we fill in the file to given size.  */
		int i, rc, l;
		i = st.st_size;
		memset(buf, 0, sizeof(buf));
		lseek(erlang_file_fd, 0, SEEK_END);
		while (i < new_size) {
			l = sizeof(buf);
			if (new_size - i < l)
				l = new_size - i;
			rc = write(erlang_file_fd, buf, l);
			if (rc < 0 && errno == EINTR)
				continue;
			if (rc != l)
				break;
			i += rc;
		}
	}

      redo_open:;

	if (erlang_mmap) {
		msync(erlang_mmap, erlang_mmap_size, MS_SYNC);
		munmap(erlang_mmap, erlang_mmap_size);
		erlang_mmap = NULL;
		erlang_mmap_size = 0;
		ErlangHead = NULL;
	}

	/* Some (early Linux) systems mmap() offset on IO pointer... */
	lseek(erlang_file_fd, 0, SEEK_SET);
	fstat(erlang_file_fd, &st);

	erlang_mmap_size = st.st_size;
	erlang_mmap =
		mmap(NULL, erlang_mmap_size,
		     PROT_READ | (do_create ? PROT_WRITE : 0), MAP_SHARED,
		     erlang_file_fd, 0);
	if (erlang_mmap == MAP_FAILED) {
		erlang_mmap = NULL;
		syslog(LOG_ERR,
		       "Erlang-file mmap() failed, fd=%d, errno=%d: %s",
		       erlang_file_fd, errno, strerror(errno));
	}
	if (erlang_mmap) {

		int rc, l;
		EF = erlang_mmap;

		ErlangHead = &EF->head;

		if (EF->head.version != ERLANGLINE_STRUCT_VERSION ||
		    EF->head.last_update == 0) {
			if (doing_init) {
				/* Not initialized ? */
				memset(erlang_mmap, 0, erlang_mmap_size);

				strcpy(EF->head.title, erlangtitle);
				EF->head.version =
					ERLANGLINE_STRUCT_VERSION;
				EF->head.linecount = 0;
				EF->head.last_update = now.tv_sec;
				ErlangLinesCount = 0;
			} else {
				/* Wrong head magic, and not doing block init..  */
				munmap(erlang_mmap, erlang_mmap_size);
				erlang_mmap = NULL;
				erlang_mmap_size = 0;
				syslog(LOG_ERR,
				       "Erlang-file has bad magic in it, not opening! Not modifying!");
				close(erlang_file_fd);
				erlang_file_fd = -1;

				goto embedded_only;	/* BAD BAD ! */
			}
		}

		if (EF->head.linecount != ErlangLinesCount
		    || add_count > 0) {
			/* must resize.. */
			int new_count = EF->head.linecount + add_count;
			new_size =
				sizeof(struct erlang_file) +
				sizeof(struct erlangline) * (new_count -
							     1);

			if (new_size % pagesize) {
				new_size /= pagesize;
				++new_size;
				new_size *= pagesize;
			}

			i = st.st_size;
			memset(buf, 0, sizeof(buf));
			lseek(erlang_file_fd, 0, SEEK_END);	/* append on the file.. */
			while (i < new_size) {
				l = sizeof(buf);
				if (new_size - i < l)
					l = new_size - i;
				rc = write(erlang_file_fd, buf, l);
				if (rc < 0 && errno == EINTR)
					continue;
				if (rc != l)
					break;
				i += rc;
			}

			if (i < new_size) {
				munmap(erlang_mmap, erlang_mmap_size);
				erlang_mmap = NULL;

				goto embedded_only;	/* BAD BAD ! */
			}

			add_count = 0;
			if (do_create)
				EF->head.linecount = new_count;
			ErlangLinesCount = new_count;

			goto redo_open;	/* redo mapping */
		}

		/* Ok, successfull open, correct linecount */
		ErlangLines =
			(void *) realloc((void *) ErlangLines,
					 (ErlangLinesCount +
					  1) * sizeof(void *));

		for (i = 0; i < ErlangLinesCount; ++i) {
			ErlangLines[i] = &EF->lines[i];
		}



		return 0;	/* OK ! */
	}

      embedded_only:;
#endif				/* ... ERLANGSTORAGE ... */

	erlang_data_is_nonshared = 1;

	if (add_count > 0 || !erlang_mmap) {
		ErlangLinesCount += add_count;
		erlang_mmap = realloc(erlang_mmap, sizeof(*EF) +
				      (ErlangLinesCount +
				       1) * sizeof(struct erlangline));
	}

	EF = erlang_mmap;
	ErlangHead = &EF->head;

	/* Ok, successfull open, correct linecount */
	ErlangLines =
		(void *) realloc((void *) ErlangLines,
				 (ErlangLinesCount + 1) * sizeof(void *));

	for (i = 0; i < ErlangLinesCount; ++i) {
		ErlangLines[i] = &EF->lines[i];
	}

	return 0;
}

static int erlang_backingstore_open(int do_create)
{
#ifdef ERLANGSTORAGE
	if (!erlang_backingstore) {
		syslog(LOG_ERR, "erlang_backingstore not defined!");
		erlang_data_is_nonshared = 1;
	}
	if (erlang_file_fd < 0 && erlang_backingstore) {
		erlang_file_fd = open(erlang_backingstore, do_create ? O_RDWR : O_RDONLY, 0644);	/* Presume: it exists! */
		if ((erlang_file_fd < 0) && do_create && (errno == ENOENT)) {
			erlang_file_fd =
				open(erlang_backingstore,
				     O_RDWR | O_CREAT | O_EXCL, 0644);
		}
	}
	if (erlang_file_fd < 0) {
		syslog(LOG_ERR,
		       "Open of '%s' for erlang_backingstore file failed!  errno=%d: %s",
		       erlang_backingstore, errno, strerror(errno));
		erlang_data_is_nonshared = 1;
	}
#endif
	return erlang_backingstore_grow(do_create, 0);	/* Just open */
}


static struct erlangline *erlang_findline(const char *portname,
					  int bytes_per_minute)
{
	int i;
	struct erlangline *E;
	if (portname == NULL) return NULL;

	if (bytes_per_minute == 0)
	  bytes_per_minute = (int) ((1200.0 * 60) / 8.2); // Default of 1200 bps

	/* Allocate a new ErlangLines[] entry for this object,
	   if no existing one is found.. */

	E = NULL;
	if (ErlangLines) {
		for (i = 0; i < ErlangLinesCount; ++i) {
			if (strcmp(portname, ErlangLines[i]->name) == 0) {
				/* HOO-RAY!  It is this one! */
				E = ErlangLines[i];
				break;
			}
		}
	}
	/* If found -- err... why we are SETing it AGAIN ? */


	if (!E) {

		/* Allocate a new one */
		erlang_backingstore_grow(1, 1);
		if (!ErlangLines)
			return NULL;	/* D'uh! */

		E = ErlangLines[ErlangLinesCount - 1];	/* Last one is the lattest.. */

		memset(E, 0, sizeof(*E));
		strncpy(E->name, portname, sizeof(E->name) - 1);
		E->name[sizeof(E->name) - 1] = 0;

		E->erlang_capa = bytes_per_minute;
		E->index = ErlangLinesCount - 1;

#ifdef ERLANGSTORAGE
		E->e1_cursor = 0;
		E->e1_max  = APRXERL_1M_COUNT;
		E->e10_cursor = 0;
		E->e10_max = APRXERL_10M_COUNT;
		E->e60_cursor = 0;
		E->e60_max = APRXERL_60M_COUNT;
#else
#if (USE_ONE_MINUTE_DATA == 1)
		E->e1_cursor = 0;
		E->e1_max  = APRXERL_1M_COUNT;
#else
		E->e10_cursor = 0;
		E->e10_max = APRXERL_10M_COUNT;
#endif
#endif
	}
	return E;
}


static void erlang_timer_init()
{

	/* Time intervals will end at next even
	   1 minute/10 minutes/60 minutes,
	   although said interval will be shorter than full. */

	erlang_time_end_1min.tv_sec = now.tv_sec + 60 - (now.tv_sec % 60);
        erlang_time_end_1min.tv_usec = 0;
	erlang_time_ival_1min = (float) (60 - now.tv_sec % 60) / 60.0;

	erlang_time_end_10min.tv_sec = now.tv_sec + 600 - (now.tv_sec % 600);
	erlang_time_end_10min.tv_usec = 0;
	erlang_time_ival_10min = (float) (600 - now.tv_sec % 600) / 600.0;

#ifdef ERLANGSTORAGE
	erlang_time_end_60min.tv_sec = now.tv_sec + 3600 - (now.tv_sec % 3600);
	erlang_time_end_60min.tv_usec = 0;
	erlang_time_ival_60min = (float) (3600 - now.tv_sec % 3600) / 3600.0;
#endif
}



/*
 *  erlang_set()
 */
void erlang_set(const char *portname, int bytes_per_minute)
{
	erlang_findline(portname, bytes_per_minute);
}

/*
 *  erlang_add()
 */
void erlang_add(const char *portname, ErlangMode erl, int bytes, int packets)
{
	struct erlangline *E;
	if (!portname) return;

	E = erlang_findline(portname, (int) ((1200.0 * 60) / 8.2));

	if (debug > 1)
	  printf("erlang_add(%s, %s, %d, %d)\n", portname,
		 (erl == ERLANG_RX ? "RX":(erl == ERLANG_TX ? "TX": "DROP")),
		 bytes, packets);

	if (!E)
		return;

	if (erl == ERLANG_RX) {
		E->SNMP.bytes_rx += bytes;
		E->SNMP.packets_rx += packets;
		E->SNMP.update = now.tv_sec;
		E->last_update = now.tv_sec;

#ifdef ERLANGSTORAGE
		E->erl1m.bytes_rx += bytes;
		E->erl1m.packets_rx += packets;
		E->erl1m.update = now.tv_sec;

		E->erl10m.bytes_rx += bytes;
		E->erl10m.packets_rx += packets;
		E->erl10m.update = now.tv_sec;

		E->erl60m.bytes_rx += bytes;
		E->erl60m.packets_rx += packets;
		E->erl60m.update = now.tv_sec;
#else
#if (USE_ONE_MINUTE_STORAGE == 1)
		E->erl1m.bytes_rx += bytes;
		E->erl1m.packets_rx += packets;
		E->erl1m.update = now.tv_sec;
#else
		E->erl10m.bytes_rx += bytes;
		E->erl10m.packets_rx += packets;
		E->erl10m.update = now.tv_sec;
#endif
#endif
	}
	if (erl == ERLANG_TX) {
		E->SNMP.bytes_tx += bytes;
		E->SNMP.packets_tx += packets;
		E->SNMP.update = now.tv_sec;
		E->last_update = now.tv_sec;

#ifdef ERLANGSTORAGE
		E->erl1m.bytes_tx += bytes;
		E->erl1m.packets_tx += packets;
		E->erl1m.update = now.tv_sec;

		E->erl10m.bytes_tx += bytes;
		E->erl10m.packets_tx += packets;
		E->erl10m.update = now.tv_sec;

		E->erl60m.bytes_tx += bytes;
		E->erl60m.packets_tx += packets;
		E->erl60m.update = now.tv_sec;
#else
#if (USE_ONE_MINUTE_STORAGE == 1)
		E->erl1m.bytes_tx += bytes;
		E->erl1m.packets_tx += packets;
		E->erl1m.update = now.tv_sec;
#else
		E->erl10m.bytes_tx += bytes;
		E->erl10m.packets_tx += packets;
		E->erl10m.update = now.tv_sec;
#endif
#endif
	}
	if (erl == ERLANG_DROP) {
		E->SNMP.bytes_rxdrop += bytes;
		E->SNMP.packets_rxdrop += packets;
		E->SNMP.update = now.tv_sec;
		E->last_update = now.tv_sec;

#ifdef ERLANGSTORAGE
		E->erl1m.bytes_rxdrop += bytes;
		E->erl1m.packets_rxdrop += packets;
		E->erl1m.update = now.tv_sec;

		E->erl10m.bytes_rxdrop += bytes;
		E->erl10m.packets_rxdrop += packets;
		E->erl10m.update = now.tv_sec;

		E->erl60m.bytes_rxdrop += bytes;
		E->erl60m.packets_rxdrop += packets;
		E->erl60m.update = now.tv_sec;
#else
#if (USE_ONE_MINUTE_STORAGE == 1)
		E->erl1m.bytes_rxdrop += bytes;
		E->erl1m.packets_rxdrop += packets;
		E->erl1m.update = now.tv_sec;
#else
		E->erl10m.bytes_rxdrop += bytes;
		E->erl10m.packets_rxdrop += packets;
		E->erl10m.update = now.tv_sec;
#endif
#endif
	}
}


/*
 *  erlang_time_end() - process erlang measurement interval time end event
 */
static void erlang_time_end(void)
{
	int i;
	char msgbuf[500];
	char logtime[40];
	FILE *fp = NULL;

	if (erlanglogfile) {
		/* actually we want it to the erlanglogfile... */
		fp = fopen(erlanglogfile, "a");
	}

	printtime(logtime, sizeof(logtime));

	if (tv_timercmp(&now, &erlang_time_end_1min) >= 0) {
		erlang_time_end_1min.tv_sec += 60;
#if (defined(ERLANGSTORAGE) || (USE_ONE_MINUTE_STORAGE == 1))
		for (i = 0; i < ErlangLinesCount; ++i) {
			struct erlangline *E = ErlangLines[i];
			E->last_update = now.tv_sec;

			if (erlanglog1min) {
				sprintf(msgbuf,
					"ERLANG%-2d %s Rx %6ld %3ld Dp %6ld %3ld Tx %6ld %3ld : %5.3f %5.3f %5.3f",
					1, E->name, 
					E->erl1m.bytes_rx,
					E->erl1m.packets_rx,
					E->erl1m.bytes_rxdrop,
					E->erl1m.packets_rxdrop,
					E->erl1m.bytes_tx, E->erl1m.packets_tx,
					((float) E->erl1m.bytes_rx /
					 (float) E->erlang_capa *
					 erlang_time_ival_1min),
					((float) E->erl1m.bytes_rxdrop /
					 (float) E->erlang_capa *
					 erlang_time_ival_1min),
					((float)E->erl1m.bytes_tx /
					 (float)E->erlang_capa *
					 erlang_time_ival_1min)
					);
				if (fp)
					fprintf(fp, "%s %s\n", logtime,
						msgbuf);
				else if (erlangout)
					printf("%ld\t%s\n", now.tv_sec, msgbuf);
				if (erlangsyslog)
					syslog(LOG_INFO, "%ld %s", now.tv_sec,
					       msgbuf);
			}

			E->erl1m.update = now.tv_sec;
			E->e1[E->e1_cursor] = E->erl1m;
			++E->e1_cursor;
			if (E->e1_cursor >= E->e1_max)
				E->e1_cursor = 0;

			memset(&E->erl1m, 0, sizeof(E->erl1m));
			E->erl1m.update = now.tv_sec;
		}
		erlang_time_ival_1min = 1.0;
#endif
	}
	if (tv_timercmp(&now, &erlang_time_end_10min) >= 0) {
		erlang_time_end_10min.tv_sec += 600;
#if (defined(ERLANGSTORAGE) || (USE_ONE_MINUTE_STORAGE == 0))
		for (i = 0; i < ErlangLinesCount; ++i) {
			struct erlangline *E = ErlangLines[i];
			E->last_update = now.tv_sec;
			sprintf(msgbuf,
				"ERLANG%-2d %s Rx %6ld %3ld Dp %6ld %3ld Tx %6ld %3ld : %5.3f %5.3f %5.3f",
				10, E->name, 
				E->erl10m.bytes_rx, E->erl10m.packets_rx,
				E->erl10m.bytes_rxdrop,
				E->erl10m.packets_rxdrop,
				E->erl10m.bytes_tx, E->erl10m.packets_tx,
				((float) E->erl10m.bytes_rx /
				 ((float) E->erlang_capa * 10.0 *
				  erlang_time_ival_10min)),
				((float) E->erl10m.bytes_rxdrop /
				 ((float) E->erlang_capa * 10.0 *
				  erlang_time_ival_10min)),
				((float)E->erl10m.bytes_tx /
				 ((float)E->erlang_capa * 10.0 *
				  erlang_time_ival_10min))
				);
			if (fp)
				fprintf(fp, "%s %s\n", logtime, msgbuf);
			else if (erlangout)
				printf("%ld\t%s\n", now.tv_sec, msgbuf);
			if (erlangsyslog)
				syslog(LOG_INFO, "%ld %s", now.tv_sec, msgbuf);

			E->erl10m.update = now.tv_sec;
			E->e10[E->e10_cursor] = E->erl10m;
			++E->e10_cursor;
			if (E->e10_cursor >= E->e10_max)
				E->e10_cursor = 0;
			memset(&E->erl10m, 0, sizeof(E->erl10m));
			E->erl10m.update = now.tv_sec;
		}
		erlang_time_ival_10min = 1.0;
#endif
	}
#ifdef ERLANGSTORAGE
	if (tv_timercmp(&now, &erlang_time_end_60min) >= 0) {
		erlang_time_end_60min.tv_sec += 3600;
		for (i = 0; i < ErlangLinesCount; ++i) {
			struct erlangline *E = ErlangLines[i];
			/* E->last_update = now.tv_sec; -- the 10 minute step does also this */
			sprintf(msgbuf,
				"ERLANG%-2d %s Rx %6ld %3ld Dp %6ld %3ld Tx %6ld %3ld : %5.3f %5.3f %5.3f",
				60, E->name, 
				E->erl60m.bytes_rx, E->erl60m.packets_rx,
				E->erl60m.bytes_rxdrop,
				E->erl60m.packets_rxdrop,
				E->erl60m.bytes_tx,  E->erl60m.packets_tx,
				((float) E->erl60m.bytes_rx /
				 ((float) E->erlang_capa * 60.0 *
				  erlang_time_ival_60min)),
				((float) E->erl60m.bytes_rxdrop /
				 ((float) E->erlang_capa * 60.0 *
				  erlang_time_ival_60min)),
				((float)E->erl60m.bytes_tx /
				 ((float)E->erlang_capa * 60.0 *
				  erlang_time_ival_60min))
				);
			if (fp)
				fprintf(fp, "%s %s\n", logtime, msgbuf);
			else if (erlangout)
				printf("%ld\t%s\n", now.tv_sec, msgbuf);
			if (erlangsyslog)
				syslog(LOG_INFO, "%ld %s", now.tv_sec, msgbuf);

			E->erl60m.update = now.tv_sec;
			E->e60[E->e60_cursor] = E->erl60m;
			++E->e60_cursor;
			if (E->e60_cursor >= E->e60_max)
				E->e60_cursor = 0;

			memset(&E->erl60m, 0, sizeof(E->erl60m));
			E->erl60m.update = now.tv_sec;
		}
		erlang_time_ival_60min = 1.0;
	}
#endif
	if (fp)
		fclose(fp);
}

int erlang_prepoll(struct aprxpolls *app)
{
	struct timeval nowplus;
        tv_timeradd_seconds(&nowplus, &now, 70); // 1 minute + 10 seconds
        if (tv_timercmp(&nowplus, &erlang_time_end_1min) < 0) {
        	erlang_timer_init();
        }
        tv_timeradd_seconds(&nowplus, &now, 610); // 10 minutes + 10 seconds
        if (tv_timercmp(&nowplus, &erlang_time_end_10min) < 0) {
        	erlang_timer_init();
        }
#ifdef ERLANGSTORAGE
        tv_timeradd_seconds(&nowplus, &now, 3610); // 60 minutes + 10 seconds
        if (tv_timercmp(&nowplus, &erlang_time_end_60min) < 0) {
        	erlang_timer_init();
        }
#endif

	if (tv_timercmp(&app->next_timeout, &erlang_time_end_1min) > 0)
		app->next_timeout = erlang_time_end_1min;
	if (tv_timercmp(&app->next_timeout, &erlang_time_end_10min) > 0)
		app->next_timeout = erlang_time_end_10min;
#ifdef ERLANGSTORAGE
	if (tv_timercmp(&app->next_timeout, &erlang_time_end_60min) > 0)
		app->next_timeout = erlang_time_end_60min;
#endif
	return 0;
}

int erlang_postpoll(struct aprxpolls *app)
{
	if (tv_timercmp(&now, &erlang_time_end_1min) >= 0 ||
	    tv_timercmp(&now, &erlang_time_end_10min) >= 0
#ifdef ERLANGSTORAGE
	    || tv_timercmp(&now, &erlang_time_end_60min) >= 0
#endif
	    )
		erlang_time_end();

	return 0;
}


void erlang_init(const char *syslog_facility_name)
{
	aprx_syslog_init(syslog_facility_name);

	now.tv_sec = time(NULL);

        erlang_timer_init();
}

void erlang_start(int do_create)
{
	erlang_backingstore_open(do_create);
	if (do_create > 1)
		erlang_backingstore_startops();
}
