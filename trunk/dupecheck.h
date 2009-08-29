/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation receive-only APRS-i-gate with            *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2009                            *
 *                                                                  *
 * **************************************************************** */

/*
 * Dupecheck interface
 *
 */


struct dupe_record_t {
	struct dupe_record_t *next;
	uint32_t hash;
	time_t	 t;
	int	 alen;	// Address length
	int	 plen;	// Payload length
	char	 addresses[20];
	char	*packet;
	char	 packetbuf[200]; /* 99.9+ % of time this is enough.. */
};

#define DUPECHECK_DB_SIZE 64        /* Hash index table size - per dupechecker */

typedef struct dupecheck_t {
	struct dupecheck_t *next;
	struct dupe_record_t *dupecheck_db[DUPECHECK_DB_SIZE]; /* Hash index table */
	

} dupecheck_t;


extern void         dupecheck_init(void);	/* Inits the dupechecker subsystem */
extern dupecheck_t *new_dupecheck(void);	/* Makes a new dupechecker  */
extern void         dupecheck_cleanup(void);   /* Regular cleaner */
extern int	    dupecheck(dupecheck_t *dp, const char *addr, const int alen, const char *data, const int dlen); /* the checker */
