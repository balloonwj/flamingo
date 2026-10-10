/************************************************************************************
  Copyright (C) 2012 Monty Program AB

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not see <http://www.gnu.org/licenses>
  or write to the Free Software Foundation, Inc.,
  51 Franklin St., Fifth Floor, Boston, MA 02110, USA

 *************************************************************************************/
#include <ma_global.h>
#include <ma_sys.h>
#include <ma_common.h>
#include <ma_pvio.h>
#include <errmsg.h>
#include <string.h>
#include <mysql/client_plugin.h>
#include <string.h>
#include <openssl/ssl.h> /* SSL and SSL_CTX */
#include <openssl/err.h> /* error reporting */
#include <openssl/conf.h>
#include <openssl/md4.h>
#include <ma_tls.h>
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#include <time.h>
#endif
#include <ma_crypt.h>

#if defined(_WIN32) && !defined(_OPENSSL_Applink) && defined(HAVE_OPENSSL_APPLINK_C)
#include <openssl/applink.c>
#endif

#if OPENSSL_VERSION_NUMBER >= 0x10002000L && !defined(LIBRESSL_VERSION_NUMBER)
#include <openssl/x509v3.h>
#define HAVE_OPENSSL_CHECK_HOST 1
#endif

#if OPENSSL_VERSION_NUMBER >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
#define HAVE_OPENSSL_1_1_API
#endif

#if OPENSSL_VERSION_NUMBER < 0x10000000L
#define SSL_OP_NO_TLSv1_1 0L
#define SSL_OP_NO_TLSv1_2 0L
#define CRYPTO_THREADID_set_callback CRYPTO_set_id_callback
#define CRYPTO_THREADID_get_callback CRYPTO_get_id_callback
#endif

#if defined(OPENSSL_USE_BIOMETHOD)
#undef OPENSSL_USE_BIOMETHOD
#endif
#ifndef HAVE_OPENSSL_DEFAULT
#include <memory.h>
#define ma_malloc(A,B) malloc((A))
#undef ma_free
#define ma_free(A) free((A))
#define ma_snprintf snprintf
#define ma_vsnprintf vsnprintf
#undef SAFE_MUTEX
#endif
#include <ma_pthread.h>

#include <mariadb_async.h>
#include <ma_context.h>

extern my_bool ma_tls_initialized;
extern unsigned int mariadb_deinitialize_ssl;

#define MAX_SSL_ERR_LEN 100
char tls_library_version[TLS_VERSION_LENGTH];

static pthread_mutex_t LOCK_openssl_config;
#ifndef HAVE_OPENSSL_1_1_API
static pthread_mutex_t *LOCK_crypto= NULL;
#endif
#if defined(OPENSSL_USE_BIOMETHOD)
static int ma_bio_read(BIO *h, char *buf, int size);
static int ma_bio_write(BIO *h, const char *buf, int size);
static BIO_METHOD ma_BIO_method;
#endif

static int ma_verification_callback(int preverify_ok, X509_STORE_CTX *ctx);

static long ma_tls_version_options(const char *version)
{
  long protocol_options,
       disable_all_protocols;

  protocol_options= disable_all_protocols=
    SSL_OP_NO_SSLv2 |
    SSL_OP_NO_SSLv3 |
    SSL_OP_NO_TLSv1 |
    SSL_OP_NO_TLSv1_1 |
    SSL_OP_NO_TLSv1_2
#ifdef TLS1_3_VERSION
    | SSL_OP_NO_TLSv1_3
#endif
    ;

  if (!version)
    return 0;

  if (strstr(version, "TLSv1.1"))
    protocol_options&= ~SSL_OP_NO_TLSv1_1;
  if (strstr(version, "TLSv1.2"))
    protocol_options&= ~SSL_OP_NO_TLSv1_2;
#ifdef TLS1_3_VERSION
  if (strstr(version, "TLSv1.3"))
    protocol_options&= ~SSL_OP_NO_TLSv1_3;
#endif

  if (protocol_options != disable_all_protocols)
    return protocol_options;
  return 0;
}

static void ma_tls_set_error(MYSQL *mysql)
{
  ulong ssl_errno= ERR_get_error();
  char  ssl_error[MAX_SSL_ERR_LEN];
  const char *ssl_error_reason;
  MARIADB_PVIO *pvio= mysql->net.pvio;
  int save_errno= errno;

  if (ssl_errno && (ssl_error_reason= ERR_reason_error_string(ssl_errno)))
  {
    pvio->set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, 
                   0, ssl_error_reason);
    return;
  } else if (!save_errno) {
    pvio->set_error(mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                    ER(CR_SERVER_LOST));
    return;
  }

  strerror_r(save_errno, ssl_error, MAX_SSL_ERR_LEN);
  pvio->set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, "TLS/SSL error: %s (%d)",
                  ssl_error, save_errno);
  return;
}

#ifndef HAVE_OPENSSL_1_1_API
static void my_cb_locking(int mode, int n,
                          const char *file __attribute__((unused)),
                          int line __attribute__((unused)))
{
  if (mode & CRYPTO_LOCK)
    pthread_mutex_lock(&LOCK_crypto[n]);
  else
    pthread_mutex_unlock(&LOCK_crypto[n]);
}

static int ssl_thread_init()
{
  if (LOCK_crypto == NULL)
  {
    int i, max= CRYPTO_num_locks();

    if (!(LOCK_crypto=
           (pthread_mutex_t *)ma_malloc(sizeof(pthread_mutex_t) * max, MYF(0))))
      return 1;

    for (i=0; i < max; i++)
      pthread_mutex_init(&LOCK_crypto[i], NULL);

    CRYPTO_set_locking_callback(my_cb_locking);
  }
  return 0;
}
#endif

#if defined(_WIN32) || !defined(DISABLE_SIGPIPE)
#define disable_sigpipe()
#else
#include  <signal.h>
static void ma_sigpipe_handler()
{
}

static void disable_sigpipe()
{
  struct sigaction old_handler, new_handler={NULL};
  if (!sigaction (SIGPIPE, NULL, &old_handler) &&
    !old_handler.sa_handler)
  {
    new_handler.sa_handler= ma_sigpipe_handler;
    new_handler.sa_flags= 0;
    if (!sigemptyset(&new_handler.sa_mask))
      sigaction(SIGPIPE, &new_handler, NULL);
  }
}
#endif

/*
  Initializes SSL

  SYNOPSIS
    my_ssl_start
      mysql        connection handle

  RETURN VALUES
    0  success
    1  error
*/
int ma_tls_start(char *errmsg __attribute__((unused)), size_t errmsg_len __attribute__((unused)))
{
  int rc= 1;
  char *p;
  if (ma_tls_initialized)
    return 0;

  /* lock mutex to prevent multiple initialization */
  pthread_mutex_init(&LOCK_openssl_config, NULL);
  pthread_mutex_lock(&LOCK_openssl_config);
#ifdef HAVE_OPENSSL_1_1_API
  if (!OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, NULL))
    goto end;
#else
  if (ssl_thread_init())
  {
    strncpy(errmsg, "Not enough memory", errmsg_len);
    goto end;
  }
  SSL_library_init();
#if SSLEAY_VERSION_NUMBER >= 0x00907000L
  OPENSSL_config(NULL);
#endif
#endif
#ifndef HAVE_OPENSSL_1_1_API
  /* load errors */
  SSL_load_error_strings();
  /* digests and ciphers */
  OpenSSL_add_all_algorithms();
#endif
  disable_sigpipe();
#ifdef OPENSSL_USE_BIOMETHOD
  memcpy(&ma_BIO_method, BIO_s_socket(), sizeof(BIO_METHOD));
  ma_BIO_method.bread= ma_bio_read;
  ma_BIO_method.bwrite= ma_bio_write;
#endif
  snprintf(tls_library_version, TLS_VERSION_LENGTH - 1, "%s",
#if defined(LIBRESSL_VERSION_NUMBER) || !defined(HAVE_OPENSSL_1_1_API)
           SSLeay_version(SSLEAY_VERSION));
#else
           OpenSSL_version(OPENSSL_VERSION));
#endif
  /* remove date from version */
  if ((p= strstr(tls_library_version, "  ")))
    *p= 0;
  rc= 0;
  ma_tls_initialized= TRUE;
end:
  pthread_mutex_unlock(&LOCK_openssl_config);
  return rc;
}

/*
   Release SSL and free resources
   Will be automatically executed by 
   mysql_server_end() function

   SYNOPSIS
     my_ssl_end()
       void

   RETURN VALUES
     void
*/
void ma_tls_end()
{
  if (ma_tls_initialized)
  {
    pthread_mutex_lock(&LOCK_openssl_config);
#ifndef HAVE_OPENSSL_1_1_API
    if (LOCK_crypto)
    {
      int i;
      CRYPTO_set_locking_callback(NULL);
      CRYPTO_THREADID_set_callback(NULL);

      for (i=0; i < CRYPTO_num_locks(); i++)
        pthread_mutex_destroy(&LOCK_crypto[i]);
      ma_free((gptr)LOCK_crypto);
      LOCK_crypto= NULL;
    }
#endif
    if (mariadb_deinitialize_ssl)
    {
#ifndef HAVE_OPENSSL_1_1_API
      ERR_remove_thread_state(NULL);
      EVP_cleanup();
      CRYPTO_cleanup_all_ex_data();
      ERR_free_strings();
      CONF_modules_free();
      CONF_modules_unload(1);
#endif
    }
    ma_tls_initialized= FALSE;
    pthread_mutex_unlock(&LOCK_openssl_config);
    pthread_mutex_destroy(&LOCK_openssl_config);
  }
  return;
}

int ma_tls_get_password(char *buf, int size,
                        int rwflag __attribute__((unused)),
                        void *userdata)
{
  memset(buf, 0, size);
  if (userdata)
    strncpy(buf, (char *)userdata, size);
  return (int)strlen(buf);
}


static int ma_tls_set_certs(MYSQL *mysql, SSL_CTX *ctx)
{
  char *certfile= mysql->options.ssl_cert,
       *keyfile= mysql->options.ssl_key;
  char *pw= (mysql->options.extension) ?
            mysql->options.extension->tls_pw : NULL;

  /* add cipher */
  if ((mysql->options.ssl_cipher &&
        mysql->options.ssl_cipher[0] != 0))
  {
    if(
#ifdef TLS1_3_VERSION
       SSL_CTX_set_ciphersuites(ctx, mysql->options.ssl_cipher) == 0 &&
#endif
       SSL_CTX_set_cipher_list(ctx, mysql->options.ssl_cipher) == 0)
      goto error;
  }

  /* ca_file and ca_path */
  if (!SSL_CTX_load_verify_locations(ctx,
                                    mysql->options.ssl_ca,
                                    mysql->options.ssl_capath))
  {
    if (mysql->options.ssl_ca || mysql->options.ssl_capath)
      goto error;
    if (SSL_CTX_set_default_verify_paths(ctx) == 0)
      goto error;
  }

  if (mysql->options.extension &&
     (mysql->options.extension->ssl_crl || mysql->options.extension->ssl_crlpath))
  {
    X509_STORE *certstore;

    if ((certstore= SSL_CTX_get_cert_store(ctx)))
    {
      if (X509_STORE_load_locations(certstore, mysql->options.extension->ssl_crl,
                                               mysql->options.extension->ssl_crlpath) == 0)
        goto error;

      if (X509_STORE_set_flags(certstore, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL) == 0)
        goto error;
    }
  } 

  if (keyfile && !certfile)
    certfile= keyfile;
  if (certfile && !keyfile)
    keyfile= certfile;

  /* set cert */
  if (certfile  && certfile[0] != 0)
  {
    if (SSL_CTX_use_certificate_chain_file(ctx, certfile) != 1)
    {
      goto error; 
    }
  }

  if (keyfile && keyfile[0])
  {
    FILE *fp;
    if ((fp= fopen(keyfile, "rb")))
    {
      EVP_PKEY *key= EVP_PKEY_new();
      PEM_read_PrivateKey(fp, &key, NULL, pw);
      fclose(fp);
      if (SSL_CTX_use_PrivateKey(ctx, key) != 1)
      {
        unsigned long err= ERR_peek_error();
        EVP_PKEY_free(key);
        if (!(ERR_GET_LIB(err) == ERR_LIB_X509 &&
	            ERR_GET_REASON(err) == X509_R_CERT_ALREADY_IN_HASH_TABLE))
          goto error;
      }
      EVP_PKEY_free(key);
    } else {
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, 
                   CER(CR_FILE_NOT_FOUND), keyfile);
      return 1;
    }
  }
  /* verify key */
  if (certfile && SSL_CTX_check_private_key(ctx) != 1)
    goto error;
  
  SSL_CTX_set_verify(ctx, (mysql->options.ssl_ca || mysql->options.ssl_capath) ? 
                     SSL_VERIFY_PEER : SSL_VERIFY_NONE, NULL);
  return 0;

error:
  ma_tls_set_error(mysql);
  return 1;
}

void *ma_tls_init(MYSQL *mysql)
{
  SSL *ssl= NULL;
  SSL_CTX *ctx= NULL;
  long default_options= SSL_OP_ALL |
                        SSL_OP_NO_SSLv2 |
                        SSL_OP_NO_SSLv3 |
                        SSL_OP_NO_TLSv1;
  long options= 0;
  pthread_mutex_lock(&LOCK_openssl_config);

  #if OPENSSL_VERSION_NUMBER >= 0x10100000L
  if (!(ctx= SSL_CTX_new(TLS_client_method())))
#else
  if (!(ctx= SSL_CTX_new(SSLv23_client_method())))
#endif
    goto error;
  if (mysql->options.extension) 
    options= ma_tls_version_options(mysql->options.extension->tls_version);
  SSL_CTX_set_options(ctx, options ? options : default_options);

  if (ma_tls_set_certs(mysql, ctx))
  {
    goto error;
  }

  if (!(ssl= SSL_new(ctx)))
    goto error;

#if !defined(OPENSSL_NO_TLSEXT)
  if (mysql->host && !ma_is_ip_address(mysql->host))
    if (!SSL_set_tlsext_host_name(ssl, mysql->host))
      goto error;
#endif

  if (!SSL_set_app_data(ssl, mysql))
    goto error;

  pthread_mutex_unlock(&LOCK_openssl_config);
  return (void *)ssl;
error:
  pthread_mutex_unlock(&LOCK_openssl_config);
  if (ctx)
    SSL_CTX_free(ctx);
  if (ssl)
    SSL_free(ssl);
  return NULL;
}

unsigned int ma_tls_get_peer_cert_info(MARIADB_TLS *ctls, uint hash_size)
{
  X509 *cert;
  unsigned int hash_alg;
  SSL *ssl;
  char fp[129];

  switch (hash_size) {
    case 0:
    case 256:
      hash_alg= MA_HASH_SHA256;
      break;
    case 384:
      hash_alg= MA_HASH_SHA384;
      break;
    case 512:
      hash_alg= MA_HASH_SHA512;
      break;
    default:
      return 1;
  }

  if (!ctls || !ctls->ssl)
    return 1;

  ssl= (SSL *)ctls->ssl;

  /* Store peer certificate information */
  if (!ctls->cert_info.version)
  {
    if ((cert= SSL_get_peer_certificate(ssl)))
    {
  #if OPENSSL_VERSION_NUMBER >= 0x10101000L
      const ASN1_TIME *not_before= X509_get0_notBefore(cert),
                      *not_after= X509_get0_notAfter(cert);
      ASN1_TIME_to_tm(not_before, (struct tm *)&ctls->cert_info.not_before);
      ASN1_TIME_to_tm(not_after, (struct tm *)&ctls->cert_info.not_after);
  #else
      const ASN1_TIME *not_before= X509_get_notBefore(cert),
                      *not_after= X509_get_notAfter(cert);
      time_t  now, from, to;
      int pday, psec;
      /* ANS1_TIME_diff returns days and seconds between now and the
         specified ASN1_TIME */
      time(&now);
      ASN1_TIME_diff(&pday, &psec, not_before, NULL);
      from= now - (pday * 86400 + psec);
      gmtime_r(&from, &ctls->cert_info.not_before);
      ASN1_TIME_diff(&pday, &psec, NULL, not_after);
      to= now + (pday * 86400 + psec);
      gmtime_r(&to, &ctls->cert_info.not_after);
  #endif
      ctls->cert_info.subject= X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
      ctls->cert_info.issuer= X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
      ctls->cert_info.version= X509_get_version(cert) + 1;
      ctls->cert_info.fingerprint[0]= 0;
      X509_free(cert);
    }
    else
      return 1;
  }
  if (strlen(ctls->cert_info.fingerprint) != hash_size/4)
  {
    ma_tls_get_finger_print(ctls, hash_alg, fp, sizeof(fp));
    mysql_hex_string(ctls->cert_info.fingerprint, fp, hash_size/8);
  }

  return 0;
}

my_bool ma_tls_connect(MARIADB_TLS *ctls)
{
  SSL *ssl = (SSL *)ctls->ssl;
  my_bool blocking, try_connect= 1;
  MYSQL *mysql;
  MARIADB_PVIO *pvio;
  int rc;
#ifdef OPENSSL_USE_BIOMETHOD
  BIO_METHOD *bio_method= NULL;
  BIO *bio;
#endif

  mysql= (MYSQL *)SSL_get_app_data(ssl);
  pvio= mysql->net.pvio;

  /* Set socket to non-blocking if not already set */
  if (!(blocking= pvio->methods->is_blocking(pvio)))
    pvio->methods->blocking(pvio, FALSE, 0);

  SSL_clear(ssl);

#ifdef OPENSSL_USE_BIOMETHOD
  bio= BIO_new(&ma_BIO_method);
  bio->ptr= pvio;
  SSL_set_bio(ssl, bio, bio);
  BIO_set_fd(bio, mysql_get_socket(mysql), BIO_NOCLOSE);
#else
  SSL_set_fd(ssl, (int)mysql_get_socket(mysql));
#endif

  /* CONC-732: Always set verification callback to avoid OpenSSL output */
  SSL_set_verify(ssl, SSL_VERIFY_PEER, ma_verification_callback);

  while (try_connect && (rc= SSL_connect(ssl)) == -1)
  {
    switch((SSL_get_error(ssl, rc))) {
    case SSL_ERROR_WANT_READ:
      /* use low timeout, see ma_tls_read */
      if (pvio->methods->wait_io_or_timeout(pvio, TRUE, 5) < 1)
        try_connect= 0;
      break;
    case SSL_ERROR_WANT_WRITE:
      /* use low timeout, see ma_tls_read */
      if (pvio->methods->wait_io_or_timeout(pvio, TRUE, 5) < 1)
        try_connect= 0;
      break;
    default:
      try_connect= 0;
    }
  }

  if (rc != 1)
  {
    ma_tls_set_error(mysql);
    return 1;
  }

  pvio->ctls->ssl= ctls->ssl= (void *)ssl;

  return 0;
}

static my_bool
ma_tls_async_check_result(int res, struct mysql_async_context *b, SSL *ssl)
{
  int ssl_err;
  b->events_to_wait_for= 0;
  if (res > 0)
    return 1;
  ssl_err= SSL_get_error(ssl, res);
  if (ssl_err == SSL_ERROR_WANT_READ)
    b->events_to_wait_for|= MYSQL_WAIT_READ;
  else if (ssl_err == SSL_ERROR_WANT_WRITE)
    b->events_to_wait_for|= MYSQL_WAIT_WRITE;
  else
    return 1;
  if (b->suspend_resume_hook)
    (*b->suspend_resume_hook)(TRUE, b->suspend_resume_hook_user_data);
  my_context_yield(&b->async_context);
  if (b->suspend_resume_hook)
    (*b->suspend_resume_hook)(FALSE, b->suspend_resume_hook_user_data);
  return 0;
}

ssize_t ma_tls_read_async(MARIADB_PVIO *pvio,
                          const unsigned char *buffer,
                          size_t length)
{
  int res;
  struct mysql_async_context *b= pvio->mysql->options.extension->async_context;
  MARIADB_TLS *ctls= pvio->ctls;

  for (;;)
  {
    res= SSL_read((SSL *)ctls->ssl, (void *)buffer, (int)length);
    if (ma_tls_async_check_result(res, b, (SSL *)ctls->ssl))
      return res;
  }
}

ssize_t ma_tls_write_async(MARIADB_PVIO *pvio,
                           const unsigned char *buffer,
                           size_t length)
{
  int res;
  struct mysql_async_context *b= pvio->mysql->options.extension->async_context;
  MARIADB_TLS *ctls= pvio->ctls;

  for (;;)
  {
    res= SSL_write((SSL *)ctls->ssl, (void *)buffer, (int)length);
    if (ma_tls_async_check_result(res, b, (SSL *)ctls->ssl))
      return res;
  }
}


ssize_t ma_tls_read(MARIADB_TLS *ctls, const uchar* buffer, size_t length)
{
  int rc;
  MARIADB_PVIO *pvio= ctls->pvio;

  while ((rc= SSL_read((SSL *)ctls->ssl, (void *)buffer, (int)length)) <= 0)
  {
    int error= SSL_get_error((SSL *)ctls->ssl, rc);
    if (error != SSL_ERROR_WANT_READ)
      break;
    /* To get a more precise error message than "resource temporary
       unavailable" (=errno 11) after read timeout occured, we check
       the socket status using a very small timeout (=5 ms) */
    if (pvio->methods->wait_io_or_timeout(pvio, TRUE, 5) < 1)
      break;
  }
  if (rc <= 0)
  {
    MYSQL *mysql= SSL_get_app_data(ctls->ssl);
    ma_tls_set_error(mysql);
  }
  return rc;
}

ssize_t ma_tls_write(MARIADB_TLS *ctls, const uchar* buffer, size_t length)
{
  int rc;
  MARIADB_PVIO *pvio= ctls->pvio;

  while ((rc= SSL_write((SSL *)ctls->ssl, (void *)buffer, (int)length)) <= 0)
  {
    int error= SSL_get_error((SSL *)ctls->ssl, rc);
    if (error != SSL_ERROR_WANT_WRITE)
      break;
    /* use low timeout, see ma_tls_read */
    if (pvio->methods->wait_io_or_timeout(pvio, TRUE, 5) < 1)
      break;
  }
  if (rc <= 0)
  {
    MYSQL *mysql= SSL_get_app_data(ctls->ssl);
    ma_tls_set_error(mysql);
  }
  return rc;
}

my_bool ma_tls_close(MARIADB_TLS *ctls)
{
  int i, rc;
  SSL *ssl;
  SSL_CTX *ctx= NULL;

  if (!ctls || !ctls->ssl)
    return 1;
  ssl= (SSL *)ctls->ssl;
  ctx= SSL_get_SSL_CTX(ssl);
  if (ctx)
    SSL_CTX_free(ctx);

  SSL_set_quiet_shutdown(ssl, 1); 
  /* 2 x pending + 2 * data = 4 */ 
  for (i=0; i < 4; i++)
    if ((rc= SSL_shutdown(ssl)))
      break;

  /* Since we transferred ownership of BIO to ssl, BIO will
     automatically freed - no need for an explicit BIO_free_all */

  SSL_free(ssl);
  ctls->ssl= NULL;

  OPENSSL_free(ctls->cert_info.issuer);
  OPENSSL_free(ctls->cert_info.subject);

  return rc;
}

/** Check for possible errors, and store the result in net.tls_verify_status.
    verification will happen after handshake by ma_tls_verify_server_cert().
    To retrieve all errors, this callback function returns always true.
    (By default OpenSSL stops verification after first error
*/
static int ma_verification_callback(int preverify_ok __attribute__((unused)), X509_STORE_CTX *ctx)
{
  SSL *ssl;

  if ((ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx())))
  {
    MYSQL *mysql= (MYSQL *)SSL_get_app_data(ssl);
    int x509_err= X509_STORE_CTX_get_error(ctx);
    my_bool verify_status= MARIADB_TLS_VERIFY_OK;

    if ((x509_err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ||
         x509_err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN))
      verify_status= MARIADB_TLS_VERIFY_TRUST;
    else if (x509_err == X509_V_ERR_CERT_REVOKED)
      verify_status= MARIADB_TLS_VERIFY_REVOKED;
    else if (x509_err == X509_V_ERR_CERT_NOT_YET_VALID ||
            x509_err == X509_V_ERR_CERT_HAS_EXPIRED)
      verify_status= MARIADB_TLS_VERIFY_PERIOD;
    else if (x509_err != X509_V_OK)
      verify_status= MARIADB_TLS_VERIFY_UNKNOWN;

    if (verify_status)
    {
      if (mysql->net.tls_verify_status < verify_status)
        my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
           ER(CR_SSL_CONNECTION_ERROR), X509_verify_cert_error_string(x509_err));
      mysql->net.tls_verify_status|= verify_status;
    }
  }

  /* continue verification */
  return 1;
}

int ma_tls_verify_server_cert(MARIADB_TLS *ctls, unsigned int verify_flags)
{
  X509 *cert= NULL;
  MYSQL *mysql;
  SSL *ssl;
  MARIADB_PVIO *pvio;
#if !defined(HAVE_OPENSSL_CHECK_HOST)
  X509_NAME *x509sn;
  int cn_pos;
  X509_NAME_ENTRY *cn_entry;
  ASN1_STRING *cn_asn1;
  const char *cn_str;
#endif
  if (!ctls || !ctls->ssl)
    return 1;
  ssl= (SSL *)ctls->ssl;

  mysql= (MYSQL *)SSL_get_app_data(ssl);
  pvio= mysql->net.pvio;

  if ((mysql->net.tls_verify_status > MARIADB_TLS_VERIFY_FINGERPRINT) ||
      (mysql->net.tls_verify_status & verify_flags))
  {
    return MARIADB_TLS_VERIFY_ERROR;
  }

  if (verify_flags & MARIADB_TLS_VERIFY_HOST)
  {
    if (!mysql->host)
    {
      pvio->set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                      ER(CR_SSL_CONNECTION_ERROR), "Invalid (empty) hostname");
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_HOST;
      return MARIADB_TLS_VERIFY_ERROR;
    }

    if (!(cert= SSL_get_peer_certificate(ssl)))
    {
      pvio->set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                      ER(CR_SSL_CONNECTION_ERROR), "Unable to get server certificate");
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_UNKNOWN;
      return MARIADB_TLS_VERIFY_ERROR;
    }

  #ifdef HAVE_OPENSSL_CHECK_HOST
    if (X509_check_host(cert, mysql->host, strlen(mysql->host), 0, 0) != 1
       && X509_check_ip_asc(cert, mysql->host, 0) != 1)
    {
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_HOST;
      goto error;
    }
  #else
    x509sn= X509_get_subject_name(cert);

    if ((cn_pos= X509_NAME_get_index_by_NID(x509sn, NID_commonName, -1)) < 0)
    {
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_HOST;
      goto error;
    }

    if (!(cn_entry= X509_NAME_get_entry(x509sn, cn_pos)))
    {
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_HOST;
      goto error;
    }

    if (!(cn_asn1 = X509_NAME_ENTRY_get_data(cn_entry)))
    {
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_HOST;
      goto error;
    }

    cn_str = (char *)ASN1_STRING_data(cn_asn1);

    /* Make sure there is no embedded \0 in the CN */
    if ((size_t)ASN1_STRING_length(cn_asn1) != strlen(cn_str))
    {
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_HOST;
      goto error;
    }

    if (strcmp(cn_str, mysql->host))
    {
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_HOST;
      goto error;
    }
  #endif
    X509_free(cert);
  }
  return 0;
error:
  X509_free(cert);

  return 1;
}

const char *ma_tls_get_cipher(MARIADB_TLS *ctls)
{
  if (!ctls || !ctls->ssl)
    return NULL;
  return SSL_get_cipher_name(ctls->ssl);
}

unsigned int ma_tls_get_finger_print(MARIADB_TLS *ctls, uint hash_type, char *fp, unsigned int len)
{
  X509 *cert= NULL;
  MYSQL *mysql;
  unsigned int fp_len;
  const EVP_MD *hash_alg;
  unsigned int max_len= EVP_MAX_MD_SIZE;

  if (!ctls || !ctls->ssl)
    return 0;

  mysql = SSL_get_app_data(ctls->ssl);

  switch (hash_type)
  {
  case MA_HASH_SHA1:
    hash_alg = EVP_sha1();
    max_len= 20;
    break;
  case MA_HASH_SHA224:
    hash_alg = EVP_sha224();
    max_len= 28;
    break;
  case MA_HASH_SHA256:
    hash_alg = EVP_sha256();
    max_len= 32;
    break;
  case MA_HASH_SHA384:
    hash_alg = EVP_sha384();
    max_len= 48;
    break;
  case MA_HASH_SHA512:
    hash_alg = EVP_sha512();
    break;
  default:
    my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
      ER(CR_SSL_CONNECTION_ERROR),
      "Cannot detect hash algorithm for fingerprint verification");
    return 0;
  }

  if (len < max_len)
  {
    my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                        ER(CR_SSL_CONNECTION_ERROR), 
                        "Finger print buffer too small");
    return 0;
  }
  
  if (!(cert= SSL_get_peer_certificate(ctls->ssl)))
  {
    my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                        ER(CR_SSL_CONNECTION_ERROR), 
                        "Unable to get server certificate");
    goto end;
  }

  if (!X509_digest(cert, hash_alg, (unsigned char *)fp, &fp_len))
  {
    my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                        ER(CR_SSL_CONNECTION_ERROR), 
                        "invalid finger print of server certificate");
    goto end;
  }

  X509_free(cert);
  return (fp_len);
end:  
  X509_free(cert);
  return 0;
}


int ma_tls_get_protocol_version(MARIADB_TLS *ctls)
{
  if (!ctls || !ctls->ssl)
    return -1;

  return SSL_version(ctls->ssl) & 0xFF;
}

void ma_tls_set_connection(MYSQL *mysql)
{
  (void)SSL_set_app_data(mysql->net.pvio->ctls->ssl, mysql);
}

