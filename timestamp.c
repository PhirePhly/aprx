#include "aprx.h"

/* Time Base Conversion Macros
 *
 * The NTP timebase is 00:00 Jan 1 1900.  The local
 * time base is 00:00 Jan 1 1970.  Convert between
 * these two by added or substracting 70 years
 * worth of time.  Note that 17 of these years were
 * leap years.
 */
#define TIME_BASEDIFF           (((70U*365U + 17U) * 24U*3600U))
#define TIME_NTP_TO_LOCAL(t)    ((t)-TIME_BASEDIFF)
#define TIME_LOCAL_TO_NTP(t)    ((t)+TIME_BASEDIFF)

typedef struct ntptime {
	uint32_t seconds;
	uint32_t fraction;
} ntptime_t;

uint64_t unix_tv_to_ntp(struct timeval *tv) {
  // Reciprocal conversion of tv_usec to fractional NTP seconds
  // Multiply tv_usec by  (2^64)/1_000_000 
  // GCC optimized this nicely on i386
  uint64_t fract = 18446744073709ULL * (uint32_t)(tv->tv_usec);
  // Scale it back by 32 bit positions
  fract >>= 32;
  // Straight-forward conversion of tv_sec to NTP seconds
  uint64_t ntptime = TIME_LOCAL_TO_NTP(tv->tv_sec);
  ntptime <<= 32;
  return ntptime + fract;
}

void unix_tv_to_ntp4(struct timeval *tv, ntptime_t *ntp) {
  // Reciprocal conversion of tv_usec to fractional NTP seconds
  // Multiply tv_usec by  ((2^64)/1_000_000) / (2^32)
  // GCC optimized this nicely on i386, and 64-bit machines
  uint32_t fract = (18446744073709ULL * (uint32_t)(tv->tv_usec)) >> 32;
  //
  //      movl    4(%ebx), %eax
  //      imull   $4294, %eax, %esi    ;; 32*32->32 --> %esi
  //      movl    $-140462611, %edi
  //      mull    %edi                 ;; 32*32->64 --> %edx:eax
  //      addl    %edx, %esi           ;; sum %esi + %edx
  //
  ntp->fraction = fract;
  // Straight-forward conversion of tv_sec to NTP seconds
  ntp->seconds  = TIME_LOCAL_TO_NTP(tv->tv_sec);
}

void unix_tv_to_ntp4a(struct timeval *tv, ntptime_t *ntp) {
  // Reciprocal conversion of tv_usec to fractional NTP seconds
  // Multiply tv_usec by  ((2^64)/1_000_000) / (2^32)
  // GCC optimizes this slightly better for ARM, than ntp4()
  //  .. for i386 ntp4() and ntp4a() are equal.
  uint64_t fract = 18446744073709ULL * (uint32_t)(tv->tv_usec);
  // Scale it back by 32 bit positions
  fract >>= 32;
  ntp->fraction = (uint32_t)fract;
  // Straight-forward conversion of tv_sec to NTP seconds
  ntp->seconds  = TIME_LOCAL_TO_NTP(tv->tv_sec);
}


uint64_t unix_tv_to_ntp2(struct timeval *tv) {
  uint64_t tt = TIME_LOCAL_TO_NTP(tv->tv_sec);
  tt <<= 32;
  uint64_t tu = tv->tv_usec;
  tu <<= 32;
  // Following causes gcc to call __udivdi3() 
  // on 32-bit machines
  tu /= 1000000; // Fixed point scaling..
  return (tt + tu);
}

// static const double usec2NtpFract = 4294.9672960D; // 2^32 / 1E6

uint64_t unix_tv_to_ntp3(struct timeval *tv) {
  uint64_t tt = TIME_LOCAL_TO_NTP(tv->tv_sec);
  tt <<= 32;
// FP math is bad on embedded systems...
//  double fract = usec2NtpFract * (uint32_t)tv->tv_usec;
//  tt += (int64_t)fract;
  return tt;
}


static const char *BASE64EncodingDictionary =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789"
  "+/";

void encode_aprsis_ntptimestamp(uint64_t ntptime, char timestamp[8])
{
	int i;

	ntptime >>= 22; // scale to 1/1024 seconds
	for (i = 6; i >= 0; --i) {
	    int n = (((int)ntptime) & 0x3F); // lowest 6 bits
	    // printf("  [n=%d]\n", n);
	    ntptime >>= 6;
	    timestamp[i] = BASE64EncodingDictionary[n];
	}
	timestamp[7] = 0;
}

static const int8_t BASE64DecodingDictionary[128] =
  { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1,  // ' ', '!', '"', '#'
    -1, -1, -1, -1,  // '$', '%', '&'', '\''
    -1, -1, -1, 62,  // '(', ')', '*', '+',
    -1, -1, -1, 63,  // ',', '-', '.', '/'
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, // '0' .. '9'
    -1, -1, -1, -1, -1, -1, // ':', ';', '<', '=', '>', '?'
    -1,  0,  1,  2,  3,  4,  5,  6, // '@', 'A' .. 'G'
     7,  8,  9, 10, 11, 12, 13, 14, // 'H' .. 'O'
    15, 16, 17, 18, 19, 20, 21, 22, // 'P' .. 'W'
    23, 24, 25, -1, -1, -1, -1, -1, // 'X'..'Z', '[', '\\', ']', '^', '_'
    -1, 26, 27, 28, 29, 30, 31, 32, // '`', 'a' .. 'g'
    33, 34, 35, 36, 37, 38, 39, 40, // 'h' .. 'o'
    41, 42, 43, 44, 45, 46, 47, 48, // 'p' .. 'w'
    49, 50, 51, -1, -1, -1, -1, -1 }; // 'x'..'z', ...


int decode_aprsis_ntptimestamp(char timestamp[8], uint64_t *ntptimep)
{
	uint64_t ntptime = 0;

	int i, n;
	char c;

	for (i = 0; i < 7; ++i) {
	  c = timestamp[i];
	  if (c <= 0 || c > 127) return -1; // BARF!
	  n = BASE64DecodingDictionary[(int)c];
	  // printf("  [n=%d]\n", n);
	  if (n < 0) {
	    // Should not happen!
	    return -1; // Decode fail!
	  }

	  ntptime <<= 6;
	  ntptime |= n;
	}
	ntptime <<= 22;
	*ntptimep = ntptime;
	return 0; // Decode OK
}

#ifdef TESTING

int main(int argc, char *argv[]) {

	struct timeval tv;
	char timestamp[8];
	uint64_t ntptime;
	ntptime_t ntp_time;

	// gettimeofday(&tv, NULL);

	// Example time.. (refvalue: NTPseconds!)
	tv.tv_sec = TIME_NTP_TO_LOCAL(3484745636U); tv.tv_usec = 709603U;

	ntptime = unix_tv_to_ntp(&tv);
	printf("NTPtime1 = %08x.%08x \n", (uint32_t)(ntptime >> 32), (uint32_t)ntptime);
	ntptime = unix_tv_to_ntp2(&tv);
	printf("NTPtime2 = %08x.%08x \n", (uint32_t)(ntptime >> 32), (uint32_t)ntptime);
	// ntptime = unix_tv_to_ntp3(&tv);
	// printf("NTPtime3 = %08x.%08x \n", (uint32_t)(ntptime >> 32), (uint32_t)ntptime);

	unix_tv_to_ntp4(&tv, &ntp_time);
	printf("NTPtime4 = %08x.%08x \n", ntp_time.seconds, ntp_time.fraction);

	encode_aprsis_ntptimestamp( ntptime, timestamp );
	printf("Timestamp = %s\n", timestamp);

	int rc = decode_aprsis_ntptimestamp( timestamp, &ntptime );
	printf("Decode rc=%d\n", rc);
	printf("NTPtime = %08x.%08x \n", (uint32_t)(ntptime >> 32), (uint32_t)ntptime);

	return 0;
}
#endif
