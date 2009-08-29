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


const char *mycall;


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

int validate_callsign_input(char *callsign)
{
	int i = strlen(callsign);

	/* If longer than 9 chars, break at 9 ... */
	callsign[i > 9 ? 9 : i] = 0;

	if (i > 9)		/* .. and complain! */
		return 1;

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

char *config_SKIPTEXT(char *Y)
{
	char *O = Y;
	char endc = *Y;
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
				} else if (*Y == 'r') {
					*O++ = '\r';
				} else if (*Y == '"') {
					*O++ = '"';
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
				}
			} else
				*O++ = *Y;
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
		}
		/* Stop at white-space or end */
		if (*Y)
			*Y++ = 0;
	}

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

static void cfgparam(char *str, int size, const char *cfgfilename,
		     int linenum)
{
	char *name, *param1;

	name = strchr(str, '\n');	/* The trailing newline chopper ... */
	if (name)
		*name = 0;
	name = strchr(str, '\r');	/* The trailing cr chopper ... */
	if (name)
		*name = 0;

	name = str;
	str = config_SKIPTEXT(str);
	str = config_SKIPSPACE(str);
	config_STRLOWER(name);

	param1 = str;

	str = config_SKIPTEXT(str);
	str = config_SKIPSPACE(str);

	if (strcmp(name, "mycall") == 0) {
		config_STRUPPER(param1);
		validate_callsign_input(param1);
		mycall = strdup(param1);
		if (debug)
			printf("%s:%d: MYCALL = '%s' '%s'\n",
			       cfgfilename, linenum, mycall, str);

	} else if (strcmp(name, "aprsis-server") == 0) {
		aprsis_add_server(param1, str);

		if (debug)
			printf("%s:%d: APRSIS-SERVER = '%s':'%s'\n",
			       cfgfilename, linenum, param1, str);

	} else if (strcmp(name, "aprsis-heartbeat-timeout") == 0) {
		int i = atoi(param1);
		if (i < 0)	/* param failure ? */
			i = 0;	/* no timeout */
		aprsis_set_heartbeat_timeout(i);

		if (debug)
			printf("%s:%d: APRSIS-HEARTBEAT-TIMEOUT = '%d' '%s'\n", cfgfilename, linenum, i, str);

	} else if (strcmp(name, "aprsis-filter") == 0) {
		aprsis_set_filter(param1);

		if (debug)
			printf("%s:%d: APRSIS-FILTER = '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

	} else if (strcmp(name, "enable-tx-igate") == 0) {
		
		enable_tx_igate(param1,str);

		if (debug)
			printf("%s:%d: ENABLE-TX-IGATE\n",
			       cfgfilename, linenum);

	} else if (strcmp(name, "aprsis-mycall") == 0) {
		/* Do not use! - multi APRSIS connection thing,
		   which is also "do not use" item.. */
		aprsis_set_mycall(param1);

		if (debug)
			printf("%s:%d: APRSIS-MYCALL = '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

	} else if (strcmp(name, "ax25-filter") == 0) {
		if (debug)
			printf("%s:%d: AX25-REJECT-FILTER '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

		ax25_filter_add(param1, str);

	} else if (strcmp(name, "ax25-reject-filter") == 0) {
		if (debug)
			printf("%s:%d: AX25-REJECT-FILTER '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

		ax25_filter_add(param1, str);

	} else if (strcmp(name, "ax25-rxport") == 0) {
		if (debug)
			printf("%s:%d: AX25-RXPORT '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

		netax25_addport(param1, str);

	} else if (strcmp(name, "netbeacon") == 0) {
		netbeacon_set(param1, str);

		if (debug)
			printf("%s:%d: NETBEACON = '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

	} else if (strcmp(name, "aprxlog") == 0) {
		if (debug)
			printf("%s:%d: APRXLOG = '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

		aprxlogfile = strdup(param1);

	} else if (strcmp(name, "rflog") == 0) {
		if (debug)
			printf("%s:%d: RFLOG = '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

		rflogfile = strdup(param1);

	} else if (strcmp(name, "pidfile") == 0) {
		if (debug)
			printf("%s:%d: PIDFILE = '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

		pidfile = strdup(param1);

	} else if (strcmp(name, "erlangfile") == 0) {
		if (debug)
			printf("%s:%d: ERLANGFILE = '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);

		erlang_backingstore = strdup(param1);

	} else if (strcmp(name, "erlang-loglevel") == 0) {
		if (debug)
			printf("%s:%d: ERLANG-LOGLEVEL = '%s' '%s'\n",
			       cfgfilename, linenum, param1, str);
		erlang_init(param1);

	} else if (strcmp(name, "erlanglog") == 0) {
		if (debug)
			printf("%s:%d: ERLANGLOG = '%s'\n",
			       cfgfilename, linenum, param1);

		erlanglogfile = strdup(param1);

	} else if (strcmp(name, "erlang-log1min") == 0) {
		if (debug)
			printf("%s:%d: ERLANG-LOG1MIN\n",
			       cfgfilename, linenum);

		erlanglog1min = 1;

	} else if (strcmp(name, "serialport") == 0) {
		if (debug)
			printf("%s:%d: SERIALPORT = %s %s..\n",
			       cfgfilename, linenum, param1, str);
		ttyreader_serialcfg(param1, str);

	} else if (strcmp(name, "radio") == 0) {
		if (debug)
			printf("%s:%d: RADIO = %s %s..\n",
			       cfgfilename, linenum, param1, str);
		ttyreader_serialcfg(param1, str);

	} else {
		printf("%s:%d: Unknown config keyword: '%s' '%s'\n",
		       cfgfilename, linenum, param1, str);
	}
}


void readconfig(const char *name)
{
	FILE *fp;
	unsigned char c = 0;
	char buf[1024];
	int linenum = 0, i;



	if ((fp = fopen(name, "r")) == NULL)
		return;

	buf[sizeof(buf) - 1] = 0;

	while (fgets(buf, sizeof buf, fp) != NULL) {
		++linenum;

		buf[sizeof(buf) - 1] = 0;	/* Trunc, just in case.. */

		for (i = 0; buf[i] != 0; ++i) {
			c = buf[i];
			if (c == ' ' || c == '\t')
				continue;
			/* Anything else, stop scanning */
			break;
		}
		if (c == '#' || c == '\n' || c == '\r')
			continue;	/* Comment line, or empty line */

		cfgparam(buf, sizeof(buf), name, linenum);
	}
	fclose(fp);
}
