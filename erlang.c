/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
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


static time_t	erlang_time_end_1min;
static float erlang_time_ival_1min  = 1.0;

static time_t	erlang_time_end_10min;
static float erlang_time_ival_10min = 1.0;

static time_t	erlang_time_end_60min;
static float erlang_time_ival_60min = 1.0;


int erlangsyslog;	/* if set, will log via syslog(3)  */
int erlanglog1min = 1;	/* if set, will log also "ERLANG1" interval  */

const  char *erlang_backingstore = "/tmp/aprx-erlang.dat";
static int erlang_file_fd = -1;

static void *erlang_mmap;
static int   erlang_mmap_size;

struct erlangline **ErlangLines;
int                ErlangLineCount;

struct erlang_file {
	struct erlanghead	head;
	struct erlangline	lines[2];
};


static int erlang_backingstore_grow(int add_count)
{
	struct stat st;
	char buf[256];
	int new_size, pagesize  = sysconf(_SC_PAGE_SIZE);

	if (erlang_file_fd < 0) return -1;

	fstat(erlang_file_fd, &st);
	lseek(erlang_file_fd, 0, SEEK_END);

	new_size = st.st_size;

	if (new_size % pagesize) {
	  new_size /= pagesize;
	  ++new_size;
	  new_size *= pagesize;
	}
	if (new_size == 0)
	  new_size = pagesize;
	/* new_size expanded to be exact page size multiple.  */

	/* If the new size is larger than the file size..
	   .. and at least one page size (e.g. 4 kB) .. */

	if (new_size > st.st_size) {
	  /* .. then we fill in the file to given size.  */
	  int i, rc, l;
	  i = st.st_size;
	  memset(buf, 0, sizeof(buf));
	  lseek(erlang_file_fd, 0, SEEK_END);
	  while ( i < new_size ) {
	    l = sizeof(buf);
	    if (new_size - i < l) l = new_size - i;
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
	}

	/* Some (early Linux) systems mmap() offset on IO pointer... */
	lseek(erlang_file_fd, 0, SEEK_SET);
	fstat(erlang_file_fd, &st);

	erlang_mmap_size = st.st_size;
	erlang_mmap      = mmap(NULL, erlang_mmap_size, PROT_READ|PROT_WRITE, MAP_SHARED, erlang_file_fd, 0);
	if (erlang_mmap == MAP_FAILED) {
	  erlang_mmap = NULL;
	  fprintf(stderr,"mmap() failed, fd=%d, errno=%d: %s\n", erlang_file_fd, errno, strerror(errno));
	}

	if (erlang_mmap) {

	  int i, rc, l;
	  struct erlang_file *EF = erlang_mmap;

	  if (EF->head.version != ERLANGLINE_STRUCT_VERSION ||
	      EF->head.last_update == 0 ) {
	    /* Not initialized ? */
	    memset(erlang_mmap, 0, erlang_mmap_size);

	    EF->head.version = ERLANGLINE_STRUCT_VERSION;
	    EF->head.linecount = 0;
	    EF->head.last_update = now;
	    ErlangLineCount = 0;
	  }

	  if (EF->head.linecount != ErlangLineCount  || add_count > 0) {
	    /* must resize.. */
	    int new_count = EF->head.linecount + add_count;
	    new_size = sizeof(struct erlang_file) + sizeof(struct erlangline) * (new_count - 2);

	    if (new_size % pagesize) {
	      new_size /= pagesize;
	      ++new_size;
	      new_size *= pagesize;
	    }

	    i = st.st_size;
	    memset(buf, 0, sizeof(buf));
	    lseek(erlang_file_fd, 0, SEEK_END); /* append on the file.. */
	    while ( i < new_size ) {
	      l = sizeof(buf);
	      if (new_size - i < l) l = new_size - i;
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

	      return -1; /* BAD BAD ! */
	    }

	    add_count = 0;
	    EF->head.linecount = new_count;
	    ErlangLineCount = new_count;

	    goto redo_open; /* redo mapping */
	  }

	  /* Ok, successfull open, correct linecount */
	  ErlangLines  = (void*)realloc((void*)ErlangLines, (ErlangLineCount+1)*sizeof(void*));

	  for (i = 0; i < ErlangLineCount; ++i) {
	    ErlangLines[i] = &EF->lines[i];
	  }



	  return 0; /* OK ! */
	}

	


	return -1; /* D'uh..  something failed! */
}

static int erlang_backingstore_open(void)
{
	if (!erlang_backingstore) {
	  fprintf(stderr,"erlang_backingstore not defined!\n");
	  return -1;
	}

	erlang_file_fd = open(erlang_backingstore, O_RDWR|O_CREAT, 0644);
	if (erlang_file_fd < 0) {
	  fprintf(stderr,"open of '%s' for erlang_backingstore file failed!  errno=%d: %s\n",
		 erlang_backingstore, errno, strerror(errno));
	  return -1;
	}

	return erlang_backingstore_grow(0); /* Just open */
}


static struct erlangline *erlang_findline(const void *refp, const char *portname, int bytes_per_minute)
{
	int i;
	struct erlangline *E = NULL;

	/* Allocate a new ErlangLines[] entry for this object, if no existing is found.. */
#if 1
	refp = NULL; /* Ref only by text name.. */
#endif

	if (ErlangLines) {
	  for (i=0; i < ErlangLineCount; ++i) {
	    if (refp && (refp != ErlangLines[i]->refp))
	      continue; /* Was not this.. */
	    if (!refp && (strcmp(portname, ErlangLines[i]->name) != 0))
	      continue; /* Was not this.. */
	    /* HOO-RAY!  It is this one! */
	    E = ErlangLines[i];
	    break;
	  }
	}
	/* If found -- err... why we are SETing it AGAIN ? */


	if (!E) {

	  /* Allocate a new one */
	  erlang_backingstore_grow(1);
	  if (!ErlangLines) return NULL; /* D'uh! */

	  E = ErlangLines[ErlangLineCount-1]; /* Last one is the lattest.. */

	  memset(E, 0, sizeof(*E));
	  E->refp = refp;
	  strncpy(E->name, portname, sizeof(E->name)-1);
	  E->name[sizeof(E->name)-1] = 0;

	  E->erlang_capa = bytes_per_minute;
	  E->index = ErlangLineCount-1;

	  E->e1_cursor = E->e10_cursor = E->e60_cursor = 0;
	  E->e1_max  = APRXERL_1M_COUNT;
	  E->e10_max = APRXERL_10M_COUNT;
	  E->e60_max = APRXERL_60M_COUNT;
	}
	return E;
}

/*
 *  erlang_set()
 */
void erlang_set(const void *refp, const char *portname, int bytes_per_minute)
{
	erlang_findline(refp, portname, bytes_per_minute);
}

/*
 *  erlang_add()
 */
void erlang_add(const void *refp, const char *portname, int rx_or_tx, int bytes, int packets)
{
	struct erlangline *E = erlang_findline(refp, portname, (1200*60/9));
	if (!E) return;

	if (rx_or_tx == ERLANG_RX) {
	  E->SNMP.bytes_rx       += bytes;
	  E->SNMP.packets_rx     += packets;
	  E->SNMP.update          = now;

	  E->erl1m.bytes_rx      += bytes;
	  E->erl1m.packets_rx    += packets;
	  E->erl1m.update         = now;

	  E->erl10m.bytes_rx     += bytes;
	  E->erl10m.packets_rx   += packets;
	  E->erl10m.update        = now;

	  E->erl60m.bytes_rx     += bytes;
	  E->erl60m.packets_rx   += packets;
	  E->erl60m.update        = now;
	}

	if (rx_or_tx == ERLANG_TX) {
	  E->SNMP.bytes_tx       += bytes;
	  E->SNMP.packets_tx     += packets;
	  E->SNMP.update          = now;

	  E->erl1m.bytes_tx      += bytes;
	  E->erl1m.packets_tx    += packets;
	  E->erl1m.update         = now;

	  E->erl10m.bytes_tx     += bytes;
	  E->erl10m.packets_tx   += packets;
	  E->erl10m.update        = now;

	  E->erl60m.bytes_tx     += bytes;
	  E->erl60m.packets_tx   += packets;
	  E->erl60m.update        = now;
	}
}


/*
 *  erlang_time_end() - process erlang measurement intercal time end event
 */
static void erlang_time_end(void)
{
	int i;
	char msgbuf[500];
	char logtime[40];
	struct tm *wallclock = localtime(&now);
	FILE *fp = NULL;

	if (erlangout > 1 && aprxlogfile) {
	  /* actually we want it to the aprxlogfile... */
	  fp = fopen(aprxlogfile,"a");
	}

	strftime(logtime, sizeof(logtime), "%Y-%m-%d %H:%M", wallclock);

	if (now >= erlang_time_end_1min) {
	  erlang_time_end_1min += 60;
	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = ErlangLines[i];
	    E->last_update = now;
	    if (erlanglog1min) {
	      sprintf(msgbuf,
		      "ERLANG%-2d %s %s Raw Bytes/Pkts Rx %6ld %3ld  Tx %6ld %3ld  - %5.3f %5.3f",
		      1,E->name,logtime,
		      E->erl1m.bytes_rx, E->erl1m.packets_rx,
		      E->erl1m.bytes_tx, E->erl1m.packets_tx,
		      ((float)E->erl1m.bytes_rx/(float)E->erlang_capa*erlang_time_ival_1min),
		      ((float)E->erl1m.bytes_tx/(float)E->erlang_capa*erlang_time_ival_1min)
		      );
	      if (fp)
		fprintf(fp,"%s %s\n", logtime, msgbuf);
	      else if (erlangout)
		printf("%ld\t%s\n", now,msgbuf);
	      if (erlangsyslog)
		syslog(LOG_INFO, "%ld %s", now, msgbuf);
	    }

	    E->e1[E->e1_cursor] = E->erl1m;
	    ++E->e1_cursor;
	    if (E->e1_cursor >= E->e1_max)
	      E->e1_cursor = 0;

	    E->erl1m.bytes_rx   = 0;
	    E->erl1m.packets_rx = 0;
	    E->erl1m.bytes_tx   = 0;
	    E->erl1m.packets_tx = 0;
	  }
	  erlang_time_ival_1min = 1.0;
	}
	if (now >= erlang_time_end_10min) {
	  erlang_time_end_10min += 600;

	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = ErlangLines[i];
	    E->last_update = now;
	    sprintf(msgbuf,
		    "ERLANG%-2d %s %s Raw Bytes/Pkts Rx %6ld %3ld  Tx %6ld %3ld  - %5.3f %5.3f",
		    10,E->name,logtime,
		    E->erl10m.bytes_rx, E->erl10m.packets_rx,
		    E->erl10m.bytes_tx, E->erl10m.packets_tx,
		    ((float)E->erl10m.bytes_rx/((float)E->erlang_capa*10.0*erlang_time_ival_10min)),
		    ((float)E->erl10m.bytes_tx/((float)E->erlang_capa*10.0*erlang_time_ival_10min))
		    );
	    if (fp)
	      fprintf(fp,"%s %s\n", logtime, msgbuf);
	    else if (erlangout)
	      printf("%ld\t%s\n", now,msgbuf);
	    if (erlangsyslog)
	      syslog(LOG_INFO, "%ld %s", now, msgbuf);

	    E->e10[E->e10_cursor] = E->erl10m;
	    ++E->e10_cursor;
	    if (E->e10_cursor >= E->e10_max)
	      E->e10_cursor = 0;

	    E->erl10m.bytes_rx   = 0;
	    E->erl10m.packets_rx = 0;
	    E->erl10m.bytes_tx   = 0;
	    E->erl10m.packets_tx = 0;
	  }
	  erlang_time_ival_10min = 1.0;
	}
	if (now >= erlang_time_end_60min) {
	  erlang_time_end_60min += 3600;

	  for (i = 0; i < ErlangLineCount; ++i) {
	    struct erlangline *E = ErlangLines[i];
	    /* E->last_update = now; -- the 10 minute step does also this */
	    sprintf(msgbuf,
		    "ERLANG%-2d %s %s Raw Bytes/Pkts Rx %6ld %3ld  Tx %6ld %3ld  - %5.3f %5.3f",
		    60,E->name,logtime,
		    E->erl60m.bytes_rx,  E->erl60m.packets_rx,
		    E->erl60m.bytes_tx,  E->erl60m.packets_tx,
		    ((float)E->erl60m.bytes_rx/((float)E->erlang_capa*60.0*erlang_time_ival_60min)),
		    ((float)E->erl60m.bytes_tx/((float)E->erlang_capa*60.0*erlang_time_ival_60min))
		    );
	    if (fp)
	      fprintf(fp,"%s %s\n", logtime, msgbuf);
	    else if (erlangout)
	      printf("%ld\t%s\n", now,msgbuf);
	    if (erlangsyslog)
	      syslog(LOG_INFO, "%ld %s", now, msgbuf);

	    E->e60[E->e60_cursor] = E->erl60m;
	    ++E->e60_cursor;
	    if (E->e60_cursor >= E->e60_max)
	      E->e60_cursor = 0;

	    E->erl60m.bytes_rx   = 0;
	    E->erl60m.packets_rx = 0;
	    E->erl60m.bytes_tx   = 0;
	    E->erl60m.packets_tx = 0;
	  }
	  erlang_time_ival_60min = 1.0;
	}
	if (fp)
	  fclose(fp);
}

int erlang_prepoll(int nfds, struct pollfd **fdsp, time_t *tout)
{

	if (*tout > erlang_time_end_1min)
	  *tout = erlang_time_end_1min;
	if (*tout > erlang_time_end_10min)
	  *tout = erlang_time_end_10min;
	if (*tout > erlang_time_end_60min)
	  *tout = erlang_time_end_60min;

	return 0;
}

int erlang_postpoll(int nfds, struct pollfd *fds)
{
	if (
	    now >= erlang_time_end_1min ||
	    now >= erlang_time_end_10min ||
	    now >= erlang_time_end_60min
	    )
	  erlang_time_end();

	return 0;
}

static struct syslog_facs {
	const char *name;
	int fac_code;
} syslog_facs[] = {
  { "NONE",       -1 },
  { "LOG_DAEMON", LOG_DAEMON },
#ifdef LOG_FTP
  { "LOG_FTP",    LOG_FTP },
#endif
#ifdef LOG_LPR
  { "LOG_LPR",    LOG_LPR },
#endif
#ifdef LOG_MAIL
  { "LOG_MAIL",   LOG_MAIL },
#endif
#ifdef LOG_USER
  { "LOG_USER",   LOG_USER },
#endif
#ifdef LOG_UUCP
  { "LOG_UUCP",   LOG_UUCP },
#endif
  { "LOG_LOCAL0", LOG_LOCAL0 },
  { "LOG_LOCAL1", LOG_LOCAL1 },
  { "LOG_LOCAL2", LOG_LOCAL2 },
  { "LOG_LOCAL3", LOG_LOCAL3 },
  { "LOG_LOCAL4", LOG_LOCAL4 },
  { "LOG_LOCAL5", LOG_LOCAL5 },
  { "LOG_LOCAL6", LOG_LOCAL6 },
  { "LOG_LOCAL7", LOG_LOCAL7 },
  { NULL, 0 }
};

void erlang_init(const char *syslog_facility_name)
{
	int syslog_fac = LOG_DAEMON, i;
	static int done_once = 0;

	now = time(NULL);

	if (done_once) {
	  closelog(); /* We reconfigure from config file! */
	} else
	  ++done_once;

	/* Time intervals will end at next even
	       1 minute/10 minutes/60 minutes,
	   although said interval will be shorter than full. */


	erlang_time_end_1min  = now +   60 - (now %   60);
	erlang_time_ival_1min =    (float)(60 - now % 60)/60.0;

	erlang_time_end_10min = now +  600 - (now %  600);
	erlang_time_ival_10min =  (float)(600 - now % 600)/600.0;

	erlang_time_end_60min = now + 3600 - (now % 3600);
	erlang_time_ival_60min = (float)(3600 - now % 3600)/3600.0;

	for (i = 0;; ++i) {
	  if (syslog_facs[i].name == NULL) {
	    fprintf(stderr, "Sorry, unknown erlang syslog facility code name: %s, not supported in this system.\n", syslog_facility_name);
	    fprintf(stderr, "Accepted list is:");
	    for (i = 0;; ++i) {
	      if (syslog_facs[i].name == NULL)
		break;
	      fprintf(stderr," %s",syslog_facs[i].name);
	    }
	    fprintf(stderr,"\n");
	    break;
	  }
	  if (strcasecmp(syslog_facs[i].name, syslog_facility_name) == 0) {
	    syslog_fac = syslog_facs[i].fac_code;
	    break;
	  }
	}

	if (syslog_fac >= 0) {
	  erlangsyslog = 1;
	  openlog("aprx", LOG_NDELAY|LOG_PID, syslog_fac);
	}
}

void erlang_start(void)
{
	erlang_backingstore_open();
}
