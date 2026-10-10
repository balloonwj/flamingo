/************************************************************************************
  Copyright (C) 2014 MariaDB Corporation AB

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

/*
 * this is the abstraction layer for communication via SSL.
 * The following SSL libraries/variants are currently supported:
 * - openssl
 * - gnutls
 * - schannel (windows only)
 * 
 * Different SSL variants are implemented as plugins
 * On Windows schannel is implemented as (standard)
 * built-in plugin.
 */

#ifdef HAVE_TLS

#include <ma_global.h>
#include <ma_sys.h>
#include <ma_common.h>
#include <string.h>
#include <errmsg.h>
#include <ma_pvio.h>
#include <ma_tls.h>
#include <mysql/client_plugin.h>
#include <mariadb/ma_io.h>
#include <ma_hash.h>

#ifdef HAVE_NONBLOCK
#include <mariadb_async.h>
#include <ma_context.h>
#endif

#define MAX_FINGERPRINT_LEN 128;

/* Errors should be handled via pvio callback function */
my_bool ma_tls_initialized= FALSE;
unsigned int mariadb_deinitialize_ssl= 1;

const char *tls_protocol_version[]=
  {"SSLv3", "TLSv1.0", "TLSv1.1", "TLSv1.2", "TLSv1.3", "Unknown"};

MARIADB_TLS *ma_pvio_tls_init(MYSQL *mysql)
{
  MARIADB_TLS *ctls= NULL;

  if (!ma_tls_initialized)
    ma_tls_start(mysql->net.last_error, MYSQL_ERRMSG_SIZE);

  if (!(ctls= (MARIADB_TLS *)calloc(1, sizeof(MARIADB_TLS))))
  {
    return NULL;
  }

  /* register error routine and methods */
  ctls->pvio= mysql->net.pvio;
  if (!(ctls->ssl= ma_tls_init(mysql)))
  {
    free(ctls);
    ctls= NULL;
  }
  return ctls;
}

my_bool ma_pvio_tls_connect(MARIADB_TLS *ctls)
{
  my_bool rc;
  
  if ((rc= ma_tls_connect(ctls)))
    ma_tls_close(ctls);
  return rc;
}

ssize_t ma_pvio_tls_read(MARIADB_TLS *ctls, const uchar* buffer, size_t length)
{
  return ma_tls_read(ctls, buffer, length);
}

ssize_t ma_pvio_tls_write(MARIADB_TLS *ctls, const uchar* buffer, size_t length)
{
  return ma_tls_write(ctls, buffer, length);
}

my_bool ma_pvio_tls_close(MARIADB_TLS *ctls)
{
  return ma_tls_close(ctls);
}

int ma_pvio_tls_verify_server_cert(MARIADB_TLS *ctls, unsigned int flags)
{
  MYSQL *mysql;
  int rc;

  if (!ctls || !ctls->pvio || !ctls->pvio->mysql)
    return 0;

  mysql= ctls->pvio->mysql;

  /* Skip peer certificate verification */
  if (mysql->options.extension->tls_allow_invalid_server_cert &&
      (!mysql->options.extension->tls_fp && !mysql->options.extension->tls_fp_list))
  {
    /* Since OpenSSL implementation sets status during TLS handshake
       we need to clear verification status */
    mysql->net.tls_verify_status= 0;
    return 0;
  }

  if (flags & MARIADB_TLS_VERIFY_FINGERPRINT)
  {
    if (ma_pvio_tls_check_fp(ctls, mysql->options.extension->tls_fp, mysql->options.extension->tls_fp_list))
    {
      mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_FINGERPRINT;
      mysql->extension->tls_validation= mysql->net.tls_verify_status;
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
        ER(CR_SSL_CONNECTION_ERROR),
        "Fingerprint validation of peer certificate failed");
      return 1;
    }
#ifdef HAVE_OPENSSL
    /* verification already happened via callback */
    if (!(mysql->net.tls_verify_status & flags))
    {
      mysql->extension->tls_validation= mysql->net.tls_verify_status;
      mysql->net.tls_verify_status= MARIADB_TLS_VERIFY_OK;
      return 0;
    }
#endif
  }
  rc= ma_tls_verify_server_cert(ctls, flags);

  /* Set error messages */
  if (!mysql->net.last_errno)
  {
    if (mysql->net.tls_verify_status & MARIADB_TLS_VERIFY_PERIOD)
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
        ER(CR_SSL_CONNECTION_ERROR),
        "Certificate not yet valid or expired");
    else if (mysql->net.tls_verify_status & MARIADB_TLS_VERIFY_FINGERPRINT)
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
        ER(CR_SSL_CONNECTION_ERROR),
        "Fingerprint validation of peer certificate failed");
    else if (mysql->net.tls_verify_status & MARIADB_TLS_VERIFY_REVOKED)
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
        ER(CR_SSL_CONNECTION_ERROR),
        "Certificate revoked");
    else if (mysql->net.tls_verify_status & MARIADB_TLS_VERIFY_HOST)
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
        ER(CR_SSL_CONNECTION_ERROR),
        "Hostname verification failed");
    else if (mysql->net.tls_verify_status & MARIADB_TLS_VERIFY_UNKNOWN)
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
        ER(CR_SSL_CONNECTION_ERROR),
        "Peer certificate verification failed");
    else if (mysql->net.tls_verify_status & MARIADB_TLS_VERIFY_TRUST)
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
        ER(CR_SSL_CONNECTION_ERROR),
        "Peer certificate is not trusted");
  }
  /* Save original validation */
  mysql->extension->tls_validation= mysql->net.tls_verify_status;
  mysql->net.tls_verify_status&= flags;
  return rc;
}

const char *ma_pvio_tls_cipher(MARIADB_TLS *ctls)
{
  return ma_tls_get_cipher(ctls);
}

void ma_pvio_tls_end()
{
  ma_tls_end();
}

int ma_pvio_tls_get_protocol_version_id(MARIADB_TLS *ctls)
{
  return ma_tls_get_protocol_version(ctls);
}

const char *ma_pvio_tls_get_protocol_version(MARIADB_TLS *ctls)
{
  int version;

  version= ma_tls_get_protocol_version(ctls);
  if (version < 0 || version > PROTOCOL_MAX)
    return tls_protocol_version[PROTOCOL_UNKNOWN];
  return tls_protocol_version[version];
}

static signed char ma_hex2int(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return 10 + c - 'A';
  if (c >= 'a' && c <= 'f')
    return 10 + c - 'a';
  return -1;
}

#ifndef EVP_MAX_MD_SIZE
#define EVP_MAX_MD_SIZE 64
#endif

static my_bool ma_pvio_tls_compare_fp(MARIADB_TLS *ctls,
                                     const char *cert_fp,
                                     unsigned int cert_fp_len)
{
  char fp[EVP_MAX_MD_SIZE];
  unsigned int fp_len= EVP_MAX_MD_SIZE;
  unsigned int hash_type;

  char *p, *c;
  uint hash_len;

  /* check length without colons */
  if (strchr(cert_fp, ':'))
    hash_len= (uint)((strlen(cert_fp) + 1) / 3) * 2;
  else
    hash_len= (uint)strlen(cert_fp);

  /* check hash size */
  switch (hash_len) {
#ifndef DISABLE_WEAK_HASH
  case MA_SHA1_HASH_SIZE * 2:
    hash_type = MA_HASH_SHA1;
    break;
#endif
  case MA_SHA224_HASH_SIZE * 2:
    hash_type = MA_HASH_SHA224;
    break;
  case MA_SHA256_HASH_SIZE * 2:
    hash_type = MA_HASH_SHA256;
    break;
  case MA_SHA384_HASH_SIZE * 2:
    hash_type = MA_HASH_SHA384;
    break;
  case MA_SHA512_HASH_SIZE * 2:
    hash_type = MA_HASH_SHA512;
    break;
  default:
    {
      MYSQL* mysql = ctls->pvio->mysql;
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
        ER(CR_SSL_CONNECTION_ERROR),
        "Unknown or invalid fingerprint hash size detected");
      return 1;
    }
  }

  if (!ma_tls_get_finger_print(ctls, hash_type, fp, fp_len))
    return 1;

  p= (char *)cert_fp;
  c = fp;

  for (p = (char*)cert_fp; p < cert_fp + cert_fp_len; c++, p += 2)
  {
    signed char d1, d2;
    if (*p == ':')
      p++;
    if ((d1 = ma_hex2int(*p)) == -1 ||
      (d2 = ma_hex2int(*(p + 1))) == -1 ||
      (char)(d1 * 16 + d2) != *c)
      return 1;
  }
  return 0;
}

my_bool ma_pvio_tls_check_fp(MARIADB_TLS *ctls, const char *fp, const char *fp_list)
{
  my_bool rc=1;
  MYSQL *mysql= ctls->pvio->mysql;

  if (fp)
  {
    rc = ma_pvio_tls_compare_fp(ctls, fp, (uint)strlen(fp));
  }
  else if (fp_list)
  {
    MA_FILE *f;
    char buff[255];

    if (!(f = ma_open(fp_list, "r", mysql)))
      goto end;

    while (ma_gets(buff, sizeof(buff)-1, f))
    {
      /* remove trailing new line character */
      char *pos= strchr(buff, '\r');
      if (!pos)
        pos= strchr(buff, '\n');
      if (pos)
        *pos= '\0';
        
      if (!ma_pvio_tls_compare_fp(ctls, buff, (uint)strlen(buff)))
      {
        /* finger print is valid: close file and exit */
        ma_close(f);
        rc= 0;
        goto end;
      }
    }

    /* No finger print matched - close file and return error */
    ma_close(f);
  }

end:
  if (rc && !mysql->net.last_errno)
  {
    my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                         ER(CR_SSL_CONNECTION_ERROR), 
                         "Fingerprint verification of server certificate failed");
  }
  return rc;
}

void ma_pvio_tls_set_connection(MYSQL *mysql)
{
  ma_tls_set_connection(mysql);
}

unsigned int ma_pvio_tls_get_peer_cert_info(MARIADB_TLS *ctls, unsigned int size)
{
  return ma_tls_get_peer_cert_info(ctls, size);
}
#endif /* HAVE_TLS */
