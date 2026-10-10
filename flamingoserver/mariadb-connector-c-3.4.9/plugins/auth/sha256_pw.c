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
#undef HAVE_GNUTLS
#undef HAVE_OPENSSL
#endif

#if defined(HAVE_OPENSSL) || defined(HAVE_WINCRYPT)

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

#if defined(HAVE_WINCRYPT)
#include <wincrypt.h>
#elif defined(HAVE_OPENSSL)
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#endif

#define MAX_PW_LEN 1024

/* function prototypes */
static int auth_sha256_client(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql);
static int auth_sha256_init(char *unused1,
    size_t unused2,
    int unused3,
    va_list);


#ifndef PLUGIN_DYNAMIC
struct st_mysql_client_plugin_AUTHENTICATION sha256_password_client_plugin=
#else
MARIADB_CLIENT_PLUGIN_EXPORT struct st_mysql_client_plugin_AUTHENTICATION
    _mysql_client_plugin_declaration_=
#endif
{
  MYSQL_CLIENT_AUTHENTICATION_PLUGIN,
  MYSQL_CLIENT_AUTHENTICATION_PLUGIN_INTERFACE_VERSION,
  "sha256_password",
  "Georg Richter",
  "SHA256 Authentication Plugin",
  {0,1,0},
  "LGPL",
  NULL,
  auth_sha256_init,
  NULL,
  NULL,
  auth_sha256_client,
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
  if (!(der_buffer= (LPBYTE)LocalAlloc(0, der_buffer_length)))
    goto end;
  /* convert to DER binary */
  if (!CryptStringToBinaryA(buffer, *buffer_len, CRYPT_STRING_BASE64HEADER,
        der_buffer, &der_buffer_length, NULL, NULL))
    goto end;

  *buffer_len= der_buffer_length;

  return der_buffer;

end:
  if (der_buffer)
    LocalFree(der_buffer);
  *buffer_len= 0;
  return NULL;
}
#endif

static char *load_pub_key_file(const char *filename, int *pub_key_size)
{
  FILE *fp= NULL;
  char *buffer= NULL;
  unsigned char error= 1;
  size_t bytes_read= 0;
  long fsize= 0;

  if (!pub_key_size)
    return NULL;

  if (!(fp= fopen(filename, "r")))
    goto end;

  if (fseek(fp, 0, SEEK_END))
    goto end;

  fsize= ftell(fp);
  if (fsize < 0)
    goto end;

  rewind(fp);

  if (!(buffer= malloc(fsize + 1)))
    goto end;

  bytes_read= fread(buffer, 1, (size_t)fsize, fp);
  if (bytes_read < (size_t)fsize)
    goto end;

  *pub_key_size= (int)bytes_read;

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


static int auth_sha256_client(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql)
{
  unsigned char *packet;
  int packet_length;
  int rc= CR_ERROR;
  char passwd[MAX_PW_LEN];
  unsigned int rsa_size;
  unsigned int pwlen, i;

#if defined(HAVE_OPENSSL)
  EVP_PKEY *pubkey= NULL;
  EVP_PKEY_CTX *ctx= NULL;
  size_t outlen= 0;
  unsigned char *rsa_enc_pw= NULL;
  BIO *bio;
#elif defined(HAVE_WINCRYPT)
  unsigned char rsa_enc_pw[MAX_PW_LEN];
  HCRYPTKEY pubkey= 0;
  HCRYPTPROV hProv= 0;
  LPBYTE der_buffer= NULL;
  DWORD der_buffer_len= 0;
  CERT_PUBLIC_KEY_INFO *publicKeyInfo= NULL;
  DWORD ParamSize= sizeof(DWORD);
  int publicKeyInfoLen= 0;
#endif
  char *filebuffer= NULL;

  /* read error */
  if ((packet_length= vio->read_packet(vio, &packet)) < 0)
    return CR_ERROR;

  if (packet_length != SCRAMBLE_LENGTH + 1)
    return CR_SERVER_HANDSHAKE_ERR;

  memmove(mysql->scramble_buff, packet, SCRAMBLE_LENGTH);
  mysql->scramble_buff[SCRAMBLE_LENGTH]= 0;

  /* if a tls session is active we need to send plain password */
  if (mysql->client_flag & CLIENT_SSL)
  {
    if (vio->write_packet(vio, (unsigned char *)mysql->passwd, (int)strlen(mysql->passwd) + 1))
      return CR_ERROR;
    return CR_OK;
  }

  /* send empty packet if no password was provided */
  if (!mysql->passwd || !mysql->passwd[0])
  {
    if (vio->write_packet(vio, 0, 0))
      return CR_ERROR;
    return CR_OK;
  }

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
    unsigned char buf= 1;
    if (vio->write_packet(vio, &buf, 1))
      return CR_ERROR;
    if ((packet_length=vio->read_packet(vio, &packet)) == -1)
      return CR_ERROR;
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
                           &publicKeyInfo, (DWORD *)&publicKeyInfoLen))
    goto error;
  LocalFree(der_buffer);

  if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, 
                           CRYPT_VERIFYCONTEXT))
    goto error;
  if (!CryptImportPublicKeyInfo(hProv, X509_ASN_ENCODING,
                                publicKeyInfo, &pubkey))
    goto error;

  /* Get rsa_size */
  CryptGetKeyParam(pubkey, KP_KEYLEN, (BYTE *)&rsa_size, &ParamSize, 0);
  rsa_size /= 8;
#endif
  if (!pubkey)
    return CR_ERROR;

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
  if (!CryptEncrypt(pubkey, 0, TRUE, CRYPT_OAEP, (BYTE *)passwd, (DWORD *)&pwlen, MAX_PW_LEN))
    goto error;
  /* Windows encrypts as little-endian, while server (openssl) expects
     big-endian, so we have to revert the string */
  for (i= 0; i < rsa_size / 2; i++)
  {
    rsa_enc_pw[i]= passwd[rsa_size - 1 - i];
    rsa_enc_pw[rsa_size - 1 - i]= passwd[i];
  }
#endif
  if (vio->write_packet(vio, rsa_enc_pw, rsa_size))
    goto error;

  rc= CR_OK;
error:
#if defined(HAVE_OPENSSL)
  if (pubkey)
    EVP_PKEY_free(pubkey);
  if (bio)
    BIO_free(bio);
  if (ctx)
    EVP_PKEY_CTX_free(ctx);
  if (rsa_enc_pw)
    free(rsa_enc_pw);
#elif defined(HAVE_WINCRYPT)
  CryptReleaseContext(hProv, 0);
  if (publicKeyInfo)
    LocalFree(publicKeyInfo);
#endif
  free(filebuffer);
  return rc;
}
/* }}} */

/* {{{ static int auth_sha256_init */
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
static int auth_sha256_init(char *unused1 __attribute__((unused)), 
    size_t unused2  __attribute__((unused)), 
    int unused3     __attribute__((unused)), 
    va_list unused4 __attribute__((unused)))
{
  return 0;
}
/* }}} */

#endif  /* defined(HAVE_OPENSSL) || defined(HAVE_WINCRYPT) */
