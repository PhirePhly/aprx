
#include "config.h"

#ifdef HAVE_OPENSSL_SSL_H
#define USE_SSL
#endif

#ifndef SSL_H
#define SSL_H

#ifdef USE_SSL

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/conf.h>
#include <openssl/engine.h>
#include <openssl/evp.h>

/* ssl error codes, must match ssl_err_labels order */
#define SSL_VALIDATE_INTERNAL_ERROR -1
#define SSL_VALIDATE_CLIENT_CERT_UNVERIFIED -2
#define SSL_VALIDATE_NO_CLIENT_CERT -3
#define SSL_VALIDATE_CERT_NO_SUBJECT -4
#define SSL_VALIDATE_CERT_NO_CALLSIGN -5
#define SSL_VALIDATE_CERT_CALLSIGN_MISMATCH -6

struct client_t;
struct worker_t;

struct ssl_t {
	SSL_CTX *ctx;
	
	unsigned	validate;
};

struct ssl_connection_t {
	SSL             *connection;
	
	unsigned	handshaked:1;
	
	unsigned	renegotiation:1;
	unsigned	buffer:1;
	unsigned	no_wait_shutdown:1;
	unsigned	no_send_shutdown:1;
	
	unsigned	validate;
	int		ssl_err_code;
};

#define NGX_SSL_SSLv2    0x0002
#define NGX_SSL_SSLv3    0x0004
#define NGX_SSL_TLSv1    0x0008
#define NGX_SSL_TLSv1_1  0x0010
#define NGX_SSL_TLSv1_2  0x0020


#define NGX_SSL_BUFFER   1
#define NGX_SSL_CLIENT   2

#define NGX_SSL_BUFSIZE  16384

/* string representations for error codes */
extern const char *ssl_strerror(int code);

/* initialize and deinit the library */
extern int ssl_init(void);
extern void ssl_atend(void);

/* per-listener structure allocators */
extern struct ssl_t *ssl_alloc(void);
extern void ssl_free(struct ssl_t *ssl);

/* create context for listener, load certs */
extern int ssl_create(struct ssl_t *ssl, void *data);
extern int ssl_certificate(struct ssl_t *ssl, const char *certfile, const char *keyfile);
extern int ssl_ca_certificate(struct ssl_t *ssl, const char *cafile, int depth);

/* create / free connection */
extern int ssl_create_connection(struct ssl_t *ssl, struct client_t *c, int i_am_client);
extern void ssl_free_connection(struct client_t *c);

/* validate a client certificate */
extern int ssl_validate_peer_cert_phase1(struct client_t *c);
extern int ssl_validate_peer_cert_phase2(struct client_t *c);

extern int ssl_write(struct worker_t *self, struct client_t *c);
extern int ssl_writable(struct worker_t *self, struct client_t *c);
extern int ssl_readable(struct worker_t *self, struct client_t *c);


#else

struct ssl_t {
};


#define ssl_init(...) { }
#define ssl_atend(...) { }

#endif /* USE_SSL */
#endif /* SSL_H */

