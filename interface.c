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


typedef enum {
	IFTYPE_UNSET,
	IFTYPE_SERIAL,
	IFTYPE_AX25,
	IFTYPE_TCPIP
} iftype_e;

struct aprx_interface {
	iftype_e    iftype;
	char       *callsign;
	int         txok;

	const void *nax25p;
	const void *serial;
};

struct aprx_interface *tx_interfaces;
int                    tx_interfaces_count;

struct aprx_interface *all_interfaces;
int                    all_interfaces_count;


static int config_kiss_subif(struct configfile *cf, struct aprx_interface *aif, char *param1, char *str, int maxsubif, int *subif, int *txokp, char **callsignpp)
{
	char *name;
	*subif = 0;
	for ( ; *param1; ++param1 ) {
		int c = *param1;
		if ('0' <= c && c <= '9') {
			*subif = *subif * 10 + (c - '0');
		} else if (c == '>') {
		  // all fine..
		} else {
		  // FIXME: <KISS-SubIF nnn>  parameter value is bad!
		  return 1;
		}
	}
	if (*subif >= maxsubif) {
		// FIXME: <KISS-SubIF nnn>  parameter value is bad!
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
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		if (strcmp(name, "</kiss-subif>") == 0) {
		  break; // End of this sub-group
		}

		// FIXME:   callsign   and   tx-ok   parameters!
		if (strcmp(name, "callsign") == 0) {
		  if (validate_callsign_input(param1,1)) {
		    // FIXME: Complain of bad input parameter!
		    return 1;
		  }
		  *callsignpp = strdup(param1);
		} else if (strcmp(name, "tx-ok") == 0) {
		  if (!config_parse_boolean(param1, txokp)) {
		    // FIXME: Bad value
		    return 1;
		  }
		} else {
		  // FIXME: Bad keyword
		  return 1;
		}
	}
	return 0;
}

void config_interface(struct configfile *cf)
{
	struct aprx_interface *aif = calloc(1, sizeof(*aif));
	aif->iftype = IFTYPE_UNSET;

	char *name, *param1;
	char *str = cf->buf;

	int pbuflen = 1024;
	char *pbuf = malloc(pbuflen);
	char *callsign = NULL;
	int  txok = 0;
	int  subif = 0;
	int  maxsubif = 16;  // 16 for most KISS modes, 8 for SMACK


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

		if (strcmp(name, "</interface>") == 0) {
		  // End of this interface definition

		  // make the interface...

		  break;
		}
		if (strcmp(name, "<kiss-subif") == 0) {
		  callsign = NULL;
		  subif = 0; txok = 0;
		  if (config_kiss_subif(cf, aif, param1, str, maxsubif, &subif, &txok, &callsign)) {
		    // FIXME: bad inputs.. complained already
		  }
		  // FIXME: store results

		  continue;
		}

		// FIXME: interface parameters

		if (strcmp(name,"ax25-rxport") == 0) {
		  if (debug)
		    printf("%s:%d: AX25-RXPORT '%s' '%s'\n",
			   cf->name, cf->linenum, param1, str);

		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_AX25;
		    // aif->nax25p = NULL;
		    netax25_addrxport(param1, str);

		  } else {
		    // FIXME: Error!  Only single interface per interface block!
		  }
		  
		} else if (strcmp(name,"ax25-device") == 0 && aif->callsign == NULL) {
		  if (debug)
		    printf("%s:%d: AX25-DEVICE '%s' '%s'\n",
			   cf->name, cf->linenum, param1, str);

		  if (validate_callsign_input(param1,1)) {
		    // FIXME: Complain about bad callsign
		    continue;
		  }

		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_AX25;
		    // aif->nax25p = NULL;
		    netax25_addrxport(param1, str);
		    aif->callsign = strdup(param1);

		  } else {
		    // FIXME: Error!  Only single interface per interface block!
		  }
		} else if (strcmp(name,"tx-ok") == 0) {

		  if (!config_parse_boolean(param1, &(aif->txok))) {
		    // FIXME: Bad value
		    continue;
		  }
		  if (aif->txok && aif->callsign) {
		    if (validate_callsign_input(aif->callsign,aif->txok)) {  // Transmitters REQUIRE valid AX.25 address
		      printf("%s:%d: TX-OK 'TRUE -- BUT PREVIOUSLY SET CALLSIGN IS NOT VALID AX.25 ADDRESS \n",
			     cf->name, cf->linenum);
		      continue;
		    }
		  }

		} else if ((strcmp(name,"callsign") == 0) && (aif->callsign == NULL)) {

		  if (validate_callsign_input(param1,aif->txok)) { // Transmitters REQUIRE valid AX.25 address
		    printf("%s:%d: CALLSIGN '%s -- IS NOT VALID AX.25 ADDRESS, AS REQUIRED ON TRANSMITTER DEVICES\n",
			   cf->name, cf->linenum, param1);
		    continue;
		  }
		  aif->callsign = strdup(param1);

		} else if ((strcmp(name,"serial-device") == 0) && (aif->serial == NULL)) {
		  // FIXME: Parse serial device

		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_SERIAL;

		  } else {
		    // FIXME: Error!  Only single interface per interface block!
		  }
		  
		} else if ((strcmp(name,"tcp-device") == 0) && (aif->serial == NULL)) {
		  // FIXME: Parse tcp device

		  if (aif->iftype == IFTYPE_UNSET) {
		    aif->iftype = IFTYPE_TCPIP;

		  } else {
		    // FIXME: Error!  Only single interface per interface block!
		  }

		} else if (strcmp(name,"initstring") == 0) {
		} else if (strcmp(name,"timeout") == 0) {
		} else {
		}
	}
}
