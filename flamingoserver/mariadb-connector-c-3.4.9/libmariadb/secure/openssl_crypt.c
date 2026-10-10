/*
    Copyright (C) 2018 MariaDB Corporation AB

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
*/
#include <ma_global.h>
#include <ma_crypt.h>
#include <openssl/evp.h>

static const EVP_MD *ma_hash_get_algorithm(unsigned int alg)
{
  switch(alg)
  {
  case MA_HASH_MD5:
    return EVP_md5();
  case MA_HASH_SHA1:
    return EVP_sha1();
  case MA_HASH_SHA224:
    return EVP_sha224();
  case MA_HASH_SHA256:
    return EVP_sha256();
  case MA_HASH_SHA384:
    return EVP_sha384();
  case MA_HASH_SHA512:
    return EVP_sha512();
  default:
    return NULL;
  }
}

MA_HASH_CTX *ma_hash_new(unsigned int algorithm)
{
  EVP_MD_CTX *ctx= NULL;
  const EVP_MD *evp_md= ma_hash_get_algorithm(algorithm);

  /* unknown or unsupported hash algorithm */
  if (!evp_md)
    return NULL;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
  if (!(ctx= EVP_MD_CTX_new()))
#else
  if (!(ctx= EVP_MD_CTX_create()))
#endif
    return NULL;
  if (!EVP_DigestInit(ctx, evp_md))
  {
    ma_hash_free(ctx);
    return NULL;
  }
  return ctx;
}

void ma_hash_free(MA_HASH_CTX *ctx)
{
  if (ctx)
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    EVP_MD_CTX_free(ctx);
#else
    EVP_MD_CTX_destroy(ctx);
#endif
}

void ma_hash_input(MA_HASH_CTX *ctx,
                    const unsigned char *buffer,
                    size_t len)
{
  EVP_DigestUpdate(ctx, buffer, len);
}

void ma_hash_result(MA_HASH_CTX *ctx, unsigned char *digest)
{
  EVP_DigestFinal_ex(ctx, digest, NULL);
}
