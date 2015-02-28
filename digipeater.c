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

static int digi_count;
static struct digipeater **digis;

#define TOKENBUCKET_INTERVAL 5  // 5 seconds per refill.
                                // 60/5 part of "ratelimit" to be max
                                // that token bucket can be filled to.

static struct timeval tokenbucket_timer;

struct viastate {
        int hopsreq;
        int hopsdone;
        int tracereq;
        int tracedone;
        int digireq;
        int digidone;

        int fixthis;
        int fixall;
        int probably_heard_direct;
};

struct digistate {
        struct viastate v;

        int     ax25addrlen;
        uint8_t ax25addr[90]; // 70 for address, a bit more for "body"
};


#define AX25ADDRMAXLEN 70  // SRC + DEST + 8*VIA (7 bytes each)
#define AX25ADDRLEN  7
#define AX25HBIT  0x80
#define AX25ATERM 0x01

static char * tracewords[] = { "WIDE","TRACE","RELAY" };
static int tracewordlens[] = { 4, 5, 5 };
static const struct tracewide default_trace_param = {
        4, 4, 1,
        3,
        tracewords,
        tracewordlens
};
static char * widewords[] = { "WIDE","RELAY" };
static int widewordlens[] = { 4,5 };
static const struct tracewide default_wide_param = {
        4, 4, 0,
        2,
        widewords,
        widewordlens
};

static int  run_tokenbucket_timers(void);


float ratelimitmax     = 9999999.9;
float rateincrementmax = 9999999.9;


/*
 * regex_filter_add() -- adds configured regular expressions
 *                       into forbidden patterns list.
 * 
 * These are actually processed on TNC2 format text line, and not
 * AX.25 datastream per se.
 */
static int regex_filter_add(struct configfile *cf,
                            struct digipeater_source *src,
                            char *param1,
                            char *str)
{
        int rc;
        int groupcode = -1;
        regex_t re, *rep;
        char errbuf[2000];

        if (strcmp(param1, "source") == 0) {
                groupcode = 0;
        } else if (strcmp(param1, "destination") == 0) {
                groupcode = 1;
        } else if (strcmp(param1, "via") == 0) {
                groupcode = 2;
        } else if (strcmp(param1, "data") == 0) {
                groupcode = 3;
        } else {
                printf("%s:%d ERROR: Bad RE target: '%s'  must be one of: source, destination, via\n",
                       cf->name, cf->linenum, param1);
                return 1;
        }

        if (!*str) {
                printf("%s:%d ERROR: Expected RE pattern missing or a NUL string.\n",
                       cf->name, cf->linenum);
                return 1;               /* Bad input.. */
        }

        param1 = str;
        str = config_SKIPTEXT(str, NULL); // Handle quoted string
        str = config_SKIPSPACE(str);

        memset(&re, 0, sizeof(re));
        rc = regcomp(&re, param1, REG_EXTENDED | REG_NOSUB);

        if (rc != 0) {          /* Something is bad.. */
                *errbuf = 0;
                regerror(rc, &re, errbuf, sizeof(errbuf));
                printf("%s:%d ERROR: Bad POSIX RE input, error: %s\n",
                       cf->name, cf->linenum, errbuf);
                return 1;
        }

        /* param1 and str were processed successfully ... */

        rep = calloc(1,sizeof(*rep));
        *rep = re;

        switch (groupcode) {
        case 0:
                src->sourceregscount += 1;
                src->sourceregs =
                        realloc(src->sourceregs,
                                src->sourceregscount * sizeof(void *));
                src->sourceregs[src->sourceregscount - 1] = rep;
                break;
        case 1:
                src->destinationregscount += 1;
                src->destinationregs =
                        realloc(src->destinationregs,
                                src->destinationregscount * sizeof(void *));
                src->destinationregs[src->destinationregscount - 1] = rep;
                break;
        case 2:
                src->viaregscount += 1;
                src->viaregs = realloc(src->viaregs,
                                       src->viaregscount * sizeof(void *));
                src->viaregs[src->viaregscount - 1] = rep;
                break;
        case 3:
                src->dataregscount += 1;
                src->dataregs =
                        realloc(src->dataregs,
                                src->dataregscount * sizeof(void *));
                src->dataregs[src->dataregscount - 1] = rep;
                break;
        }
        return 0; // OK state
}


static int match_tracewide(const char *via, const struct tracewide *twp)
{
        int i;
        if (twp == NULL) return 0;

        for (i = 0; i < twp->nkeys; ++i) {
                // if (debug>2) printf(" match:'%s'",twp->keys[i]);
                if (memcmp(via, twp->keys[i], twp->keylens[i]) == 0) {
                        return twp->keylens[i];
                }
        }
        return 0;
}

static int match_aliases(const char *via, struct aprx_interface *txif)
{
        int i;
        for (i = 0; i < txif->aliascount; ++i) {
                if (strcmp(via, txif->aliases[i]) == 0)
                        return 1;
        }
        return 0;
}

// Counts the number of requested and consumed hops in an alias
// and adds those to the viastate->{digireq,digidone,tracereq,tracedone}.
// returns 1 on horrific failure
static int count_single_tnc2_tracewide(struct viastate *state,
		const char *viafield, const int istrace,
		const int matchlen, const int viaindex)
{
        const char *p = viafield + matchlen;
        const char reqc = p[0];
        const char c    = p[1];
        const char remc = p[2];
        int req, done;

        int hasHflag = (strchr(viafield,'*') != NULL);

        // Non-matched case, may have H-bit flag
        if (matchlen == 0) {
                req  = 1;
                done = hasHflag;
                if (viaindex == 2 && !hasHflag)
                  state->probably_heard_direct = 1;
                // if (debug>1) printf(" a[req=%d,done=%d,trace=%d]",0,0,hasHflag);
			 goto addtostate;
        }

        // Is the character following matched part not [1-7]
        if (!('1' <= reqc && reqc <= '7')) {
                // Not a digit, this is single matcher..
                req = 1;
                done = hasHflag;

                if (viaindex == 2 && !hasHflag)
                  state->probably_heard_direct = 1;
                // if (debug>1) printf(" d[req=%d,done=%d]",1,hasHflag);
			 goto addtostate;
        }

        req = reqc - '0';

        if (c == '*' && remc == 0) { // WIDE1*
		   done = req;
                // if (debug>1) printf(" e[req=%d,done=%d]",req,req);
		goto addtostate;
        }
        if (c == 0) { // Bogus WIDE1 - uidigi puts these out.
                state->fixthis = 1;
			 done = req;
                // if (debug>1) printf(" E[req=%d,done=%d]",req,req);
		goto addtostate;
        }
        // Not WIDE1-
        if (c != '-') {
		   req = 1;
		   done = hasHflag;
                // if (debug>1) printf(" f[req=%d,done=%d]",1,hasHflag);
		goto addtostate;
        }

        // OK, it is "WIDEn-" plus "N"
        if ('0' <= remc  && remc <= '7' && p[3] == 0) {
          done = req - (remc - '0');
          if (done < 0) {
            // Something like "WIDE3-7", which is definitely bogus!
		  done = 0;
            state->fixall = 1;
            if (viaindex == 2 && !hasHflag)
              state->probably_heard_direct = 1;
		  goto addtostate;
          }
          if (viaindex == 2) {
            if (istrace) // A real "TRACE" in first slot?
              state->probably_heard_direct = 1;

            else if (!hasHflag && done == 0) // WIDE1-1/2-2/3-3/etc on first slot
              state->probably_heard_direct = 1;
          }
          // if (debug>1) printf(" g[req=%d,done=%d%s]",req,done,hasHflag ? ",Hflag!":"");
		goto addtostate;

        } else if (('8' <= remc && remc <= '9' && p[3] == 0) ||
                   (remc == '1' && '0' <= p[3] && p[3] <= '5' && p[4] == 0)) {
          // The request has SSID value in range of 8 to 15
          state->fixall = 1;
          if (viaindex == 2 && !hasHflag)
            state->probably_heard_direct = 1;
          return 0;

        } else {
          // Yuck, impossible/syntactically invalid
          state->hopsreq  += 1;
          state->hopsdone += hasHflag;
          if (istrace) {
            state->tracereq  += 1;
            state->tracedone += hasHflag;
          }
          if (viaindex == 2 && !hasHflag)
            state->probably_heard_direct = 1;
          // if (debug>1) printf(" h[req=%d,done=%d]",1,hasHflag);
          return 1;
        }

addtostate:;
	// We've successfully parsed the field. Update the viastate and return 0
	state->hopsreq  += req;
	state->hopsdone += done;
	if (istrace) {
		state->tracereq  += req;
		state->tracedone += done;
	}
	return 0;
}

static int match_transmitter(const char *viafield, const struct digipeater_source *src, const int lastviachar)
{
        struct aprx_interface *aif = src->parent->transmitter;
        int tlen = strlen(aif->callsign);

        if (memcmp(viafield, aif->callsign, tlen) == 0) {
          if (viafield[tlen] == lastviachar)
            return 1;
        }

        return 0;
}

static int try_reject_filters(const int  fieldtype,
                              const char *field,
                              struct digipeater_source *src)
{
        int i;
        int stat = 0;
        switch (fieldtype) {
        case 0: // Source
                for (i = 0; i < src->sourceregscount; ++i) {
                        stat = regexec(src->sourceregs[i],
                                       field, 0, NULL, 0);
                        if (stat == 0)
                                return 1;       /* MATCH! */
                }
                if (memcmp("MYCALL",field,6)==0) return 1;
                if (memcmp("N0CALL",field,6)==0) return 1;
                if (memcmp("NOCALL",field,6)==0) return 1;
                break;
        case 1: // Destination

                for (i = 0; i < src->destinationregscount; ++i) {
                        int stat = regexec(src->destinationregs[i],
                                           field, 0, NULL, 0);
                        if (stat == 0)
                                return 1;       /* MATCH! */
                }
                if (memcmp("MYCALL",field,6)==0) return 1;
                if (memcmp("N0CALL",field,6)==0) return 1;
                if (memcmp("NOCALL",field,6)==0) return 1;
                break;
        case 2: // Via

                for (i = 0; i < src->viaregscount; ++i) {
                        int stat = regexec(src->viaregs[i],
                                           field, 0, NULL, 0);
                        if (stat == 0)
                                return 1;       /* MATCH! */
                }
                if (memcmp("MYCALL",field,6)==0) return 1;
                if (memcmp("N0CALL",field,6)==0) return 1;
                if (memcmp("NOCALL",field,6)==0) return 1;
                break;
        case 3: // Data

                for (i = 0; i < src->dataregscount; ++i) {
                        int stat = regexec(src->dataregs[i],
                                           field, 0, NULL, 0);
                        if (stat == 0)
                                return 1;       /* MATCH! */
                }
                break;
        default:
                if (debug)
                  printf("try_reject_filters(fieldtype=%d) - CODE BUG\n",
                         fieldtype);
                return 1;
        }
        if (stat != 0 && stat != REG_NOMATCH) {
                // Some odd reason for an error?
                
        }
        return 0;
}

/* Parse executed and requested WIDEn-N/TRACEn-N info */
static int parse_tnc2_hops(struct digistate *state, struct digipeater_source *src, struct pbuf_t *pb)
{
        const char *p = pb->dstcall_end+1;
        const char *s;
        const struct digipeater *digi = src->parent;
        const char *lastviastar;
        char viafield[15]; // temp buffer for many uses
        int have_fault = 0;
        int viaindex = 1; // First via index will be 2..
        int activeviacount = 0;
        int len;
        int digiok;

        if (debug>1) printf(" hops count of buffer: %s\n",p);

        if (src->src_relaytype == DIGIRELAY_THIRDPARTY) {
          state->v.hopsreq = 1; // Bonus for tx-igated 3rd-party frames
          state->v.tracereq = 1; // Bonus for tx-igated 3rd-party frames
          state->v.hopsdone = 0;
          state->v.tracedone = 0;
          state->v.probably_heard_direct = 1;
          return 0;
        }

        // Copy the SRCCALL part of  SRCALL>DSTCALL  to viafield[] buffer
        len = pb->srccall_end - pb->data;
        if (len >= sizeof(viafield)) len = sizeof(viafield)-1;
        memcpy(viafield, pb->data, len);
        viafield[len] = 0;
        // if (debug>2)printf(" srccall='%s'",viafield);
        if (try_reject_filters(0, viafield, src)) {
          if (debug>1) printf(" - Src filters reject\n");
          return 1; // Src reject filters
        }

        // Copy the DSTCALL part of  SRCALL>DSTCALL  to viafield[] buffer
        len = pb->dstcall_end - pb->srccall_end -1;
        if (len >= sizeof(viafield)) len = sizeof(viafield)-1;
        memcpy(viafield, pb->srccall_end+1, len);
        viafield[len] = 0;
        // if (debug>2)printf(" destcall='%s'",viafield);
        if (try_reject_filters(1, viafield, src)) {
          if (debug>1) printf(" - Dest filters reject\n");
          return 1; // Dest reject filters
        }

        // Where is the last via-field with a star on it?
        len = pb->info_start - p; if (len < 0) len=0;
        lastviastar = memrchr(p, len, '*');

        // Loop over VIA fields to see if we need to digipeat anything.
        while (p < pb->info_start && !have_fault) {
          len = 0;

          if (*p == ':') {
            // A round may stop at ':' found at the end of the processed field,
            // then next round finds it at the start of the field.
            break;
          }

          // Scan for a VIA field ...
          for (s = p; s < pb->info_start; ++s) {
            if (*s == ',' || *s == ':') {
              // ... until comma or double-colon.
              break;
            }
          }
          // [p..s) is now one VIA field.
          if (s == p && *p != ':') {  // BAD!
            have_fault = 1;
            if (debug>1) printf(" S==P ");
            break;
          }
          if (*p == 'q') break; // APRSIS q-constructs..
          ++viaindex;

          // Pick-up a viafield to separate buffer for processing
          len = s-p;
          if (len >= sizeof(viafield)) len = sizeof(viafield)-2;
          memcpy(viafield, p, len);
          viafield[len] = 0;
          if (*s == ',') ++s;
          p = s;

          // Only last via field with H-bit is indicated at TNC2 format,
          // but this digi code logic needs it at every VIA field where
          // it is set.  Therefore this crooked way to add it to picked
          // up fields.
          if (strchr(viafield,'*') == NULL) {
            // If it exists somewhere, and we are not yet at it..
            if (lastviastar != NULL && p < lastviastar)
              strcat(viafield,"*"); // we do know that there is space for this.
          }

          if (debug>1) printf(" - ViaField[%d]: '%s'\n", viaindex, viafield);

          // VIA-field picked up, now analyze it..

          if (try_reject_filters(2, viafield, src)) {
            if (debug>1) printf(" - Via filters reject\n");
            return 1; // via reject filters
          }

          // Transmitter callsign match with H-flag set.
          if (match_transmitter(viafield, src, '*')) {
            if (debug>1) printf(" - Tx match reject\n");
            // Oops, LOOP!  I have transmit this in past
            // (according to my transmitter callsign present
            // in a VIA field!)
            return 1;
          }

          // If there is no '*' meaning this has not been
          // processed, then this is active field..
          if (strchr(viafield, '*') == NULL)
            ++activeviacount;

          digiok = 0;

          // If first active field (without '*') matches
          // transmitter or alias, then this digi is accepted
          // regardless if it is APRS or some other protocol.
          if (activeviacount == 1 &&
              (match_transmitter(viafield, src, 0) ||
               match_aliases(viafield, digi->transmitter))) {
            if (debug>1) printf(" - Tx match accept!\n");
            state->v.hopsreq  += 1;
            state->v.tracereq += 1;
            digiok = 1;
          }

          // .. otherwise following rules are applied only to APRS packets.
          if (pb->is_aprs) {
		  
            if ((len = match_tracewide(viafield, src->src_trace))) {
              if (debug>1) printf("Trace 1 \n");
              have_fault = count_single_tnc2_tracewide(&state->v, viafield, 1, len, viaindex);
              if (!have_fault)
                digiok = 1;
            } else if ((len = match_tracewide(viafield, digi->trace))) {
              if (debug>1) printf("Trace 2 \n");
              have_fault = count_single_tnc2_tracewide(&state->v, viafield, 1, len, viaindex);
              if (!have_fault)
                digiok = 1;
            } else if ((len = match_tracewide(viafield, src->src_wide))) {
              if (debug>1) printf("Trace 3 \n");
              have_fault = count_single_tnc2_tracewide(&state->v, viafield, 0, len, viaindex);
              if (!have_fault)
                digiok = 1;
            } else if ((len = match_tracewide(viafield, digi->wide))) {
              if (debug>1) printf("Trace 4 \n");
              have_fault = count_single_tnc2_tracewide(&state->v, viafield, 0, len, viaindex);
              if (!have_fault)
                digiok = 1;
            } else {
              // No match on trace or wide, but if there was earlier
              // match on interface or alias, then it set "digiok" for us.
              state->v.digidone += (int) (strchr(viafield,'*') != NULL);
              if (debug>1) printf("Trace 5 digi=%d\n",state->v.digidone);
            }
          }
          if (state->v.fixthis || state->v.fixall) {
            // Argh..  bogus WIDEn seen, which is what UIDIGIs put out..
            // Also some other broken requests are "fixed": like WIDE3-7
            // Fixing it: We set the missing H-bit, and continue processing.
            // (That fixing is done in incoming AX25 address field, which
            //  we generally do not touch - with this exception.)
            pb->ax25addr[ AX25ADDRLEN*viaindex + AX25ADDRLEN-1 ] |= AX25HBIT;
            state->v.fixthis = 0;
          }

        if (debug>1) printf("OD: digi=%d,req=%d,done=%d [%s,%s,%s]\n",
        		state->v.digidone,
        		state->v.hopsreq,state->v.hopsdone,
        		have_fault ? "FAULT":"OK",
        		(state->v.hopsreq > state->v.hopsdone) ? "DIGIPEAT":"DROP",
        		(state->v.tracereq > state->v.tracedone) ? "TRACE":"WIDE");


          //wb4bxo - I think this was backwards... I believe it should exit the
          //  loop on the first viapath it finds available to use, not the first failed.
          if (digiok) {
            if (debug>1) printf(" via field match %s\n", viafield);
            if(state->v.hopsreq>state->v.hopsdone) break;
          }
        }
        return have_fault;
}


static void free_tracewide(struct tracewide *twp)
{
        int i;

        if (twp == NULL) return;
        if (twp->keys) {
          for (i = 0; i < twp->nkeys; ++i)
            free((void*)(twp->keys[i]));
          free(twp->keys);
        }
        if (twp->keylens)
          free((void*)(twp->keylens));

        free(twp);
}
static void free_source(struct digipeater_source *src)
{
        if (src == NULL) return;
        free(src);
}

static struct tracewide *digipeater_config_tracewide(struct configfile *cf, int is_trace)
{
        char  *name, *param1;
        char  *str       = cf->buf;
        int    has_fault = 0;
        int    nkeys     = 0;
        char **keywords  = NULL;
        int   *keylens   = NULL;
        int    maxreq    = 4;
        int    maxdone   = 4;
        struct tracewide *tw;

        while (readconfigline(cf) != NULL) {
                if (configline_is_comment(cf))
                        continue;       /* Comment line, or empty line */

                // It can be severely indented...
                str = config_SKIPSPACE(cf->buf);

                name = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);
                config_STRLOWER(name);

                param1 = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);

                if (is_trace) {
                  if (strcmp(name, "</trace>") == 0) {
                    break;
                  }
                } else {
                  if (strcmp(name, "</wide>") == 0) {
                    break;
                  }
                }

                // ... actual parameters
                if (strcmp(name,"maxreq") == 0) {
                  maxreq = atoi(param1);
                  // if (debug) printf(" maxreq %d\n",maxreq);

                } else if (strcmp(name,"maxdone") == 0) {
                  maxdone = atoi(param1);
                  // if (debug) printf(" maxdone %d\n",maxdone);

                } else if (strcmp(name,"keys") == 0) {
                  char *k = strtok(param1, ",");
                  for (; k ; k = strtok(NULL,",")) {
                    ++nkeys;
                    // if (debug) printf(" n=%d key='%s'\n",nkeys,k);
                    keywords = realloc(keywords, sizeof(char*) * nkeys);
                    keywords[nkeys-1] = strdup(k);

                    keylens  = realloc(keylens,  sizeof(int) * nkeys);
                    keylens[nkeys-1] = strlen(k);
                  }

                } else {
                  has_fault = 1;
                  printf("%s:%d ERROR: Unknown keyword inside %s subblock: '%s'\n",
                         cf->name, cf->linenum, is_trace ? "<trace>":"<wide>", name);
                }
        }

        if (has_fault) {
          int i;
          for (i = 0; i < nkeys; ++i)
            free(keywords[i]);
          if (keywords != NULL)
            free(keywords);
          if (keylens != NULL)
            free(keylens);
          return NULL;
        }

        tw = calloc(1,sizeof(*tw));

        tw->maxreq   = maxreq;
        tw->maxdone  = maxdone;
        tw->is_trace = is_trace;
        tw->nkeys    = nkeys;
        tw->keys     = keywords;
        tw->keylens  = keylens;

        return tw;
}

static struct digipeater_source *digipeater_config_source(struct configfile *cf)
{
        char *name, *param1;
        char *str = cf->buf;
        int has_fault = 0;
        int viscous_delay = 0;
        float ratelimit = 120;
        float rateincrement = 60;

        struct aprx_interface *source_aif = NULL;
        struct digipeater_source  *source = NULL;
        digi_relaytype          relaytype = DIGIRELAY_DIGIPEAT;
        struct filter_t          *filters = NULL;
        struct tracewide    *source_trace = NULL;
        struct tracewide     *source_wide = NULL;
        struct digipeater_source regexsrc;
#ifndef DISABLE_IGATE
        char                    *via_path = NULL;
        char                    *msg_path = NULL;
        uint8_t               ax25viapath[AX25ADDRLEN];
        uint8_t                msgviapath[AX25ADDRLEN];
#endif

        memset(&regexsrc, 0, sizeof(regexsrc));
#ifndef DISABLE_IGATE
        memset(ax25viapath, 0, sizeof(ax25viapath));
        memset(msgviapath,  0, sizeof(msgviapath));
#endif

        while (readconfigline(cf) != NULL) {
                if (configline_is_comment(cf))
                        continue;       /* Comment line, or empty line */

                // It can be severely indented...
                str = config_SKIPSPACE(cf->buf);

                name = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);
                config_STRLOWER(name);

                param1 = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);

                if (strcmp(name, "</source>") == 0) {
                        break;

                        // ... actual parameters
                } else if (strcmp(name,"source") == 0) {
                        if (debug)
                          printf("%s:%d <source> source = '%s'\n",
                                 cf->name, cf->linenum, param1);

                        if (strcasecmp(param1,"$mycall") == 0)
                                param1 = (char*)mycall;

                        source_aif = find_interface_by_callsign(param1);
                        if (source_aif == NULL) {
                                has_fault = 1;
                                printf("%s:%d ERROR: Digipeater source '%s' not found\n",
                                       cf->name, cf->linenum, param1);
                        }
                        if (debug>1)
                          printf(" .. source_aif = %p\n", source_aif);

                } else if (strcmp(name, "viscous-delay") == 0) {
                        viscous_delay = atoi(param1);
                        if (debug) printf(" viscous-delay = %d\n",viscous_delay);
                        if (viscous_delay < 0) {
                          printf("%s:%d ERROR: Bad value for viscous-delay: '%s'\n",
                                 cf->name, cf->linenum, param1);
                          viscous_delay = 0;
                          has_fault = 1;
                        }
                        if (viscous_delay > 9) {
                          printf("%s:%d ERROR: Too large value for viscous-delay: '%s'\n",
                                 cf->name, cf->linenum, param1);
                          viscous_delay = 9;
                          has_fault = 1;
                        }

                } else if (strcmp(name, "ratelimit") == 0) {
                        char *param2 = str;
                        str = config_SKIPTEXT(str, NULL);
                        str = config_SKIPSPACE(str);

                        rateincrement = (float)atof(param1);
                        ratelimit     = (float)atof(param2);
                        if (rateincrement < 0.01 || rateincrement > rateincrementmax)
                                rateincrement = 60;
                        if (ratelimit < 0.01 || ratelimit > ratelimitmax)
                                ratelimit = 120;
                        if (ratelimit < rateincrement)
                          rateincrement = ratelimit;
                        if (debug)
                          printf("  .. ratelimit %f %f\n",
                                 rateincrement, ratelimit);

                } else if (strcmp(name,"regex-filter") == 0) {
                        if (regex_filter_add(cf, &regexsrc, param1, str)) {
                          // prints errors internally
                          has_fault = 1;
                        }

#ifndef DISABLE_IGATE
                } else if (strcmp(name, "via-path") == 0) {

                        // Validate that source callsign is "APRSIS"
                        // or "DPRS" for this parameter

                        if (source_aif == NULL ||
                            (strcmp(source_aif->callsign,"APRSIS") != 0 &&
                             strcmp(source_aif->callsign,"DPRS") != 0)) {
                          printf("%s:%d ERROR: via-path parameter is available only on 'source APRSIS' and 'source DPRS' cases.\n",
                                 cf->name, cf->linenum);
                          has_fault = 1;
                          continue;
                        }

                        via_path  = strdup(param1);
                        config_STRUPPER(via_path);

                        if (parse_ax25addr(ax25viapath, via_path, 0x00)) {
                          has_fault = 1;
                          printf("%s:%d ERROR: via-path parameter is not valid AX.25 callsign: '%s'\n",
                                 cf->name, cf->linenum, via_path);
                          free(via_path);
                          via_path = NULL;
                          continue;
                        }

                        if (debug)
                                printf("via-path '%s'\n", via_path);

                } else if (strcmp(name, "msg-path") == 0) {

                        // Validate that source callsign is "APRSIS"
                        // or "DPRS" for this parameter

                        if (source_aif == NULL ||
                            (strcmp(source_aif->callsign,"APRSIS") != 0 &&
                             strcmp(source_aif->callsign,"DPRS") != 0)) {
                          printf("%s:%d ERROR: msg-path parameter is available only on 'source APRSIS' and 'source DPRS' cases.\n",
                                 cf->name, cf->linenum);
                          has_fault = 1;
                          continue;
                        }

                        msg_path  = strdup(param1);
                        config_STRUPPER(msg_path);

                        if (parse_ax25addr(msgviapath, msg_path, 0x00)) {
                          has_fault = 1;
                          printf("%s:%d ERROR: msg-path parameter is not valid AX.25 callsign: '%s'\n",
                                 cf->name, cf->linenum, msg_path);
                          free(msg_path);
                          msg_path = NULL;
                          continue;
                        }

                        if (debug)
                                printf("msg-path '%s'\n", msg_path);
#endif
                } else if (strcmp(name,"<trace>") == 0) {
                        if (source_trace == NULL) {
                          source_trace = digipeater_config_tracewide(cf, 1);
                          // prints errors internally
                        } else {
                          has_fault = 1;
                          printf("%s:%d ERROR: double definition of <trace> block.\n",
                                 cf->name, cf->linenum);
                        }

                } else if (strcmp(name,"<wide>") == 0) {
                        if (source_wide == NULL) {
                          source_wide  = digipeater_config_tracewide(cf, 0);
                          // prints errors internally
                        } else {
                          has_fault = 1;
                          printf("%s:%d ERROR: double definition of <wide> block.\n",
                                 cf->name, cf->linenum);
                        }

                } else if (strcmp(name,"filter") == 0) {
                        if (filter_parse(&filters, param1)) {
                          // prints errors internally
                          
                          has_fault = 1;
                          printf("%s:%d ERROR: Error at filter parser.\n",
                                 cf->name, cf->linenum);
                        } else {
                          if (debug)
                            printf(" .. OK filter %s\n", param1);
                        }

                } else if (strcmp(name,"relay-type") == 0 ||   // documented name
                           strcmp(name,"relay-format") == 0 || // an alias
                           strcmp(name,"digi-mode") == 0) {    // very old alias
                        config_STRLOWER(param1);
                        if (strcmp(param1,"digipeat") == 0) {
                          relaytype = DIGIRELAY_DIGIPEAT;
                        } else if (strcmp(param1,"digipeated") == 0) {
                          relaytype = DIGIRELAY_DIGIPEAT;
                        } else if (strcmp(param1,"digipeater") == 0) {
                          relaytype = DIGIRELAY_DIGIPEAT;
                        } else if (strcmp(param1,"directonly") == 0) {
                          relaytype = DIGIRELAY_DIGIPEAT_DIRECTONLY;
                        } else if (strcmp(param1,"third-party") == 0) {
                          relaytype = DIGIRELAY_THIRDPARTY;
                        } else if (strcmp(param1,"3rd-party") == 0) {
                          relaytype = DIGIRELAY_THIRDPARTY;
                        } else {
                          printf("%s:%d ERROR: Digipeater <source>'s %s did not recognize: '%s' \n", cf->name, cf->linenum, name, param1);
                          has_fault = 1;
                        }
                } else {
                        printf("%s:%d ERROR: Digipeater <source>'s %s did not recognize: '%s' \n", cf->name, cf->linenum, name, param1);
                        has_fault = 1;
                }
        }

        if (source_aif == NULL) {
                has_fault = 1;
                printf("%s:%d ERROR: Missing or bad 'source =' definition at this <source> group.\n",
                       cf->name, cf->linenum);
        }

        if (!has_fault && (source_aif != NULL)) {
                source = calloc(1,sizeof(*source));
                
                source->src_if        = source_aif;
                source->src_relaytype = relaytype;
                source->src_filters   = filters;
                source->src_trace     = source_trace;
                source->src_wide      = source_wide;
#ifndef DISABLE_IGATE
                source->via_path      = via_path;
                source->msg_path      = msg_path;
                memcpy(source->ax25viapath, ax25viapath, sizeof(ax25viapath));
                memcpy(source->msgviapath,  msgviapath,  sizeof(msgviapath));
                if (msg_path == NULL) { // default value of via-path !
                  source->msg_path    = via_path;
                  memcpy(source->msgviapath,  ax25viapath,  sizeof(ax25viapath));
                }
#endif

                source->viscous_delay = viscous_delay;

                source->tbf_limit     = (ratelimit * TOKENBUCKET_INTERVAL)/60;
                source->tbf_increment = (rateincrement * TOKENBUCKET_INTERVAL)/60;
                source->tokenbucket   = source->tbf_limit;
                
                // RE pattern reject filters
                source->sourceregscount      = regexsrc.sourceregscount;
                source->sourceregs           = regexsrc.sourceregs;
                source->destinationregscount = regexsrc.destinationregscount;
                source->destinationregs      = regexsrc.destinationregs;
                source->viaregscount         = regexsrc.viaregscount;
                source->viaregs              = regexsrc.viaregs;
                source->dataregscount        = regexsrc.dataregscount;
                source->dataregs             = regexsrc.dataregs;

        } else {
                // Errors detected
                free_tracewide(source_trace);
                free_tracewide(source_wide);
                // filters_free(filters);
                // free regexsrc's allocations
                if (debug)
                  printf("Seen errors at <digipeater><source> definition.\n");
        }

        if (debug>1)printf(" .. <source> definition returning %p\n",source);
        return source;
}

int digipeater_config(struct configfile *cf)
{
        char *name, *param1;
        char *str = cf->buf;
        int has_fault = 0;
        int i;
        const int line0 = cf->linenum;

        struct aprx_interface *aif = NULL;
        float ratelimit = 60;
        float rateincrement = 60;
        float srcratelimit = 60;
        float srcrateincrement = 60;
        int sourcecount = 0;
        int dupestoretime = 30; // FIXME: parametrize! 30 is minimum..
        struct digipeater_source **sources = NULL;
        struct digipeater *digi = NULL;
        struct tracewide *traceparam = NULL;
        struct tracewide *wideparam  = NULL;

        while (readconfigline(cf) != NULL) {
                if (configline_is_comment(cf))
                        continue;       /* Comment line, or empty line */

                // It can be severely indented...
                str = config_SKIPSPACE(cf->buf);

                name = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);
                config_STRLOWER(name);

                param1 = str;
                str = config_SKIPTEXT(str, NULL);
                str = config_SKIPSPACE(str);

                if (strcmp(name, "</digipeater>") == 0) {
                        break;
                }
                if (strcmp(name, "transmit") == 0 ||
                    strcmp(name, "transmitter") == 0) {
                        if (strcasecmp(param1,"$mycall") == 0)
                                param1 = (char*)mycall;

                        aif = find_interface_by_callsign(param1);
                        if (aif != NULL && (!aif->tx_ok)) {
                          aif = NULL; // Not 
                          printf("%s:%d ERROR: This transmit interface has no TX-OK TRUE setting: '%s'\n",
                                 cf->name, cf->linenum, param1);
                          has_fault = 1;
                        } else if (aif != NULL && aif->txrefcount > 0) {
                          aif = NULL;
                          printf("%s:%d ERROR: This transmit interface is being used on multiple <digipeater>s as transmitter: '%s'\n",
                                 cf->name, cf->linenum, param1);
                          has_fault = 1;
                        } else if (aif == NULL) {
                          printf("%s:%d ERROR: Unknown interface: '%s'\n",
                                 cf->name, cf->linenum, param1);
                          has_fault = 1;
                        }

                } else if (strcmp(name, "ratelimit") == 0) {
                        char *param2 = str;
                        str = config_SKIPTEXT(str, NULL);
                        str = config_SKIPSPACE(str);

                        rateincrement = (float)atof(param1);
                        ratelimit     = (float)atof(param2);
                        if (rateincrement < 0.01 || rateincrement > rateincrementmax)
                                rateincrement = 60;
                        if (ratelimit < 0.01 || ratelimit > ratelimitmax)
                                ratelimit = 60;
                        if (ratelimit < rateincrement)
                                rateincrement = ratelimit;
                        if (debug)
                                printf("  .. ratelimit %f %f\n",
                                       rateincrement, ratelimit);

                } else if (strcmp(name, "srcratelimit") == 0) {
                        char *param2 = str;
                        str = config_SKIPTEXT(str, NULL);
                        str = config_SKIPSPACE(str);

                        srcrateincrement = (float)atof(param1);
                        srcratelimit     = (float)atof(param2);
                        if (srcrateincrement < 0.01 || srcrateincrement > rateincrementmax)
                                srcrateincrement = 60;
                        if (srcratelimit < 0.01 || srcratelimit > ratelimitmax)
                                srcratelimit = 60;
                        if (srcratelimit < srcrateincrement)
                                srcrateincrement = srcratelimit;
                        if (debug)
                                printf("  .. srcratelimit %f %f\n",
                                       srcrateincrement, srcratelimit);

                } else if (strcmp(name, "<trace>") == 0) {
                        if (traceparam == NULL) {
                          traceparam = digipeater_config_tracewide(cf, 1);
                          if (traceparam == NULL) {
                            printf("%s:%d ERROR: <trace> definition failed!\n",
                                 cf->name, cf->linenum);
                            has_fault = 1;
                          }
                        } else {
                          printf("%s:%d ERROR: Double definition of <trace> !\n",
                                 cf->name, cf->linenum);
                          has_fault = 1;
                        }

                } else if (strcmp(name, "<wide>") == 0) {
                        if (wideparam == NULL) {
                          wideparam = digipeater_config_tracewide(cf, 0);
                          if (wideparam == NULL) {
                            printf("%s:%d ERROR: <wide> definition failed!\n",
                                 cf->name, cf->linenum);
                            has_fault = 1;
                          }
                        } else {
                          printf("%s:%d ERROR: Double definition of <wide> !\n",
                                 cf->name, cf->linenum);
                          has_fault = 1;
                        }

                } else if (strcmp(name, "<source>") == 0) {
                        struct digipeater_source *src =
                                digipeater_config_source(cf);
                        if (src != NULL) {
                                // Found a source, link it!
                                sources = realloc(sources, sizeof(void*) * (sourcecount+1));
                                sources[sourcecount] = src;
                                ++sourcecount;
                        } else {
                                has_fault = 1;
                                printf("%s:%d ERROR: <source> definition failed\n",
                                       cf->name, cf->linenum);
                        }

                } else {
                  printf("%s:%d ERROR: Unknown <digipeater> config keyword: '%s'\n",
                         cf->name, cf->linenum, name);
                  has_fault = 1;
                  continue;
                }
        }

        if (aif == NULL && !has_fault) {
                printf("%s:%d ERROR: Digipeater defined without transmit interface.\n",
                       cf->name, cf->linenum);
                has_fault = 1;
        }
        if (sourcecount == 0 && !has_fault) {
                printf("%s:%d ERROR: Digipeater defined without <source>:s.\n",
                       cf->name, cf->linenum);
                has_fault = 1;
        }
        // Check that source definitions are unique
        for ( i = 0; i < sourcecount; ++i ) {
                int j;
                for (j = i+1; j < sourcecount; ++j) {
                        if (sources[i]->src_if == sources[j]->src_if) {
                                has_fault = 1;
                                printf("%s:%d Two <source>s on this <digipeater> definition use same <interface>: '%s'\n",
                                       cf->name, line0, sources[i]->src_if->callsign);
                        }
                }
        }

        if (has_fault) {
                // Free allocated resources and link pointers, if any
                for ( i = 0; i < sourcecount; ++i ) {
                        free_source(sources[i]);
                }
                if (sources != NULL)
                        free(sources);

                free_tracewide(traceparam);
                free_tracewide(wideparam);

                printf("ERROR: Config fault observed on <digipeater> definitions! \n");
        } else {
                // Construct the digipeater

                digi = calloc(1,sizeof(*digi));

                if (debug>1)printf("<digipeater> sourcecount=%d\n",sourcecount);

                // up-link all interfaces used as sources
                for ( i = 0; i < sourcecount; ++i ) {
                        struct digipeater_source *src = sources[i];
                        src->parent = digi; // Set parent link

                        src->src_if->digisources = realloc( src->src_if->digisources,
                                                            (src->src_if->digisourcecount +1) * (sizeof(void*)));
                        src->src_if->digisources[src->src_if->digisourcecount] = src;
                        src->src_if->digisourcecount += 1;
                }

                aif->txrefcount += 1; // Increment Tx usage Reference count.
                                      // We permit only one <digipeater> to
                                      // use any given Tx-interface. (Rx:es
                                      // permit multiple uses.)
                digi->transmitter   = aif;
                digi->tbf_limit     = (ratelimit * TOKENBUCKET_INTERVAL)/60;
                digi->tbf_increment = (rateincrement * TOKENBUCKET_INTERVAL)/60;
                digi->src_tbf_limit = (srcratelimit * TOKENBUCKET_INTERVAL)/60;
                digi->src_tbf_increment = (srcrateincrement * TOKENBUCKET_INTERVAL)/60;
                digi->tokenbucket   = digi->tbf_limit;

                digi->dupechecker   = dupecheck_new(dupestoretime);  // Dupecheck is per transmitter
#ifndef DISABLE_IGATE
                digi->historydb     = historydb_new();  // HistoryDB is per transmitter
#endif

                digi->trace         = (traceparam != NULL) ? traceparam : & default_trace_param;
                digi->wide          = (wideparam  != NULL) ? wideparam  : & default_wide_param;

                digi->sourcecount   = sourcecount;
                digi->sources       = sources;

                digis = realloc( digis, sizeof(void*) * (digi_count+1));
                digis[digi_count] = digi;
                ++digi_count;
        }
        return has_fault;
}


static int decrement_ssid(uint8_t *ax25addr)
{
        // bit-field manipulation
        int ssid = (ax25addr[AX25ADDRLEN-1] >> 1) & 0x0F;
        if (ssid > 0)
          --ssid;
        ax25addr[AX25ADDRLEN-1] = (ax25addr[AX25ADDRLEN-1] & 0xE1) | (ssid << 1);
        return ssid;
}


/* 0 == accept, otherwise reject */
/*
int digipeater_receive_filter(struct digipeater_source *src, struct pbuf_t *pb)
{
        
        if (src->src_filters == NULL) {
          if (debug>1)
            printf("No source filters, accepted the packet from %s.\n", src->src_if->callsign);
          return 0;
        }
        int rc = filter_process(pb, src->src_filters, src->parent->historydb);
        if (rc != 1) {
          if (debug>1)
            printf("Source filtering rejected the packet from %s.\n", src->src_if->callsign);
          return 1;
        }
        if (debug>1)
          printf("Source filtering accepted the packet from %s.\n", src->src_if->callsign);
        return 0;
}
*/

static void digipeater_receive_backend(struct digipeater_source *src, struct pbuf_t *pb)
{
        int len, viaindex;
        struct digistate state;
        struct viastate  viastate;
        struct digipeater *digi = src->parent;
        char viafield[14]; // room for text format
        uint8_t *axaddr, *e;

        memset(&state,    0, sizeof(state));
        memset(&viastate, 0, sizeof(viastate));

        //  2) Verify that none of our interface callsigns does match any
        //     of already DIGIPEATED via fields! (fields that have H-bit set)
        //   ( present implementation: this digi's transmitter callsign is
        //     verified)

        // Parse executed and requested WIDEn-N/TRACEn-N info
        if (parse_tnc2_hops(&state, src, pb)) {
                // A fault was observed! -- tests include "not this transmitter"
                if (debug>1)
                  printf("Parse_tnc2_hops rejected this.");
                return;
        }

        if (pb->is_aprs) {

          if (state.v.probably_heard_direct) {
            // Collect a decaying average of distances to stations?
            //  .. could auto-beacon an aloha-circle - maybe
            //  .. note: this does not get packets that have no VIA fields.
            // Score of direct DX:es?
            //  .. note: this does not get packets that have no VIA fields.
          } else {
            if (src->src_relaytype == DIGIRELAY_DIGIPEAT_DIRECTONLY) {
              // Source relaytype is DIRECTONLY, and this was not
              // likely directly heard...
              if (debug>1) printf("DIRECTONLY -mode, and packet is probably not direct heard.");
              return;
            }
          }
          // Keep score of all DX packets?

          if (try_reject_filters(3, pb->info_start, src)) {
            if (debug>1)
              printf(" - Data body regexp filters reject\n");
            return; // data body regexp reject filters
          }

          // FIXME: 3) aprsc style filters checking in service area of the packet..

        }

        // 4) Hop-count filtering:

        // APRSIS sourced packets have different rules than DIGIPEAT
        // packets...
        if (state.v.hopsreq <= state.v.hopsdone) {
          if (debug>1) printf(" No remaining hops to execute.\n");
          return;
        }
        if (state.v.hopsreq   > digi->trace->maxreq  ||
            state.v.hopsreq   > digi->wide->maxreq   ||
            state.v.tracereq  > digi->trace->maxreq  ||
            state.v.digidone  > digi->trace->maxdone ||
            state.v.digidone  > digi->wide->maxdone  ||
            state.v.hopsdone  > digi->trace->maxdone ||
            state.v.hopsdone  > digi->wide->maxdone  ||
            state.v.tracedone > digi->trace->maxdone) {
          if (debug) printf(" Packet exceeds digipeat limits\n");
          if (!state.v.probably_heard_direct) {
            if (debug) printf(".. discard.\n");
            return;
          } else {
            state.v.fixall = 1;
          }
        }

        // if (debug) printf(" Packet accepted to digipeat!\n");

        state.ax25addrlen = pb->ax25addrlen;
        memcpy(state.ax25addr, pb->ax25addr, pb->ax25addrlen);
        axaddr = state.ax25addr + 2*AX25ADDRLEN;
        e      = state.ax25addr + state.ax25addrlen;

        if (state.v.fixall) {
          // Okay, insert my transmitter callsign on the first
          // VIA field, and mark the rest with H-bit
          // (in search loop below)
          int taillen = e - axaddr;
          if (state.ax25addrlen >= AX25ADDRMAXLEN) {
            if (debug) printf(" FIXALL TRACE overgrows the VIA fields! Dropping last of incoming ones.\n");
            // Drop the last via field to make room for insert below.
            state.ax25addrlen -= AX25ADDRLEN;
            taillen           -= AX25ADDRLEN;
          }
          // If we have a tail, move it up (there is always room for it)
          if (taillen > 0)
            memmove(axaddr+AX25ADDRLEN, axaddr, taillen);
          state.ax25addrlen += AX25ADDRLEN;
          e = state.ax25addr + state.ax25addrlen; // recalculate!

          // Put the transmitter callsign in
          memcpy(axaddr, digi->transmitter->ax25call, AX25ADDRLEN);

          // Set Address Termination bit at the last VIA field
          // (possibly ours, or maybe the previous one was truncated..)
          axaddr[state.ax25addrlen-1] |= AX25ATERM;
        }

        // Search for first AX.25 VIA field that does not have H-bit set:
        viaindex = 1; // First via field is number 2
        *viafield = 0; // clear that buffer for starters
        for (; axaddr < e; axaddr += AX25ADDRLEN, ++viaindex) {
          ax25_to_tnc2_fmtaddress(viafield, axaddr, 0);
          // if (debug>1) {
          //   printf(" via: %s", viafield);
          // }

          // Initial parsing said that things are seriously wrong..
          // .. and we will digipeat the packet with all H-bits set.
          if (state.v.fixall) axaddr[AX25ADDRLEN-1] |= AX25HBIT;

          if (!(axaddr[AX25ADDRLEN-1] & AX25HBIT)) // No "Has Been Digipeated" bit set
            break; // this doesn't happen in "fixall" mode
        }

        switch (src->src_relaytype) {
        case DIGIRELAY_THIRDPARTY:
                // Effectively disable the digipeat modifying of address
                axaddr = e;
                break;
        case DIGIRELAY_DIGIPEAT:
                // Normal functionality
                break;
        default: ;
        }

        // Unprocessed VIA field found (not in FIXALL mode)
        if (axaddr < e) {       // VIA-field of interest has been found

// FIXME: 5) / 6) Cross-frequency/cross-band digipeat may add a special
//                label telling that the message originated on other band

          // 7) WIDEn-N treatment (as well as transmitter matching digi)
          if (pb->digi_like_aprs) {
            if (strcmp(viafield,digi->transmitter->callsign) == 0 ||
                // Match on the transmitter callsign without the star...
                match_aliases(viafield, digi->transmitter)) {
                // .. or match transmitter interface alias.

              // Treat it as a TRACE request.

              int aterm = axaddr[AX25ADDRLEN-1] & AX25ATERM; // save old address termination bit
              // Put the transmitter callsign in, and set the H-bit.
              memcpy(axaddr, digi->transmitter->ax25call, AX25ADDRLEN);
              axaddr[AX25ADDRLEN-1] |= (AX25HBIT | aterm); // Set H-bit
              
            } else if ((len = match_tracewide(viafield, src->src_trace))) {
              count_single_tnc2_tracewide(&viastate, viafield, 1, len, viaindex);
            } else if ((len = match_tracewide(viafield, digi->trace))) {
              count_single_tnc2_tracewide(&viastate, viafield, 1, len, viaindex);
            } else if ((len = match_tracewide(viafield, src->src_wide))) {
              count_single_tnc2_tracewide(&viastate, viafield, 0, len, viaindex);
            } else if ((len = match_tracewide(viafield, digi->wide))) {
              count_single_tnc2_tracewide(&viastate, viafield, 0, len, viaindex);
            }

          } else { // Not "digi_as_aprs" rules

            if (strcmp(viafield,digi->transmitter->callsign) == 0) {
              // Match on the transmitter callsign without the star.
              // Treat it as a TRACE request.
              int aterm = axaddr[AX25ADDRLEN-1] & AX25ATERM; // save old address termination bit
              // Put the transmitter callsign in, and set the H-bit.
              memcpy(axaddr, digi->transmitter->ax25call, AX25ADDRLEN);
              axaddr[AX25ADDRLEN-1] |= (AX25HBIT | aterm); // Set H-bit
              
            } else if (match_aliases(viafield, digi->transmitter)) {
              // Match on the aliases.
              // Treat it as a TRACE request.
              int aterm = axaddr[AX25ADDRLEN-1] & AX25ATERM; // save old address termination bit
              // Put the transmitter callsign in, and set the H-bit.
              memcpy(axaddr, digi->transmitter->ax25call, AX25ADDRLEN);
              axaddr[AX25ADDRLEN-1] |= (AX25HBIT | aterm); // Set H-bit
            }
          }

          if (viastate.tracereq > viastate.tracedone) {
            // if (debug) printf(" TRACE on %s!\n",viafield);
            // Must move it up in memory to be able to put
            // transmitter callsign in
            int taillen = e - axaddr;
            int newssid;
            if (state.ax25addrlen >= AX25ADDRMAXLEN) {
              if (debug) printf(" TRACE overgrows the VIA fields! Discard.\n");
              return;
            }
            memmove(axaddr+AX25ADDRLEN, axaddr, taillen);
            state.ax25addrlen += AX25ADDRLEN;

            newssid = decrement_ssid(axaddr+AX25ADDRLEN);
            if (newssid <= 0)
              axaddr[2*AX25ADDRLEN-1] |= AX25HBIT; // Set H-bit
            // Put the transmitter callsign in, and set the H-bit.
            memcpy(axaddr, digi->transmitter->ax25call, AX25ADDRLEN);
            axaddr[AX25ADDRLEN-1] |= AX25HBIT; // Set H-bit

          } else if (viastate.hopsreq > viastate.hopsdone) {
            // If configuration didn't process "WIDE" et.al. as
            // a TRACE, then here we process them without trace..
            int newssid;
            if (debug) printf(" VIA on %s!\n",viafield);
            newssid = decrement_ssid(axaddr);
            if (newssid <= 0)
              axaddr[AX25ADDRLEN-1] |= AX25HBIT; // Set H-bit
          }
        }
        {
          history_cell_t *hcell;
          char tbuf[2800];
          int is_ui = 0, ui_pid = -1, frameaddrlen = 0, tnc2addrlen = 0, t2l;
          // uint8_t *u = state.ax25addr + state.ax25addrlen;
          // *u++ = 0;
          // *u++ = 0;
          // *u++ = 0;
          t2l = ax25_format_to_tnc( state.ax25addr,
                                    state.ax25addrlen+AX25ADDRLEN-1,
                                    tbuf, sizeof(tbuf),
                                    & frameaddrlen, &tnc2addrlen,
                                    & is_ui, &ui_pid );
          tbuf[t2l] = 0;
          if (debug) {
            printf(" out-hdr: '%s' data='",tbuf);
            (void)fwrite(pb->ax25data+2, pb->ax25datalen-2,  // without Control+PID
                         1, stdout);
            printf("'\n");
          }

#ifndef DISABLE_IGATE
          // Insert into history database - track every packet
          hcell = historydb_insert_( digi->historydb, pb, 1 );

          if (hcell != NULL) {
            if (hcell->tokenbucket < 1.0) {
              if (debug) printf("TRANSMITTER SOURCE CALLSIGN RATELIMIT DISCARD.\n");
              return;
            }
            hcell->tokenbucket -= 1.0;
          }
#endif

          // Now we do token bucket filtering -- rate limiting
          if (digi->tokenbucket < 1.0) {
            if (debug) printf("TRANSMITTER RATELIMIT DISCARD.\n");
            return;
          }
          digi->tokenbucket -= 1.0;

          if (pb->is_aprs && rflogfile) {
            int t2l2;
            // Essentially Debug logging.. to file

            if (sizeof(tbuf) - pb->ax25datalen > t2l && t2l > 0) {
              // Have space for body too, skip leading Ctrl+PID bytes
              memcpy(tbuf+t2l, pb->ax25data+2, pb->ax25datalen-2); // Ctrl+PID skiped
              t2l2 = t2l + pb->ax25datalen-2; // tbuf size sans Ctrl+PID

              rflog( digi->transmitter->callsign, 'T', 0, tbuf, t2l2 );
              tbuf[t2l]=0;
            }
          }

        // Feed to dupe-filter (transmitter specific)
        // this means we have already seen it, and when 
        // it comes back from somewhere, we do not digipeat
        // it ourselves.

        // This recording is needed at output side of digipeater
        // for APRSIS and DPRS transmit gates.

          if (t2l>0) {
            dupecheck_aprs( digi->dupechecker,
                            (const char *)tbuf,
                            t2l,
                            (const char *)pb->ax25data+2,
                            pb->ax25datalen-2 );  // ignore Ctrl+PID
          } else {
            dupecheck_aprs( digi->dupechecker,
                            (const char *)state.ax25addr,
                            state.ax25addrlen,
                            (const char *)pb->ax25data+2,
                            pb->ax25datalen-2 );  // ignore Ctrl+PID
          }
        }

        // Feed to interface_transmit_ax25() with new header and body
        interface_transmit_ax25( digi->transmitter,
                                 state.ax25addr, state.ax25addrlen,
                                 (const char*)pb->ax25data, pb->ax25datalen );
        if (debug>1) printf("Done.\n");
}


void digipeater_receive( struct digipeater_source *src,
                         struct pbuf_t *pb )
{
        // Below numbers like "4)" refer to Requirement Specification
        // paper chapter 2.6: Digipeater Rules

        //  The dupe-filter exists for APRS frames, possibly for some
        // selected UI frame types, and definitely not for CONS frames.

        if (debug)
          printf("digipeater_receive() from %s, is_aprs=%d viscous_delay=%d\n",
                 src->src_if->callsign, pb->is_aprs, src->viscous_delay);

        if (src->tokenbucket < 1.0) {
          if (debug) printf("SOURCE RATELIMIT DISCARD\n");
          return;
        }
        src->tokenbucket -= 1.0;


        if (pb->is_aprs) {

                const int source_is_transmitter = (src->src_if ==
                                                   src->parent->transmitter);

                // 1) Feed to dupe-filter (transmitter specific)
                //    If the dupe detector on this packet has reached
                //    count > 1, drop it.

                int jittery = src->viscous_delay > 0 ? random() % 3 + src->viscous_delay : 0;
                dupe_record_t *dupe = dupecheck_pbuf( src->parent->dupechecker,
                                                      pb, jittery);
                if (dupe == NULL) {  // Oops.. allocation error!
                  if (debug)
                    printf("digipeater_receive() - dupecheck_pbuf() allocation error, packet discarded\n");
                  return;
                }

                // 1.1) optional viscous delay!

                if (src->viscous_delay == 0) { // No delay, direct cases

                        // First packet on direct source arrives here
                        // with  seen = 1

                        // 1.x) Analyze dupe checking

                        if (debug>1)
                          printf("Seen this packet %d times (delayed=%d)\n",
                                 dupe->delayed_seen + dupe->seen,
                                 dupe->delayed_seen);

                        if (dupe->seen > 1) {
                          // N:th direct packet, duplicate.
                          // Drop this direct packet.
                          if (debug>1) printf(".. discarded\n");
                          return;
                        }

                        if (dupe->seen == 1 && dupe->delayed_seen > 0 &&
                            dupe->pbuf == NULL) {
                          // First direct, but dupe record does not have
                          // pbuf anymore indicating that a delayed 
                          // handling did process it sometime in past.
                          // Drop this direct packet.
                          if (debug>1) printf(".. discarded\n");
                          return;
                        }

                        if (dupe->seen == 1 && dupe->delayed_seen >= 0 &&
                            dupe->pbuf != NULL) {

                          // First direct, and pbuf exists in dupe record.
                          // It was added first to viscous queue, and
                          // a bit latter came this direct one.
                          // Remove one from viscous queue, and proceed
                          // with direct processing.

                          if (debug>1) printf(" .. discard dupe record, process immediately");

                          pbuf_put(dupe->pbuf);
                          dupe->pbuf = NULL;
                          dupe = NULL; // Do not do  dupecheck_put() here!
                        }

                } else {  // src->viscous_delay > 0

                        // First packet on viscous source arrives here
                        // with   dupe->delayed_seen = 1

                        // Has this been seen on direct channel?
                        if (dupe->seen > 0) {
                          // Already processed thru direct processing,
                          // no point in adding this to viscous delay queue
                          if (debug>1)
                            printf("Seen this packet %d times. Discarding it.\n",
                                   dupe->delayed_seen + dupe->seen);
                          return;
                        }

                        // Depending on source definition, the transmitter is
                        // either non-viscous or viscous.  We care about it
                        // only when the source is viscous:
                        if (source_is_transmitter)
                                dupe->seen_on_transmitter += 1;

                        if (dupe->delayed_seen > 1) {
                          // 2nd or more of same packet from delayed source
                          if (debug>1)
                            printf("Seen this packet %d times.\n",
                                   dupe->delayed_seen + dupe->seen);

                          // If any of them is transmitter interface, then
                          // drop the queued packet, and drop current one.
                          if (dupe->seen_on_transmitter > 0) {

                            // If pbuf is on delayed queue, drop it.
                            if (dupe->pbuf != NULL) {
                              pbuf_put(dupe->pbuf);
                              dupe->pbuf = NULL;
                              dupe = NULL; // Do not do  dupecheck_put() here!
                            }

                          }
                          if (debug>1) printf(".. discarded\n");
                          return;
                        }

                        // First time that we have seen this packet at all.
                        // Put the pbuf_t on viscous delay queue.. (Put
                        // this dupe_record_t there, and the pbuf_t pointer
                        // is already in that dupe_record_t.)
                        src->viscous_queue_size += 1;
                        if (src->viscous_queue_size > src->viscous_queue_space) {
                          src->viscous_queue_space += 16;
                          src->viscous_queue = realloc( src->viscous_queue,
                                                        sizeof(void*) *
                                                        src->viscous_queue_space );
                        }
                        src->viscous_queue[ src->viscous_queue_size -1 ]
                                = dupecheck_get(dupe);
                        
                        if (debug) printf("%ld ENTER VISCOUS QUEUE: len=%d pbuf=%p\n",
                                          tick.tv_sec, src->viscous_queue_size, pb);
                        return; // Put on viscous queue

                } 
        }
        // Send directly to backend
        if (debug>1) printf(".. direct to processing\n");
        digipeater_receive_backend(src, pb);
}

dupecheck_t *digipeater_find_dupecheck(const struct aprx_interface *aif)
{
        int i;
        for (i = 0; i < digi_count; ++i) {
          if (aif == digis[i]->transmitter)
            return digis[i]->dupechecker;
        }
        return NULL;
}

static void digipeater_resettime(void *arg)
{
        struct timeval *tv = (struct timeval *)arg;
        *tv = tick;
}


// Viscous queue processing needs poll digis <source>s for delayed actions
int  digipeater_prepoll(struct aprxpolls *app)
{
        int d, s;

        if (tokenbucket_timer.tv_sec == 0) {
                tokenbucket_timer = tick; // init this..
        }

        // If the time(2) has jumped around a lot,
        // and we didn't get around to do our work, reset the timer.

        if (time_reset) {
                digipeater_resettime(&tokenbucket_timer);
        }
        
        if (tv_timercmp( &tokenbucket_timer, &tick ) <= 0) {
          // Run the digipeater timer handling now
          // Will also advance the timer!
          if (debug>2) printf("digipeater_prepoll() run tokenbucket_timers\n");
          tv_timeradd_seconds( &tokenbucket_timer, &tokenbucket_timer, TOKENBUCKET_INTERVAL);
          run_tokenbucket_timers();
        }

        if (tv_timercmp( &tokenbucket_timer, &app->next_timeout ) <= 0) {
          app->next_timeout = tokenbucket_timer;
        }

        // if (debug>2) printf("digipeater_prepoll - 1 - timeout millis=%d\n",aprxpolls_millis(app));

        // Over all digipeaters..
        for (d = 0; d < digi_count; ++d) {
          struct digipeater *digi = digis[d];
          // Over all sources in those digipeaters
          for (s = 0; s < digi->sourcecount; ++s) {
            struct timeval tv;
            struct digipeater_source * src = digi->sources[s];
            // If viscous delay is zero, there is no work...
            // if (src->viscous_delay == 0)
            //   continue;
            // Delay is non-zero, perhaps there is work?
            if (src->viscous_queue_size == 0) // Empty queue
              continue;
            // First entry expires first
            tv.tv_sec = src->viscous_queue[0]->t + src->viscous_delay;
            tv.tv_usec = 0;
            if (tv_timercmp(&app->next_timeout, &tv) > 0) {
              app->next_timeout = tv;
              // if (debug>2) printf("digipeater_prepoll - 2 - timeout millis=%d\n",aprxpolls_millis(app));
            }
          }
        }

        return 0;
}

static void sourcecalltick(struct digipeater *digi);

int  digipeater_postpoll(struct aprxpolls *app)
{
        int d, s, i, donecount;

        if (tv_timercmp(&tokenbucket_timer, &tick) < 0) {
          tv_timeradd_seconds( &tokenbucket_timer, &tokenbucket_timer, TOKENBUCKET_INTERVAL);
          run_tokenbucket_timers();
        }

        // Over all digipeaters..
        for (d = 0; d < digi_count; ++d) {
          struct digipeater *digi = digis[d];

          // Over all sources in those digipeaters
          for (s = 0; s < digi->sourcecount; ++s) {
            struct digipeater_source * src = digi->sources[s];

            // If viscous delay is zero, there is no work...
            // if (src->viscous_delay == 0)
            //   continue;
            // Delay is non-zero, perhaps there is work?
            if (src->viscous_queue_size == 0) // Empty queue
              continue;
            // Feed backend from viscous queue
            donecount = 0;
            for (i = 0; i < src->viscous_queue_size; ++i) {
              struct dupe_record_t *dupe = src->viscous_queue[i];
              time_t t = dupe->t + src->viscous_delay;
              if ((t - tick.tv_sec) <= 0) {
                if (debug)printf("%ld LEAVE VISCOUS QUEUE: dupe=%p pbuf=%p\n",
                                 tick.tv_sec, dupe, dupe->pbuf);
                if (dupe->pbuf != NULL) {
                  // We send the pbuf from viscous queue, if it still is
                  // present in the dupe record.  (For example direct sourced
                  // packets remove a packet from queued dupe record.)
                  digipeater_receive_backend(src, dupe->pbuf);

                  // Remove the delayed pbuf from this dupe record.
                  pbuf_put(dupe->pbuf);
                  dupe->pbuf = NULL;
                }
                dupecheck_put(dupe);
                ++donecount;
              } else {
                break; // found a case we are not yet interested in.
              }
            }
            if (donecount > 0) {
              if (donecount >= src->viscous_queue_size) {
                // All cleared
                src->viscous_queue_size = 0;
              } else {
                // Compact the queue left after this processing round
                i = src->viscous_queue_size - donecount;
                memcpy(&src->viscous_queue[0],
                       &src->viscous_queue[donecount],
                       sizeof(void*) * i);
                src->viscous_queue_size = i;
              }
            }
          }
        }

        return 0;
}

static int  run_tokenbucket_timers()
{
        int d, s;
        // Over all digipeaters..
        for (d = 0; d < digi_count; ++d) {
          struct digipeater *digi = digis[d];

          digi->tokenbucket += digi->tbf_increment;
          if (digi->tokenbucket > digi->tbf_limit)
            digi->tokenbucket = digi->tbf_limit;

#ifndef DISABLE_IGATE
          sourcecalltick(digi);
#endif

          // Over all sources in those digipeaters
          for (s = 0; s < digi->sourcecount; ++s) {
            struct digipeater_source * src = digi->sources[s];

            src->tokenbucket += src->tbf_increment;
            if (src->tokenbucket > src->tbf_limit)
              src->tokenbucket = src->tbf_limit;

          }
        }

        return 0;
}

#ifndef DISABLE_IGATE
static void sourcecalltick(struct digipeater *digi)
{
        int i;
        historydb_t *db = digi->historydb;
        if (db == NULL) return; // Should never happen..

        for (i = 0; i < HISTORYDB_HASH_MODULO; ++i) {
          history_cell_t *c = db->hash[i];
          for ( ; c != NULL; c = c->next ) {
            c->tokenbucket += digi->src_tbf_increment;
            if (c->tokenbucket > digi->src_tbf_limit)
              c->tokenbucket = digi->src_tbf_limit;
          }
        }
}
#endif

// An utility function that exists at GNU Libc..

#if !defined(HAVE_MEMRCHR) && !defined(_FOR_VALGRIND_)
void   *memrchr(const void *s, int c, size_t n) {
  const unsigned char *p = s;
  c &= 0xFF;
  for (p = s+n; n > 0; --n, --p) {
    if (*p == c) return (void*)p;
  }
  return NULL;
}
#endif
