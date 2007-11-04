/* **************************************************************** *
 *                                                                  *
 *  APRSG-NG -- 2nd generation receive-only APRS-i-gate with        *
 *              minimal requirement of esoteric facilities or       *
 *              libraries of any kind beyond UNIX system libc.      *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007                                 *
 *                                                                  *
 * **************************************************************** */

#include "aprsg.h"


const char *mycall;


static char *SKIPSPACE ( char *Y )
{
	if (!Y) return Y;

	while (*Y == ' ' || *Y == '\t')
	  ++Y;

	return Y;
}

static char *SKIPDIGIT ( char *Y)
{
	if (!Y) return Y;

	while ('0' <= *Y && *Y <= '9')
	  ++Y;

	return Y;
}

/* SKIPTEXT:
 *
 *  Detect " -> scan until matching double quote
 *  Detect ' -> scan until matching single quote
 *  Detect non-eol, non-space(tab): scan until eol, or white-space
 *
 *  Will thus stop when found non-quoted space/tab, or
 *  end of line/string.
 */

static char * SKIPTEXT ( char *Y )
{
	if (!Y) return Y;

	if (*Y == '"') {
	  ++Y;
	  while (*Y && *Y != '"')
	    ++Y;
	  /* STOP at the tail-end " */
	} else if ( *Y == '\'' ) {
	  ++Y;
	  while(*Y && *Y != '\'')
	    ++Y;
	  /* STOP at the tail-end ' */
	} else {
	  while (*Y && *Y != ' ' && *Y != '\t')
	    ++Y;
	  /* Stop at white-space */
	}

	return Y;
}

static void STRLOWER(char *s)
{
	int c;
	for ( ; *s; ++s ) {
	  c = *s;
	  if ('A' <= c && c <= 'Z') {
	    *s = c + ('a' - 'A');
	  }
	}
}

static const char *serialcfg( struct serialport *tty, char *param1, char *param2, char *str )
{	/* serialport /dev/ttyUSB123   19200  8n1   {KISS|TNC2|AEA|..}  */
	int i;
	speed_t baud;
	char *param3;

	if (*param1 == 0) return "Bad tty-name";

	strncpy(tty->ttyname, param1, sizeof(tty->ttyname));
	tty->ttyname[sizeof(tty->ttyname)-1] = 0;

	/* setup termios parameters for this line.. */
	cfmakeraw(& tty->tio );
	tty->tio.c_cc[VMIN] = 1;  /* pick at least one char .. */
	tty->tio.c_cc[VTIME] = 1; /* 0.1 seconds timeout */
	tty->tio.c_cflag |= (CREAD | CLOCAL);

	tty->kissstate = KISSSTATE_SYNCHUNT;
	tty->linetype  = LINETYPE_KISS;           /* default */


	i = atol(param2);  /* serial port speed - baud rate */
	baud = B1200;
	switch (i) {
	case 1200:  baud = B1200;   break;
	case 2400:  baud = B2400;   break;
	case 4800:  baud = B4800;   break;
	case 9600:  baud = B9600;   break;
	case 19200: baud = B19200;  break;
	case 38400: baud = B38400;  break;
	default: return "Bad baud rate"; break;
	}

	cfsetispeed(& tty->tio, baud );
	cfsetospeed(& tty->tio, baud );

	STRLOWER(str); /* until end of line */


	param1 = str;		/* serial port databits-parity-stopbits */
	str = SKIPTEXT (str);
	if (*str != 0)
	  *str++ = 0;
	str = SKIPSPACE (str);

	/* FIXME:  analyze correct serial port data and parity format settings,
	   now defaulting to 8-n-1 */

	param1 = str;		/* Mode: KISS or something else */
	str = SKIPTEXT (str);
	if (*str != 0)
	  *str++ = 0;
	str = SKIPSPACE (str);

	if (strcmp(param1, "kiss") == 0) {
	  tty->linetype = LINETYPE_KISS;  /* plain basic KISS */


	/* ttys[0].linetype = LINETYPE_AEA; */
	/* ttys[0].linetype = LINETYPE_KISSBPQCRC; */

	} else {
	  return "Bad linetype parameter, known ones: KISS";
	}

	/* Optional parameters */
	while (*str != 0) {
	  param1 = str;
	  str = SKIPTEXT (str);
	  if (*str != 0)
	    *str++ = 0;
	  str = SKIPSPACE (str);

	  if (strcmp(param1, "xorsum") == 0) {
	  } else if (strcmp(param1, "bpqcrc") == 0) {
	  } else if (strcmp(param1, "smack") == 0) {
	  } else if (strcmp(param1, "crc16") == 0) {
	  } else if (strcmp(param1, "poll") == 0) {
	  }

	}


	return NULL;
}

static void cfgparam(char *str, int size, char *cfgfilename, int linenum)
{
	char *name, *param1, *param2;
	char *str0 = str;

	static int ttyindex;

	name = strchr(str, '\n');	/* The trailing newline chopper ... */
	if (name)
	  *name = 0;
	name = strchr(str, '\r');	/* The trailing cr chopper ... */
	if (name)
	  *name = 0;

	name = str;
	str = SKIPTEXT (str);
	if (*str != 0)
	  *str++ = 0;
	STRLOWER(name);

	str = SKIPSPACE (str);
	param1 = str;
	str = SKIPTEXT (str);
	if (*str != 0)
	  *str++ = 0;

	str = SKIPSPACE (str);
	param2 = str;
	str = SKIPTEXT (str);
	if (*str != 0)
	  *str++ = 0;


	if (strcmp(name, "mycall") == 0) {
	  mycall = strdup(param1);

	} else if (strcmp(name, "aprsis-server") == 0) {
	  aprsis_server_name = strdup(param1);
	  aprsis_server_port = strdup(param2);

	} else if (strcmp(name, "aprsis-heartbeat-timeout") == 0) {
	  aprsis_heartbeat_monitor_timeout = atol(param1);
	  if (aprsis_heartbeat_monitor_timeout < 0) /* param failure ? */
	    aprsis_heartbeat_monitor_timeout = 0; /* no timeout */

	} else if (strcmp(name, "netbeacon") == 0) {
	  beacon_set(param1);

	} else if (strcmp(name, "serialport") == 0) {
	  /* serialport /dev/ttyUSB123 [19200 [8n1] ] */
	  if (ttyindex >= MAXTTYS) return; /* Too many, sorry no.. */
	  if (ttys[ttyindex].ttyname[0]) /* Already defined something */
	    ++ttyindex;
	  if (ttyindex >= MAXTTYS) return; /* Too many, sorry no.. */

	  serialcfg(&ttys[ttyindex], param1, param2, str);

	} else if (strcmp(name, "initstring") == 0) {
	  if (ttyindex >= MAXTTYS) return; /* Too many, sorry no.. */
	  // TODO: ...  parse C-style escaped string into storage string..

	}

#if 0
	/* hard-coded init-string for OH2MQK with old AEA PK-96 ... */
	switch (ttys[0].linetype) {
	case LINETYPE_KISS:
	  s = "\xC0\xC0\xFF\xC0\r\rMO 0\rKISS $01\r";
	  break;
	case LINETYPE_KISSSMACK: /* SMACK ... */
	  break;
	case LINETYPE_KISSBPQCRC:
	  s = "\xC0\xC0\xFF\xC0\r\rMO 0\rKISS $0B\r";
	  break;
	case LINETYPE_TNC2:
	  break;
	case LINETYPE_AEA:
	  s = "\xC0\xC0\xFF\xC0\xC0\xFF\xC0\xC0\r\r\rMO 1\r";
	  break;
	default:
	  break;
	}

	ttys[0].initstring = s;
	ttys[0].initlen = s ? strlen(s) : 0;
	
	// beacon_set("OH2MQK-1>APRS,OH2MQK-1,I:!6016.35N/02506.36E-aprsg-ng Rx-only \"i-gate\"\n");
#endif
}


int readconfig(char *name)
{
    FILE *fp;
    unsigned char c;
    char *cp, buf[1024], *s, *s0;
    int linenum = 0, i;



    if ((fp = fopen(name, "r")) == NULL)
      return -1;

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
    return 0;
}
