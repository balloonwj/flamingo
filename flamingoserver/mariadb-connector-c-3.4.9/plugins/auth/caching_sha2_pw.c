/************************************************************************************
  Copyright (C) 2017, 2022, MariaDB Corporation AB

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
#ifndef _WIN32
#define _GNU_SOURCE 1
#endif

#ifdef HAVE_WINCRYPT
#undef HAVE_OPENSSL
#undef HAVE_GNUTLS
#endif

#if defined(HAVE_OPENSSL) || defined(HAVE_WINCRYPT) || defined(HAVE_GNUTLS)

#include <ma_global.h>
#include <mysql.h>
#include <mysql/client_plugin.h>
#include <string.h>
#include <memory.h>
#include <errmsg.h>
#include <ma_global.h>
#include <ma_sys.h>
#include <ma_common.h>

#ifndef WIN32
#include <dlfcn.h>
#endif

#if defined(HAVE_OPENSSL)
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#elif defined(HAVE_GNUTLS)
#include <gnutls/gnutls.h>
#elif defined(HAVE_WINCRYPT)
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

#endif

#include <ma_crypt.h>

#define MAX_PW_LEN 1024

#define REQUEST_PUBLIC_KEY     2
#define CACHED_LOGIN_SUCCEEDED 3
#define RSA_LOGIN_REQUIRED 4

/* MySQL server allows requesting public key only for non-secure connections.
   secure connections are:
     - TLS/SSL connections
     - unix_socket connections
*/
static unsigned char is_connection_secure(MYSQL *mysql)
{
  if (mysql->options.use_ssl ||
      mysql->net.pvio->type != PVIO_TYPE_SOCKET)
    return 1;
  return 0;
}

static int ma_sha256_scramble(unsigned char *scramble, size_t scramble_len,
                              unsigned char *source, size_t source_len,
                              unsigned char *salt, size_t salt_len)
{
  unsigned char digest1[MA_SHA256_HASH_SIZE],
                digest2[MA_SHA256_HASH_SIZE],
                new_scramble[MA_SHA256_HASH_SIZE];

  MA_HASH_CTX *ctx = NULL;
  size_t i;

  /* check if all specified lengths are valid */
  if (!scramble_len || !source_len || !salt_len)
    return 1;

  /* Step1: create sha256 from source */
  if (!(ctx= ma_hash_new(MA_HASH_SHA256)))
    return 1;
  ma_hash_input(ctx, source, source_len);
  ma_hash_result(ctx, digest1);
  ma_hash_free(ctx);


  /* Step2: create sha256 digest from digest1 */
  if (!(ctx= ma_hash_new(MA_HASH_SHA256)))
    return 1;
  ma_hash_input(ctx, digest1, MA_SHA256_HASH_SIZE);
  ma_hash_result(ctx, digest2);
  ma_hash_free(ctx);

  /* Step3: create sha256 digest from digest2 + salt */
  if (!(ctx= ma_hash_new(MA_HASH_SHA256)))
    return 1;
  ma_hash_input(ctx, digest2, MA_SHA256_HASH_SIZE);
  ma_hash_input(ctx, salt, salt_len);
  ma_hash_result(ctx, new_scramble);
  ma_hash_free(ctx);

  /* Step4: xor(digest1, scramble1) */
  for (i= 0; i < scramble_len; i++)
    scramble[i]= digest1[i] ^ new_scramble[i];
  return 0;
}

/* function prototypes */
static int auth_caching_sha2_client(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql);
static int auth_caching_sha2_deinit(void);
static int auth_caching_sha2_init(char *unused1,
    size_t unused2,
    int unused3,
    va_list);


#ifndef PLUGIN_DYNAMIC
struct st_mysql_client_plugin_AUTHENTICATION caching_sha2_password_client_plugin=
#else
MARIADB_CLIENT_PLUGIN_EXPORT struct st_mysql_client_plugin_AUTHENTICATION
    _mysql_client_plugin_declaration_=
#endif
{
  MYSQL_CLIENT_AUTHENTICATION_PLUGIN,
  MYSQL_CLIENT_AUTHENTICATION_PLUGIN_INTERFACE_VERSION,
  "caching_sha2_password",
  "Georg Richter",
  "Caching SHA2 Authentication Plugin",
  {0,1,0},
  "LGPL",
  NULL,
  auth_caching_sha2_init,
  auth_caching_sha2_deinit,
  NULL,
  auth_caching_sha2_client,
  NULL
};

#ifdef HAVE_WINCRYPT
static LPBYTE ma_load_pem(const char *buffer, DWORD *buffer_len)
{
  LPBYTE der_buffer= NULL;
  DWORD der_buffer_length= 0;

  if (buffer_len == NULL || *buffer_len == 0)
    return NULL;
  /* calculate the length of DER binary */
  if (!CryptStringToBinaryA(buffer, *buffer_len, CRYPT_STRING_BASE64HEADER,
        NULL, &der_buffer_length, NULL, NULL))
    goto end;
  /* allocate DER binary buffer */
  if (!(der_buffer= (LPBYTE)malloc(der_buffer_length)))
    goto end;
  /* convert to DER binary */
  if (!CryptStringToBinaryA(buffer, *buffer_len, CRYPT_STRING_BASE64HEADER,
        der_buffer, &der_buffer_length, NULL, NULL))
    goto end;

  *buffer_len= der_buffer_length;

  return der_buffer;

end:
  if (der_buffer)
    free(der_buffer);
  *buffer_len= 0;
  return NULL;
}
#endif

#ifndef HAVE_GNUTLS
static char *load_pub_key_file(const char *filename, int *pub_key_size)
{
  FILE *fp= NULL;
  char *buffer= NULL;
  unsigned char error= 1;

  if (!pub_key_size)
    return NULL;

  if (!(fp= fopen(filename, "r")))
    goto end;

  if (fseek(fp, 0, SEEK_END))
    goto end;

  if ((*pub_key_size= ftell(fp)) < 0)
    goto end;

  rewind(fp);

  if (!(buffer= malloc(*pub_key_size + 1)))
    goto end;

  if (fread(buffer, *pub_key_size, 1, fp) != (size_t)*pub_key_size)
    goto end;

  error= 0;

end:
  if (fp)
    fclose(fp);
  if (error && buffer)
  {
    free(buffer);
    buffer= NULL;
  }
  return buffer;
}
#endif

static int auth_caching_sha2_client(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql)
{
  unsigned char *packet;
  int packet_length;
  int rc= CR_ERROR;
#if !defined(HAVE_GNUTLS)
  char passwd[MAX_PW_LEN];
#ifdef HAVE_OPENSSL
  unsigned char *rsa_enc_pw= NULL;
  int rsa_size;
#else
  unsigned char rsa_enc_pw[MAX_PW_LEN];
  ULONG rsa_size;
#endif
  unsigned int pwlen, i;
  char *filebuffer= NULL;
#endif
  unsigned char buf[MA_SHA256_HASH_SIZE];

#if defined(HAVE_OPENSSL)
  EVP_PKEY *pubkey= NULL;
  EVP_PKEY_CTX *ctx= NULL;
  BIO *bio;
  size_t outlen;
#elif defined(HAVE_WINCRYPT)
  BCRYPT_KEY_HANDLE pubkey= 0;
  BCRYPT_OAEP_PADDING_INFO paddingInfo;
  LPBYTE der_buffer= NULL;
  DWORD der_buffer_len= 0;
  CERT_PUBLIC_KEY_INFO *publicKeyInfo= NULL;
  DWORD publicKeyInfoLen;
#endif

  /* read error */
  if ((packet_length= vio->read_packet(vio, &packet)) < 0)
    return CR_ERROR;

  if (packet_length != SCRAMBLE_LENGTH + 1)
    return CR_SERVER_HANDSHAKE_ERR;

  memmove(mysql->scramble_buff, packet, SCRAMBLE_LENGTH);
  mysql->scramble_buff[SCRAMBLE_LENGTH]= 0;

  /* send empty packet if no password was provided */
  if (!mysql->passwd || !mysql->passwd[0])
  {
    if (vio->write_packet(vio, 0, 0))
      return CR_ERROR;
    return CR_OK;
  }

  /* This is the normal authentication, if the host/user key is already in server
     cache. In case authentication will fail, we will not return an error but will
     try to connect via RSA encryption.
  */
  if (ma_sha256_scramble(buf, MA_SHA256_HASH_SIZE,
                         (unsigned char *)mysql->passwd, strlen(mysql->passwd),
                         (unsigned char *)mysql->scramble_buff, SCRAMBLE_LENGTH))
    return CR_ERROR;

  if (vio->write_packet(vio, buf, MA_SHA256_HASH_SIZE))
    return CR_ERROR;
  if ((packet_length=vio->read_packet(vio, &packet)) == -1)
    return CR_ERROR;
  if (packet_length == 1)
  {
    switch (*packet) {
    case CACHED_LOGIN_SUCCEEDED:
      return CR_OK;
    case RSA_LOGIN_REQUIRED:
      break;
    default:
      return CR_ERROR;
    }
  }

  if (!is_connection_secure(mysql))
  {
#if defined(HAVE_GNUTLS)
     mysql->methods->set_error(mysql, CR_AUTH_PLUGIN_ERR, "HY000", 
                               "RSA Encryption not supported - caching_sha2_password plugin was built with GnuTLS support");
     return CR_ERROR;
#else
    /* read public key file (if specified) */
    if (mysql->options.extension &&
        mysql->options.extension->server_public_key)
    {
      filebuffer= load_pub_key_file(mysql->options.extension->server_public_key,
                                    &packet_length);
    }

    /* if no public key file was specified or if we couldn't read the file,
       we ask server to send public key */
    if (!filebuffer)
    {
      unsigned char request= REQUEST_PUBLIC_KEY;
      if (vio->write_packet(vio, &request, 1) ||
         (packet_length=vio->read_packet(vio, &packet)) == -1)
      {
        mysql->methods->set_error(mysql, CR_AUTH_PLUGIN_ERR, "HY000", "Couldn't read RSA public key from server");
        return CR_ERROR;
      }
    }
#if defined(HAVE_OPENSSL)
    bio= BIO_new_mem_buf(filebuffer ? (unsigned char *)filebuffer : packet,
                         packet_length);
    if (!(pubkey= PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL)))
      goto error;
    if (!(ctx= EVP_PKEY_CTX_new(pubkey, NULL)))
      goto error;
    if (EVP_PKEY_encrypt_init(ctx) <= 0)
      goto error;
    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) <= 0)
      goto error;
    rsa_size= EVP_PKEY_size(pubkey);
    BIO_free(bio);
    bio= NULL;
    ERR_clear_error();
#elif defined(HAVE_WINCRYPT)
    der_buffer_len= packet_length;
    /* Load pem and convert it to binary object. New length will be returned
       in der_buffer_len */
    if (!(der_buffer= ma_load_pem(filebuffer ? filebuffer : (char *)packet, &der_buffer_len)))
      goto error;

    /* Create context and load public key */
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
                             der_buffer, der_buffer_len,
                             CRYPT_DECODE_ALLOC_FLAG, NULL,
                             &publicKeyInfo, &publicKeyInfoLen))
      goto error;
    free(der_buffer);

    /* Import public key as cng key */
    if (!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, publicKeyInfo,
                                     CRYPT_OID_INFO_PUBKEY_ENCRYPT_KEY_FLAG,
                                     NULL, &pubkey))
      goto error;

#endif
    if (!pubkey)
    {
      rc= CR_ERROR;
      goto error;
    }

    pwlen= (unsigned int)strlen(mysql->passwd) + 1;  /* include terminating zero */
    if (pwlen > MAX_PW_LEN)
      goto error;
    memcpy(passwd, mysql->passwd, pwlen);

    /* xor password with scramble */
    for (i=0; i < pwlen; i++)
      passwd[i]^= *(mysql->scramble_buff + i % SCRAMBLE_LENGTH);

    /* encrypt scrambled password */
#if defined(HAVE_OPENSSL)
    if (EVP_PKEY_encrypt(ctx, NULL, &outlen, (unsigned char *)passwd, pwlen) <= 0)
      goto error;
    if (!(rsa_enc_pw= malloc(outlen)))
      goto error;
    if (EVP_PKEY_encrypt(ctx, rsa_enc_pw, &outlen, (unsigned char *)passwd, pwlen) <= 0)
      goto error;
#elif defined(HAVE_WINCRYPT)
    ZeroMemory(&paddingInfo, sizeof(paddingInfo));
    paddingInfo.pszAlgId = BCRYPT_SHA1_ALGORITHM;
    if ((rc= BCryptEncrypt(pubkey, (PUCHAR)passwd, pwlen, &paddingInfo, NULL, 0, rsa_enc_pw,
                     MAX_PW_LEN, &rsa_size, BCRYPT_PAD_OAEP)))
      goto error;

#endif
    if (vio->write_packet(vio, rsa_enc_pw, rsa_size))
      goto error;

    rc= CR_OK;
#endif
  }
  else
  {
    if (vio->write_packet(vio, (unsigned char *)mysql->passwd, (int)strlen(mysql->passwd) + 1))
      return CR_ERROR;
    return CR_OK;
  }
#if !defined(HAVE_GNUTLS)
error:
#if defined(HAVE_OPENSSL)
  if (pubkey)
    EVP_PKEY_free(pubkey);
  if (rsa_enc_pw)
    free(rsa_enc_pw);
  if (bio)
    BIO_free(bio);
  if (ctx)
    EVP_PKEY_CTX_free(ctx);
#elif defined(HAVE_WINCRYPT)
  if (pubkey)
    BCryptDestroyKey(pubkey);
  if (publicKeyInfo)
    LocalFree(publicKeyInfo);
#endif
  free(filebuffer);
#endif
  return rc;
}
/* }}} */

/* {{{ static int auth_caching_sha2_init */
/*
   Initialization routine

   SYNOPSIS
   auth_sha256_init
   unused1
   unused2
   unused3
   unused4

   DESCRIPTION
   Init function checks if the caller provides own dialog function.
   The function name must be mariadb_auth_dialog or
   mysql_authentication_dialog_ask. If the function cannot be found,
   we will use owr own simple command line input.

   RETURN
   0           success
 */
static int auth_caching_sha2_init(char *unused1 __attribute__((unused)),
    size_t unused2  __attribute__((unused)),
    int unused3     __attribute__((unused)),
    va_list unused4 __attribute__((unused)))
{
  return 0;
}
/* }}} */

/* {{{ auth_caching_sha2_deinit */
static int auth_caching_sha2_deinit(void)
{
  return 0;
}
/* }}} */

#endif  /* defined(HAVE_OPENSSL) || defined(HAVE_WINCRYPT) || defined(HAVE_GNUTLS)*/

