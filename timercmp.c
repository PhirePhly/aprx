/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 * **************************************************************** */
#include "aprx.h"

/* Bits used only in the main program.. */
#include <signal.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_TIME_H
# include <time.h>
#endif
#include <fcntl.h>

struct timeval now; // public wall clock that can jump around
struct timeval tick;  // monotonic clock

/*
 * Calculate difference from now time to target time in milliseconds.
 */
int tv_timerdelta_millis(struct timeval *_now, struct timeval *_target)
{
	int deltasec  = _target->tv_sec  - _now->tv_sec;
        int deltausec = _target->tv_usec - _now->tv_usec;
        while (deltausec < 0) {
        	deltausec += 1000000;
                --deltasec;
        }
        return deltasec * 1000 + deltausec / 1000;
}

/*
 * Add milliseconds to input parameter a returning
 * the result though parameter ret.
 */
void tv_timeradd_millis(struct timeval *ret, struct timeval *a, int millis)
{
	if (ret != a) {
          // Copy if different pointers..
          *ret = *a;
        }
        int usec = (int)(ret->tv_usec) + millis * 1000;
        if (usec >= 1000000) {
          int dsec = (usec / 1000000);
          ret->tv_sec += dsec;
          usec %= 1000000;
          // if (debug>3) printf("tv_timeadd_millis() dsec=%d dusec=%d\n",dsec, usec);
        }
        ret->tv_usec = usec;
}

/*
 * Add seconds to input parameter a returning
 * the result though parameter ret.
 */
void tv_timeradd_seconds(struct timeval *ret, struct timeval *a, int seconds)
{
	if (ret != a) {
          // Copy if different pointers..
          *ret = *a;
        }
        ret->tv_sec += seconds;
}


/*
 * Comparison returning -1/0/+1 depending on ( a <=> b )
 *
 * This handles overflow wraparound of Y2038 issue of 32-bit UNIX time_t.
 */
int timecmp(const time_t a, const time_t b)
{
	const int i = (int)(a - b);
        if (i == 0) return 0;
        if (i > 0) return 1;
        return -1;
}

/*
 * Time compare function returning -1/0/+1 depending
 * which parameter presents time before the other.
 * Zero means equals.
 */
int tv_timercmp(struct timeval * const a, struct timeval * const b)
{
  // if (debug>3) {
  // int dt_sec  = a->tv_sec - b->tv_sec;
  // int dt_usec = a->tv_usec - b->tv_usec;
  // printf("tv_timercmp(%d.%06d <=> %d.%06d) dt=%d:%06d ret= ",
  // a->tv_sec, a->tv_usec, b->tv_sec, b->tv_usec, dt_sec, dt_usec);
  // }

	// Time delta calculation to avoid year 2038 issue
	const int dt = timecmp(a->tv_sec, b->tv_sec);
        if (dt != 0) {
          // if (debug>3) printf("%ds\n", dt);
          return dt;
        }
        // tv_usec is always in range 0 .. 999 999
        if (a->tv_usec < b->tv_usec) {
          // if (debug>3) printf("-1u\n");
          return -1;
        }
        if (a->tv_usec > b->tv_usec) {
          // if (debug>3) printf("1u\n");
          return 1;
        }
        // if (debug>3) printf("0\n");
        return 0; // equals!
}

/*
 * Compare *tv with current time value (now), and if the difference
 * is more than margin seconds, then call resetfunc with resetarg.
 *
 * Usually resetarg == tv, but not always.
 * See 
 */
void tv_timerbounds(const char *timername,
                    struct timeval *tv,
                    const int margin,
                    void (*resetfunc)(void*),
                    void *resetarg)
{
	// Check that system time has not jumped too far ahead/back;
	// that it is within margin seconds to tv.

	struct timeval nowminus;
	struct timeval nowplus;

        tv_timeradd_seconds(&nowminus, &tick, -margin);

        // If current time MINUS margin is AFTER tv, then reset.
        if (tv_timercmp(tv, &nowminus) < 0) {
        	if (debug)
                	printf("System time has gone too much forwards, Resetting timer '%s'. dt=%d margin=%d\n",
                               timername,  (int)(tv->tv_sec - nowminus.tv_sec), margin);
                resetfunc(resetarg);
        }

        tv_timeradd_seconds(&nowplus,  &tick,  margin);

        // If current time PLUS margin is BEFORE tv, then reset.
        if (tv_timercmp(&nowplus, tv) < 0) {
        	if (debug)
                	printf("System time has gone too much backwards, Resetting timer '%s'. dt=%d margin=%d\n",
                               timername, (int)(nowplus.tv_sec - tv->tv_sec), margin);
                resetfunc(resetarg);
        }
}
