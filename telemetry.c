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

#define  telemetry_timescaler 2              // scale to 10 minute sums

static int telemetry_interval = 20 * 60;     // every 20 minutes
static int telemetry_labelinterval = 120*60; // every 2 hours
static int telemetry_labelindex = 0;

#if (defined(ERLANGSTORAGE) || (USE_ONE_MINUTE_STORAGE == 1))
static int telemetry_1min_steps = 20;
#endif
#if (defined(ERLANGSTORAGE) || (USE_ONE_MINUTE_STORAGE == 0))
static int telemetry_10min_steps = 2;
#endif

static struct timeval telemetry_time;
static struct timeval telemetry_labeltime;
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

static void rf_telemetry(const struct aprx_interface *sourceaif, const char *beaconaddr,
			 const const char *buf, const int buflen);

static void telemetry_resettime(void *arg)
{
	struct timeval *tv = (struct timeval*)arg;
	tv_timeradd_seconds( tv, &tick, telemetry_interval );
}

static void telemetry_resetlabeltime(void *arg)
{
	struct timeval *tv = (struct timeval*)arg;
	tv_timeradd_seconds( tv, &tick, 120 );  // first label 2 minutes from now
}


void telemetry_start()
{
	/*
	 * Initialize the sequence start to be highly likely
	 * different from previous one...  This really should
	 * be in some persistent database, but this is reasonable
	 * compromise.
	 */
	telemetry_seq = (time(NULL)) & 255;

	// "tick" is supposedly current time..
        telemetry_resettime( &telemetry_time );
        telemetry_resetlabeltime( &telemetry_labeltime );

	if (debug) printf("telemetry_start()\n");
}

int telemetry_prepoll(struct aprxpolls *app)
{
	// Check that time has not jumped too far ahead/back (1.5 telemetry intervals)
	if (time_reset) {
        	telemetry_resettime(&telemetry_time);
                telemetry_resetlabeltime(&telemetry_labeltime);
        }

        // Normal operational step

        if (tv_timercmp(&app->next_timeout, &telemetry_time) > 0)
		app->next_timeout = telemetry_time;
	if (tv_timercmp(&app->next_timeout, &telemetry_labeltime) > 0)
		app->next_timeout = telemetry_labeltime;

        if (debug>1) printf("telemetry_prepoll()\n");

	return 0;
}

static void telemetry_datatx(void);
static void telemetry_labeltx(void);

int telemetry_postpoll(struct aprxpolls *app)
{
	if (debug>1) {
          printf("telemetry_postpoll()  telemetrytime=%ds  labeltime=%ds\n",
                 tv_timerdelta_millis(&tick, &telemetry_time)/1000,
                 tv_timerdelta_millis(&tick, &telemetry_labeltime)/1000);
        }
        if (tv_timercmp(&telemetry_time, &tick) <= 0) {
          tv_timeradd_seconds(&telemetry_time, &telemetry_time, telemetry_interval);
	  telemetry_datatx();
	}

        if (tv_timercmp(&telemetry_labeltime, &tick) <= 0) {
	  tv_timeradd_seconds(&telemetry_labeltime, &telemetry_labeltime, telemetry_labelinterval);
	  telemetry_labeltx();
	}

	return 0;
}

static void telemetry_datatx(void)
{
	int  i, j, k, t;
	char buf[200], *s;
	int  buflen;
	char beaconaddr[60];
	int  beaconaddrlen;
	long erlmax;
	float erlcapa;
	float f;


	if (debug)
	  printf("Telemetry Tx run; next one in %.2f minutes\n", (telemetry_interval/60.0));

	// Init these for RF transmission
	buf[0] = 0x03; // AX.25 Control
	buf[1] = 0xF0; // AX.25 PID

	++telemetry_seq;
	telemetry_seq %= 1000;
	for (i = 0; i < ErlangLinesCount; ++i) {
		struct erlangline *E = ErlangLines[i];
		struct aprx_interface *sourceaif = find_interface_by_callsign(E->name);
		if (!sourceaif || !interface_is_telemetrable(sourceaif))
		  continue;

		beaconaddrlen = sprintf(beaconaddr, "%s>%s,TCPIP*", E->name, tocall);
		// First two bytes of BUF are for AX.25 control+PID fields
		s = buf+2;
		s += sprintf(s, "T#%03d,", telemetry_seq);


		// Raw Rx Erlang - plotting scale factor: 1/200
		erlmax = 0;
#if (USE_ONE_MINUTE_DATA == 1)
		// Find busiest 1 minute
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
#else
		// Find busiest 10 minute
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
#endif
		f = (200.0 * erlcapa * erlmax);
		s += sprintf(s, "%.1f,", f);
		
		// Raw Tx Erlang - plotting scale factor: 1/200
		erlmax = 0;
#if (USE_ONE_MINUTE_DATA == 1)
		// Find busiest 1 minute
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
#else
		// Find busiest 10 minute
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
#endif
		f = (200.0 * erlcapa * erlmax);
		s += sprintf(s, "%.1f,", f);

		erlmax = 0;
#if (USE_ONE_MINUTE_DATA == 1)
		// Sum of 1 minute packet counts
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
#else
		// Sum of 10 minute packet counts
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
#endif
		f = erlmax / telemetry_timescaler;
		s += sprintf(s, "%.1f,", f);

		erlmax = 0;
#if (USE_ONE_MINUTE_DATA == 1)
		// Sum of 1 minute packet drop counts
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
#else
		// Sum of 10 minute packet drop counts
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
#endif
		f = erlmax / telemetry_timescaler;
		s += sprintf(s, "%.1f,", f);

		erlmax = 0;
#if (USE_ONE_MINUTE_DATA == 1)
		// Sum of 1 minute packet tx counts
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
#else
		// Sum of 10 minute packet tx counts
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
#endif
		f = erlmax / telemetry_timescaler;
		s += sprintf(s, "%.1f,", f);
		
		/* Tail filler */
		s += sprintf(s, "00000000");  // FIXME: flag telemetry?

                if (debug>2) printf("%s (to is=%d rf=%d) %s\n",
                                    beaconaddr, sourceaif->telemeter_to_is,
                                    sourceaif->telemeter_to_rf,
                                    buf+2);
		
		/* _NO_ ending CRLF, the APRSIS subsystem adds it. */
		
		/* Send those (net)beacons.. */
		buflen = s - buf;
#ifndef DISABLE_IGATE
                if (sourceaif->telemeter_to_is) {
                  aprsis_queue(beaconaddr, beaconaddrlen, 
                               qTYPE_LOCALGEN, aprsis_login,
                               buf+2, buflen-2);
                }
#endif
                rf_telemetry(sourceaif, beaconaddr, buf, buflen);

	}
	++telemetry_params;
}

// Telemetry Labels are transmitted separately
static void telemetry_labeltx()
{
	int  i;
	char buf[200], *s;
	int  buflen;
	char beaconaddr[60];
	int  beaconaddrlen;


	if (debug)
	  printf("Telemetry LabelTx run; next one in %.2f minutes\n", (telemetry_labelinterval/60.0));

	// Init these for RF transmission
	buf[0] = 0x03; // AX.25 Control
	buf[1] = 0xF0; // AX.25 PID

	++telemetry_seq;
	telemetry_seq %= 1000;
	for (i = 0; i < ErlangLinesCount; ++i) {
		struct erlangline *E = ErlangLines[i];
		struct aprx_interface *sourceaif = find_interface_by_callsign(E->name);
		if (!sourceaif || !interface_is_telemetrable(sourceaif))
		  continue;
		beaconaddrlen = sprintf(beaconaddr, "%s>%s,TCPIP*", E->name, tocall);
		// First two bytes of BUF are for AX.25 control+PID fields

		/* Send every 5h20m or thereabouts. */

                switch (telemetry_labelindex) {
                case 0:
		  s = buf+2 + sprintf(buf+2,
				      ":%-9s:PARM.Avg 10m,Avg 10m,RxPkts,IGateDropRx,TxPkts",
				      E->name);
                  break;
                case 1:
		  s = buf+2 + sprintf(buf+2,
				      ":%-9s:UNIT.Rx Erlang,Tx Erlang,count/10m,count/10m,count/10m",
				      E->name);
                  break;
                case 2:
		  
		  s = buf+2 + sprintf(buf+2,
				      ":%-9s:EQNS.0,0.005,0,0,0.005,0,0,1,0,0,1,0,0,1,0",
				      E->name);
                  break;
                default:
			   s = buf+2;
                  break;
                }

                if (debug>2) printf("%s (to is=%d rf=%d) %s\n",
                                    beaconaddr, sourceaif->telemeter_to_is,
                                    sourceaif->telemeter_to_rf,
                                    buf+2);

                buflen = s - buf;
#ifndef DISABLE_IGATE
                if (sourceaif->telemeter_to_is) {
                  aprsis_queue(beaconaddr, beaconaddrlen,
                               qTYPE_LOCALGEN, aprsis_login,
                               buf+2, buflen-2);
                }
#endif
                rf_telemetry(sourceaif, beaconaddr, buf, buflen);
	}
	++telemetry_params;

	// Switch label-index..
	++telemetry_labelindex;
	if (telemetry_labelindex > 2)
	  telemetry_labelindex = 0;
}

/*
 * Transmit telemetry to the RF interface that is being monitored.
 * Interface 'flags' contain controls on thist.
 */
static void rf_telemetry(const struct aprx_interface *sourceaif,
                         const char *beaconaddr,
			 const char *buf,
                         const const int buflen)
{
	int i;
	int t_idx;
	char *dest;

	if (rftelemetrycount == 0) return; // Nothing to do!
	if (sourceaif == NULL) return; // Huh? Unknown source..

        if (!sourceaif->telemeter_to_rf) return; // not wanted
	if (!interface_is_telemetrable(sourceaif)) return; // not possible


	// The beaconaddr comes in as:
	//    "interfacecall>APRXxx,TCPIP*"
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

int telemetry_config(struct configfile *cf)
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
			if (strcasecmp(param1,"$mycall") == 0)
				param1 = (char*)mycall;

			aif = find_interface_by_callsign(param1);
			if (aif != NULL && (!aif->tx_ok)) {
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

			if (strcasecmp(param1,"$mycall") == 0)
				param1 = (char*)mycall;

			source_aif = find_interface_by_callsign(param1);
			if (source_aif == NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Digipeater source '%s' not found\n",
				 cf->name, cf->linenum, param1);
			} else {
			  // Collect them all...
			  sources = realloc(sources, sizeof(void*)*(source_count+3));
			  sources[source_count++] = source_aif;
			  sources[source_count+1] = NULL;
			}
			if (debug>1)
			  printf(" .. source_aif = %p\n", source_aif);
		} else {
		  printf("%s:%d ERROR: Unknown <telemetry> block keyword '%s'\n",
			 cf->name, cf->linenum, name);
		}
	}

	if (has_fault) {
	  if (sources != NULL)
	    free(sources);
	  if (viapath != NULL)
	    free(viapath);
	  printf("ERROR: Failures on defining <telemetry> block parameters\n");
	  printf("       APRS RF-Telemetry will not be activated.\n");
	} else {
	  struct rftelemetry *newrf = calloc(1, sizeof(*newrf));
	  newrf->transmitter = aif;
	  newrf->viapath     = viapath;
	  newrf->sources     = sources;
	  newrf->source_count = source_count;
	  rftelemetry = realloc(rftelemetry, sizeof(void*)*(rftelemetrycount+2));
	  rftelemetry[rftelemetrycount++] = newrf;

	  if (debug) printf("Defined <telemetry> to transmitter %s\n", aif ? aif->callsign : "ALL");
	}
	return has_fault;
}
