/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation APRS iGate and digi with                 *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 * **************************************************************** */
#include "aprx.h"

#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
#include <signal.h>
#include <pthread.h>
pthread_t      netresolv_thread;
pthread_attr_t pthr_attrs;
#endif

static int                 nrcount;
static struct netresolver **nr;

static int RE_RESOLVE_INTERVAL = 300; // 15 minutes ?

struct netresolver *netresolv_add(const char *hostname, const char *port) {
	struct netresolver *n = malloc(sizeof(*n));
	memset(n, 0, sizeof(*n));
	n->hostname   = hostname;
	n->port       = port;
	n->ai.ai_addr = &n->sa;

	++nrcount;
	nr = realloc(nr, sizeof(void*)*nrcount);
	nr[nrcount-1] = n;
	return n;
}


static void resolve_all(void) {
	int i;

	if (debug>1)
	  printf("netresolve nrcount=%d\n", nrcount);

	for (i = 0; i < nrcount; ++i) {
		struct netresolver *n = nr[i];
		struct addrinfo *ai, req;
		int rc;

                timetick();

		if (timecmp(n->re_resolve_time, tick.tv_sec) > 0) {
		  // Not yet to re-resolve this one
		  if (debug>1)
		    printf("nr[%d] re_resolve_time in future (%d secs)\n",
			   i, (int)(n->re_resolve_time - tick.tv_sec));
		  continue;
		}

		memset(&req, 0, sizeof(req));
		req.ai_socktype = SOCK_STREAM;
		req.ai_protocol = IPPROTO_TCP;
		req.ai_flags = 0;
#if 1
		req.ai_family = AF_UNSPEC;	/* IPv4 and IPv6 are both OK */
#else
		req.ai_family = AF_INET;	/* IPv4 only */
#endif
		ai = NULL;

		rc = getaddrinfo(n->hostname, n->port, &req, &ai);
		if (rc != 0) {
		  // re-resolving failed, discard possible junk result
		  if (debug>1)
		    printf("nr[%d] resolving of %s:%s failed, error: %s\n",
			   i, n->hostname, n->port, gai_strerror(errno));
		  if (ai != NULL)
		    freeaddrinfo(ai);
		  continue;
		}

		if (debug>1)
		  printf("nr[%d] resolving of %s:%s success!\n",
			 i, n->hostname, n->port);

                timetick();

		// Make local static copy of first result
		memcpy(&n->sa, ai->ai_addr, ai->ai_addrlen);
		n->ai.ai_flags     = ai->ai_flags;
		n->ai.ai_family    = ai->ai_family;
		n->ai.ai_socktype  = ai->ai_socktype;
		n->ai.ai_protocol  = ai->ai_protocol;
		n->ai.ai_addrlen   = ai->ai_addrlen;
		n->ai.ai_addrlen   = ai->ai_addrlen;

		freeaddrinfo(ai);
		n->re_resolve_time  = tick.tv_sec + RE_RESOLVE_INTERVAL;
	}
}


#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
static void netresolv_runthread(void) {
	sigset_t sigs_to_block;

	sigemptyset(&sigs_to_block);
	sigaddset(&sigs_to_block, SIGALRM);
	sigaddset(&sigs_to_block, SIGINT);
	sigaddset(&sigs_to_block, SIGTERM);
	sigaddset(&sigs_to_block, SIGQUIT);
	sigaddset(&sigs_to_block, SIGHUP);
	sigaddset(&sigs_to_block, SIGURG);
	sigaddset(&sigs_to_block, SIGPIPE);
	sigaddset(&sigs_to_block, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &sigs_to_block, NULL);

	// the main program can cancel us at will
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

	while (!die_now) {
	  poll(NULL, 0, 30000); // Sleep 30 seconds (in a reliable way)
	  resolve_all();
	}
}
#endif

// Start netresolver thread, but at first run one round of resolving!
void netresolv_start(void) {
	resolve_all();

#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
	pthread_attr_init(&pthr_attrs);
	/* 64 kB stack is enough for this thread (I hope!)
	   default of 2 MB is way too much...*/
	pthread_attr_setstacksize(&pthr_attrs, 64*1024);

	pthread_create(&netresolv_thread, &pthr_attrs, (void*)netresolv_runthread, NULL);

#endif
}

// Shutdown the netresolver thread
void netresolv_stop(void)
{
	die_now = 1;
#if defined(HAVE_PTHREAD_CREATE) && defined(ENABLE_PTHREAD)
	pthread_cancel(netresolv_thread);
	pthread_join(netresolv_thread, NULL);
#endif
}
