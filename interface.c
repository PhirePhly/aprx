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


struct aprx_interface aprsis_interface = {
	IFTYPE_APRSIS, 0, "APRSIS",
	{'A'<<1,'P'<<1,'R'<<1,'S'<<1,'I'<<1,'S'<<1, 0x60},
	0, NULL,
	0, 0, 0, NULL,
	NULL, NULL,
	0, NULL
};

static char *interface_default_aliases[] = { "RELAY","WIDE","TRACE" };

static void interface_store(struct aprx_interface *aif)
{
  if (debug)
    printf("interface_store() aif->callsign = '%s'\n", aif->callsign);

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
	int   aliascount = 0;
	char **aliases   = NULL;

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

		  if (strcmp(param1,"$mycall") == 0)
		    param1 = strdup(mycall);

		  if (!validate_callsign_input(param1,txok)) {
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

		} else if (strcmp(name, "alias") == 0) {
		  char *k = strtok(param1, ",");
		  for (; k ; k = strtok(NULL,",")) {
		    ++aliascount;
		    if (debug) printf(" n=%d alias='%s'\n",aliascount,k);
		    aliases = realloc(aliases, sizeof(char*) * aliascount);
		    aliases[aliascount-1] = strdup(k);
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
	parse_ax25addr((*aifp)->ax25call, callsign, 0x60);
	(*aifp)->subif    = subif;
	(*aifp)->txok     = txok;

	aif->tty->interface  [subif] = *aifp;
	aif->tty->ttycallsign[subif] = callsign;
	aif->tty->netax25    [subif] = netax25_open(callsign);

	if (aliascount == 0) {
	  aif->aliascount = 3;
	  aif->aliases    = interface_default_aliases;
	} else {
	  aif->aliascount = aliascount;
	  aif->aliases    = aliases;
	}

	interface_store(*aifp);

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

void interface_config(struct configfile *cf)
{
	struct aprx_interface *aif = calloc(1, sizeof(*aif));
	struct aprx_interface *aif2 = NULL; // subif copies here..

	char  *name, *param1;
	char  *str = cf->buf;
	int    parlen     = 0;

	int  maxsubif = 16;  // 16 for most KISS modes, 8 for SMACK

	aif->iftype = IFTYPE_UNSET;
	aif->aliascount = 3;
	aif->aliases    = interface_default_aliases;

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

		  if (strcmp(param1,"$mycall") == 0)
		    param1 = strdup(mycall);

		  if (!validate_callsign_input(param1,1)) {
		    printf("%s:%d The CALLSIGN parameter on AX25-DEVICE must be of valid AX.25 format! '%s'\n",
			   cf->name, cf->linenum, param1);
		    continue;
		  }

		  if (debug)
		    printf("%s:%d: AX25-DEVICE '%s' '%s'\n",
			   cf->name, cf->linenum, param1, str);

		  netax25_addrxport(param1, str, aif);
		  aif->callsign = strdup(param1);
		  parse_ax25addr(aif->ax25call, aif->callsign, 0x60);
		  
		  interface_store(aif);

		} else if ((strcmp(name,"serial-device") == 0) && (aif->tty == NULL)) {
		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype              = IFTYPE_SERIAL;
		    aif->callsign            = strdup(mycall);
		    parse_ax25addr(aif->ax25call, aif->callsign, 0x60);
		    aif->tty                 = ttyreader_new();
		    aif->tty->ttyname        = strdup(param1);
		    aif->tty->interface[0]   = aif;
		    aif->tty->ttycallsign[0] = mycall;

		    ttyreader_register(aif->tty);
		    interface_store(aif);

		  } else {
		    printf("%s:%d Only single device specification per interface block!\n",
			   cf->name, cf->linenum);
		    continue;
		  }

		  if (debug)
		    printf(".. new style serial:  '%s' '%s'.. tncid=0 callsign=%s\n",
			   aif->tty->ttyname, str, aif->callsign);

		  ttyreader_parse_ttyparams(cf, aif->tty, str);


		  
		} else if ((strcmp(name,"tcp-device") == 0) && (aif->tty == NULL)) {
		  int len;
		  char *host, *port;

		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_TCPIP;
		    aif->tty = ttyreader_new();
		    aif->tty->interface[0] = aif;
		    aif->tty->ttycallsign[0]  = mycall;

		    ttyreader_register(aif->tty);
		    interface_store(aif);

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
		    if (!validate_callsign_input(aif->callsign,aif->txok)) {  // Transmitters REQUIRE valid AX.25 address
		      printf("%s:%d: TX-OK 'TRUE' -- BUT PREVIOUSLY SET CALLSIGN IS NOT VALID AX.25 ADDRESS \n",
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

		} else if (strcmp(name, "callsign") == 0) {
		  if (strcmp(param1,"$mycall") == 0)
		    param1 = strdup(mycall);

		  if (!validate_callsign_input(param1,aif->txok)) {
		    if (aif->txok)
		      printf("%s:%d The CALLSIGN parameter on AX25-DEVICE must be of valid AX.25 format! '%s'\n",
			     cf->name, cf->linenum, param1);
		    else
		      printf("%s:%d The CALLSIGN parameter on AX25-DEVICE must be of valid APRSIS format! '%s'\n",
			     cf->name, cf->linenum, param1);
		    break;
		  }
		  aif->callsign = strdup(param1);
		  parse_ax25addr(aif->ax25call, aif->callsign, 0x60);
		  if (aif->tty != NULL)
		    aif->tty->ttycallsign[0] = aif->callsign;

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

		} else {
		  printf("%s:%d Unknown config entry name: '%s'\n",
			 cf->name, cf->linenum, name);

		}
	}
}


/*
 * Process received AX.25 packet
 *   - from AIF do find all DIGIPEATERS wanting this source.
 *   - If there are none, end processing.
 *   - Parse the received frame for possible latter filters
 *   - Feed the resulting parsed packet to each digipeater
 *
 */

void interface_receive_ax25(const struct aprx_interface *aif,
			    const char *ifaddress, int is_aprs, int ui_pid,
			    const unsigned char *axbuf, const int axaddrlen, const int axlen,
			    const char *tnc2buf, const int tnc2addrlen, const int tnc2len)
{
	int i;
	int digi_like_aprs = is_aprs;

	if (debug) printf("interface_receive_ax25()\n");

	if (aif == NULL) return;         // Not a real interface for digi use
	if (aif->digicount == 0) return; // No receivers for this source

	if (axaddrlen <= 14) return;     // SOURCE>DEST without any VIAs..
	// Note: Above one disables MICe destaddress-SSID embedded
	//       extremely compressed WIDEn-N notation.

// FIXME: match ui_pid to list of UI PIDs that are treated with similar
//        digipeat rules as is APRS New-N.

	// ui_pid < 0 means that this frame is not an UI frame at all.
	if (ui_pid >= 0)  digi_like_aprs = 1; // FIXME: more precise matching?


	// Allocate pbuf, it is born "gotten" (refcount == 1)
	struct pbuf_t *pb = pbuf_new(is_aprs, digi_like_aprs, axlen, tnc2len);

	memcpy((void*)(pb->data), tnc2buf, tnc2len);
	pb->data[tnc2len] = 0;

	pb->info_start = pb->data + tnc2addrlen + 1;
	char *p = (char*)&pb->info_start[-1]; *p = 0;

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

	int tnc2infolen = tnc2len - tnc2addrlen -3; /* ":" +  "\r\l" */
	p = (char*)&pb->info_start[tnc2infolen]; *p = 0;

	// Copy incoming AX.25 frame into its place too.
	memcpy(pb->ax25addr, axbuf, axlen);
	pb->ax25addrlen = axaddrlen;
	pb->ax25data    = pb->ax25addr + axaddrlen;
	pb->ax25datalen = axlen - axaddrlen;

	// If APRS packet, then parse for APRS meaning ...
	if (is_aprs) {
		int rc = parse_aprs(pb);
		char *srcif = aif ? (aif->callsign ? aif->callsign : "??") : "?";
		printf(".. parse_aprs() rc=%s  srcif=%s  tnc2addr='%s'  info_start='%s'\n",
		       rc ? "OK":"FAIL", srcif, pb->data, pb->info_start);
	}


	// Feed it to digipeaters ...
	for (i = 0; i < aif->digicount; ++i) {
		digipeater_receive( aif->digipeaters[i], pb);
	}

	// .. and finally free up the pbuf (if refcount goes to zero)
	pbuf_put(pb);
}

/*
 * Process AX.25 packet transmit; beacons, digi output, igate output...
 *
 */

void interface_transmit_ax25(const struct aprx_interface *aif, const unsigned char *axaddr, const int axaddrlen, const unsigned char *axdata, const int axdatalen)
{
	int axlen = axaddrlen + axdatalen;
	unsigned char *axbuf = alloca(axlen);
	memcpy(axbuf, axaddr, axaddrlen);
	memcpy(axbuf + axaddrlen, axdata, axdatalen);

	if (debug)
	  printf("interface_transmit_ax25(aif=%p, .., axlen=%d)\n",aif,axlen);


	if (aif == NULL) return;

	switch (aif->iftype) {
	case IFTYPE_SERIAL:
	case IFTYPE_TCPIP:
		// If there is linetype error, kisswrite detects it.
		ttyreader_kisswrite(aif->tty, aif->subif, axbuf, axlen);
		break;
	case IFTYPE_AX25:
		netax25_sendto(aif->nax25p, axbuf, axlen);
		break;
	default:
		break;
	}
}

/*
 * Process received AX.25 packet  -- for APRSIS
 *   - from AIF do find all DIGIPEATERS wanting this source.
 *   - If there are none, end processing.
 *   - Parse the received frame for possible latter filters
 *   - Feed the resulting parsed packet to each digipeater
 *
 */

void interface_receive_tnc2(const struct aprx_interface *aif, const char *ifaddress, const char *tnc2buf, const int tnc2len)
{
	int i;
	struct pbuf_t *pb;
	const char *p;
	int tnc2addrlen = 0;
	int ax25addrlen, ax25len = tnc2len; // A bit less, usually
	unsigned char ax25buf[2800];

	if (debug)
	  printf("interface_receive_tnc2() aif=%p, aif->digicount=%d\n",
		 aif, aif ? aif->digicount : -1);


	if (aif == NULL) return;         // Not a real interface for digi use
	if (aif->digicount == 0) return; // No receivers for this source

	p = memchr(tnc2buf, ':', tnc2len);
	if (p != NULL) {
		tnc2addrlen = p - tnc2buf;
	} else {
		// Bad TNC2 packet, no ':' in it..
		if (debug)
		  printf("Not found ':' in TNC2 buffer: '%*s'\n",
			 tnc2len, tnc2buf);
		return;
	}

	// FIXME: Parse the TNC2 format to AX.25 format
	//        using ax25buf[] storage area.

	// Allocate pbuf, it is born "gotten" (refcount == 1)
	pb = pbuf_new(1 /*is_aprs*/, 1 /* digi_like_aprs */, 0, tnc2len);

	memcpy((void*)(pb->data), tnc2buf, tnc2len);
	pb->info_start = pb->data + tnc2addrlen + 1;
	char *p2    = (char*)&pb->info_start[-1]; *p2 = 0;
	char *srcif = aif ? (aif->callsign ? aif->callsign : "??") : "?";

	int tnc2infolen = tnc2len - tnc2addrlen -3; /* ":" +  "\r\l" */
	p2 = (char*)&pb->info_start[tnc2infolen]; *p2 = 0;

	p = pb->data;
	for ( p = pb->data; p < (pb->info_start); ++p ) {
		if (*p == '>') {
			pb->srccall_end = p;
			pb->destcall    = p+1;
			continue;
		}
		if (*p == ',') {
			pb->dstcall_end = p;
			break;
		}
	}

	// memcpy(pb->ax25addr, axbuf, axlen);
	// pb->ax25data    = pb->ax25addr + ax25addrlen;
	// pb->ax25datalen = ax25len - ax25addrlen;

	// This is APRS packet, parse for APRS meaning ...
	int rc = parse_aprs(pb);
	printf(".. parse_aprs() rc=%s  srcif=%s tnc2addr='%s'  info_start='%s'\n",
	       rc ? "OK":"FAIL", srcif, pb->data,
	       pb->info_start);

	// Feed it to digipeaters ...
	for (i = 0; i < aif->digicount; ++i) {
		digipeater_receive( aif->digipeaters[i], pb);
	}

	// .. and finally free up the pbuf (if refcount goes to 0)
	pbuf_put(pb);
}

/*
 * Process transmit of AX.25 packet in TNC2 form  -- for beacons
 *
 */

void interface_transmit_tnc2(const struct aprx_interface *aif, const char *txbuf, const int txlen)
{
	if (debug)
	  printf("interface_transmit_tnc2() aif=%p, aif->txok=%d aif->callsign='%s'\n",
		 aif, aif ? aif->txok : 0, aif ? aif->callsign : "<nil>");

	if (aif == NULL) return;
}
