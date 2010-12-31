/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2011                            *
 *                                                                  *
 * **************************************************************** */

#ifdef _FOR_VALGRIND_
#include "aprx.h"

/*
 * High-efficiency algorithms used by libc cause terrible complaints
 * from valgrind..
 *
 * These naive single char at the time things don't go reading into
 * uninitialized areas..
 *
 */

int memcmp(const void *p1, const void *p2, size_t n) {
  const char *s1 = p1;
  const char *s2 = p2;
  for( ; n > 0 && *s1 == *s2 ; ++s1, ++s2, --n ) ;
  if (n == 0) return 0;
  return (*s1 - *s2);
}
void *memcpy(void *dest, const void *src, size_t n) {
  char *p = dest;
  const char *s = src;
  for ( ; n > 0; --n ) {
    *p++ = *s++;
  }
  return dest;
}
size_t strlen(const char *p) {
  size_t i;
  for ( i = 0; *p != 0; ++p, ++i ) ;
  return i;
}
char *strdup(const char *s) {
  int len = strlen(s)+1;
  char *p = malloc(len);
  memcpy(p, s, len);
  return p;
}
int strcmp(const char *s1, const char *s2) {
  for( ; *s1 && *s2 && *s1 == *s2 ; ++s1, ++s2 ) ;
  if (*s1 == 0 && *s2 == 0) return 0;
  if (*s1 == 0) return -1;
  if (*s2 == 0) return  1;
  return (*s1 - *s2);
}
int strncmp(const char *s1, const char *s2, size_t n) {
  for( ; n > 0 && *s1 && *s2 && *s1 == *s2 ; ++s1, ++s2, --n ) ;
  if (n == 0) return 0;
  if (*s1 == 0) return -1;
  if (*s2 == 0) return  1;
  return (*s1 - *s2);
}
char   *strcpy(char *dest, const char *src) {
  char *p = dest;
  while (*src != 0) {
    *p++ = *src++;
  }
  return dest;
}
char   *strncpy(char *dest, const char *src, size_t n) {
  char *p = dest;
  for (;*src != 0 && n > 0; --n) {
    *p++ = *src++;
  }
  return dest;
}
void   *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = s;
  c &= 0xFF;
  for (p = s; n > 0; --n, ++p) {
    if (*p == c) return (void*)p;
  }
  return NULL;
}
char  *strchr(const char *s, int c) {
  c &= 0xFF;
  for (; *s != 0; ++s) {
    if (((*s) & 0xFF) == c) return (char*)s;
  }
  return NULL;
}

#endif
