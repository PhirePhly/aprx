/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2011                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"
#include <sys/socket.h>

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
   tcp-device   172.168.1.1 4001 KISS
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
int			top_interfaces_group;

// Init-code stores this with ifindex = 0.
// This is necessary even for system where igate is removed
struct aprx_interface aprsis_interface = {
	IFTYPE_APRSIS, 0, 0, 0, "APRSIS",
	{'A'<<1,'P'<<1,'R'<<1,'S'<<1,'I'<<1,'S'<<1, 0x60},
	0, NULL,
	0, 0, 0, 0, NULL,
	NULL,
#ifdef ENABLE_AGWPE
	NULL,
#endif
	NULL,
	0, NULL
};

#ifndef DISABLE_IGATE
/*
 * A helper for interface_receive_ax25() - analyze 3rd-party packets received
 * via radio.  If data content inside has path saying "TCPIP" or "TCPXX", consider
 * the packet to be indication that fromcall is an IGate.
 */
static void rx_analyze_3rdparty( historydb_t *historydb, struct pbuf_t *pb )
{
	const char *e = pb->data + pb->packet_len - 6;
	const char *p = pb->info_start;
	int from_igate = 0;
	history_cell_t *hist_rx;

	if (!p) return; // Bad packet..
	++p;

	for ( ; p < e; ++p ) {
	  if (*p == ':') break;
	  if (*p == ',') {
	    // The "TCPIP*" or "TCPXX*" will always have preceding ","
	    if (memcmp(",TCPIP*", p, 7) == 0) {
	      from_igate = 1;
	      break;
	    }
	    if (memcmp(",TCPXX*", p, 7) == 0) {
	      from_igate = 1;
	      break;
	    }
	  } // Start with 'T'.
	}

	if (!from_igate) return;  // Not recognized as being sent from another TX-IGATE

	// OK, this packet originated from an TX-IGATE

	// Insert it afresh
	hist_rx = historydb_insert_heard(historydb, pb);
	if (hist_rx != NULL) {
	  // Explicitly mark it as "received from APRSIS"
	  // The packet was received from a TX-IGATE, therefore
	  // the source of that packet is now logged as "from APRSIS".
	  hist_rx->last_heard[0] = pb->t;
	}
}
#endif

static char *interface_default_aliases[] = { "RELAY","WIDE","TRACE" };

static void interface_store(struct aprx_interface *aif)
{
	if (debug)
	  printf("interface_store() aif->callsign = '%s'\n", aif->callsign);

	// Init the interface specific Erlang accounting
	erlang_add(aif->callsign, ERLANG_RX, 0, 0);

	all_interfaces_count += 1;
	all_interfaces = realloc(all_interfaces,
				 sizeof(all_interfaces) * all_interfaces_count);
	all_interfaces[all_interfaces_count -1] = aif;
	if (aif->ifindex < 0)
	  aif->ifindex = all_interfaces_count -1;
	if (aif->ifgroup < 0)
	  aif->ifgroup = all_interfaces_count -1;
	if (top_interfaces_group <= aif->ifgroup)
	  top_interfaces_group = aif->ifgroup +1;
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

struct aprx_interface *find_interface_by_index(const int index)
{
	if (index >= all_interfaces_count ||
	    index < 0) {
	  return NULL; // Invalid index value
	} else {
	  return all_interfaces[index];
	}
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
	int   aliascount = 0;
	char **aliases   = NULL;
	int   ifgroup    = -1;

	const char *p = param1;
	int c;

	for ( ; *p; ++p ) {
		c = *p;
		if ('0' <= c && c <= '9') {
			subif = subif * 10 + (c - '0');
		} else if (c == '>') {
		  // all fine..
		  break;
		} else {
		  // FIXME: <KISS-SubIF nnn>  parameter value is bad!
		  printf("%s:%d ERROR: <kiss-subif %s parameter value is bad\n",
			 cf->name, cf->linenum, param1);
		  return 1;
		}
	}
	if (subif >= maxsubif) {
		// FIXME: <KISS-SubIF nnn>  parameter value is bad!
		printf("%s:%d ERROR: <kiss-subif %s parameter value is too large for this KISS variant.\n",
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

		if (strcmp(name, "callsign") == 0) {

		  if (strcasecmp(param1,"$mycall") == 0)
		    callsign = strdup(mycall);
		  else
		    callsign = strdup(param1);

		  if (!validate_callsign_input(callsign,txok)) {
		    if (txok)
		      printf("%s:%d ERROR: The CALLSIGN parameter on AX25-DEVICE must be of valid AX.25 format! '%s'\n",
			     cf->name, cf->linenum, callsign);
		    else
		      printf("%s:%d ERROR: The CALLSIGN parameter on AX25-DEVICE must be of valid APRSIS format! '%s'\n",
			     cf->name, cf->linenum, callsign);
		    fail = 1;
		    break;
		  }

		} else if (strcmp(name, "initstring") == 0) {
		  
		  initlength = parlen;
		  initstring = malloc(parlen);
		  memcpy(initstring, param1, parlen);

		} else if (strcmp(name, "tx-ok") == 0) {
		  if (!config_parse_boolean(param1, &txok)) {
		    printf("%s:%d ERROR: Bad TX-OK parameter value -- not a recognized boolean: %s\n",
			   cf->name, cf->linenum, param1);
		    fail = 1;
		    break;
		  }

		} else if (strcmp(name, "alias") == 0) {
		  char *k = strtok(param1, ",");
		  for (; k ; k = strtok(NULL,",")) {
		    ++aliascount;
		    if (debug) printf(" n=%d alias='%s'\n",aliascount,k);
		    aliases = realloc(aliases, sizeof(char*) * aliascount);
		    aliases[aliascount-1] = strdup(k);
		  }

#ifndef DISABLE_IGATE
		} else if (strcmp(name, "igate-group") == 0) {
		  // param1 = integer 1 to N.
		  ifgroup = atol(param1);
		  if (ifgroup < 1) {
		    printf("%s:%d ERROR: interface 'igate-group' parameter value: '%s'  is an integer with minimum value of 1.\n",
			   cf->name, cf->linenum, param1);
		    fail = 1;
		    break;
		  } else if (ifgroup >= MAX_IF_GROUP) {
		    printf("%s:%d ERROR: interface 'igate-group' parameter value: '%s'  is an integer with maximum value of %d.\n",
			   cf->name, cf->linenum, param1, MAX_IF_GROUP-1);
		    fail = 1;
		    break;
		  }
#endif

		} else {
		  printf("%s:%d ERROR: Unrecognized <interface> block keyword: %s\n",
			   cf->name, cf->linenum, name);
		  fail = 1;
		  break;
		}
	}
	if (fail) return 1;

	if (callsign == NULL) {
	  // FIXME: Must define at least a callsign!
	  printf("%s:%d ERROR: <kiss-subif ..> MUST define CALLSIGN parameter!\n",
		 cf->name, cf->linenum);
	  return 1;
	}

	if (find_interface_by_callsign(callsign) != NULL) {
	  // An interface with THIS callsign does exist already!
	  printf("%s:%d ERROR: Same callsign (%s) exists already on another interface.\n",
		 cf->name, cf->linenum, callsign);
	  return 1;
	}

	*aifp = malloc(sizeof(*aif));
	memcpy(*aifp, aif, sizeof(*aif));

	(*aifp)->callsign = callsign;
	parse_ax25addr((*aifp)->ax25call, callsign, 0x60);
	(*aifp)->subif    = subif;
	(*aifp)->txok     = txok;
	(*aifp)->ifindex  = -1; // system sets automatically at store time
	(*aifp)->ifgroup  = ifgroup; // either user sets, or system sets at store time

	aif->tty->interface  [subif] = *aifp;
	aif->tty->ttycallsign[subif] = callsign;
#ifdef PF_AX25	/* PF_AX25 exists -- highly likely a Linux system ! */
	aif->tty->netax25    [subif] = netax25_open(callsign);
#endif

	if (aliascount == 0) {
	  (*aifp)->aliascount = 3;
	  (*aifp)->aliases    = interface_default_aliases;
	} else {
	  (*aifp)->aliascount = aliascount;
	  (*aifp)->aliases    = aliases;
	}

	if (initstring != NULL) {
	  aif->tty->initlen[subif]    = initlength;
	  aif->tty->initstring[subif] = initstring;
	}

	return 0;
}

void interface_init()
{
	interface_store( &aprsis_interface );
}

int interface_config(struct configfile *cf)
{
	struct aprx_interface *aif = calloc(1, sizeof(*aif));
	struct aprx_interface *aif2 = NULL; // subif copies here..

	char  *name, *param1;
	char  *str = cf->buf;
	int    parlen     = 0;
	int    have_fault = 0;
	int    maxsubif   = 16;  // 16 for most KISS modes, 8 for SMACK
	int    defined_subinterface_count = 0;
	int    ifgroup    = -1;

	aif->iftype = IFTYPE_UNSET;
	aif->aliascount = 3;
	aif->aliases    = interface_default_aliases;
	aif->ifindex    = -1; // system sets automatically at store time
	aif->ifgroup    = ifgroup; // either user sets, or system sets at store time

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
		    // Bad inputs.. complained already
		    have_fault = 1;
		  }
		  // Always count as defined, even when an error happened!
		  ++defined_subinterface_count;

		  continue;
		}

		// Interface parameters

		if (strcmp(name,"ax25-device") == 0) {
#ifdef PF_AX25		// PF_AX25 exists -- highly likely a Linux system !
		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_AX25;
		    // aif->nax25p = NULL;
		  } else {
		    printf("%s:%d ERROR: Only single device specification per interface block!\n",
			   cf->name, cf->linenum);
		    have_fault = 1;
		    continue;
		  }

		  if (strcasecmp(param1,"$mycall") == 0)
		    param1 = strdup(mycall);

		  if (!validate_callsign_input(param1,1)) {
		    printf("%s:%d ERROR: The CALLSIGN parameter on AX25-DEVICE must be of valid AX.25 format! '%s'\n",
			   cf->name, cf->linenum, param1);
		    have_fault = 1;
		    continue;
		  }

		  if (find_interface_by_callsign(param1) != NULL) {
		    // An interface with THIS callsign does exist already!
		    printf("%s:%d ERROR: Same callsign (%s) exists already on another interface.\n",
			   cf->name, cf->linenum, param1);
		    have_fault = 1;
		    continue;
		  }

		  if (debug)
		    printf("%s:%d: AX25-DEVICE '%s' '%s'\n",
			   cf->name, cf->linenum, param1, str);

		  aif->callsign = strdup(param1);
		  parse_ax25addr(aif->ax25call, aif->callsign, 0x60);
		  aif->nax25p = netax25_addrxport(param1, aif);
		  if (aif->nax25p == NULL) {
		    printf("%s:%d ERROR: Failed to open this AX25-DEVICE: '%s'\n",
			   cf->name, cf->linenum, param1);
		    have_fault = 1;
		    continue;
		  }
#else
		  printf("%s:%d ERROR: AX25-DEVICE interfaces are not supported at this system!\n",
			 cf->name, cf->linenum, param1);
		  have_fault = 1;
#endif

		} else if ((strcmp(name,"serial-device") == 0) && (aif->tty == NULL)) {

		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype              = IFTYPE_SERIAL;
		    aif->tty                 = ttyreader_new();
		    aif->tty->ttyname        = strdup(param1);
		    aif->tty->interface[0]   = aif;
		    aif->tty->ttycallsign[0] = mycall;

		    // end processing registers it

		  } else {
		    printf("%s:%d ERROR: Only single device specification per interface block!\n",
			   cf->name, cf->linenum);
		    have_fault = 1;
		    continue;
		  }

		  if (debug)
		    printf(".. new style serial:  '%s' '%s'.. tncid=0\n",
			   aif->tty->ttyname, str);

		  have_fault |= ttyreader_parse_ttyparams(cf, aif->tty, str);

		  switch (aif->tty->linetype) {
		  case LINETYPE_KISSSMACK:
		    maxsubif = 8;  // 16 for most KISS modes, 8 for SMACK
		    break;
		  case LINETYPE_KISSFLEXNET:
		    // ???
		    break;
		  default:
		    break;
		  }

		  // Always count as defined, even when an error happened!
		  ++defined_subinterface_count;

		} else if ((strcmp(name,"tcp-device") == 0) && (aif->tty == NULL)) {
		  int len;
		  char *host, *port;

		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_TCPIP;
		    aif->tty = ttyreader_new();
		    aif->tty->interface[0] = aif;
		    aif->tty->ttycallsign[0]  = mycall;

		    // end-step processing registers it

		  } else {
		    printf("%s:%d ERROR: Only single device specification per interface block!\n",
			   cf->name, cf->linenum);
		    have_fault = 1;
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

		  have_fault |= ttyreader_parse_ttyparams( cf, aif->tty, str );

		  switch (aif->tty->linetype) {
		  case LINETYPE_KISSSMACK:
		    maxsubif = 8;  // 16 for most KISS modes, 8 for SMACK
		    break;
		  case LINETYPE_KISSFLEXNET:
		    // ???
		    break;
		  default:
		    break;
		  }

		  // Always count as defined, even when an error happened!
		  ++defined_subinterface_count;

		} else if (strcmp(name,"null-device") == 0) {
		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_NULL;
		    // aif->nax25p = NULL;
		  } else {
		    printf("%s:%d ERROR: Only single device specification per interface block!\n",
			   cf->name, cf->linenum);
		    have_fault = 1;
		    continue;
		  }
		  aif->txok = 1;

		  if (strcasecmp(param1,"$mycall") == 0)
		    param1 = strdup(mycall);

		  if (find_interface_by_callsign(param1) != NULL) {
		    // An interface with THIS callsign does exist already!
		    printf("%s:%d ERROR: Same callsign (%s) exists already on another interface.\n",
			   cf->name, cf->linenum, param1);
		    have_fault = 1;
		    continue;
		  }

		  if (debug)
		    printf("%s:%d: NULL-DEVICE '%s' '%s'\n",
			   cf->name, cf->linenum, param1, str);

		  aif->callsign = strdup(param1);
		  parse_ax25addr(aif->ax25call, aif->callsign, 0x60);


		} else if (strcmp(name,"tx-ok") == 0) {

		  if (!config_parse_boolean(param1, &(aif->txok))) {
		    printf("%s:%d ERROR: Bad TX-OK parameter value -- not a recognized boolean: %s\n",
			   cf->name, cf->linenum, param1);
		    have_fault = 1;
		    continue;
		  }
		  if (aif->txok && aif->callsign) {
		    if (!validate_callsign_input(aif->callsign,aif->txok)) {  // Transmitters REQUIRE valid AX.25 address
		      printf("%s:%d: ERROR: TX-OK 'TRUE' -- BUT PREVIOUSLY SET CALLSIGN IS NOT VALID AX.25 ADDRESS \n",
			     cf->name, cf->linenum);
		      continue;
		    }
		  }

		} else if (strcmp(name,"timeout") == 0) {
		  if (config_parse_interval(param1, &(aif->timeout) ) ||
		      (aif->timeout < 0) || (aif->timeout > 1200)) {
		    aif->timeout = 0;
		    printf("%s:%d ERROR: Bad TIMEOUT parameter value: '%s' accepted range: 0 to 1200 seconds.\n",
			   cf->name, cf->linenum, param1);
		    have_fault = 1;
		    continue;
		  }
		  if (aif->tty != NULL) {
		    aif->tty->read_timeout = aif->timeout;
		  }

		} else if (strcmp(name, "callsign") == 0) {
		  if (strcasecmp(param1,"$mycall") == 0)
		    param1 = strdup(mycall);

		  if (find_interface_by_callsign(param1) != NULL) {
		    // An interface with THIS callsign does exist already!
		    printf("%s:%d ERROR: Same callsign (%s) exists already on another interface.\n",
			   cf->name, cf->linenum, param1);
		    have_fault = 1;
		    continue;
		  }

		  if (!validate_callsign_input(param1,aif->txok)) {
		    if (aif->txok && aif->iftype != IFTYPE_NULL) {
		      printf("%s:%d ERROR: The CALLSIGN parameter on transmit capable interface must be of valid AX.25 format! '%s'\n",
			     cf->name, cf->linenum, param1);
		      have_fault = 1;
		      continue;
		    }
		  }
		  aif->callsign = strdup(param1);
		  parse_ax25addr(aif->ax25call, aif->callsign, 0x60);
		  if (aif->tty != NULL)
		    aif->tty->ttycallsign[0] = aif->callsign;

		  if (debug)
		    printf("  callsign= '%s'\n", aif->callsign);

		} else if (strcmp(name, "initstring") == 0) {
		  
		  if (aif->tty != NULL) {
		    int   initlength = parlen;
		    char *initstring = malloc(parlen);
		    memcpy(initstring, param1, parlen);
		    aif->tty->initstring[0] = initstring;
		    aif->tty->initlen[0]    = initlength;
		  }

		} else if (strcmp(name, "alias") == 0) {
		  char *k = strtok(param1, ",");
		  if (aif->aliases == interface_default_aliases) {
		    aif->aliascount = 0;
		    aif->aliases = NULL;
		  }
		  for (; k ; k = strtok(NULL,",")) {
		    aif->aliascount += 1;
		    if (debug) printf(" n=%d alias='%s'\n",aif->aliascount,k);
		    aif->aliases = realloc(aif->aliases, sizeof(char*) * aif->aliascount);
		    aif->aliases[aif->aliascount-1] = strdup(k);
		  }

#ifndef DISABLE_IGATE
		} else if (strcmp(name, "igate-group") == 0) {
		  // param1 = integer 1 to N.
		  ifgroup = atol(param1);
		  if (ifgroup < 1) {
		    printf("%s:%d ERROR: interface 'igate-group' parameter value: '%s'  is an integer with minimum value of 1.\n",
			   cf->name, cf->linenum, param1);
		    have_fault = 1;
		    continue;
		  } else if (ifgroup >= MAX_IF_GROUP) {
		    printf("%s:%d ERROR: interface 'igate-group' parameter value: '%s'  is an integer with maximum value of %d.\n",
			   cf->name, cf->linenum, param1, MAX_IF_GROUP-1);
		    have_fault = 1;
		    continue;
		  }
#endif

		} else {
		  printf("%s:%d ERROR: Unknown <interface> config entry name: '%s'\n",
			 cf->name, cf->linenum, name);
		  have_fault = 1;
		}
	}


	while (!have_fault &&
	       aif->callsign == NULL &&
	       (aif->iftype == IFTYPE_SERIAL || aif->iftype == IFTYPE_TCPIP) &&
	       defined_subinterface_count == 1) {

	        // First check if there already is an interface with $mycall
		// callsign on it..
	
		if (find_interface_by_callsign(mycall) != NULL) {
		  // An interface with $MYCALL callsign does exist already!
		  printf("%s:%d ERROR: The $MYCALL callsign (%s) exists already on another interface.\n",
			 cf->name, cf->linenum, mycall);
		  have_fault = 1;
		  break;
		}

		// Supply a default value
		aif->callsign = strdup(mycall);
		parse_ax25addr(aif->ax25call, aif->callsign, 0x60);
		
#ifdef PF_AX25	// PF_AX25 exists -- highly likely a Linux system !
		// With enough defaults being used, the callsign is defined
		// by global "macro"  mycall,  and never ends up activating
		// the tty -> linux kernel kiss/smack pty  interface.
		// This part does that final step for minimalistic config.
		if (aif->tty != NULL &&
		    aif->tty->netax25[0] == NULL &&
		    aif->tty->ttycallsign[0] != NULL) {
			aif->tty->netax25[0]
			  = netax25_open(aif->tty->ttycallsign[0]);
		}
#endif
		// Done it, leave..
		break;
	}

	if (!have_fault) {
		int i;

		if (aif->tty != NULL) {
		  // Register all tty subinterfaces
		  for (i = 0; i < maxsubif; ++i) {
		    if (aif->tty->interface[i] != NULL) {
		      interface_store(aif->tty->interface[i]);
		    }
		  }
		} else {
		  // Not TTY multiplexed ( = KISS ) interface,
		  // register just the primary.
		  aif->ifgroup = ifgroup; // either user sets, or system sets at store time
		  interface_store(aif);

		}

		if (aif->iftype == IFTYPE_SERIAL)
		  ttyreader_register(aif->tty);

		if (aif->iftype == IFTYPE_TCPIP)
		  ttyreader_register(aif->tty);
	}

	return have_fault;
}


/*
 * Process received AX.25 packet
 *   - from AIF do find all DIGIPEATERS wanting this source.
 *   - If there are none, end processing.
 *   - Parse the received frame for possible latter filters
 *   - Feed the resulting parsed packet to each digipeater
 *
 *
 * Tx-IGate rules:
 *
 // 2) - sending station has not been heard recently
 //      on radio
 // 1) - verify receiving station has been heard
 //      recently on radio
 // 4) - the receiving station has not been heard via
 //      the Internet within a predefined time period.
 //      (Note that _this_ packet is heard from internet,
 //      so one must not confuse this to history..
 //      Nor this siblings that are being created
 //      one for each tx-interface...)
 // 
 //  A station is said to be heard via the Internet if packets
 //  from the station contain TCPIP* or TCPXX* in the header or
 //  if gated (3rd-party) packets are seen on RF gated by the
 //  station and containing TCPIP or TCPXX in the 3rd-party
 //  header (in other words, the station is seen on RF as being
 //  an IGate). 
 *
 * That is, this part of code collects knowledge of RF-wise near-by TX-IGATEs.
 */

void interface_receive_ax25(const struct aprx_interface *aif,
			    const char *ifaddress, const int is_aprs, const int ui_pid,
			    const uint8_t *axbuf, const int axaddrlen, const int axlen,
			    const char    *tnc2buf, const int tnc2addrlen, const int tnc2len)
{
	int i;
	int digi_like_aprs = is_aprs;

	if (aif == NULL) return;         // Not a real interface for digi use
	if (aif->digisourcecount == 0) {
	  if (debug>1) printf("interface_receive_ax25() no receivers for source %s\n",aif->callsign);
	  return; // No receivers for this source
	}

	if (debug) printf("interface_receive_ax25() from %s axlen=%d tnc2len=%d\n",aif->callsign,axlen,tnc2len);


	if (axaddrlen <= 14) return;     // SOURCE>DEST without any VIAs..
	// Note: Above one disables MICe destaddress-SSID embedded
	//       extremely compressed WIDEn-N notation.

// FIXME: match ui_pid to list of UI PIDs that are treated with similar
//        digipeat rules as is APRS New-N.

	// ui_pid < 0 means that this frame is not an UI frame at all.
	if (ui_pid >= 0)  digi_like_aprs = 1; // FIXME: more precise matching?


	for (i = 0; i < aif->digisourcecount; ++i) {
	    struct digipeater_source *digisource = aif->digisources[i];
#ifndef DISABLE_IGATE
	    historydb_t *historydb = digisource->parent->historydb;
#endif
	    char *p;
	    int tnc2infolen;

	    // Allocate pbuf, it is born "gotten" (refcount == 1)
	    struct pbuf_t *pb = pbuf_new(is_aprs, digi_like_aprs, axlen, tnc2len);
	    if (pb == NULL) {
	      // Urgh!  Can't do a thing to this!
	      // Likely reason: axlen+tnc2len  > 2100 bytes!
	      continue;
	    }
	    pb->source_if_group = aif->ifgroup;

	    memcpy((void*)(pb->data), tnc2buf, tnc2len);
	    pb->data[tnc2len] = 0;

	    pb->info_start = pb->data + tnc2addrlen + 1;
	    p = (char*)&pb->info_start[-1]; *p = 0;

	    p = pb->data;
	    for ( p = pb->data; p < (pb->info_start); ++p ) {
	      if (*p == '>') {
		pb->srccall_end = p;
		pb->destcall    = p+1;
		continue;
	      }
	      if (*p == ',' || *p == ':') {
		pb->dstcall_end = p;
		break;
	      }
	    }
	    if (pb->dstcall_end == NULL)
	      pb->dstcall_end = p;

	    tnc2infolen = tnc2len - tnc2addrlen -1; /* ":" */
	    p = (char*)&pb->info_start[tnc2infolen]; *p = 0;

	    // Copy incoming AX.25 frame into its place too.
	    memcpy(pb->ax25addr, axbuf, axlen);
	    pb->ax25addrlen = axaddrlen;
	    pb->ax25data    = pb->ax25addr + axaddrlen;
	    pb->ax25datalen = axlen - axaddrlen;

	    // If APRS packet, then parse for APRS meaning ...
	    if (is_aprs) {
		int rc = parse_aprs(pb, 0,
#ifndef DISABLE_IGATE
				    historydb
#else
				    NULL
#endif
				    ); // don't look inside 3rd party
		char *srcif = aif ? (aif->callsign ? aif->callsign : "??") : "?";
		if (debug)
		  printf(".. parse_aprs() rc=%s  srcif=%s  tnc2addr='%s'  info_start='%s'\n",
			 rc ? "OK":"FAIL", srcif, pb->data, pb->info_start);

		// If there are no filters, permit all packets
		if (digisource->src_filters != NULL) {
		  int filter_discard =
		    filter_process(pb,
				   digisource->src_filters,
#ifndef DISABLE_IGATE
				   historydb
#else
				   NULL
#endif
				   );
		  // filter_discard > 0: accept
		  // filter_discard = 0: indifferent (not reject, not accept), tx-igate rules as is.
		  // filter_discard < 0: reject
		  if (debug)
		    printf("source filtering result: %s\n",
			   (filter_discard < 0 ? "DISCARD" :
			    (filter_discard > 0 ? "ACCEPT" : "no-match")));

		  if (filter_discard <= 0) {
		    pbuf_put(pb);
		    continue; // allow only explicitly accepted
		  }
		}

#ifndef DISABLE_IGATE
		// Find out IGATE callsign (if any), and record it on historydb.
		if (pb->packettype & T_THIRDPARTY) {
		  rx_analyze_3rdparty( historydb, pb );
		} else {
		  // Everything else, feed to history-db
		  historydb_insert_heard( historydb, pb );
		}
#endif
	    }

	    // Feed it to digipeater ...
	    digipeater_receive( digisource, pb);

	    // .. and finally free up the pbuf (if refcount goes to zero)
	    pbuf_put(pb);
	}
}


/*
 * Process AX.25 packet transmit; beacons, digi output, igate output...
 *
 *   - aif:    output interface
 *   - axaddr: ax.25 address
 *   - axdata: payload content, with control and PID bytes prefixing them
 */

void interface_transmit_ax25(const struct aprx_interface *aif, uint8_t *axaddr, const int axaddrlen, const char *axdata, const int axdatalen)
{
	int axlen = axaddrlen + axdatalen;
	uint8_t *axbuf;

	if (debug) {
	  const char *callsign = "";
	  if (aif != NULL) callsign=aif->callsign;
	  printf("interface_transmit_ax25(aif=%p[%s], .., axlen=%d)\n",
		 aif, callsign, axlen);
	}
	if (axlen == 0) return;
	if (aif == NULL) return;


	switch (aif->iftype) {
	case IFTYPE_SERIAL:
	case IFTYPE_TCPIP:
		// If there is linetype error, kisswrite detects it.
		// Make it into single buffer to give to KISS sender
		axbuf = alloca(axlen);
		memcpy(axbuf, axaddr, axaddrlen);
		memcpy(axbuf + axaddrlen, axdata, axdatalen);
		kiss_kisswrite(aif->tty, aif->subif, axbuf, axlen);
		break;
#ifdef PF_AX25	/* PF_AX25 exists -- highly likely a Linux system ! */
	case IFTYPE_AX25:
		// The Linux netax25 sender takes same data as this interface
		netax25_sendto( aif->nax25p,
				axaddr, axaddrlen,
				axdata, axdatalen );
		break;
#endif
#ifdef ENABLE_AGWPE
	case IFTYPE_AGWPE:
		agwpe_sendto( aif->agwpe,
			      axaddr, axaddrlen,
			      axdata, axdatalen );
		break;
#endif
	case IFTYPE_NULL:
		// Efficient transmitter :-)
		
		// Account the transmission anyway ;-)
		erlang_add(aif->callsign, ERLANG_TX, axaddrlen+axdatalen + 10, 1);
		break;
	default:
		break;
	}
}

#ifndef DISABLE_IGATE
/*
 * Process received AX.25 packet  -- for APRSIS
 *   - from AIF do find all DIGIPEATERS wanting this source.
 *   - If there are none, end processing.
 *   - Parse the received frame for possible latter filters
 *   - Feed the resulting parsed packet to each digipeater
 *
 * See:  http://www.aprs-is.net/IGateDetails.aspx
 *
 *  Paths
 *
 * IGates should use the 3rd-party format on RF of
 * IGATECALL>APRS,GATEPATH}FROMCALL>TOCALL,TCPIP,IGATECALL*:original packet data
 * where GATEPATH is the path that the gated packet is to follow
 * on RF. This format will allow IGates to prevent gating the packet
 * back to APRS-IS.
 * 
 * q constructs should never appear on RF.
 * The I construct should never appear on RF.
 * Except for within gated packets, TCPIP and TCPXX should not be
 * used on RF.
 *
 * Part of the Tx-IGate logic is here because we use pbuf_t data blocks:
 *
 *  1) The receiving station has been heard recently
 *     within defined range limits, and more recently
 *     than since given interval T1. (Range as digi-hops [N1]
 *     or coordinates, or both.)
 *
 *  2) The sending station has not been heard via RF
 *     within timer interval T2. (Third-party relayed
 *     frames are not analyzed for this.)
 *
 *  4) the receiving station has not been heard via the Internet
 *     within a predefined time period.
 *     A station is said to be heard via the Internet if packets
 *     from the station contain TCPIP* or TCPXX* in the header or
 *     if gated (3rd-party) packets are seen on RF gated by the
 *     station and containing TCPIP or TCPXX in the 3rd-party
 *     header (in other words, the station is seen on RF as being
 *     an IGate). 
 *
 * 5)  Gate all packets to RF based on criteria set by the sysop
 *     (such as callsign, object name, etc.).
 *
 * c)  Drop everything else.
 */

static uint8_t toaprs[7] =    { 'A'<<1,'P'<<1,'R'<<1,'S'<<1,' '<<1,' '<<1,0x60 };

void interface_receive_3rdparty(const struct aprx_interface *aif, const char *fromcall, const char *origtocall, const char *gwtype, const char *tnc2data, const int tnc2datalen)
{
	int d;
	struct pbuf_t *pb;
	const char *p;
	int first = 1;

	int tnc2addrlen;
	int tnc2len;
	char    *t;

	int ax25addrlen;
	int ax25len;
	uint8_t *a;
	
	char     tnc2buf[2800];
	uint8_t  ax25buf[2800];

	time_t recent_time = now - 3600; // "recent" = 1 hour



	if (debug)
	  printf("interface_receive_3rdparty() aif=%p, aif->digicount=%d\n",
		 aif, aif ? aif->digisourcecount : -1);


	if (aif == NULL) {
	  return;         // Not a real interface for digi use
	}
	if (aif->digisourcecount == 0) {
	  return; // No receivers for this source
	}

	// Feed it to digipeaters ...
	for (d = 0; d < aif->digisourcecount; ++d) {
	  struct digipeater_source *digisrc = aif->digisources[d];
	  struct digipeater        *digi    = digisrc->parent;
	  struct aprx_interface    *tx_aif  = digi->transmitter;
	  char *p2, *srcif;
	  int tnc2infolen, rc, discard_this, filter_discard;

	  historydb_t *historydb = digisrc->parent->historydb;

	  //   IGATECALL>APRS,GATEPATH:}FROMCALL>TOCALL,TCPIP,IGATECALL*:original packet data

	  // Parse the TNC2 format to AX.25 format
	  // using ax25buf[] storage area.
	  memcpy(ax25buf,    toaprs, 7);           // AX.25 DEST call

	  // FIXME: should this be IGATECALL, not tx_aif->ax25call ??
	  memcpy(ax25buf+7,  tx_aif->ax25call, 7); // AX.25 SRC call

	  a = ax25buf + 2*7;

	  if (digisrc->via_path != NULL) {
	    memcpy(a, digisrc->ax25viapath, 7);    // AX.25 VIA call
	    a += 7;
	  }

	  *(a-1) |= 0x01;                  // DEST,SRC(,VIA1) - end-of-address bit
	  ax25addrlen = a - ax25buf;

	  if (debug>2) {
	    printf("ax25hdr ");
	    hexdumpfp(stdout, ax25buf, ax25addrlen, 1);
	    printf("\n");
	  }

	  *a++ = 0x03; // UI
	  *a++ = 0xF0; // PID = 0xF0

	  a += sprintf((char*)a, "}%s>%s,%s,%s*:",
		       fromcall, origtocall, gwtype, tx_aif->callsign );
	  ax25len = a - ax25buf;
	  if (tnc2datalen + ax25len > sizeof(ax25buf)) {
	    // Urgh...  Can not fit it in :-(
	    if(debug)printf("data does not fit into ax25buf: %d > %d\n",
			    tnc2datalen+ax25len, (int)sizeof(ax25buf));
	    continue;
	  }
	  memcpy(a, tnc2data, tnc2datalen);
	  ax25len += tnc2datalen;

	  // AX.25 packet is built, now build TNC2 version of it
	  t = tnc2buf;
	  t += sprintf(t, "%s>%s", tx_aif->callsign, tocall);
	  if (digisrc->via_path != NULL) {
	    t += sprintf(t, ",%s", digisrc->via_path);
	  }
	  if (debug>1)printf(" tnc2addr = %s\n", tnc2buf);

	  tnc2addrlen = t - tnc2buf;
	  *t++ = ':';
	  t += sprintf(t, "}%s>%s,%s,%s*:",
		       fromcall, origtocall, gwtype, tx_aif->callsign );
	  if (tnc2datalen + (t-tnc2buf) +4 > sizeof(tnc2buf)) {
	    // Urgh...  Can not fit it in :-(
	    if(debug)printf("data does not fit into tnc2buf: %d > %d\n",
			    (int)(tnc2datalen+(t-tnc2buf)+4),
			    (int)sizeof(tnc2buf));
	    continue;
	  }
	  memcpy(t, tnc2data, tnc2datalen);
	  t += tnc2datalen;
	  tnc2len = (t - tnc2buf);

	  // Allocate pbuf, it is born "gotten" (refcount == 1)
	  pb = pbuf_new(1 /*is_aprs*/, 1 /* digi_like_aprs */, ax25len, tnc2len);
	  if (pb == NULL) {
	    // Urgh!  Can't do a thing to this!
	    // Likely reason: ax25len+tnc2len  > 2100 bytes!
	    continue;
	  }

	  pb->source_if_group = 0; // 3rd-party frames are always from APRSIS

	  memcpy((void*)(pb->data), tnc2buf, tnc2len);
	  pb->info_start = pb->data + tnc2addrlen + 1;
	  p2    = (char*)&pb->info_start[-1]; *p2 = 0;
	  srcif = aif ? (aif->callsign ? aif->callsign : "??") : "?";

	  tnc2infolen = tnc2len - tnc2addrlen -1; /* ":" */
	  p2 = (char*)&pb->info_start[tnc2infolen]; *p2 = 0;

	  p = pb->data;
	  for ( p = pb->data; p < (pb->info_start); ++p ) {
	    if (*p == '>') {
	      pb->srccall_end = p;
	      pb->destcall    = p+1;
	      continue;
	    }
	    if (*p == ',' || *p == ':') {
	      pb->dstcall_end = p;
	      break;
	    }
	  }
	  if (pb->dstcall_end == NULL)
	    pb->dstcall_end = p;

	  memcpy(pb->ax25addr, ax25buf, ax25len);
	  pb->ax25addrlen = ax25addrlen;
	  pb->ax25data    = pb->ax25addr + ax25addrlen;
	  pb->ax25datalen = ax25len - ax25addrlen;

	  // This is APRS packet, parse for APRS meaning ...
	  rc = parse_aprs(pb, 1, historydb); // look inside 3rd party
	  if (debug)
	    printf(".. parse_aprs() rc=%s  srcif=%s tnc2addr='%s'  info_start='%s'\n",
		   rc ? "OK":"FAIL", srcif, pb->data,
		   pb->info_start);

          // 1) - verify receiving station has been heard
          //      recently on radio
          // 2) - sending station has not been heard recently
          //      on radio
          // 4) - the receiving station has not been heard via
          //      the Internet within a predefined time period.
          //      (Note that _this_ packet is heard from internet,
          //      so one must not confuse this to history..
          //      Nor this siblings that are being created
          //      one for each tx-interface...)
	  // 
	  //  A station is said to be heard via the Internet if packets
	  //  from the station contain TCPIP* or TCPXX* in the header or
	  //  if gated (3rd-party) packets are seen on RF gated by the
	  //  station and containing TCPIP or TCPXX in the 3rd-party
	  //  header (in other words, the station is seen on RF as being
	  //  an IGate). 


	  // Message Tx-IGate rules..
	  discard_this = 0;

	  if (pb->recipient == NULL) {
	    // Sanity -- not a message..
	    discard_this = 1;
	  }
	  if ((pb->packettype & T_MESSAGE) == 0) {
	    // Not a message packet
	    discard_this = 1;
	  }
	  if ((pb->packettype & (T_NWS)) != 0) {
	    // Not a message packet
	    discard_this = 1;
	  }

	  if (!discard_this) {
	    // 1) - verify receiving station has been heard
	    //      recently on radio
	    int i;
	    char recipient[10];
	    history_cell_t *hist_rx;
	    strncpy(recipient, pb->recipient, sizeof(recipient)-1);
	    recipient[9] = 0; // Zero-terminate at 10 chars
	    for (i = 0; i < 10; ++i) {
	      if (recipient[i] == ' ')
		recipient[i] = 0; // Zero-terminate
	    }

// FIXME?  Should test all SSIDs of this target callsign, not just this one target,
//         if this is a T_MESSAGE!  (strange BoB rules...)

	    hist_rx = historydb_lookup(historydb, recipient, strlen(recipient));
	    if (hist_rx == NULL) {
	      if (debug) printf("No history entry for receiving call: '%s'  DISCARDING.\n", pb->recipient);
	      discard_this = 1;
	    }
	    // See that it has 'heard on radio' flag on this tx interface
	    if (hist_rx != NULL && discard_this == 0) {
	      if (hist_rx->last_heard[tx_aif->ifgroup] >= recent_time) {
		// Heard recently enough
		discard_this = 0;
		if (debug) printf("History entry for receiving call '%s' from RADIO is not recent enough.  DISCARDING.\n", pb->recipient);
	      }
	    }

	    // FIXME: Check that recipient is in our service area
	    //        a) coordinate is "near by"
	    //        b) last known hop-count is low enough
	    //           (FIXME: RF hop-count recording infra needed!)

	    // 4) the receiving station has not been heard via the internet
	    if (hist_rx != NULL && hist_rx->last_heard[0] > recent_time) {
	      // "is heard recently via internet"
	      discard_this = 1;
	      if (debug) printf("History entry for sending call '%s' from APRSIS is too new.  DISCARDING.\n", fromcall);
	    }
	  }

	  if (!discard_this) {
	    history_cell_t *hist_tx = historydb_lookup(historydb, fromcall, strlen(fromcall));
	    // If no history entry for this tx callsign,
	    // then rules 2 and 4 permit tx-igate
	    if (hist_tx != NULL) {
	      // There is a history entry for this tx callsign, check rules 2+4
	      // 2) Sending station has not been heard recently on radio (this target)
	      if (hist_tx->last_heard[tx_aif->ifgroup] > recent_time) {
		// "is heard recently"
		discard_this = 1;
		if (debug) printf("History entry for sending call '%s' from RADIO is too new.  DISCARDING.\n", fromcall);
	      }
	    }
	  }

	  // Accept/Reject the packet by digipeater rx filter?
	  filter_discard = 0;
	  if (digisrc->src_filters == NULL) {
	    // No filters defined, default Tx-iGate rules apply
	  } else {
	    filter_discard = filter_process(pb, digisrc->src_filters, digisrc->parent->historydb);
	  }
	  // filter_discard > 0: accept
	  // filter_discard = 0: indifferent (not reject, not accept), tx-igate rules as is.
	  // filter_discard < 0: reject

	  // Manual filter says: Accept!
	  if (discard_this && filter_discard > 0)
	    discard_this = 0;
	  // Manual filter says: Discard!
	  if (filter_discard < 0)
	    discard_this = 1;

	  if (first) {
	    // Stores position, and message references
	    historydb_insert_heard( digi->historydb, pb );
	    //if (debug) printf("historydb_insert_heard(APRSIS)\n");
	  }
	  first = 0;

	  if (!discard_this) {
	    // Not discarding - approved for transmission

	    if ((pb->packettype & T_POSITION) == 0) {
	      // TODO: For position-less packets send at first a position packet
	      //       for same source call sign -- if available.
	      
	    }

	    digipeater_receive( digisrc, pb);
	  }

	  // .. and finally free up the pbuf (if refcount goes to 0)
	  pbuf_put(pb);
	}
}
#endif

/*
 * Process transmit of APRS beacons
 *
 * Note:  txbuf  starts if AX.25 Control+PID bytes!
 */

int interface_transmit_beacon(const struct aprx_interface *aif, const char *src, const char *dest, const char *via, const char *txbuf, const int txlen)
{
	uint8_t ax25addr[90];
	int     ax25addrlen;
	int	have_fault = 0;
	int	viaindex   = 1; // First via field will be index 2
	char    axaddrbuf[128];
	char    *a = axaddrbuf;
	dupecheck_t *dupechecker;

	if (debug)
	  printf("interface_transmit_beacon() aif=%p, aif->txok=%d aif->callsign='%s'\n",
		 aif, aif ? aif->txok : 0, aif ? aif->callsign : "<nil>");

	if (aif == NULL)    return 0;
	if (aif->txok == 0) return 0; // Sorry, no Tx

	dupechecker = digipeater_find_dupecheck(aif);

	// _FOR_VALGRIND_  -- and just in case for normal use
	memset(ax25addr, 0, sizeof(ax25addr));
	memset(axaddrbuf, 0, sizeof(axaddrbuf));
	
	if (parse_ax25addr(ax25addr +  7, src,  0x60)) {
	  if (debug) printf("parse_ax25addr('%s') failed.\n", src);
	  return -1;
	}
	if (parse_ax25addr(ax25addr +  0, dest, 0x60)) {
	  if (debug) printf("parse_ax25addr('%s') failed.\n", dest);
	  return -1;
	}
	ax25addrlen = 14; // Initial Src+Dest without any Via.

	a += sprintf(axaddrbuf, "%s>%s", src, dest);
	*a = 0;

	if (via != NULL) {
	  char    viafield[12];
	  const char *s, *p = via;
	  const char *ve = via + strlen(via);

	  *a++ = ',';
	  strncpy(a, via, sizeof(axaddrbuf)-(a-axaddrbuf)-3);
	  axaddrbuf[sizeof(axaddrbuf)-3] = 0; // just in case do zero-terminate..

	  while (p < ve) {
	    int len;
	    
	    for (s = p; s < ve; ++s) {
	      if (*s == ',') {
		break;
	      }
	    }
	    // [p..s] is now one VIA field.
	    if (s == p) {  // BAD!
	      have_fault = 1;
	      if (debug>1) printf(" S==P ");
	      break;
	    }
	    ++viaindex;
	    if (viaindex >= 10) {
	      if (debug) printf("too many via-fields: '%s'\n", via);
	      return -1; // Too many VIA fields
	    }

	    len = s - p;
	    if (len >= sizeof(viafield)) len = sizeof(viafield)-1;
	    memcpy(viafield, p, len);
	    viafield[len] = 0;
	    if (*s == ',') ++s;
	    p = s;
	    // VIA-field picked up, now parse it..

	    if (parse_ax25addr(ax25addr + viaindex * 7, viafield, 0x60)) {
	      // Error on VIA field value
	      if (debug) printf("parse_ax25addr('%s') failed.\n", viafield);
	      return -1;
	    }
	    ax25addrlen += 7;
	  }
	}

	ax25addr[ax25addrlen-1] |= 0x01; // set address field end bit


	// Feed to dupe-filter (transmitter specific)
	// this means we have already seen it, and when 
	// it comes back from somewhere, we do not digipeat
	// it ourselves.

	if (dupechecker != NULL)
	  dupecheck_aprs( dupechecker,
			  axaddrbuf, strlen(axaddrbuf),
			  txbuf+2, txlen-2  ); // ignore Ctrl+PID

	// Transmit it to actual radio interface

	interface_transmit_ax25( aif,
				 ax25addr, ax25addrlen,
				 txbuf, txlen);


	if (rflogfile) {
	  int     axlen;
	  char    *axbuf;

	  axlen = txlen + strlen(axaddrbuf);
	  axbuf = alloca(axlen+3);
	  strcpy( axbuf, axaddrbuf );
	  a = axbuf + strlen(axbuf);
	  *a++ = ':';
	  memcpy(a, txbuf+2, txlen-2); // forget control+pid bytes..
	  a += txlen -2;   // final assembled message end pointer

	  rflog(aif->callsign, 1, 0, axbuf, a - axbuf);
	}

	return 0;
}
