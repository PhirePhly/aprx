/********************************************************************
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 ********************************************************************/

/*
 * Keyhash routines for the system.
 *
 * What is needed is _fast_ hash function.  Preferrably arithmethic one,
 * which does not need table lookups, and can work with aligned 32 bit
 * data -- but also on unaligned, and on any byte counts...
 *
 * Contenders:
 *   http://burtleburtle.net/bob/c/lookup3.c
 *   http://www.ibiblio.org/pub/Linux/devel/lang/c/mph-1.2.tar.gz
 *   http://www.concentric.net/~Ttwang/tech/inthash.htm
 *   http://isthe.com/chongo/tech/comp/fnv/
 *
 * Currently using FNV-1a
 *
 */

/*
//  FNV-1a  hash from   http://isthe.com/chongo/tech/comp/fnv/
//
//  It is algorithmic hash without memory lookups.
//  Compiler seems to prefer actual multiplication over a bunch of
//  fixed shifts and additions.
*/


/* ======================================================================
// The Linux kernel CRC32 computation code, heavily bastardized to simplify
// used code, and aimed for performance..  pure and simple.
// Furthermore, as we use this ONLY INTERNALLY, there is NO NEED to compute
// INTEROPERABLE format of this thing!
//
//  Some further notes:  Origins of the loop-unroll algorithm are from
//  Richard Black at Cambridge University, UK, 1993:
//  http://www.cl.cam.ac.uk/research/srg/bluebook/21/crc/node6.html
//
*/


/*
 * ON THIS CRC32 HASH CALCULATION PROCESSING ALONE THE SYSTEM IS PUSHING
 * AROUND 8-12 % OF CPU TIME!
 *
 * ... but the previous top waster, the aprsc output filters, are optimized
 * to the hilt...
 *
 * There exists alternate implementations of CRC32 which are 1.7 - 2.3 times
 * faster than this one with an expense of using 4kB / 8 kB / 16 kB of tables,
 * which of course fill caches...
 *
 */

/*
 * Oct 15, 2000 Matt Domsch <Matt_Domsch@dell.com>
 * Nicer crc32 functions/docs submitted by linux@horizon.com.  Thanks!
 * Code was from the public domain, copyright abandoned.  Code was
 * subsequently included in the kernel, thus was re-licensed under the
 * GNU GPL v2.
 *
 * Oct 12, 2000 Matt Domsch <Matt_Domsch@dell.com>
 * Same crc32 function was used in 5 other places in the kernel.
 * I made one version, and deleted the others.
 * There are various incantations of crc32().  Some use a seed of 0 or ~0.
 * Some xor at the end with ~0.  The generic crc32() function takes
 * seed as an argument, and doesn't xor at the end.  Then individual
 * users can do whatever they need.
 *   drivers/net/smc9194.c uses seed ~0, doesn't xor with ~0.
 *   fs/jffs2 uses seed 0, doesn't xor with ~0.
 *   fs/partitions/efi.c uses seed ~0, xor's with ~0.
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */


#include <stdint.h>
#include <sys/types.h>

#include "keyhash.h"

#ifdef __GNUC__ // compiling with GCC ?

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)

#else

#define likely(x)     (x)
#define unlikely(x)   (x)

#define __attribute__(x) 

#endif

#if 1 // lets see if  FVN-1a  can do better than CRC32..

void keyhash_init(void) { }

unsigned int __attribute__((pure)) keyhash(const void const *p, int len, unsigned int hash)
{
	const uint8_t *u = p;
	int i;
#define FNV_32_PRIME     16777619
#define FVN_32_OFFSET  2166136261

	if (hash == 0)
		hash = FVN_32_OFFSET;

	for (i = 0; i < len; ++i, ++u) {
#if defined(NO_FNV_GCC_OPTIMIZATION)
		hash *= FNV_32_PRIME;
#else
		hash += (hash<<1) + (hash<<4) + (hash<<7) +
		        (hash<<8) + (hash<<24);
#endif
		hash ^= (unsigned int) *u;
	}
	return hash;
}

#else

/*
 * There are multiple 16-bit CRC polynomials in common use, but this is
 * *the* standard CRC-32 polynomial, first popularized by Ethernet.
 * x^32+x^26+x^23+x^22+x^16+x^12+x^11+x^10+x^8+x^7+x^5+x^4+x^2+x^1+x^0
 */

#define CRCPOLY_LE 0xedb88320
#define CRCPOLY_BE 0x04c11db7


#define BE_TABLE_SIZE (1 << 8) /* 256 entries - 1 kB */

/* this table should be aligned by CPU cache line size... */
static uint32_t crc32table_be[BE_TABLE_SIZE];


/**
 * crc32_be() - Calculate bitwise big-endian Ethernet AUTODIN II CRC32
 * @crc - seed value for computation.  ~0 for Ethernet, sometimes 0 for
 *        other uses, or the previous crc32 value if computing incrementally.
 * @p   - pointer to buffer over which CRC is run
 * @len - length of buffer @p
 * 
 */
uint32_t __attribute__((pure)) keyhash(const void const *p, int len, uint32_t crc)
{
        const uint32_t      *b =(uint32_t *)p;
        const uint32_t      *tab = crc32table_be;

#  define DO_CRC(x) crc = tab[ (crc ^ (x)) & 255 ] ^ (crc>>8)
#  define DO_CRC0() crc = tab[ (crc)       & 255 ] ^ (crc>>8)

        /* Align it */
        if (unlikely(((long)b)&3 && len)){
                do {
                        uint8_t *s = (uint8_t *)b;
                        DO_CRC(*s++);
                        b = (uint32_t *)s;
                } while ((--len) && ((long)b)&3 );
        }
        if (likely(len >= 4)){
                /* load data 32 bits wide, xor data 32 bits wide. */
		size_t save_len = len & 3; // length of tail left over
                len = len >> 2; // number of 32-bit words to process
                --b; /* use pre increment below(*++b) for speed */
                do {
                        crc ^= *++b;
                        DO_CRC0();
                        DO_CRC0();
                        DO_CRC0();
                        DO_CRC0();
                } while (--len);
                b++; /* point to next byte(s) */
                len = save_len;
        }
        /* And the last few bytes */
        if (len){
                do {
                        uint8_t *s = (uint8_t *)b;
                        DO_CRC(*s++);
                        b = (void *)s;
                } while (--len);
        }
        return crc;
}

/**
 * crc32init_be() - allocate and initialize BE table data
 */
void keyhash_init(void)
{
        unsigned i, j;
        uint32_t crc = 0x80000000;

        crc32table_be[0] = 0;

        for (i = 1; i < BE_TABLE_SIZE; i <<= 1) {
                crc = (crc << 1) ^ ((crc & 0x80000000) ? CRCPOLY_BE : 0);
                for (j = 0; j < i; j++)
                        crc32table_be[i + j] = crc ^ crc32table_be[j];
        }
}

#endif
