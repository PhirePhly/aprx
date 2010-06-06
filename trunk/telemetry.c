/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2010                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

#define  telemetry_timescaler 2       // scale to 10 minute sums
static int telemetry_interval = 20 * 60; // 20 minutes
static int telemetry_1min_steps = 20;
static int telemetry_10min_steps = 2;

static time_t telemetry_time;
static int telemetry_seq;
static int telemetry_params;

void telemetry_start()
{
	/*
	 * Initialize the sequence start to be highly likely
	 * different from previous one...  This really should
	 * be in some persistent database, but this is reasonable
	 * compromise.
	 */
	telemetry_seq = (time(NULL)) & 255;

	telemetry_time = now + telemetry_interval;

}

int telemetry_prepoll(struct aprxpolls *app)
{
	if (telemetry_time == 0)
		telemetry_time = now + 30;

	if (app->next_timeout > telemetry_time)
		app->next_timeout = telemetry_time;

	return 0;
}

int telemetry_postpoll(struct aprxpolls *app)
{
	int i, j, k, t;
	char buf[200], *s;
	char beaconaddr[30];
	int  beaconaddrlen;
	long erlmax;
	float erlcapa;

	if (telemetry_time > now)
		return 0;	/* Not yet ... */

	telemetry_time += telemetry_interval;

	if (debug)
	  printf("Telemetry Tx run; next one in %.2f minutes\n", (telemetry_interval/60.0));


	++telemetry_seq;
	telemetry_seq %= 256;
	for (i = 0; i < ErlangLinesCount; ++i) {
		struct erlangline *E = ErlangLines[i];

		beaconaddrlen = sprintf(beaconaddr, "%s>RXTLM-%d,TCPIP", E->name, i + 1);
		s = buf;
		s += sprintf(s, "T#%03d,", telemetry_seq);

#define USE_ONE_MINUTE_DATA 0

		// Raw Rx Erlang - plotting scale factor: 1/200
		if (USE_ONE_MINUTE_DATA) {
			erlmax = 0;
			k = E->e1_cursor;
			t = E->e1_max;
			if (t > telemetry_1min_steps)
				t = telemetry_1min_steps;	// Up to 10 of 1 minute samples
			erlcapa = 1.0 / E->erlang_capa; // 1/capa of 1 minute
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e1_max - 1;
				if (E->e1[k].bytes_rx > erlmax)
					erlmax = E->e1[k].bytes_rx;
			}
			k = (int) (200.0 * erlcapa * erlmax);
			// if (k > 255) k = 255;
			s += sprintf(s, "%d,", k);
		} else {
			erlmax = 0;
			k = E->e10_cursor;
			t = E->e10_max;
			if (t > telemetry_10min_steps)
				t = telemetry_10min_steps;	// Up to 1 of 10 minute samples
			erlcapa = 0.1 / E->erlang_capa; // 1/capa of 10 minute 
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e10_max - 1;
				if (E->e10[k].bytes_rx > erlmax)
					erlmax = E->e10[k].bytes_rx;
			}
			k = (int) (200.0 * erlcapa * erlmax);
			// if (k > 255) k = 255;
			s += sprintf(s, "%d,", k);
		}

		// Raw Tx Erlang - plotting scale factor: 1/200
		if (USE_ONE_MINUTE_DATA) {
			erlmax = 0;
			k = E->e1_cursor;
			t = E->e1_max;
			if (t > telemetry_1min_steps)
				t = telemetry_1min_steps;	// Up to 10 of 1 minute samples
			erlcapa = 1.0 / E->erlang_capa; // 1/capa of 1 minute
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e1_max - 1;
				if (E->e1[k].bytes_tx > erlmax)
					erlmax = E->e1[k].bytes_tx;
			}
			k = (int) (200.0 * erlcapa * erlmax);
			// if (k > 255) k = 255;
			s += sprintf(s, "%d,", k);
		} else {
			erlmax = 0;
			k = E->e10_cursor;
			t = E->e10_max;
			if (t > telemetry_10min_steps)
				t = telemetry_10min_steps;	// Up to 1 of 10 minute samples
			erlcapa = 0.1 / E->erlang_capa; // 1/capa of 10  minute
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e10_max - 1;
				if (E->e10[k].bytes_tx > erlmax)
					erlmax = E->e10[k].bytes_tx;
			}
			k = (int) (200.0 * erlcapa * erlmax);
			// if (k > 255) k = 255;
			s += sprintf(s, "%d,", k);
		}

		if (USE_ONE_MINUTE_DATA) {
			erlmax = 0;
			k = E->e1_cursor;
			t = E->e1_max;
			if (t > telemetry_1min_steps)
				t = telemetry_1min_steps;	/* Up to 10 of 1 minute samples */
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e1_max - 1;
				erlmax += E->e1[k].packets_rx;
			}
			erlmax /= telemetry_timescaler;
			s += sprintf(s, "%d,", (int) erlmax); // scale to same as 10 minute data
		} else {
			erlmax = 0;
			k = E->e10_cursor;
			t = E->e10_max;
			if (t > telemetry_10min_steps)
				t = telemetry_10min_steps;	// Up to 1 of 10 minute samples
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e10_max - 1;
				erlmax += E->e10[k].packets_rx;
			}
			erlmax /= telemetry_timescaler;
			s += sprintf(s, "%d,", (int) erlmax);
		}

		if (USE_ONE_MINUTE_DATA) {
			erlmax = 0;
			k = E->e1_cursor;
			t = E->e1_max;
			if (t > telemetry_1min_steps)
				t = telemetry_1min_steps;	/* Up to 10 of 1 minute samples */
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e1_max - 1;
				erlmax += E->e1[k].packets_rxdrop;
			}
			erlmax /= telemetry_timescaler;
			s += sprintf(s, "%d,", 10*(int) erlmax); // scale to same as 10 minute data
		} else {
			erlmax = 0;
			k = E->e10_cursor;
			t = E->e10_max;
			if (t > telemetry_10min_steps)
				t = telemetry_10min_steps;	// Up to 1 of 10 minute samples
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e10_max - 1;
				erlmax += E->e10[k].packets_rxdrop;
			}
			erlmax /= telemetry_timescaler;
			s += sprintf(s, "%d,", (int) erlmax);
		}

		if (USE_ONE_MINUTE_DATA) {
			erlmax = 0;
			k = E->e1_cursor;
			t = E->e1_max;
			if (t > telemetry_1min_steps)
				t = telemetry_1min_steps;	/* Up to 10 of 1 minute samples */
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e1_max - 1;
				erlmax += E->e1[k].packets_tx;
			}
			erlmax /= telemetry_timescaler;
			s += sprintf(s, "%d,", 10*(int) erlmax); // scale to same as 10 minute data
		} else {
			erlmax = 0;
			k = E->e10_cursor;
			t = E->e10_max;
			if (t > telemetry_10min_steps)
				t = telemetry_10min_steps;	// Up to 1 of 10 minute samples
			for (j = 0; j < t; ++j) {
				--k;
				if (k < 0)
					k = E->e10_max - 1;
				erlmax += E->e10[k].packets_tx;
			}
			erlmax /= telemetry_timescaler;
			s += sprintf(s, "%03d,", (int) erlmax);
		}
		
		/* Tail filler */
		s += sprintf(s, "00000000");  // FIXME: flag telemetry?

		/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

		/* Send those (net)beacons.. */
		aprsis_queue(beaconaddr, beaconaddrlen,  aprsis_login,
			     buf, (int) (s - buf));

		if ((telemetry_params % 32) == 0) { /* every 5h20m */

			/* Send every 5h20m or thereabouts. */

			s = buf + sprintf(buf,
					  ":%-9s:PARM.Avg 10m,Avg 10m,RxPkts,IGateDropRx,TxPkts",
					  E->name);
			aprsis_queue(beaconaddr, beaconaddrlen, aprsis_login,
				     buf, (int) (s - buf));

			s = buf + sprintf(buf,
					  ":%-9s:UNIT.Rx Erlang,Tx Erlang,count/10m,count/10m,count/10m",
					  E->name);
			aprsis_queue(beaconaddr, beaconaddrlen, aprsis_login,
				     buf, (int) (s - buf));

			s = buf + sprintf(buf,
					  ":%-9s:EQNS.0,0.005,0,0,0.005,0,0,1,0,0,1,0,0,1,0",
					  E->name);
			aprsis_queue(beaconaddr, beaconaddrlen, aprsis_login,
				     buf, (int) (s - buf));
		}
	}
	++telemetry_params;

	return 0;
}
