/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"


const char *mycall;


char *config_SKIPSPACE ( char *Y )
{
	if (!Y) return Y;

	while (*Y == ' ' || *Y == '\t')
	  ++Y;

	return Y;
}

#if 0
char *config_SKIPDIGIT ( char *Y)
{
	if (!Y) return Y;

	while ('0' <= *Y && *Y <= '9')
	  ++Y;

	return Y;
}
#endif

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

char * config_SKIPTEXT ( char *Y )
{
	char *O = Y;
	char endc = *Y;
	if (!Y) return Y;

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
	      } else if (*Y == 'x') {
		/* Hex encoded char */
		int i;
		char hx[3];
		if (*Y) ++Y;
		hx[0] = *Y;
		if (*Y) ++Y;
		hx[1] = *Y;
		hx[2] = 0;
		i = (int)strtol(hx, NULL, 16);
		*O++ = i;
	      }
	    } else
	      *O++ = *Y;
	    if (*Y != 0)
	      ++Y;
	  }
	  *O = 0; /* String end */
	  /* STOP at the tail-end " */
	} else {
	  while (*Y && *Y != ' ' && *Y != '\t') {
	    ++Y;
	  }
	  /* Stop at white-space */
	}

	return Y;
}

void config_STRLOWER(char *s)
{
	int c;
	for ( ; *s; ++s ) {
	  c = *s;
	  if ('A' <= c && c <= 'Z') {
	    *s = c + ('a' - 'A');
	  }
	}
}

static void cfgparam(char *str, int size, const char *cfgfilename, int linenum)
{
	char *name, *param1, *param2;

	name = strchr(str, '\n');	/* The trailing newline chopper ... */
	if (name)
	  *name = 0;
	name = strchr(str, '\r');	/* The trailing cr chopper ... */
	if (name)
	  *name = 0;

	name = str;
	str = config_SKIPTEXT (str);
	if (*str != 0)
	  *str++ = 0;
	config_STRLOWER(name);

	str = config_SKIPSPACE (str);
	param1 = str;

	str = config_SKIPTEXT (str);
	if (*str != 0)
	  *str++ = 0;

	str = config_SKIPSPACE (str);
	param2 = str;
	str = config_SKIPTEXT (str);
	if (*str != 0)
	  *str++ = 0;


	if (strcmp(name, "mycall") == 0) {
	  mycall = strdup(param1);
	  if (debug)
	    printf("%s:%d: MYCALL = '%s'\n", cfgfilename, linenum, mycall);

	} else if (strcmp(name, "aprsis-server") == 0) {
	  aprsis_add_server(param1, param2);

	  if (debug)
	    printf("%s:%d: APRSIS-SERVER = '%s':'%s'\n", cfgfilename, linenum,
		   param1, param2);

	} else if (strcmp(name, "aprsis-heartbeat-timeout") == 0) {
	  int i = atoi(param1);
	  if (i < 0) /* param failure ? */
	    i = 0; /* no timeout */
	  aprsis_set_heartbeat_timeout(i);

	  if (debug)
	    printf("%s:%d: APRSIS-HEARTBEAT-TIMEOUT = '%d'\n", cfgfilename, linenum, i);

	} else if (strcmp(name, "aprsis-filter") == 0) {
	  aprsis_set_filter(param1);

	  if (debug)
	    printf("%s:%d: APRSIS-FILTER = '%s'\n", cfgfilename, linenum, param1);

	} else if (strcmp(name, "aprsis-mycall") == 0) {
	  /* Do not use! - multi APRSIS connection thing, which is also "do not use" item.. */
	  aprsis_set_mycall(param1);

	  if (debug)
	    printf("%s:%d: APRSIS-MYCALL = '%s'\n", cfgfilename, linenum, param1);

	} else if (strcmp(name, "netbeacon") == 0) {
	  beacon_set(param1);

	  if (debug)
	    printf("%s:%d: NETBEACON = '%s'\n", cfgfilename, linenum, param1);

	} else if (strcmp(name, "aprxlog") == 0) {
	  if (debug)
	    printf("%s:%d: APRXLOG = '%s'\n", cfgfilename, linenum, param1);

	  rflogfile = strdup(param1);

	} else if (strcmp(name, "aprxlog-erlang") == 0) {
	  if (debug)
	    printf("%s:%d: APRXLOG-ERLANG\n", cfgfilename, linenum);

	  erlangout = 2;

	} else if (strcmp(name, "rflog") == 0) {
	  if (debug)
	    printf("%s:%d: RFLOG = '%s'\n", cfgfilename, linenum, param1);

	  aprxlogfile = strdup(param1);

	} else if (strcmp(name, "erlangfile") == 0) {
	  if (debug)
	    printf("%s:%d: ERLANGFILE = '%s'\n", cfgfilename, linenum, param1);

	  erlang_backingstore = strdup(param1);

	} else if (strcmp(name, "erlang-loglevel") == 0) {
	  if (debug)
	    printf("%s:%d: ERLANG-LOGLEVEL = '%s'\n", cfgfilename, linenum, param1);
	  erlang_init(param1);

	} else if (strcmp(name, "serialport") == 0) {
	  const char *s = ttyreader_serialcfg(param1, param2, str);
	  if (debug)
	    printf("%s:%d: SERIALPORT = %s %s %s..  %s\n", cfgfilename, linenum, param1, param2, str, s ? s : "");

	} else {
	  printf("%s:%d: Unknown config keyword: '%s'\n", cfgfilename, linenum, param1);
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
	  continue;  /* Comment line, or empty line */

	cfgparam(buf, sizeof(buf), name, linenum);
    }
    fclose(fp);
}
