/********************************************************************
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2010                            *
 *                                                                  *
 ********************************************************************/

#ifndef KEYHASH_H
#define KEYHASH_H

extern void         keyhash_init(void);
extern unsigned int keyhash(const void *s, int slen, unsigned int hash0);

#endif
