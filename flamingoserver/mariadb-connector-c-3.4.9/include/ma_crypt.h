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

#ifndef _ma_crypt_h_
#define _ma_crypt_h_

#include <ma_hash.h>
#include <stddef.h>
#include <stdarg.h>

/*! Hash algorithms */
#define MA_HASH_RIPEMD160 7
#define MA_HASH_MAX       8

/*! Hash digest sizes */
#define MA_RIPEMD160_HASH_SIZE 20

#if defined(HAVE_WINCRYPT)
typedef void MA_HASH_CTX;
#elif defined(HAVE_OPENSSL)
#include <openssl/evp.h>
typedef EVP_MD_CTX MA_HASH_CTX;
#elif defined(HAVE_GNUTLS)
typedef struct {
  void *ctx;
  const struct nettle_hash *hash;
} MA_HASH_CTX;
#endif

/**
  @brief acquire and initialize new hash context

  @param[in] algorithm   hash algorithm

  @return    hash context on success, NULL on error
*/
MA_HASH_CTX *ma_hash_new(unsigned int algorithm);

/**
  @brief release and deinitializes a hash context

  @param[in] hash context

  @return    void
*/
void ma_hash_free(MA_HASH_CTX *ctx);

/**
  @brief hashes len bytes of data into the hash context.
  This function can be called several times on same context to
  hash additional data.

  @param[in] ctx        hash context
  @param[in] buffer     data buffer
  @param[in] len        size of buffer

  @return               void
*/
void ma_hash_input(MA_HASH_CTX *ctx,
                   const unsigned char *buffer,
                   size_t len);

/**
  @brief retrieves the hash value from hash context 

  @param[in] ctx        hash context
  @param[out] digest    digest containing hash value

  @return               void
 */
void ma_hash_result(MA_HASH_CTX *ctx, unsigned char *digest);


/**
  @brief  returns digest size for a given hash algorithm

  @param[in] hash algorithm

  @returns digest size or 0 on error
*/
static inline size_t ma_hash_digest_size(unsigned int hash_alg)
{
  switch(hash_alg) {
  case MA_HASH_MD5:
    return MA_MD5_HASH_SIZE;
  case MA_HASH_SHA1:
    return MA_SHA1_HASH_SIZE;
  case MA_HASH_SHA224:
    return MA_SHA224_HASH_SIZE;
  case MA_HASH_SHA256:
    return MA_SHA256_HASH_SIZE;
  case MA_HASH_SHA384:
    return MA_SHA384_HASH_SIZE;
  case MA_HASH_SHA512:
    return MA_SHA512_HASH_SIZE;
  default:
    return 0;
  }
}

/**
  @brief function to compute hash from buffer.

  @param[in] hash_alg     hash algorithm
  @param[in] buffer       buffer
  @param[in] buffer_leng  length of buffer
  @param[out] digest      computed hash digest

  @return                 0 on success, 1 on failure
*/
static inline int ma_hash(unsigned int algorithm,
                           const unsigned char *buffer,
                           size_t buffer_length,
                           unsigned char *digest)
{
  MA_HASH_CTX *ctx;
  ctx= ma_hash_new(algorithm);
  if (!ctx)
    return 1;
  ma_hash_input(ctx, buffer, buffer_length);
  ma_hash_result(ctx, digest);
  ma_hash_free(ctx);
  return 0;
}

#endif /* _ma_crypt_h_ */
