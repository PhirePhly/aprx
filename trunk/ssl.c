
/*
 *	This OpenSSL interface code has been proudly copied from
 *	the excellent NGINX web server.
 *
 *	Its license is reproduced here.
 */

/* 
 * Copyright (C) 2002-2013 Igor Sysoev
 * Copyright (C) 2011-2013 Nginx, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 *	OpenSSL thread-safe example code (ssl_thread_* functions) have been
 *	proudly copied from the excellent CURL package, the original author
 *	is Jeremy Brown.
 *
 *	https://github.com/bagder/curl/blob/master/docs/examples/opensslthreadlock.c
 *
 * COPYRIGHT AND PERMISSION NOTICE
 * Copyright (c) 1996 - 2013, Daniel Stenberg, <daniel@haxx.se>.
 *
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright
 * notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall not
 * be used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization of the copyright holder.
 *	
 */

#include "config.h"
#include "ssl.h"
#include "hlog.h"
#include "hmalloc.h"
#include "worker.h"

#ifdef USE_SSL

#include <ctype.h>
#include <pthread.h>
#include <openssl/conf.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define SSL_DEFAULT_CIPHERS     "HIGH:!aNULL:!MD5"
#define SSL_PROTOCOLS (NGX_SSL_SSLv3|NGX_SSL_TLSv1 |NGX_SSL_TLSv1_1|NGX_SSL_TLSv1_2)

/* ssl error strings */
#define SSL_ERR_LABELS_COUNT 6
static const char *ssl_err_labels[][2] = {
	{ "invalid_err", "Invalid, unknown error" },
	{ "internal_err", "Internal error" },
	{ "peer_cert_unverified", "Peer certificate is not valid or not trusted" },
	{ "no_peer_cert", "Peer did not present a certificate" },
	{ "cert_no_subj", "Certificate does not contain a Subject field" },
	{ "cert_no_callsign", "Certificate does not contain a TQSL callsign in CN - not a ham cert" },
	{ "cert_callsign_mismatch", "Certificate callsign does not match login username" }
};

/* pthread wrapping for openssl */
#define MUTEX_TYPE       pthread_mutex_t
#define MUTEX_SETUP(x)   pthread_mutex_init(&(x), NULL)
#define MUTEX_CLEANUP(x) pthread_mutex_destroy(&(x))
#define MUTEX_LOCK(x)    pthread_mutex_lock(&(x))
#define MUTEX_UNLOCK(x)  pthread_mutex_unlock(&(x))
#define THREAD_ID        pthread_self(  )

int  ssl_available;
int  ssl_connection_index;
int  ssl_server_conf_index;
int  ssl_session_cache_index;

/* This array will store all of the mutexes available to OpenSSL. */
static MUTEX_TYPE *mutex_buf= NULL;

static void ssl_thread_locking_function(int mode, int n, const char * file, int line)
{
	if (mode & CRYPTO_LOCK)
		MUTEX_LOCK(mutex_buf[n]);
	else
		MUTEX_UNLOCK(mutex_buf[n]);
}

static unsigned long ssl_thread_id_function(void)
{
	return ((unsigned long)THREAD_ID);
}

static int ssl_thread_setup(void)
{
	int i;
	
	hlog(LOG_DEBUG, "Creating OpenSSL mutexes (%d)...", CRYPTO_num_locks());
	
	mutex_buf = hmalloc(CRYPTO_num_locks() * sizeof(MUTEX_TYPE));
	
	for (i = 0;  i < CRYPTO_num_locks();  i++)
		MUTEX_SETUP(mutex_buf[i]);
	
	CRYPTO_set_id_callback(ssl_thread_id_function);
	CRYPTO_set_locking_callback(ssl_thread_locking_function);
	
	return 0;
}

static int ssl_thread_cleanup(void)
{
	int i;
	
	if (!mutex_buf)
		return 0;
	
	CRYPTO_set_id_callback(NULL);
	CRYPTO_set_locking_callback(NULL);
	
	for (i = 0;  i < CRYPTO_num_locks(  );  i++)
		MUTEX_CLEANUP(mutex_buf[i]);
		
	hfree(mutex_buf);
	mutex_buf = NULL;
	
	return 0;
}

/*
 *	string representations for error codes
 */

const char *ssl_strerror(int code)
{
	code *= -1;
	
	if (code >= 0 && code < (sizeof ssl_err_labels / sizeof ssl_err_labels[0]))
		return ssl_err_labels[code][1];
		
	return ssl_err_labels[0][1];
}

/*
 *	Clear OpenSSL error queue
 */

static void ssl_error(int level, const char *msg)
{
	unsigned long n;
	char errstr[512];
	
	for ( ;; ) {
		n = ERR_get_error();
		
		if (n == 0)
			break;
		
		ERR_error_string_n(n, errstr, sizeof(errstr));
		errstr[sizeof(errstr)-1] = 0;
		
		hlog(level, "%s (%d): %s", msg, n, errstr);
	}
}

static void ssl_clear_error(void)
{
	while (ERR_peek_error()) {
		ssl_error(LOG_INFO, "Ignoring stale SSL error");
	}
	
	ERR_clear_error();
}

/*
 *	TrustedQSL custom X.509 certificate objects
 */

#define TRUSTEDQSL_OID				"1.3.6.1.4.1.12348.1."
#define TRUSTEDQSL_OID_CALLSIGN			TRUSTEDQSL_OID "1"
#define TRUSTEDQSL_OID_QSO_NOT_BEFORE		TRUSTEDQSL_OID "2"
#define TRUSTEDQSL_OID_QSO_NOT_AFTER		TRUSTEDQSL_OID "3"
#define TRUSTEDQSL_OID_DXCC_ENTITY		TRUSTEDQSL_OID "4"
#define TRUSTEDQSL_OID_SUPERCEDED_CERT		TRUSTEDQSL_OID "5"
#define TRUSTEDQSL_OID_CRQ_ISSUER_ORGANIZATION	TRUSTEDQSL_OID "6"
#define TRUSTEDQSL_OID_CRQ_ISSUER_ORGANIZATIONAL_UNIT TRUSTEDQSL_OID "7"

static const char *tqsl_NIDs[][2] = {
	{ TRUSTEDQSL_OID_CALLSIGN, "AROcallsign" },
	{ TRUSTEDQSL_OID_QSO_NOT_BEFORE, "QSONotBeforeDate" },
	{ TRUSTEDQSL_OID_QSO_NOT_AFTER, "QSONotAfterDate" },
	{ TRUSTEDQSL_OID_DXCC_ENTITY, "dxccEntity" },
	{ TRUSTEDQSL_OID_SUPERCEDED_CERT, "supercededCertificate" },
	{ TRUSTEDQSL_OID_CRQ_ISSUER_ORGANIZATION, "tqslCRQIssuerOrganization" },
	{ TRUSTEDQSL_OID_CRQ_ISSUER_ORGANIZATIONAL_UNIT, "tqslCRQIssuerOrganizationalUnit" },
};

static int load_tqsl_custom_objects(void)
{
	int i;
	
	for (i = 0; i < (sizeof tqsl_NIDs / sizeof tqsl_NIDs[0]); ++i)
		if (OBJ_create(tqsl_NIDs[i][0], tqsl_NIDs[i][1], NULL) == 0)
			return -1;
	
	return 0;
}

static void ssl_info_callback(SSL *ssl, int where, int ret)
{
	struct client_t *c = SSL_get_ex_data(ssl, ssl_connection_index);
	
	if (!c) {
		hlog(LOG_ERR, "ssl_info_callback: no application data for connection");
		return;
	}
	
	struct ssl_connection_t *ssl_conn = c->ssl_con;
	
	if (!ssl_conn) {
		hlog(LOG_ERR, "ssl_info_callback: no ssl_conn for connection");
		return;
	}
	
	if (where & SSL_CB_HANDSHAKE_START) {
		hlog(LOG_INFO, "%s/%d: SSL handshake start", c->addr_rem, c->fd);
		if (ssl_conn->handshaked) {
			ssl_conn->renegotiation = 1;
		}
	}
	
	if (where & SSL_CB_HANDSHAKE_DONE) {
		hlog(LOG_INFO, "%s/%d: SSL handshake done", c->addr_rem, c->fd);
	}
}

/*
 *	Initialize SSL
 */

int ssl_init(void)
{
	hlog(LOG_INFO, "Initializing OpenSSL, built against %s ...", OPENSSL_VERSION_TEXT);
	
	OPENSSL_config(NULL);
	
	SSL_library_init();
	SSL_load_error_strings();
	
	ssl_thread_setup();
	
	OpenSSL_add_all_algorithms();
	
	load_tqsl_custom_objects();
	
#if OPENSSL_VERSION_NUMBER >= 0x0090800fL
#ifndef SSL_OP_NO_COMPRESSION
	{
	/*
	 * Disable gzip compression in OpenSSL prior to 1.0.0 version,
	 * this saves about 522K per connection.
	 */
	int                  n;
	STACK_OF(SSL_COMP)  *ssl_comp_methods;
	
	ssl_comp_methods = SSL_COMP_get_compression_methods();
	n = sk_SSL_COMP_num(ssl_comp_methods);
	
	while (n--) {
		(void) sk_SSL_COMP_pop(ssl_comp_methods);
	}
	
	}
#endif
#endif

	ssl_connection_index = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
	
	if (ssl_connection_index == -1) {
		ssl_error(LOG_ERR, "SSL_get_ex_new_index for connection");
		return -1;
	}
	
	ssl_server_conf_index = SSL_CTX_get_ex_new_index(0, NULL, NULL, NULL, NULL);
	
	if (ssl_server_conf_index == -1) {
		ssl_error(LOG_ERR, "SSL_CTX_get_ex_new_index for conf");
		return -1;
	}
	
	ssl_session_cache_index = SSL_CTX_get_ex_new_index(0, NULL, NULL, NULL, NULL);
	
	if (ssl_session_cache_index == -1) {
		ssl_error(LOG_ERR, "SSL_CTX_get_ex_new_index for session cache");
		return -1;
	}
	
	ssl_available = 1;
	
	return 0;
}

void ssl_atend(void)
{
	ssl_thread_cleanup();
}

struct ssl_t *ssl_alloc(void)
{
	struct ssl_t *ssl;
	
	ssl = hmalloc(sizeof(*ssl));
	memset(ssl, 0, sizeof(*ssl));
	
	return ssl;
}

void ssl_free(struct ssl_t *ssl)
{
	if (ssl->ctx)
	    SSL_CTX_free(ssl->ctx);
	    
	hfree(ssl);
}

int ssl_create(struct ssl_t *ssl, void *data)
{
	ssl->ctx = SSL_CTX_new(SSLv23_method());
	
	if (ssl->ctx == NULL) {
		ssl_error(LOG_ERR, "ssl_create SSL_CTX_new failed");
		return -1;
	}
	
	if (SSL_CTX_set_ex_data(ssl->ctx, ssl_server_conf_index, data) == 0) {
		ssl_error(LOG_ERR, "ssl_create SSL_CTX_set_ex_data failed");
		return -1;
	}
	
	/* client side options */
	
	SSL_CTX_set_options(ssl->ctx, SSL_OP_MICROSOFT_SESS_ID_BUG);
	SSL_CTX_set_options(ssl->ctx, SSL_OP_NETSCAPE_CHALLENGE_BUG);
	
	/* server side options */
	
	SSL_CTX_set_options(ssl->ctx, SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG);
	SSL_CTX_set_options(ssl->ctx, SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER);
	
	/* this option allow a potential SSL 2.0 rollback (CAN-2005-2969) */
	SSL_CTX_set_options(ssl->ctx, SSL_OP_MSIE_SSLV2_RSA_PADDING);
	
	SSL_CTX_set_options(ssl->ctx, SSL_OP_SSLEAY_080_CLIENT_DH_BUG);
	SSL_CTX_set_options(ssl->ctx, SSL_OP_TLS_D5_BUG);
	SSL_CTX_set_options(ssl->ctx, SSL_OP_TLS_BLOCK_PADDING_BUG);
	
	SSL_CTX_set_options(ssl->ctx, SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS);
	
	SSL_CTX_set_options(ssl->ctx, SSL_OP_SINGLE_DH_USE);
	
	/* SSL protocols not configurable for now */
	int protocols = SSL_PROTOCOLS;
	
	if (!(protocols & NGX_SSL_SSLv2)) {
		SSL_CTX_set_options(ssl->ctx, SSL_OP_NO_SSLv2);
	}
	if (!(protocols & NGX_SSL_SSLv3)) {
		SSL_CTX_set_options(ssl->ctx, SSL_OP_NO_SSLv3);
	}
	if (!(protocols & NGX_SSL_TLSv1)) {
		SSL_CTX_set_options(ssl->ctx, SSL_OP_NO_TLSv1);
	}
	
#ifdef SSL_OP_NO_TLSv1_1
	if (!(protocols & NGX_SSL_TLSv1_1)) {
		SSL_CTX_set_options(ssl->ctx, SSL_OP_NO_TLSv1_1);
	}
#endif
#ifdef SSL_OP_NO_TLSv1_2
	if (!(protocols & NGX_SSL_TLSv1_2)) {
		SSL_CTX_set_options(ssl->ctx, SSL_OP_NO_TLSv1_2);
	}
#endif

#ifdef SSL_OP_NO_COMPRESSION
	SSL_CTX_set_options(ssl->ctx, SSL_OP_NO_COMPRESSION);
#endif

#ifdef SSL_MODE_RELEASE_BUFFERS
	SSL_CTX_set_mode(ssl->ctx, SSL_MODE_RELEASE_BUFFERS);
#endif

	SSL_CTX_set_mode(ssl->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
	
	SSL_CTX_set_read_ahead(ssl->ctx, 1);
	
	SSL_CTX_set_info_callback(ssl->ctx, (void *)ssl_info_callback);
	
	if (SSL_CTX_set_cipher_list(ssl->ctx, SSL_DEFAULT_CIPHERS) == 0) {
		ssl_error(LOG_ERR, "ssl_create SSL_CTX_set_cipher_list failed");
		return -1;
	}
	
	/* prefer server-selected ciphers */
	SSL_CTX_set_options(ssl->ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
	
	return 0;
}

/*
 *	Load server key and certificate
 */

int ssl_certificate(struct ssl_t *ssl, const char *certfile, const char *keyfile)
{
	if (SSL_CTX_use_certificate_chain_file(ssl->ctx, certfile) == 0) {
		hlog(LOG_ERR, "Error while loading SSL certificate chain file \"%s\"", certfile);
		ssl_error(LOG_ERR, "SSL_CTX_use_certificate_chain_file");
		return -1;
	}
	
	
	if (SSL_CTX_use_PrivateKey_file(ssl->ctx, keyfile, SSL_FILETYPE_PEM) == 0) {
		hlog(LOG_ERR, "Error while loading SSL private key file \"%s\"", keyfile);
		ssl_error(LOG_ERR, "SSL_CTX_use_PrivateKey_file");
		return -1;
	}
	
	if (!SSL_CTX_check_private_key(ssl->ctx)) {
		hlog(LOG_ERR, "SSL private key (%s) does not work with this certificate (%s)", keyfile, certfile);
		ssl_error(LOG_ERR, "SSL_CTX_check_private_key");
		return -1;
	}
	
	
	return 0;
}

static int ssl_verify_callback(int ok, X509_STORE_CTX *x509_store)
{
	hlog(LOG_DEBUG, "ssl_verify_callback, ok: %d", ok);
	
#if (NGX_DEBUG)
    char              *subject, *issuer;
    int                err, depth;
    X509              *cert;
    X509_NAME         *sname, *iname;
    ngx_connection_t  *c;
    ngx_ssl_conn_t    *ssl_conn;

    ssl_conn = X509_STORE_CTX_get_ex_data(x509_store,
                                          SSL_get_ex_data_X509_STORE_CTX_idx());

    c = ngx_ssl_get_connection(ssl_conn);

    cert = X509_STORE_CTX_get_current_cert(x509_store);
    err = X509_STORE_CTX_get_error(x509_store);
    depth = X509_STORE_CTX_get_error_depth(x509_store);

    sname = X509_get_subject_name(cert);
    subject = sname ? X509_NAME_oneline(sname, NULL, 0) : "(none)";

    iname = X509_get_issuer_name(cert);
    issuer = iname ? X509_NAME_oneline(iname, NULL, 0) : "(none)";

    ngx_log_debug5(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "verify:%d, error:%d, depth:%d, "
                   "subject:\"%s\",issuer: \"%s\"",
                   ok, err, depth, subject, issuer);

    if (sname) {
        OPENSSL_free(subject);
    }

    if (iname) {
        OPENSSL_free(issuer);
    }
#endif

    return 1;
}


/*
 *	Load trusted CA certs for verifying our peers
 */

int ssl_ca_certificate(struct ssl_t *ssl, const char *cafile, int depth)
{
	STACK_OF(X509_NAME)  *list;
	
	SSL_CTX_set_verify(ssl->ctx, SSL_VERIFY_PEER, ssl_verify_callback);
	SSL_CTX_set_verify_depth(ssl->ctx, depth);
	
	if (SSL_CTX_load_verify_locations(ssl->ctx, cafile, NULL) == 0) {
		hlog(LOG_ERR, "Failed to load trusted CA list from \"%s\"", cafile);
		ssl_error(LOG_ERR, "SSL_CTX_load_verify_locations");
		return -1;
	}
	
	list = SSL_load_client_CA_file(cafile);
	
	if (list == NULL) {
		hlog(LOG_ERR, "Failed to load client CA file from \"%s\"", cafile);
		ssl_error(LOG_ERR, "SSL_load_client_CA_file");
		return -1;
	}
	
	/*
	 * before 0.9.7h and 0.9.8 SSL_load_client_CA_file()
	 * always leaved an error in the error queue
	 */
	
	ERR_clear_error();
	
	SSL_CTX_set_client_CA_list(ssl->ctx, list);
	
	ssl->validate = 1;
	
	return 0;
}

/*
 *	Create a connect
 */

int ssl_create_connection(struct ssl_t *ssl, struct client_t *c, int i_am_client)
{
	struct ssl_connection_t  *sc;
	
	sc = hmalloc(sizeof(*sc));
	sc->connection = SSL_new(ssl->ctx);
	
	if (sc->connection == NULL) {
		ssl_error(LOG_ERR, "SSL_new failed");
		hfree(sc);
		return -1;
	}
	
	if (SSL_set_fd(sc->connection, c->fd) == 0) {
		ssl_error(LOG_ERR, "SSL_set_fd failed");
		SSL_free(sc->connection);
		hfree(sc);
		return -1;
	}
	
	if (i_am_client) {
		SSL_set_connect_state(sc->connection);
	} else {
		SSL_set_accept_state(sc->connection);
	}
	
	if (SSL_set_ex_data(sc->connection, ssl_connection_index, c) == 0) {
		ssl_error(LOG_ERR, "SSL_set_ex_data failed");
		SSL_free(sc->connection);
		hfree(sc);
		return -1;
	}
	
	sc->validate = ssl->validate;
	c->ssl_con = sc;
	
	return 0;
}

void ssl_free_connection(struct client_t *c)
{
	if (!c->ssl_con)
		return;
		
	SSL_free(c->ssl_con->connection);
	hfree(c->ssl_con);
	c->ssl_con = NULL;
}

int ssl_cert_callsign_match(const char *subj_call, const char *username)
{
	if (subj_call == NULL || username == NULL)
		return 0;
	
	while (*username != '-' && *username != 0 && *subj_call != 0) {
		if (toupper(*username) != toupper(*subj_call))
			return 0; /* mismatch */
		
		subj_call++;
		username++;
	}
	
	if (*subj_call != 0)
		return 0; /* if username is shorter than subject callsign, we fail */
	
	if (*username != '-' && *username != 0)
		return 0; /* if we ran to end of subject callsign but not to end of username or start of SSID, we fail */
	
	return 1;
}


/*
 *	Validate client certificate
 */

int ssl_validate_peer_cert_phase1(struct client_t *c)
{
	X509 *cert;
	
	int rc = SSL_get_verify_result(c->ssl_con->connection);
	
	if (rc != X509_V_OK) {
		/* client gave a certificate, but it's not valid */
		hlog(LOG_DEBUG, "%s/%s: Peer SSL certificate verification error %d: %s",
			c->addr_rem, c->username, rc, X509_verify_cert_error_string(rc));
		c->ssl_con->ssl_err_code = rc;
		return SSL_VALIDATE_CLIENT_CERT_UNVERIFIED;
	}
	
	cert = SSL_get_peer_certificate(c->ssl_con->connection);
	
	if (cert == NULL) {
		/* client did not give a certificate */
		return SSL_VALIDATE_NO_CLIENT_CERT;
	}
	
	X509_free(cert);
	
	return 0;
}

int ssl_validate_peer_cert_phase2(struct client_t *c)
{
	int ret = -1;
	X509 *cert;
	X509_NAME *sname, *iname;
	char *subject, *issuer;
	char *subj_cn = NULL;
	char *subj_call = NULL;
	int nid, idx;
	X509_NAME_ENTRY *entry;
	ASN1_STRING *edata;
	
	cert = SSL_get_peer_certificate(c->ssl_con->connection);
	
	if (cert == NULL) {
		/* client did not give a certificate */
		return SSL_VALIDATE_NO_CLIENT_CERT;
	}
	
	/* ok, we have a cert, find subject */
	sname = X509_get_subject_name(cert);
	if (!sname) {
		ret = SSL_VALIDATE_CERT_NO_SUBJECT;
		goto fail;
	}
	subject = X509_NAME_oneline(sname, NULL, 0);
	
	/* find tqsl callsign */
	nid = OBJ_txt2nid("AROcallsign");
	if (nid == NID_undef) {
		hlog(LOG_ERR, "OBJ_txt2nid could not find NID for AROcallsign");
		ret = SSL_VALIDATE_INTERNAL_ERROR;
		goto fail;
	}
	
	idx = X509_NAME_get_index_by_NID(sname, nid, -1);
	if (idx == -1) {
		hlog(LOG_DEBUG, "%s/%s: peer certificate has no callsign: %s", c->addr_rem, c->username, subject);
		ret = SSL_VALIDATE_CERT_NO_CALLSIGN;
		goto fail;
	}
	
	entry = X509_NAME_get_entry(sname, idx);
	if (entry != NULL) {
		edata = X509_NAME_ENTRY_get_data(entry);
		if (edata != NULL)
			ASN1_STRING_to_UTF8((unsigned char **)&subj_call, edata);
	}
	
	/* find CN of subject */
	idx = X509_NAME_get_index_by_NID(sname, NID_commonName, -1);
	if (idx == -1) {
		hlog(LOG_DEBUG, "%s/%s: peer certificate has no CN: %s", c->addr_rem, c->username, subject);
	} else {
		entry = X509_NAME_get_entry(sname, idx);
		if (entry != NULL) {
			edata = X509_NAME_ENTRY_get_data(entry);
			if (edata != NULL)
				ASN1_STRING_to_UTF8((unsigned char **)&subj_cn, edata);
		}
	}
	
	if (!subj_call) {
		hlog(LOG_DEBUG, "%s/%s: peer certificate callsign conversion failed: %s", c->addr_rem, c->username, subject);
		ret = SSL_VALIDATE_CERT_NO_CALLSIGN;
		goto fail;
	}
	
	if (!ssl_cert_callsign_match(subj_call, c->username)) {
		ret = SSL_VALIDATE_CERT_CALLSIGN_MISMATCH;
		goto fail;
	}
	
	/* find issuer */
	iname = X509_get_issuer_name(cert);
	issuer = iname ? X509_NAME_oneline(iname, NULL, 0) : "(none)";
	
	ret = 0;
	hlog(LOG_INFO, "%s/%s: Peer validated using SSL certificate: subject '%s' callsign '%s' CN '%s' issuer '%s'",
		c->addr_rem, c->username, subject, subj_call, (subj_cn) ? subj_cn : "(none)", issuer);
	
	/* store copies of cert subject and issuer */
	strncpy(c->cert_subject, subject, sizeof(c->cert_subject));
	c->cert_subject[sizeof(c->cert_subject)-1] = 0;
	strncpy(c->cert_issuer, issuer, sizeof(c->cert_issuer));
	c->cert_issuer[sizeof(c->cert_issuer)-1] = 0;
	
fail:	
	/* free up whatever we allocated */
	X509_free(cert);
	
	if (subj_call)
		OPENSSL_free(subj_call);
	if (subj_cn)
		OPENSSL_free(subj_cn);
	
	return ret;
}

/*
 *	Write data to an SSL socket
 */

int ssl_write(struct worker_t *self, struct client_t *c)
{
	int n;
	int sslerr;
	int err;
	int to_write;
	
	to_write = c->obuf_end - c->obuf_start;
	
	//hlog(LOG_DEBUG, "ssl_write fd %d of %d bytes", c->fd, to_write);
	ssl_clear_error();
	
	n = SSL_write(c->ssl_con->connection, c->obuf + c->obuf_start, to_write);
	
	//hlog(LOG_DEBUG, "SSL_write fd %d returned %d", c->fd, n);
	
	if (n > 0) {
		/* ok, we wrote some */
		c->obuf_start += n;
		c->obuf_wtime = tick;
		
		/* All done ? */
		if (c->obuf_start >= c->obuf_end) {
			//hlog(LOG_DEBUG, "ssl_write fd %d (%s) obuf empty", c->fd, c->addr_rem);
			c->obuf_start = 0;
			c->obuf_end   = 0;
			
			/* tell the poller that we have no outgoing data */
			xpoll_outgoing(&self->xp, c->xfd, 0);
			return n;
		}
		
		xpoll_outgoing(&self->xp, c->xfd, 1);
		
		return n;
	}
	
	sslerr = SSL_get_error(c->ssl_con->connection, n);
	err = (sslerr == SSL_ERROR_SYSCALL) ? errno : 0;
	
	if (sslerr == SSL_ERROR_WANT_WRITE) {
		hlog(LOG_INFO, "ssl_write fd %d: SSL_write wants to write again, marking socket for write events", c->fd);
		
		/* tell the poller that we have outgoing data */
		xpoll_outgoing(&self->xp, c->xfd, 1);
		
		return 0;
	}
	
	if (sslerr == SSL_ERROR_WANT_READ) {
		hlog(LOG_INFO, "ssl_write fd %d: SSL_write wants to read, returning 0", c->fd);
		
		/* tell the poller that we won't be writing now, until we've read... */
		xpoll_outgoing(&self->xp, c->xfd, 0);
		
		return 0;
	}
	
	if (err) {
		hlog(LOG_DEBUG, "ssl_write fd %d: I/O syscall error: %s", c->fd, strerror(err));
	} else {
		char ebuf[255];
		
		ERR_error_string_n(sslerr, ebuf, sizeof(ebuf));
		hlog(LOG_INFO, "ssl_write fd %d failed with ret %d sslerr %u errno %d: %s (%s)",
			c->fd, n, sslerr, err, ebuf, ERR_reason_error_string(sslerr));
	}
	
	c->ssl_con->no_wait_shutdown = 1;
	c->ssl_con->no_send_shutdown = 1;
	
	hlog(LOG_DEBUG, "ssl_write fd %d: SSL_write() failed", c->fd);
	client_close(self, c, err);
	
	return -13;
}

int ssl_writable(struct worker_t *self, struct client_t *c)
{
	int to_write;
	
	to_write = c->obuf_end - c->obuf_start;
	
	//hlog(LOG_DEBUG, "ssl_writable fd %d, %d available for writing", c->fd, to_write);
	
	/* SSL_write does not appreciate writing a 0-length buffer */
	if (to_write == 0) {
		/* tell the poller that we have no outgoing data */
		xpoll_outgoing(&self->xp, c->xfd, 0);
		return 0;
	}
	
	return ssl_write(self, c);
}

int ssl_readable(struct worker_t *self, struct client_t *c)
{
	int r;
	int sslerr, err;
	
	//hlog(LOG_DEBUG, "ssl_readable fd %d", c->fd);
	
	ssl_clear_error();
	
	r = SSL_read(c->ssl_con->connection, c->ibuf + c->ibuf_end, c->ibuf_size - c->ibuf_end - 1);
	
	if (r > 0) {
		/* we got some data... process */
		//hlog(LOG_DEBUG, "SSL_read fd %d returned %d bytes of data", c->fd, r);
		
		/* TODO: whatever the client_readable does */
		return client_postread(self, c, r);
	}
	
	sslerr = SSL_get_error(c->ssl_con->connection, r);
	err = (sslerr == SSL_ERROR_SYSCALL) ? errno : 0;
	
	if (sslerr == SSL_ERROR_WANT_READ) {
		hlog(LOG_DEBUG, "ssl_readable fd %d: SSL_read wants to read again, doing it later", c->fd);
		
		if (c->obuf_end - c->obuf_start > 0) {
			/* tell the poller that we have outgoing data */
			xpoll_outgoing(&self->xp, c->xfd, 1);
		}
		
		return 0;
	}
	
	if (sslerr == SSL_ERROR_WANT_WRITE) {
		hlog(LOG_INFO, "ssl_readable fd %d: SSL_read wants to write (peer starts SSL renegotiation?), calling ssl_write", c->fd);
		return ssl_write(self, c);
	}
	
	c->ssl_con->no_wait_shutdown = 1;
	c->ssl_con->no_send_shutdown = 1;
	
	if (sslerr == SSL_ERROR_ZERO_RETURN || ERR_peek_error() == 0) {
		hlog(LOG_DEBUG, "ssl_readable fd %d: peer shutdown SSL cleanly", c->fd);
		client_close(self, c, CLIERR_EOF);
		return -1;
	}
	
	if (err) {
		hlog(LOG_DEBUG, "ssl_readable fd %d: I/O syscall error: %s", c->fd, strerror(err));
	} else {
		char ebuf[255];
		
		ERR_error_string_n(sslerr, ebuf, sizeof(ebuf));
		hlog(LOG_INFO, "ssl_readable fd %d failed with ret %d sslerr %d errno %d: %s (%s)",
			c->fd, r, sslerr, err, ebuf, ERR_reason_error_string(sslerr));
	}
	
	client_close(self, c, err);
	return -1;
}


#endif

