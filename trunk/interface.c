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

/*
 *  The interface subsystem describes all interfaces in one
 *  coherent way, independent of their actual implementation.
 *
 */

/*

<interface>
   serial-device /dev/ttyUSB1 19200 8n1 KISS
   tx-ok         false		# receive only (default)
   callsign      OH2XYZ-R2	# KISS subif 0
   initstring    "...."		# initstring option
   timeout       900            # 900 seconds of no Rx
</interface>

<interface>
   serial-device /dev/ttyUSB2 19200 8n1 KISS
   initstring    "...."
   timeout       900            # 900 seconds of no Rx
   <kiss-subif 0>
      callsign OH2XYZ-2
      tx-ok    true		# This is our transmitter
   </kiss-subif>
   <kiss-subif 1>
      callsign OH2XYZ-R3	# This is receiver
      tx-ok    false		# receive only (default)
   </kiss-subif>
</interface>

<interface>
   tcp   172.168.1.1 4001 KISS
   tx-ok         false		# receive only (default)
   callsign      OH2XYZ-R4	# KISS subif 0
   initstring    "...."		# initstring option
   timeout       900            # 900 seconds of no Rx
</interface>

<interface>
   ax25-device OH2XYZ-6		# Works only on Linux systems
   tx-ok       true		# This is also transmitter
</interface>

 */

struct aprx_interface **all_interfaces;
int                     all_interfaces_count;


static void store_interface(struct aprx_interface *aif)
{
	all_interfaces_count += 1;
	all_interfaces = realloc(all_interfaces,
				 sizeof(all_interfaces) * all_interfaces_count);
	all_interfaces[all_interfaces_count -1] = aif;
}

struct aprx_interface *find_interface_by_callsign(const char *callsign)
{
	int i;
	for (i = 0; i < all_interfaces_count; ++i) {
	  if ((all_interfaces[i]->callsign != NULL) &&
	      (strcasecmp(callsign, all_interfaces[i]->callsign) == 0)) {
	    return all_interfaces[i];
	  }
	}
	return NULL; // Not found!
}

static int config_kiss_subif(struct configfile *cf, struct aprx_interface *aif, struct aprx_interface **aifp, char *param1, char *str, int maxsubif)
{
	int   fail = 0;

	char *name;
	int   parlen = 0;

	char *initstring = NULL;
	int   initlength = 0;
	char *callsign   = NULL;
	int   subif      = 0;
	int   txok       = 0;

	const char *p = param1;
	int c;

	for ( ; *p; ++p ) {
		c = *p;
		if ('0' <= c && c <= '9') {
			subif = subif * 10 + (c - '0');
		} else if (c == '>') {
		  // all fine..
		} else {
		  // FIXME: <KISS-SubIF nnn>  parameter value is bad!
		  printf("%s:%d  <kiss-subif %s parameter value is bad\n",
			 cf->name, cf->linenum, param1);
		  return 1;
		}
	}
	if (subif >= maxsubif) {
		// FIXME: <KISS-SubIF nnn>  parameter value is bad!
	  printf("%s:%d  <kiss-subif %s parameter value is too large for this KISS variant.\n",
		 cf->name, cf->linenum, param1);
	  return 1;
	}

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
		str = config_SKIPTEXT(str, &parlen);
		str = config_SKIPSPACE(str);

		if (strcmp(name, "</kiss-subif>") == 0) {
		  break; // End of this sub-group
		}

		// FIXME:   callsign   and   tx-ok   parameters!
		if (strcmp(name, "callsign") == 0) {
		  if (validate_callsign_input(param1,txok)) {
		    if (txok)
		      printf("%s:%d The CALLSIGN parameter on AX25-DEVICE must be of valid AX.25 format! '%s'\n",
			     cf->name, cf->linenum, param1);
		    else
		      printf("%s:%d The CALLSIGN parameter on AX25-DEVICE must be of valid APRSIS format! '%s'\n",
			     cf->name, cf->linenum, param1);
		    fail = 1;
		    break;
		  }
		  callsign = strdup(param1);
		} else if (strcmp(name, "initstring") == 0) {
		  
		  initlength = parlen;
		  initstring = malloc(parlen);
		  memcpy(initstring, param1, parlen);

		} else if (strcmp(name, "tx-ok") == 0) {
		  if (!config_parse_boolean(param1, &txok)) {
		    printf("%s:%d Bad TX-OK parameter value -- not a recognized boolean: %s\n",
			   cf->name, cf->linenum, param1);
		    fail = 1;
		    break;
		  }
		} else {
		  printf("%s:%d Unrecognized keyword: %s\n",
			   cf->name, cf->linenum, name);
		  fail = 1;
		  break;
		}
	}
	if (fail) return 1;

	if (callsign == NULL) {
	  // FIXME: Must define at least a callsign!
	  return 1;
	}

	*aifp = malloc(sizeof(*aif));
	memcpy(*aifp, aif, sizeof(*aif));

	(*aifp)->callsign = callsign;
	(*aifp)->subif    = subif;
	(*aifp)->txok     = txok;

	aif->tty->interface  [subif] = *aifp;
	aif->tty->ttycallsign[subif] = callsign;
	aif->tty->netax25    [subif] = netax25_open(callsign);

	store_interface(*aifp);

	if (initstring != NULL) {
	  aif->tty->initlen[subif]    = initlength;
	  aif->tty->initstring[subif] = initstring;
	}

	return 0;
}

void config_interface(struct configfile *cf)
{
	struct aprx_interface *aif = calloc(1, sizeof(*aif));
	struct aprx_interface *aif2 = NULL; // subif copies here..

	char *name, *param1;
	char *str = cf->buf;
	int  parlen = 0;

	int  maxsubif = 16;  // 16 for most KISS modes, 8 for SMACK

	aif->iftype = IFTYPE_UNSET;

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
		str = config_SKIPTEXT(str, &parlen);
		str = config_SKIPSPACE(str);

		if (strcmp(name, "</interface>") == 0) {
		  // End of this interface definition

		  // make the interface...

		  break;
		}
		if (strcmp(name, "<kiss-subif") == 0) {
		  aif2 = NULL;
		  if (config_kiss_subif(cf, aif, &aif2, param1, str, maxsubif)) {
		    // FIXME: bad inputs.. complained already
		  } else {
		    if (aif2 != NULL) {
		    }
		  }

		  continue;
		}

		// FIXME: interface parameters

		if (strcmp(name,"ax25-device") == 0) {
		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_AX25;
		    // aif->nax25p = NULL;
		  } else {
		    printf("%s:%d Only single device specification per interface block!\n",
			   cf->name, cf->linenum);
		    continue;
		  }

		  if (validate_callsign_input(param1,1)) {
		    printf("%s:%d The CALLSIGN parameter on AX25-DEVICE must be of valid AX.25 format! '%s'\n",
			   cf->name, cf->linenum, param1);
		    continue;
		  }

		  if (debug)
		    printf("%s:%d: AX25-DEVICE '%s' '%s'\n",
			   cf->name, cf->linenum, param1, str);

		  netax25_addrxport(param1, str);
		  aif->callsign = strdup(param1);
		  
		  store_interface(aif);

		} else if ((strcmp(name,"serial-device") == 0) && (aif->tty == NULL)) {
		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_SERIAL;
		    aif->tty = ttyreader_new();
		    aif->tty->interface[0] = aif;

		  } else {
		    printf("%s:%d Only single device specification per interface block!\n",
			   cf->name, cf->linenum);
		    continue;
		  }

		  aif->tty->ttyname = strdup(param1);
		  if (debug)
		    printf(".. new style serial:  '%s' '%s'..\n",
			   aif->tty->ttyname, str);

		  ttyreader_parse_ttyparams(cf, aif->tty, str);


		  
		} else if ((strcmp(name,"tcp-device") == 0) && (aif->tty == NULL)) {
		  int len;
		  char *host, *port;

		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_TCPIP;
		    aif->tty = ttyreader_new();
		    aif->tty->interface[0] = aif;

		  } else {
		    printf("%s:%d Only single device specification per interface block!\n",
			   cf->name, cf->linenum);
		    continue;
		  }

		  host = param1;

		  port = str;
		  str = config_SKIPTEXT(str, NULL);
		  str = config_SKIPSPACE(str);

		  if (debug)
		    printf(".. new style tcp!:  '%s' '%s' '%s'..\n",
			   host, port, str);

		  len = strlen(host) + strlen(port) + 8;

		  aif->tty->ttyname = malloc(len);
		  sprintf((char *) (aif->tty->ttyname), "tcp!%s!%s!", host, port);

		  ttyreader_parse_ttyparams( cf, aif->tty, str );

		} else if (strcmp(name,"tx-ok") == 0) {

		  if (!config_parse_boolean(param1, &(aif->txok))) {
		    printf("%s:%d Bad TX-OK parameter value -- not a recognized boolean: %s\n",
			   cf->name, cf->linenum, param1);
		    continue;
		  }
		  if (aif->txok && aif->callsign) {
		    if (validate_callsign_input(aif->callsign,aif->txok)) {  // Transmitters REQUIRE valid AX.25 address
		      printf("%s:%d: TX-OK 'TRUE -- BUT PREVIOUSLY SET CALLSIGN IS NOT VALID AX.25 ADDRESS \n",
			     cf->name, cf->linenum);
		      continue;
		    }
		  }

		} else if (strcmp(name,"timeout") == 0) {
		  if (config_parse_interval(param1, &(aif->timeout) ) ||
		      (aif->timeout < 0) || (aif->timeout > 1200)) {
		    aif->timeout = 0;
		    printf("%s:%d Bad TIMEOUT parameter value: %s\n",
			   cf->name, cf->linenum, param1);
		    continue;
		  }
		  if (aif->tty != NULL) {
		    aif->tty->read_timeout = aif->timeout;
		  }

/*
		} else if ((strcmp(name,"callsign") == 0) && (aif->callsign == NULL)) {

		  if (validate_callsign_input(param1,aif->txok)) { // Transmitters REQUIRE valid AX.25 address
		    // FIX MESSAGE TEXTS PER txok VALUE
		    printf("%s:%d: CALLSIGN '%s -- IS NOT VALID AX.25 ADDRESS, AS REQUIRED ON TRANSMITTER DEVICES\n",
			   cf->name, cf->linenum, param1);
		    continue;
		  }
		  aif->callsign = strdup(param1);

		} else if (strcmp(name,"initstring") == 0) {
		  if (aif->tty != NULL) {
		    aif->tty->initlen[0]    = parlen;
		    aif->tty->initstring[0] = malloc(parlen);
		    memcpy(aif->tty->initstring[0], param1, parlen);
		  } else {
		    printf("%s:%d The initstring parameter is available only on serial-device, and tcp-device\n",
			   cf->name, cf->linenum);
		    continue;
		  }
*/
		} else {
		  printf("%s:%d Unknown config entry name: '%s'\n",
			 cf->name, cf->linenum, name);

		}
	}
}
