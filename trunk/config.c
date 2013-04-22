/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2013                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

#ifndef DISABLE_IGATE
const char *aprsis_login;
#endif


char *config_SKIPSPACE(char *Y)
{
	assert(Y != NULL);
	while (*Y == ' ' || *Y == '\t')
		++Y;
	return Y;
}

#if 0
char *config_SKIPDIGIT(char *Y)
{
	assert(Y != NULL);
	while ('0' <= *Y && *Y <= '9')
		++Y;
	return Y;
}
#endif

// return 0 for failures, 1 for OK.
int validate_callsign_input(char *callsign, int strict)
{
	int i = strlen(callsign);
	char *p = callsign;
	char c = 0;
	int seen_minus = 0;
	int ssid = 0;

	for ( ; *p ; ++p ) {
		c = *p;
		if ('a' <= c && c <= 'z') {
		  // Presuming ASCII
		  c -= ('a'-'A');
		  *p = c; // Side-effect: translates the callsign to uppercase
		}
		if (!seen_minus && c == '-') {
		  if (p == callsign || p[1] == 0)
		    return 0; // Hyphen is at beginning or at end!
		  if ((p - callsign) > 6)
		    return 0; // Hyphen too far!  Max 6 characters preceding it.
		  seen_minus = 1;
		  continue;
		} else if (seen_minus && c == '-') {
		  return 0; // Seen a hyphen again!
		}

		// The "SSID" value can be alphanumeric here!

		if (!seen_minus) {
		  // Callsign prefix
		  if (('A' <= c && c <= 'Z') || ('0' <= c && c <= '9')) {
		    // Valid character!
		  } else {
		    return 0; // Invalid characters in callsign part
		  }
		} else {
		  // SSID tail
		  if (strict) {
		    if ('0' <= c && c <= '9') {
		      // Valid character!
		      ssid = ssid * 10 + c - '0';
		      if (ssid > 15) { // SSID value range: 0 .. 15
			return 0;
		      }
		    } else {
		      return 0; // Invalid characters in SSID part
		    }
		  } else { // non-strict
		    if (('A' <= c && c <= 'Z') || ('0' <= c && c <= '9')) {
		      // Valid character!
		    } else {
		      return 0; // Invalid characters in SSID part
		    }
		  }
		}
	}
	if (i > 3 && (callsign[i - 1] == '0' && callsign[i - 2] == '-')) {
		callsign[i - 2] = 0;
		/* If tailed with "-0" SSID, chop it off.. */
	}
	return 1;
}

/* SKIPTEXT:
 *
 *  Detect "/' -> scan until matching double quote
 *    Process \-escapes on string: \xFD, \n, \", \'
 *  Detect non-eol, non-space(tab): scan until eol, or white-space
 *    No \-escapes
 *
 *  Will thus stop when found non-quoted space/tab, or
 *  end of line/string.
 */

char *config_SKIPTEXT(char *Y, int *lenp)
{
	char *O;
	char endc;
	int  len;

	assert(Y != NULL);

	O = Y;
        endc = *Y;
        len = 0;

	if (*Y == '"' || *Y == '\'') {
		++Y;
		while (*Y && *Y != endc) {
			if (*Y == '\\') {
				/* backslash escape.. */
				++Y;
				if (*Y == 'n') {
					*O++ = '\n';
					++len;
				} else if (*Y == 'r') {
					*O++ = '\r';
					++len;
				} else if (*Y == '"') {
					*O++ = '"';
					++len;
				} else if (*Y == '\'') {
					*O++ = '\'';
				} else if (*Y == '\\') {
					*O++ = '\\';
				} else if (*Y == 'x') {
					/* Hex encoded char */
					int i;
					char hx[3];
					if (*Y)
						++Y;
					hx[0] = *Y;
					if (*Y)
						++Y;
					hx[1] = *Y;
					hx[2] = 0;
					i = (int) strtol(hx, NULL, 16);
					*O++ = i;
					++len;
				}
			} else {
				*O++ = *Y;
				++len;
			}
			if (*Y != 0)
				++Y;
		}
		if (*Y == endc)
			++Y;
		*O = 0;		/* String end */
		/* STOP at the tail-end " */
	} else {
		while (*Y && *Y != ' ' && *Y != '\t') {
			++Y;
			++len;
		}
		/* Stop at white-space or end */
		if (*Y)
			*Y++ = 0;
	}

	if (lenp != NULL)
	  *lenp = len;
	return Y;
}

void config_STRLOWER(char *s)
{
	int c;
	assert(s != NULL);
	for (; *s; ++s) {
		c = *s;
		if ('A' <= c && c <= 'Z') {
			*s = c + ('a' - 'A');
		}
	}
}

void config_STRUPPER(char *s)
{
	int c;
	assert(s != NULL);
	for (; *s; ++s) {
		c = *s;
		if ('a' <= c && c <= 'z') {
			*s = c + ('A' - 'a');
		}
	}
}

static int logging_config(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int has_fault = 0;

	while (readconfigline(cf) != NULL) {
		if (configline_is_comment(cf))
			continue;	/* Comment line, or empty line */

		str = cf->buf;
		str = config_SKIPSPACE(str); // arbitrary indention
		name = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);
		config_STRLOWER(name);
	
		param1 = str;
	
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);
	
		if (strcmp(name, "</logging>") == 0)
			break;
	
		if (strcmp(name, "aprxlog") == 0) {
			if (debug)
				printf("%s:%d: INFO: APRXLOG = '%s' '%s'\n",
				       cf->name, cf->linenum, param1, str);
	
			aprxlogfile = strdup(param1);
	
		} else if (strcmp(name, "dprslog") == 0) {
			if (debug)
				printf("%s:%d: INFO: DPRSLOG = '%s' '%s'\n",
				       cf->name, cf->linenum, param1, str);
	
			dprslogfile = strdup(param1);
	
		} else if (strcmp(name, "rflog") == 0) {
			if (debug)
				printf("%s:%d: INFO: RFLOG = '%s' '%s'\n",
				       cf->name, cf->linenum, param1, str);
	
			rflogfile = strdup(param1);
	
		} else if (strcmp(name, "pidfile") == 0) {
			if (debug)
				printf("%s:%d: INFO: PIDFILE = '%s' '%s'\n",
				       cf->name, cf->linenum, param1, str);
	
			pidfile = strdup(param1);
	
		} else if (strcmp(name, "erlangfile") == 0) {
			if (debug)
				printf("%s:%d: INFO: ERLANGFILE = '%s' '%s'\n",
				       cf->name, cf->linenum, param1, str);
	
			erlang_backingstore = strdup(param1);
	
		} else if (strcmp(name, "erlang-loglevel") == 0) {
			if (debug)
				printf("%s:%d: INFO: ERLANG-LOGLEVEL = '%s' '%s'\n",
				       cf->name, cf->linenum, param1, str);
			erlang_init(param1);
	
		} else if (strcmp(name, "erlanglog") == 0) {
			if (debug)
				printf("%s:%d: INFO: ERLANGLOG = '%s'\n",
				       cf->name, cf->linenum, param1);
	
			erlanglogfile = strdup(param1);
	
		} else if (strcmp(name, "erlang-log1min") == 0) {
			if (debug)
				printf("%s:%d: INFO: ERLANG-LOG1MIN\n",
				       cf->name, cf->linenum);
	
			erlanglog1min = 1;
	
		} else {
			printf("%s:%d: ERROR: Unknown <logging> keyword: '%s' '%s'\n",
			       cf->name, cf->linenum, name, param1);
			has_fault = 1;
		}
	}
	return has_fault;
}

static int cfgparam(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;

	str = config_SKIPSPACE(str); // arbitrary indention
	name = str;
	str = config_SKIPTEXT(str, NULL);
	str = config_SKIPSPACE(str);
	config_STRLOWER(name);

	param1 = str;

	str = config_SKIPTEXT(str, NULL);
	str = config_SKIPSPACE(str);

#ifndef DISABLE_IGATE
	if (strcmp(name, "<aprsis>") == 0) {
	  return aprsis_config(cf);
	}
#endif
	if (strcmp(name, "<interface>") == 0) {
	  return interface_config(cf);
	}
	if (strcmp(name, "<telemetry>") == 0) {
	  return telemetry_config(cf);
	}
	if (strcmp(name, "<digipeater>") == 0) {
	  return digipeater_config(cf);
	}
	if (strcmp(name, "<beacon>") == 0) {
	  return beacon_config(cf);
	}
	if (strcmp(name, "<logging>") == 0) {
	  return logging_config(cf);
	}


	if (strcmp(name, "mycall") == 0) {
		config_STRUPPER(param1);
		// Store these always, it helps with latter error diagnostics
		mycall       = strdup(param1);
#ifndef DISABLE_IGATE
		aprsis_login = mycall;
#endif
		if (validate_callsign_input(param1,1)) {
		  if (debug)
		    printf("%s:%d: MYCALL = '%s' '%s'\n",
			   cf->name, cf->linenum, mycall, str);
		} else {
		  if (validate_callsign_input(param1,0)) {
		    printf("%s:%d: MYCALL = '%s'  value is OK for APRSIS login, and Rx-IGate, but not valid AX.25 node callsign.\n",
			   cf->name, cf->linenum, param1);

		  } else {
		    // but sometimes the parser yields an error!
		    printf("%s:%d: MYCALL = '%s'  value is not valid AX.25 node callsign, nor valid for APRSIS login.\n",
			   cf->name, cf->linenum, param1);
		    return 1;
		  }
		}

        } else if (strcmp(name, "myloc") == 0) {
        	// lat xx lon yy
		char *latp;
                char *lonp;
                float lat, lng;
                int i, la, lo;
                char lac, loc;

                const char *const errmsg = "%s:%d: myloc parameters wrong, expected format:  'myloc' 'lat' 'ddmm.mmN' 'lon' 'dddmm.mmE'\n";

                if (strcmp(param1, "lat") != 0) {
                  printf(errmsg, cf->name, cf->linenum);
                  printf(" .. 'lat' missing, got: '%s'\n", param1);
                  return 1;
                }

                latp = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);

                param1 = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);

                if (strcmp(param1, "lon") != 0) {
                  printf(errmsg, cf->name, cf->linenum);
                  printf(" .. 'lon' missing, got: '%s'\n", param1);
                  return 1;
                }

                lonp = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);

                if (validate_degmin_input(latp, 90)) {
                  printf(errmsg, cf->name, cf->linenum);
                  printf(" got lat: '%s'\n", latp);
                  return 1;
                }
                if (validate_degmin_input(lonp, 180)) {
                  printf(errmsg, cf->name, cf->linenum);
                  printf(" got lon: '%s'\n", lonp);
                  return 1;
                }

                i = sscanf(latp, "%2d%5f%c,", &la, &lat, &lac);
                if (i != 3) {
                  printf(errmsg, cf->name, cf->linenum);
                  printf(" got parse-field-count: %d on '%s'\n", i, latp);
                  return 1; // parse failure
                }
                i = sscanf(lonp, "%3d%5f%c,", &lo, &lng, &loc);
                if (i != 3) {
                  printf(errmsg, cf->name, cf->linenum);
                  printf(" got parse-field-count: %d on '%s'\n", i, lonp);
                  return 1; // parse failure
                }

                if (lac != 'N' && lac != 'S' && lac != 'n' && lac != 's') {
                  printf(errmsg, cf->name, cf->linenum);
                  printf(" .. lat expected N/S tail, got: '%c'\n", lac);
                  return 1; // bad indicator value
                }
                if (loc != 'E' && loc != 'W' && loc != 'e' && loc != 'w') {
                  printf(errmsg, cf->name, cf->linenum);
                  printf(" .. lon expected E/W tail, got: '%c'\n", loc);
                  return 1; // bad indicator value
                }

                myloc_latstr = strdup(latp);
                myloc_lonstr = strdup(lonp);

                myloc_lat = filter_lat2rad((float)la + lat/60.0);
                myloc_lon = filter_lon2rad((float)lo + lng/60.0);
	
                if (lac == 'S' || lac == 's')
                  myloc_lat = -myloc_lat;
                if (loc == 'W' || loc == 'w')
                  myloc_lon = -myloc_lon;

                if (debug)
                  printf("%s:%d: MYLOC LAT %8.5f degrees  LON %8.5f degrees\n",
                         cf->name, cf->linenum, myloc_lat, myloc_lon);


#ifndef DISABLE_IGATE
	} else if (strcmp(name, "aprsis-login") == 0) {

		printf("%s:%d WARNING: Old-style top-level 'aprsis-login' definition, it should be inside <aprsis> group tags.\n",
		       cf->name, cf->linenum);

		config_STRUPPER(param1);
		aprsis_login = strdup(param1);
		if (validate_callsign_input(param1,0)) {
		  if (debug)
		    printf("%s:%d: APRSIS-LOGIN = '%s' '%s'\n",
			   cf->name, cf->linenum, aprsis_login, str);
		} else {
		    printf("%s:%d: APRSIS-LOGIN = '%s' value is not valid AX25-like node'\n",
			   cf->name, cf->linenum, aprsis_login);
		    return 1;
		}

	} else if (strcmp(name, "aprsis-server") == 0) {

		printf("%s:%d WARNING: Old-style top-level 'aprsis-server' definition, it should be inside <aprsis> group tags.\n",
		       cf->name, cf->linenum);

		if (debug)
			printf("%s:%d: APRSIS-SERVER = '%s':'%s'\n",
			       cf->name, cf->linenum, param1, str);

		return aprsis_add_server(param1, str);

	} else if (strcmp(name, "aprsis-heartbeat-timeout") == 0) {
		int i = atoi(param1);
		if (i < 0)	/* param failure ? */
			i = 0;	/* no timeout */

		printf("%s:%d WARNING: Old-style top-level 'aprsis-heartbeat-timeout' definition, it should be inside <aprsis> group tags.\n",
		       cf->name, cf->linenum);

		if (debug)
			printf("%s:%d: APRSIS-HEARTBEAT-TIMEOUT = '%d' '%s'\n",
			       cf->name, cf->linenum, i, str);

		return aprsis_set_heartbeat_timeout(i);


	} else if (strcmp(name, "aprsis-filter") == 0) {

		printf("%s:%d WARNING: Old-style top-level 'aprsis-filter' definition, it should be inside <aprsis> group tags.\n",
		       cf->name, cf->linenum);

		return aprsis_set_filter(param1);
#endif

#ifdef PF_AX25	/* PF_AX25 exists -- highly likely a Linux system ! */
	} else if (strcmp(name, "ax25-rxport") == 0) {

		printf("%s:%d WARNING: Old-style top-level 'ax25-rxport' definition.  See <interface> groups, 'ax25-device' definitions.\n",
		       cf->name, cf->linenum);

		if (debug)
			printf("%s:%d: AX25-RXPORT '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

		return (netax25_addrxport(param1, NULL) == NULL);
#endif
	} else if (strcmp(name, "radio") == 0) {

		printf("%s:%d WARNING: Old-style top-level 'radio' definition.  See <interface> groups, 'serial-device' or 'tcp-device' definitions.\n",
		       cf->name, cf->linenum);

		if (debug)
			printf("%s:%d: RADIO = %s %s..\n",
			       cf->name, cf->linenum, param1, str);
		return (ttyreader_serialcfg(cf, param1, str) == NULL);

	} else if (strcmp(name, "ax25-device") == 0) {
		printf("%s:%d ERROR: The 'ax25-device' entry must be inside an <interface> group tag.\n",
		       cf->name, cf->linenum);
		return 1;

	} else if (strcmp(name, "serial-device") == 0) {
		printf("%s:%d ERROR: The 'serial-device' entry must be inside an <interface> group tag.\n",
		       cf->name, cf->linenum);
		return 1;

	} else if (strcmp(name, "tcp-device") == 0) {
		printf("%s:%d ERROR: The 'tcp-device' entry must be inside an <interface> group tag.\n",
		       cf->name, cf->linenum);
		return 1;

	} else if (strcmp(name, "beacon") == 0) {
		printf("%s:%d ERROR: The 'beacon' entry must be inside a <beacon> group tag.\n",
		       cf->name, cf->linenum);
		return 1;

	} else {
		printf("%s:%d: ERROR: Unknown config keyword: '%s' '%s'\n",
		       cf->name, cf->linenum, name, param1);
		printf("%s:%d: Perhaps this is due to lack of some surrounding <group> tag ?\n",
		       cf->name, cf->linenum);
		return 1;
	}
	return 0;
}


const char* scan_int(const char *p, int len, int *val, int *seen_space)
{
	int i;
	char c;
	*val = 0;
	for (i = 0; i < len; ++i, ++p) {
		c = *p;
		if (('0' <= c && c <= '9') && !(*seen_space)) {
			*val = (*val) * 10 + (c - '0');
		} else if (c == ' ') {
			*val = (*val) * 10;
			*seen_space = 1;
		} else {
			return NULL;
		}
	}
	return p;
}

int validate_degmin_input(const char *s, int maxdeg)
{
	int deg;
	int m1, m2;
	char c;
	const char *t;
	int seen_space = 0;
	if (maxdeg > 90) {
		t = scan_int(s, 3, &deg, &seen_space);
		if (t != s+3) return 1; // scan failure
		if (deg > 179) return 1; // too large value
		s = t;
		t = scan_int(s, 2, &m1, &seen_space);
		if (t != s+2) return 1;
		if (m1 > 59) return 1;
		s = t;
		c = *s;
		if (!seen_space && c == '.') {
			// OK
		} else if (!seen_space && c == ' ') {
			seen_space = 1;
		} else {
			return 1; // Bad char..
		}
		++s;
		t = scan_int(s, 2, &m2, &seen_space);
		if (t != s+2) return 1;
		s = t;
		c = *s;
		if (c != 'E' && c != 'e' && c != 'W' && c != 'w') return 1;
	} else {
		t = scan_int(s, 2, &deg, &seen_space);
		if (t != s+2) return 1; // scan failure
		if (deg > 89) return 1; // too large value
		s = t;
		t = scan_int(s, 2, &m1, &seen_space);
		if (t != s+2) return 1;
		if (m1 > 59) return 1;
		s = t;
		c = *s;
		if (!seen_space && c == '.') {
			// OK
		} else if (!seen_space && c == ' ') {
			seen_space = 1;
		} else {
			return 1; // Bad char..
		}
		++s;
		t = scan_int(s, 2, &m2, &seen_space);
		if (t != s+2) return 1;
		s = t;
		c = *s;
		if (c != 'N' && c != 'n' && c != 'S' && c != 's') return 1;
	}
	return 0;		/* zero for OK */
}


/*
 *  This interval parser is originally from ZMailer MTA.
 *  Slightly expanded to permit white-spaces inside the string.
 *  (c) Matti Aarnio, Rayan Zachariassen..
 */

static int parse_interval(const char *string, const char **restp)
{
	int	intvl = 0;
	int	val;
	int	c;

	for (; *string; ++string) {

	  val = 0;
	  c = *string;
	  while ('0' <= c && c <= '9') {
	    val = val * 10 + (c - '0');
	    c = *++string;
	  }

	  switch (c) {
	  case 'd':		/* days */
	  case 'D':		/* days */
	    val *= (24*60*60);
            break;
	  case 'h':		/* hours */
	  case 'H':		/* hours */
	    val *= 60*60;
            break;
	  case 'm':		/* minutes */
	  case 'M':		/* minutes */
	    val *= 60;
            break;
	  case 's':		/* seconds */
	  case 'S':		/* seconds */
	    /* val *= 1; */
	  case '\t':            /* just whitespace */
	  case ' ':             /* just whitespace */
	    break;
	  default: /* Not of: "dhms" - maybe string end, maybe junk ? */
	    if (restp) *restp = string;
	    return intvl + val;
	  }
	  intvl += val;
	}

	if (restp) *restp = string;

	return intvl;
}

// Return 0 on OK, != 0 on error
int config_parse_interval(const char *par, int *resultp)
{
	const char *rest = NULL;
	int ret = parse_interval(par, &rest);

	if (*rest != 0) return 1; // Did not consume whole input string
	*resultp = ret;
	return 0;
}

// Return 0 on OK, != 0 on error
int config_parse_boolean(const char *par, int *resultp)
{
	if (strcasecmp(par, "true") == 0 ||
	    strcmp(par, "1") == 0 ||
	    strcasecmp(par, "yes") == 0 ||
	    strcasecmp(par, "on") == 0 ||
	    strcasecmp(par, "y") == 0) {

		*resultp = 1;
		return 1;

	} else if (strcasecmp(par, "false") == 0 ||
		   strcmp(par, "0") == 0 ||
		   strcasecmp(par, "no") == 0 ||
		   strcasecmp(par, "off") == 0 ||
		   strcasecmp(par, "n") == 0) {

		*resultp = 0;
		return 1;

	} else {
		return 0;
	}
}


void *readconfigline(struct configfile *cf)
{
	char *bufp = cf->buf;
	int buflen = sizeof(cf->buf);
	//int llen;
	cf->linenum = cf->linenum_i;
	for (;;) {
	  char *p = fgets(bufp, buflen, cf->fp);
	  bufp[buflen - 1] = 0;	/* Trunc, just in case.. */
	  if (p == NULL) {
	    if (bufp == cf->buf)
	      return NULL; // EOF!
	    return cf->buf; // Got EOF, but got also data before it!
	  }
	  cf->linenum_i += 1;
	  // Line ending LF ?
	  p = strchr(bufp, '\n');
	  if (p != NULL) {
	    *p-- = 0;
	    // Possible preceding CR ?
	    if (*p == '\r')
	      *p-- = 0;
	    // Line ending whitespaces ?
	    while (p > bufp && (*p == ' '||*p == '\t'))
	      *p-- = 0;
	    //llen = p - bufp;
	  }
	  if (p == NULL) {
	    p = bufp + strlen(bufp);
	  }
	  if (*p == '\\') {
	    bufp = p;
	    buflen = sizeof(cf->buf) - (p - cf->buf) -1;
	    continue;
	  } else {
	    // Not lone \ at line end.  Not a line with continuation line..
	    break;
	  }
	}

	if (debug > 2)
	  printf("Config line: '%s'\n",cf->buf);

	return cf->buf;
}

int configline_is_comment(struct configfile *cf)
{
	const char *buf = cf->buf;
	const int buflen = sizeof(cf->buf);
	char c = 0;
	int i;

	for (i = 0; buf[i] != 0 && i < buflen; ++i) {
		c = buf[i];
		if (c == ' ' || c == '\t')
			continue;
		/* Anything else, stop scanning */
		break;
	}
	if (c == '#' || c == '\n' || c == '\r' || c == 0)
		return 1;

	return 0;
}

int readconfig(const char *name)
{
	struct configfile cf;
	int has_fault = 0;
	int i;

	cf.linenum_i = 1;
	cf.linenum   = 1;
	cf.name      = name;

	if ((cf.fp = fopen(name, "r")) == NULL) {
		int e = errno;
		printf("ERROR: Can not open named config file: '%s' -> %d %s\n",
		       name, e, strerror(e)); 
		return 1;
	}

	while (readconfigline(&cf) != NULL) {
		if (configline_is_comment(&cf))
			continue;	/* Comment line, or empty line */

		i = cfgparam(&cf);
		if (i) has_fault = 1;
	}
	fclose(cf.fp);

	return has_fault;
}
