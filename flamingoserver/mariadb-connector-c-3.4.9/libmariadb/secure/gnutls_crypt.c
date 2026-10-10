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
#include <ma_crypt.h>
#include <gnutls/gnutls.h>
#include <gnutls/crypto.h>

static gnutls_digest_algorithm_t ma_hash_get_algorithm(unsigned int alg)
{
  switch(alg)
  {
  case MA_HASH_MD5:
    return GNUTLS_DIG_MD5;
  case MA_HASH_SHA1:
    return GNUTLS_DIG_SHA1;
  case MA_HASH_SHA256:
    return GNUTLS_DIG_SHA256;
  case MA_HASH_SHA384:
    return GNUTLS_DIG_SHA384;
  case MA_HASH_SHA512:
    return GNUTLS_DIG_SHA512;
  default:
    return GNUTLS_DIG_UNKNOWN;
  }
}

MA_HASH_CTX *ma_hash_new(unsigned int algorithm)
{
  gnutls_hash_hd_t ctx= NULL;
  gnutls_digest_algorithm_t hash_alg= ma_hash_get_algorithm(algorithm);

  /* unknown or unsupported hash algorithm */
  if (hash_alg == GNUTLS_DIG_UNKNOWN)
    return NULL;

  if (gnutls_hash_init(&ctx, hash_alg) < 0)
    return NULL;

  return (MA_HASH_CTX *)ctx;
}

void ma_hash_free(MA_HASH_CTX *ctx)
{
  if (ctx)
    gnutls_hash_deinit((gnutls_hash_hd_t)ctx, NULL);
}

void ma_hash_input(MA_HASH_CTX *ctx,
                    const unsigned char *buffer,
                    size_t len)
{
  gnutls_hash((gnutls_hash_hd_t)ctx, (const void *)buffer, len);
}

void ma_hash_result(MA_HASH_CTX *ctx, unsigned char *digest)
{
  gnutls_hash_output((gnutls_hash_hd_t)ctx, digest);
}


