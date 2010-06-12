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
#if (defined(ERLANGSTORAGE) || (USE_ONE_MINUTE_STORAGE == 1))
static int telemetry_1min_steps = 20;
#endif
#if (defined(ERLANGSTORAGE) || (USE_ONE_MINUTE_STORAGE == 0))
static int telemetry_10min_steps = 2;
#endif

static time_t telemetry_time;
static int telemetry_seq;
static int telemetry_params;

struct rftelemetry {
	struct aprx_interface  *transmitter;
	struct aprx_interface **sources;
	int		        source_count;
	char	               *viapath;
};

static int                  rftelemetrycount;
static struct rftelemetry **rftelemetry;

static void rf_telemetry(struct aprx_interface *sourceaif, char *beaconaddr,
			 const char *buf, const int buflen);

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
	int  buflen;
	char beaconaddr[60];
	int  beaconaddrlen;
	long erlmax;
	float erlcapa;

	if (telemetry_time > now)
		return 0;	/* Not yet ... */

	telemetry_time += telemetry_interval;

	if (debug)
	  printf("Telemetry Tx run; next one in %.2f minutes\n", (telemetry_interval/60.0));

	// Init these for RF transmission
	buf[0] = 0x03; // AX.25 Control
	buf[1] = 0xF0; // AX.25 PID

	++telemetry_seq;
	telemetry_seq %= 256;
	for (i = 0; i < ErlangLinesCount; ++i) {
		struct erlangline *E = ErlangLines[i];
		struct aprx_interface *sourceaif = find_interface_by_callsign(E->name);

		beaconaddrlen = sprintf(beaconaddr, "%s>RXTLM-%d,TCPIP", E->name, (i % 15) + 1);
		// First two bytes of BUF are for AX.25 control+PID fields
		s = buf+2;
		s += sprintf(s, "T#%03d,", telemetry_seq);


		// Raw Rx Erlang - plotting scale factor: 1/200
#if (USE_ONE_MINUTE_DATA == 1)
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
#else
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
#endif

		// Raw Tx Erlang - plotting scale factor: 1/200
#if (USE_ONE_MINUTE_DATA == 1)
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
#else
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
#endif

#if (USE_ONE_MINUTE_DATA == 1)
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
#else
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
#endif

#if (USE_ONE_MINUTE_DATA == 1)
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
#else
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
#endif

#if (USE_ONE_MINUTE_DATA == 1)
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
#else
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
#endif
		
		/* Tail filler */
		s += sprintf(s, "00000000");  // FIXME: flag telemetry?

		/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

		/* Send those (net)beacons.. */
		buflen = s - buf;
		aprsis_queue(beaconaddr, beaconaddrlen,  aprsis_login,
			     buf+2, buflen-2);
		rf_telemetry(sourceaif, beaconaddr, buf, buflen);

		if ((telemetry_params % 32) == 0) { /* every 5h20m */

			/* Send every 5h20m or thereabouts. */

			s = buf+2 + sprintf(buf+2,
					    ":%-9s:PARM.Avg 10m,Avg 10m,RxPkts,IGateDropRx,TxPkts",
					    E->name);
			buflen = s - buf;
			aprsis_queue(beaconaddr, beaconaddrlen, aprsis_login,
				     buf+2, buflen-2);
			rf_telemetry(sourceaif, beaconaddr, buf, buflen);

			s = buf+2 + sprintf(buf+2,
					    ":%-9s:UNIT.Rx Erlang,Tx Erlang,count/10m,count/10m,count/10m",
					    E->name);
			buflen = s - buf;
			aprsis_queue(beaconaddr, beaconaddrlen, aprsis_login,
				     buf+2, buflen-2);
			rf_telemetry(sourceaif, beaconaddr, buf, buflen);

			s = buf+2 + sprintf(buf+2,
					    ":%-9s:EQNS.0,0.005,0,0,0.005,0,0,1,0,0,1,0,0,1,0",
					    E->name);
			buflen = s - buf;
			aprsis_queue(beaconaddr, beaconaddrlen, aprsis_login,
				     buf+2, buflen-2);
			rf_telemetry(sourceaif, beaconaddr, buf, buflen);
		}
	}
	++telemetry_params;

	return 0;
}

static void rf_telemetry(struct aprx_interface *sourceaif, char *beaconaddr,
			 const char *buf, const int buflen)
{
	int i;
	int t_idx;
	char *dest;

	if (rftelemetrycount == 0) return; // Nothing to do!
	if (sourceaif == NULL) return; // Huh? Unknown source..

	// The beaconaddr comes in as:
	//    "interfacecall>RXTLM-n,TCPIP"
	dest = strchr(beaconaddr, ',');
	if (dest != NULL) *dest = 0;
	dest = strchr(beaconaddr, '>');
	if (dest != NULL) *dest++ = 0;
	if (dest == NULL) {
	  // Impossible -- said she...
	  return;
	}

	for (t_idx = 0; t_idx < rftelemetrycount; ++t_idx) {
	  struct rftelemetry *rftlm = rftelemetry[t_idx];
	  if (rftlm == NULL) break;
	  for (i = 0; i < rftlm->source_count; ++i) {
	    if (rftlm->sources[i] == sourceaif) {
	      // Found telemetry transmitter which wants this source

	      interface_transmit_beacon(rftlm->transmitter,
					beaconaddr,
					dest,
					rftlm->viapath,
					buf, buflen);
	    }
	  }
	}
}

void telemetry_config(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int   has_fault = 0;

	struct aprx_interface  *aif          = NULL;
	struct aprx_interface **sources      = NULL;
	int			source_count = 0;
	char                   *viapath      = NULL;

	while (readconfigline(cf) != NULL) {
		if (configline_is_comment(cf))
			continue;	/* Comment line, or empty line */

		// It can be severely indented...
		str = config_SKIPSPACE(cf->buf);

		name = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);
		config_STRLOWER(name);

		param1 = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		if (strcmp(name, "</telemetry>") == 0)
		  break;

		if (strcmp(name, "transmit") == 0 ||
		    strcmp(name, "transmitter") == 0) {
			if (strcmp(param1,"$mycall") == 0)
				param1 = (char*)mycall;

			aif = find_interface_by_callsign(param1);
			if (aif != NULL && (!aif->txok)) {
			  aif = NULL; // Not 
			  printf("%s:%d ERROR: This transmit interface has no TX-OK TRUE setting: '%s'\n",
				 cf->name, cf->linenum, param1);
			  has_fault = 1;
			} else if (aif == NULL) {
			  printf("%s:%d ERROR: Unknown interface: '%s'\n",
				 cf->name, cf->linenum, param1);
			  has_fault = 1;
			}
		} else if (strcmp(name, "via") == 0) {
			if (viapath != NULL) {
			  printf("%s:%d ERROR: Double definition of 'via'\n",
				 cf->name, cf->linenum);
			  has_fault = 1;
			} else if (*param1 == 0) {
			  printf("%s:%d ERROR: 'via' keyword without parameter\n",
				 cf->name, cf->linenum);
			  has_fault = 1;
			}
			if (!has_fault) {
			  const char *check;
			  config_STRUPPER(param1);
			  check = tnc2_verify_callsign_format(param1, 0, 1, param1+strlen(param1));
			  if (check == NULL) {
			    has_fault = 1;
			    printf("%s:%d ERROR: The 'via %s' parameter is not acceptable AX.25 format\n",
				   cf->name, cf->linenum, param1);

			  }
			}
			if (!has_fault) {
			  // Save it
			  viapath = strdup(param1);
			}
		} else if (strcmp(name, "source") == 0) {
			struct aprx_interface *source_aif = NULL;
			if (debug)
			  printf("%s:%d <telemetry> source = '%s'\n",
				 cf->name, cf->linenum, param1);

			if (strcmp(param1,"$mycall") == 0)
			  param1 = (char*)mycall;

			source_aif = find_interface_by_callsign(param1);
			if (source_aif == NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Digipeater source '%s' not found\n",
				 cf->name, cf->linenum, param1);
			} else {
			  // Collect them all...
			  sources = realloc(sources, sizeof(void*)*(source_count+2));
			  sources[source_count++] = source_aif;
			  sources[source_count+1] = NULL;
			}
			if (debug>1)
			  printf(" .. source_aif = %p\n", source_aif);
		}
	}

	if (has_fault) {
	  if (sources != NULL)
	    free(sources);
	  if (viapath != NULL)
	    free(viapath);
	} else {
	  struct rftelemetry *newrf = malloc(sizeof(*newrf));
	  newrf->transmitter = aif;
	  newrf->viapath     = viapath;
	  newrf->sources     = sources;
	  newrf->source_count = source_count;
	  rftelemetry = realloc(rftelemetry, sizeof(void*)*(rftelemetrycount+2));
	  rftelemetry[rftelemetrycount++] = newrf;

	  if (debug) printf("Defined <telemetry> to transmitter %s\n", aif->callsign);
	}
}
