/********************************************************************
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 ********************************************************************/

#ifndef _CELLMALLOC_H_
#define _CELLMALLOC_H_

/*
 *   cellmalloc() -- manages arrays of cells of data 
 *
 */

typedef struct cellarena_t cellarena_t;

extern cellarena_t *cellinit(const char *arenaname, const int cellsize, const int alignment, const int policy, const int createkb, const int minfree);

#define CELLMALLOC_POLICY_FIFO    0
#define CELLMALLOC_POLICY_LIFO    1
#define CELLMALLOC_POLICY_NOMUTEX 2

extern void *cellmalloc(cellarena_t *cellarena);
extern int   cellmallocmany(cellarena_t *cellarena, void **array, const int numcells);
extern void  cellfree(cellarena_t *cellarena, void *p);
extern void  cellfreemany(cellarena_t *cellarena, void **array, const int numcells);

#endif
