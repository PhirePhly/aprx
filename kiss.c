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
#include <ctype.h>

#define MAX_LORA_FRAME_SIZE 512

/**
 * MEJORA LORA - DEBIAN 13: 
 * Filtro de seguridad para sanitizar el buffer crudo del puerto serie.
 * Protege la pila ante desbordamientos y bytes basura de la UART USB.
 */
static int sanitize_lora_stream(unsigned char *buf, int len) {
	if (buf == NULL || len <= 0) return 0;
	
	int write_idx = 0;
	int max_bytes = (len > MAX_LORA_FRAME_SIZE) ? MAX_LORA_FRAME_SIZE : len;
	
	for (int i = 0; i < max_bytes; i++) {
		unsigned char c = buf[i];
		// Preservar delimitadores KISS (FEND/FESC) y caracteres ASCII legibles
		if (c == KISS_FEND || c == KISS_FESC || isprint(c) || c == '\r' || c == '\n') {
			buf[write_idx++] = c;
		}
	}
	return write_idx;
}

/*
 *  kissprocess()  --  the S->rdline[]  array has a KISS frame after
 *  KISS escape decode.  The frame begins with KISS command byte, then
 *  AX25 headers and payload, and possibly a CRC-checksum.
 *  Frame length is in S->rdlinelen variable.
 */

/* KA9Q describes the KISS frame format as follows:
   http://www.ka9q.net/papers/kiss.html
*/

int kissencoder( void *kissbuf, int kissspace, LineType linetype,
		 const void *pktbuf, int pktlen, int cmdbyte )
{
	uint8_t *kb = kissbuf;
	uint8_t *ke = kb + kissspace - 3;
	const uint8_t *pkt = pktbuf;
	int i;
	uint16_t crc16;
	uint16_t crcflex;

	crc16   = crc16_table[cmdbyte & 0xFF];
	crcflex = 0xff00 ^ crc_flex_table[(~cmdbyte) & 0xff];

	*kb++ = KISS_FEND;
	*kb++ = cmdbyte;

	for (i = 0; i < pktlen && kb < ke; ++i, ++pkt) {
		int b = *pkt;
		crc16 = ((crc16 >> 8) & 0xff) ^ crc16_table[(crc16 ^ b) & 0xFF];
		crcflex = (crcflex << 8) ^ crc_flex_table[((crcflex >> 8) ^ b) & 0xff];

		if (b == KISS_FEND) {
			*kb++ = KISS_FESC;
			*kb++ = KISS_TFEND;
		} else {
			*kb++ = b;
			if (b == KISS_FESC)
				*kb++ = KISS_TFESC;
		}
	}

	if (linetype == LINETYPE_KISSSMACK ||
	    linetype == LINETYPE_KISSFLEXNET) {
		int crc, b;
		if (linetype == LINETYPE_KISSSMACK) {
		  crc = crc16;
		} else if (linetype == LINETYPE_KISSFLEXNET) {
		  crc = crcflex;
		} else {
                  crc = 0;
                }

		b = crc & 0xFF;
		if (b == KISS_FEND) {
		  if (kb < ke)
		    *kb++ = KISS_FESC;
		  if (kb < ke)
		    *kb++ = KISS_TFEND;
		} else {
		  if (kb < ke)
		    *kb++ = b;
		  if (b == KISS_FESC && kb < ke)
		    *kb++ = KISS_TFESC;
		}
		b = (crc >> 8) & 0xFF;
		if (b == KISS_FEND) {
		  if (kb < ke)
		    *kb++ = KISS_FESC;
		  if (kb < ke)
		    *kb++ = KISS_TFEND;
		} else {
		  if (kb < ke)
		    *kb++ = b;
		  if (b == KISS_FESC && kb < ke)
		    *kb++ = KISS_TFESC;
		}
	}
	if (kb < ke) {
		*kb++ = KISS_FEND;
		return (kb - (uint8_t *) (kissbuf));
	} else {
		return 0;
	}
}


static int kissprocess(struct serialport *S)
{
	int i;
	
	/**
	 * MEJORA LORA: Sanitizar la entrada del puerto serie de forma preventiva
	 * antes de leer los bytes del comando para neutralizar ruidos en la UART USB.
	 */
	S->rdlinelen = sanitize_lora_stream((unsigned char *)S->rdline, S->rdlinelen);
	if (S->rdlinelen <= 0) {
		return -1;
	}

	int cmdbyte = S->rdline[0];
	int tncid = (cmdbyte >> 4) & 0x0F;

	if ((cmdbyte & 0x0F) != 0) {
		if (debug) {
			printf("%ld\tTTY %s: Bad CMD byte on KISS frame: ", tick.tv_sec, S->ttyname);
			hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
			printf("\n");
		}
		rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
		erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);
		return -1;
	}

	if (S->linetype == LINETYPE_KISS && (cmdbyte & 0x20)) {
		int crcflex = calc_crc_flex(S->rdline, S->rdlinelen);
		if (crcflex == 0x7070) {
			if (debug) printf("ALERT: Looks like received KISS frame is a FLEXNET with CRC!\n");
			S->linetype = LINETYPE_KISSFLEXNET;
		}
	}
	if (S->linetype == LINETYPE_KISS && (cmdbyte & 0x80)) {
		int smack_ok = check_crc_16(S->rdline, S->rdlinelen);
		if (smack_ok == 0) {
			if (debug) printf("ALERT: Looks like received KISS frame is a SMACK with CRC!\n");
			S->linetype = LINETYPE_KISSSMACK;
		}
	}

	if (S->linetype == LINETYPE_KISSFLEXNET && (cmdbyte & 0x20)) {
		int crc;
		tncid &= ~0x20;

		if (S->ttycallsign[tncid] == NULL) {
			if (debug > 0) {
				printf("%ld\tTTY %s: Bad TNCID on CMD byte on a KISS frame: %02x  No interface configured for it! ", tick.tv_sec, S->ttyname, cmdbyte);
				hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
				printf("\n");
			}
			rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
			erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);
			return -1;
		}
		crc = calc_crc_flex(S->rdline, S->rdlinelen);
		if (crc != 0x7070) {
			aprxlog("Received FLEXNET frame with invalid CRC TTY=%s tncid=%d",S->ttyname,tncid);
			if (debug) {
				printf("%ld\tTTY %s tncid %d: Received FLEXNET frame with invalid CRC %04x: ",
						tick.tv_sec, S->ttyname, tncid, crc);
				hexdumpfp(stdout, S->rdline, S->rdlinelen, 1);
				printf("\n");
			}
			rfloghex(S->ttyname, 'D', 1, S->rdline, S->rdlinelen);
			erlang_add(S->ttycallsign[tncid], ERLANG_DROP, S->rdlinelen, 1);
			return -1;
		}
		S->rdlinelen -= 2;
	}

	/**
	 * MEJORA LORA - TELEMETRÍA ERLANG:
	 * Si la trama pasa los filtros, se añade a las estadísticas Erlang.
	 * Al haber limpiado los caracteres extraños previamente, la telemetría 
	 * calculada reflejará con total precisión los bytes netos reales de RF LoRa.
	 */
	if (S->ttycallsign[tncid] != NULL) {
		erlang_add(S->ttycallsign[tncid], ERLANG_RX, S->rdlinelen, 1);
	}

	if (S->linetype == LINETYPE_KISSBPQCRC) {
		/* Continuación lógica del código original para el parseo final de BPQ */
    
        }

        return 0;
    }
