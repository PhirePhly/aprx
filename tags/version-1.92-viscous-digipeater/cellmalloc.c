/********************************************************************
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 ********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "cellmalloc.h"

/*
 *   cellmalloc() -- manages arrays of cells of data 
 *
 */

struct cellhead;

struct cellarena_t {
	int	cellsize;
	int	alignment;
	int	increment; /* alignment overhead applied.. */
	int	lifo_policy;
  	int	minfree;
	int	use_mutex;

	const char *arenaname;

	pthread_mutex_t mutex;

  	struct cellhead *free_head;
  	struct cellhead *free_tail;

	int	 freecount;
	int	 createsize;

	int	 cellblocks_count;
#define CELLBLOCKS_MAX 40 /* track client cell allocator limit! */
	char	*cellblocks[CELLBLOCKS_MAX];	/* ref as 'char pointer' for pointer arithmetics... */
};

#define CELLHEAD_DEBUG 0

struct cellhead {
#if CELLHEAD_DEBUG == 1
	struct cellarena_t *ca;
#endif
	struct cellhead *next;
};


/*
 * new_cellblock() -- must be called MUTEX PROTECTED
 *
 */

int new_cellblock(cellarena_t *ca)
{
	int i;
	char *cb;

#ifdef MEMDEBUG /* External backing-store files, unique ones for each cellblock,
		   which at Linux names memory blocks in  /proc/nnn/smaps "file"
		   with this filename.. */
	int fd;
	char name[2048];

	sprintf(name, "/tmp/.-%d-%s-%d.mmap", getpid(), ca->arenaname, ca->cellblocks_count );
	unlink(name);
	fd = open(name, O_RDWR|O_CREAT, 644);
	unlink(name);
	if (fd >= 0) {
	  memset(name, 0, sizeof(name));
	  i = 0;
	  while (i < ca->createsize) {
	    int rc = write(fd, name, sizeof(name));
	    if (rc < 0) break;
	    i += rc;
	  }
	}

	cb = mmap( NULL, ca->createsize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
#else

#ifndef MAP_ANON
#  define MAP_ANON 0
#endif
	cb = mmap( NULL, ca->createsize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
#endif
	if (cb == NULL || cb == (char*)-1)
	  return -1;

	if (ca->cellblocks_count >= CELLBLOCKS_MAX) return -1;

	ca->cellblocks[ca->cellblocks_count++] = cb;

	for (i = 0; i <= ca->createsize-ca->increment; i += ca->increment) {
		struct cellhead *ch = (struct cellhead *)(cb + i); /* pointer arithmentic! */
		if (!ca->free_head) {
		  ca->free_head = ch;
		} else {
		  ca->free_tail->next = ch;
		}
		ca->free_tail = ch;
		ch->next = NULL;
#if CELLHEAD_DEBUG == 1
		ch->ca   = ca; // cellhead pointer space
#endif

		ca->freecount += 1;
	}

	return 0;
}



/*
 * cellinit()  -- the main program calls this once for each used cell type/size
 *
 */


cellarena_t *cellinit( const char *arenaname, const int cellsize, const int alignment, const int policy, const int createkb, const int minfree )
{
	cellarena_t *ca = malloc(sizeof(*ca));
	int n;

	memset(ca, 0, sizeof(*ca));

	ca->arenaname = arenaname;

#if CELLHEAD_DEBUG == 1
	if (alignment < __alignof__(void*))
		alignment = __alignof__(void*);   // cellhead pointer space
#endif

	ca->cellsize  = cellsize;
	ca->alignment = alignment;
	ca->minfree   = minfree;
#if CELLHEAD_DEBUG == 1
	ca->increment = cellsize + sizeof(void*); // cellhead pointer space
#else
	ca->increment = cellsize;
#endif
	if ((cellsize % alignment) != 0) {
		ca->increment +=  alignment - cellsize % alignment;
	}
	ca->lifo_policy =  policy & CELLMALLOC_POLICY_LIFO;
	ca->use_mutex   = (policy & CELLMALLOC_POLICY_NOMUTEX) ? 0 : 1;

	ca->createsize = createkb * 1024;

	n = ca->createsize / ca->increment;
	// hlog( LOG_DEBUG, "cellinit: %-12s block size %4d kB, cells/block: %d", arenaname, createkb, n );

	pthread_mutex_init(&ca->mutex, NULL);

	new_cellblock(ca); /* First block of cells, not yet need to be mutex protected */
	while (ca->freecount < ca->minfree)
		new_cellblock(ca); /* more until minfree is full */

#if CELLHEAD_DEBUG == 1
	// hlog(LOG_DEBUG, "cellinit()  cellhead=%p", ca);
#endif
	return ca;
}


inline void *cellhead_to_clientptr(struct cellhead *ch)
{
	char *p = (char*)ch;
#if CELLHEAD_DEBUG == 1
	p += sizeof(void*);
#endif
	return p;
}

inline struct cellhead *clientptr_to_cellhead(void *v)
{
#if CELLHEAD_DEBUG == 1
	struct cellhead *ch = (struct cellhead *)(((char*)v) - sizeof(void*));
#else
	struct cellhead *ch = (struct cellhead*)v;
#endif
	return ch;
}


void *cellmalloc(cellarena_t *ca)
{
	void *cp;
	struct cellhead *ch;

	if (ca->use_mutex)
		pthread_mutex_lock(&ca->mutex);

	while (!ca->free_head  || (ca->freecount < ca->minfree))
		if (new_cellblock(ca)) {
			pthread_mutex_unlock(&ca->mutex);
			return NULL;
		}

	/* Pick new one off the free-head ! */
	ch = ca->free_head;
	ca->free_head = ch->next;
	ch->next = NULL;
	cp = ch;
	if (ca->free_head == NULL)
	  ca->free_tail = NULL;

	ca->freecount -= 1;

	if (ca->use_mutex)
		pthread_mutex_unlock(&ca->mutex);

	// hlog(LOG_DEBUG, "cellmalloc(%p at %p) freecount %d", cellhead_to_clientptr(cp), ca, ca->freecount);

	return cellhead_to_clientptr(cp);
}

/*
 *  cellmallocmany() -- give many cells in single lock region
 *
 */

int   cellmallocmany(cellarena_t *ca, void **array, int numcells)
{
	int count;
	struct cellhead *ch;

	if (ca->use_mutex)
		pthread_mutex_lock(&ca->mutex);

	for (count = 0; count < numcells; ++count) {

		while (!ca->free_head ||
		       ca->freecount < ca->minfree) {
			/* Out of free cells ? alloc new set */
			if (new_cellblock(ca)) {
			  /* Failed ! */
			  break;
			}
		}

		/* Pick new one off the free-head ! */

		ch = ca->free_head;

		// hlog( LOG_DEBUG, "cellmallocmany(%d of %d); freecount %d; %p at %p",
		//       count, numcells, ca->freecount, cellhead_to_clientptr(ch), ca );

		// if (!ch)
		// 	break;	// Should not happen...

		ca->free_head = ch->next;
		ch->next = NULL;

		if (ca->free_head == NULL)
			ca->free_tail = NULL;

		array[count] = cellhead_to_clientptr(ch);

		ca->freecount -= 1;

	}

	if (ca->use_mutex)
		pthread_mutex_unlock(&ca->mutex);

	return count;
}



void  cellfree(cellarena_t *ca, void *p)
{
	struct cellhead *ch = clientptr_to_cellhead(p);
	ch->next = NULL;
#if CELLHEAD_DEBUG == 1
	if (ch->ca != ca) {
	  // hlog(LOG_ERR, "cellfree(%p to %p) wrong cellhead->ca pointer %p", p, ca, ch->ca);
	}
#endif

	// hlog(LOG_DEBUG, "cellfree() %p to %p", p, ca);

	if (ca->use_mutex)
		pthread_mutex_lock(&ca->mutex);

	if (ca->lifo_policy) {
	  /* Put the cell on free-head */
	  ch->next = ca->free_head;
	  ca->free_head = ch;

	} else {
	  /* Put the cell on free-tail */
	  if (ca->free_tail)
	    ca->free_tail->next = ch;
	  ca->free_tail = ch;
	  if (!ca->free_head)
	    ca->free_head = ch;
	  ch->next = NULL;
	}

	ca->freecount += 1;

	if (ca->use_mutex)
		pthread_mutex_unlock(&ca->mutex);
}

/*
 *  cellfreemany() -- release many cells in single lock region
 *
 */

void  cellfreemany(cellarena_t *ca, void **array, int numcells)
{
	int count;

	if (ca->use_mutex)
		pthread_mutex_lock(&ca->mutex);

	for (count = 0; count < numcells; ++count) {

	  struct cellhead *ch = clientptr_to_cellhead(array[count]);

#if CELLHEAD_DEBUG == 1
	  if (ch->ca != ca) {
	    // hlog(LOG_ERR, "cellfreemany(%p to %p) wrong cellhead->ca pointer %p", array[count], ca, ch->ca);
	  }
#endif

	  // hlog(LOG_DEBUG, "cellfreemany() %p to %p", ch, ca);

	  if (ca->lifo_policy) {
	    /* Put the cell on free-head */
	    ch->next = ca->free_head;
	    ca->free_head = ch;

	  } else {
	    /* Put the cell on free-tail */
	    if (ca->free_tail)
	      ca->free_tail->next = ch;
	    ca->free_tail = ch;
	    if (!ca->free_head)
	      ca->free_head = ch;
	    ch->next = NULL;
	  }

	  ca->freecount += 1;

	}

	if (ca->use_mutex)
		pthread_mutex_unlock(&ca->mutex);
}