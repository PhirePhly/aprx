/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"


const char *aprsis_login;


char *config_SKIPSPACE(char *Y)
{
	if (!Y)
		return Y;

	while (*Y == ' ' || *Y == '\t')
		++Y;

	return Y;
}

#if 0
char *config_SKIPDIGIT(char *Y)
{
	if (!Y)
		return Y;

	while ('0' <= *Y && *Y <= '9')
		++Y;

	return Y;
}
#endif

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
		    return 1; // Hyphen is at beginning or at end!
		  if ((p - callsign) > 6)
		    return 1; // Hyphen too far!  Max 6 characters preceding it.
		  seen_minus = 1;
		  continue;
		} else if (seen_minus && c == '-') {
		  return 1; // Seen a hyphen again!
		}

		// The "SSID" value can be alphanumeric here!

		if (!seen_minus) {
		  // Callsign prefix
		  if (('A' <= c && c <= 'Z') || ('0' <= c && c <= '9')) {
		    // Valid character!
		  } else {
		    return 1; // Invalid characters in callsign part
		  }
		} else {
		  // SSID tail
		  if (strict) {
		    if ('0' <= c && c <= '9') {
		      // Valid character!
		      ssid = ssid * 10 + c - '0';
		      if (ssid > 15) { // SSID value range: 0 .. 15
			return 1;
		      }
		    } else {
		      return 1; // Invalid characters in SSID part
		    }
		  } else { // non-strict
		    if (('A' <= c && c <= 'Z') || ('0' <= c && c <= '9')) {
		      // Valid character!
		    } else {
		      return 1; // Invalid characters in SSID part
		    }
		  }
		}
	}

	if (i > 3 && (callsign[i - 1] == '0' && callsign[i - 2] == '-')) {
		callsign[i - 2] = 0;
		/* If tailed with "-0" SSID, chop it off.. */
	}

	return 0;
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
	char *O = Y;
	char endc = *Y;
	int  len = 0;
	if (!Y)
		return Y;

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
	for (; *s; ++s) {
		c = *s;
		if ('a' <= c && c <= 'z') {
			*s = c + ('A' - 'a');
		}
	}
}

static void cfgparam(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;

	name = str;
	str = config_SKIPTEXT(str, NULL);
	str = config_SKIPSPACE(str);
	config_STRLOWER(name);

	param1 = str;

	str = config_SKIPTEXT(str, NULL);
	str = config_SKIPSPACE(str);

	if (strcmp(name, "<aprsis>") == 0) {
	  aprsis_config(cf);
	  return;
	}
	if (strcmp(name, "<interface>") == 0) {
	  interface_config(cf);
	  return;
	}
	if (strcmp(name, "<digipeater>") == 0) {
	  digipeater_config(cf);
	  return;
	}
	if (strcmp(name, "<netbeacon>") == 0) {
	  rfbeacon_config(cf,1);
	  return;
	}
	if (strcmp(name, "<rfbeacon>") == 0) {
	  rfbeacon_config(cf,0);
	  return;
	}
	if (strcmp(name, "<logging>") == 0) {
	  // logging_config(cf);
	  return;
	}
	if (strcmp(name, "</logging>") == 0) // temporary helper
	  return;

	else {
	}

	if (strcmp(name, "aprsis-login") == 0) {
		config_STRUPPER(param1);
		validate_callsign_input(param1,0);
		aprsis_login = strdup(param1);
		if (debug)
			printf("%s:%d: APRSIS-LOGIN = '%s' '%s'\n",
			       cf->name, cf->linenum, aprsis_login, str);

	} else if (strcmp(name, "aprsis-server") == 0) {
		aprsis_add_server(param1, str);

		if (debug)
			printf("%s:%d: APRSIS-SERVER = '%s':'%s'\n",
			       cf->name, cf->linenum, param1, str);

	} else if (strcmp(name, "aprsis-heartbeat-timeout") == 0) {
		int i = atoi(param1);
		if (i < 0)	/* param failure ? */
			i = 0;	/* no timeout */
		aprsis_set_heartbeat_timeout(i);

		if (debug)
			printf("%s:%d: APRSIS-HEARTBEAT-TIMEOUT = '%d' '%s'\n",
			       cf->name, cf->linenum, i, str);

	} else if (strcmp(name, "aprsis-filter") == 0) {
		aprsis_set_filter(param1);

		if (debug)
			printf("%s:%d: APRSIS-FILTER = '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

	} else if (strcmp(name, "enable-tx-igate") == 0) {
		
		enable_tx_igate(param1,str);

		if (debug)
			printf("%s:%d: ENABLE-TX-IGATE\n",
			       cf->name, cf->linenum);

	} else if (strcmp(name, "ax25-filter") == 0) {
		if (debug)
			printf("%s:%d: AX25-REJECT-FILTER '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

		ax25_filter_add(param1, str);

	} else if (strcmp(name, "ax25-reject-filter") == 0) {
		if (debug)
			printf("%s:%d: AX25-REJECT-FILTER '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

		ax25_filter_add(param1, str);

	} else if (strcmp(name, "ax25-rxport") == 0) {
		if (debug)
			printf("%s:%d: AX25-RXPORT '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

		netax25_addrxport(param1, str, NULL);

	} else if (strcmp(name, "netbeacon") == 0) {
		netbeacon_set(param1, str);

		if (debug)
			printf("%s:%d: NETBEACON = '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

	} else if (strcmp(name, "aprxlog") == 0) {
		if (debug)
			printf("%s:%d: APRXLOG = '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

		aprxlogfile = strdup(param1);

	} else if (strcmp(name, "rflog") == 0) {
		if (debug)
			printf("%s:%d: RFLOG = '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

		rflogfile = strdup(param1);

	} else if (strcmp(name, "pidfile") == 0) {
		if (debug)
			printf("%s:%d: PIDFILE = '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

		pidfile = strdup(param1);

	} else if (strcmp(name, "erlangfile") == 0) {
		if (debug)
			printf("%s:%d: ERLANGFILE = '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);

		erlang_backingstore = strdup(param1);

	} else if (strcmp(name, "erlang-loglevel") == 0) {
		if (debug)
			printf("%s:%d: ERLANG-LOGLEVEL = '%s' '%s'\n",
			       cf->name, cf->linenum, param1, str);
		erlang_init(param1);

	} else if (strcmp(name, "erlanglog") == 0) {
		if (debug)
			printf("%s:%d: ERLANGLOG = '%s'\n",
			       cf->name, cf->linenum, param1);

		erlanglogfile = strdup(param1);

	} else if (strcmp(name, "erlang-log1min") == 0) {
		if (debug)
			printf("%s:%d: ERLANG-LOG1MIN\n",
			       cf->name, cf->linenum);

		erlanglog1min = 1;

	} else if (strcmp(name, "radio") == 0) {
		if (debug)
			printf("%s:%d: RADIO = %s %s..\n",
			       cf->name, cf->linenum, param1, str);
		ttyreader_serialcfg(cf, param1, str);

	} else {
		printf("%s:%d: Unknown config keyword: '%s' '%s'\n",
		       cf->name, cf->linenum, name, param1);
	}
}

int config_parse_interval(const char *par, int *resultp)
{
	*resultp = atoi(par);
	return 0;
}

int config_parse_boolean(const char *par, int *resultp)
{
	if (strcasecmp(par, "true") == 0 ||
	    strcmp(par, "1") == 0 ||
	    strcasecmp(par, "yes") == 0 ||
	    strcasecmp(par, "y") == 0) {

		*resultp = 1;
		return 1;

	} else if (strcasecmp(par, "false") == 0 ||
		   strcmp(par, "0") == 0 ||
		   strcasecmp(par, "no") == 0 ||
		   strcasecmp(par, "n") == 0) {

		*resultp = 0;
		return 1;

	} else {
		return 0;
	}
}


void *readconfigline(struct configfile *cf)
{
	char *p = fgets(cf->buf, sizeof(cf->buf), cf->fp);
	cf->buf[sizeof(cf->buf) - 1] = 0;	/* Trunc, just in case.. */
	if (p != NULL)
		cf->linenum += 1;
	return p;
}

int configline_is_comment(struct configfile *cf)
{
	const char *buf = cf->buf;
	const int buflen = sizeof(cf->buf);
	char c = 0, *s;
	int i;

	for (i = 0; buf[i] != 0 && i < buflen; ++i) {
		c = buf[i];
		if (c == ' ' || c == '\t')
			continue;
		/* Anything else, stop scanning */
		break;
	}
	if (c == '#' || c == '\n' || c == '\r')
		return 1;

	s = strchr(buf, '\n');	/* The trailing NL chopper ... */
	if (s)
		*s = 0;
	s = strchr(buf, '\r');	/* The trailing CR chopper ... */
	if (s)
		*s = 0;

	return 0;
}

void readconfig(const char *name)
{
	struct configfile cf;

	cf.linenum = 0;
	cf.name    = name;

	if ((cf.fp = fopen(name, "r")) == NULL)
		return;

	while (readconfigline(&cf) != NULL) {
		if (configline_is_comment(&cf))
			continue;	/* Comment line, or empty line */

		cfgparam(&cf);
	}
	fclose(cf.fp);
}
