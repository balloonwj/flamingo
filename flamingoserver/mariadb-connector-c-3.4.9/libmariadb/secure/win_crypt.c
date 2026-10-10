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
#include <windows.h>
#include <bcrypt.h>
#include <ma_crypt.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>

/*
  Error handling for bcrypt.
  If we can't meaningfully return an error, dump error on stderr
  and abort. Those errors are mostly likely programming errors
  (invalid parameters and such)
*/
static inline void check_nt_status(int ret, const char *function,
                                   const char *file, int line)
{
  if (ret)
  {
    fprintf(stderr,"invalid return %d from bcrypt, "
        "function %s with file %s, line %d\n",
        ret, function, file, line);
    abort();
  }
}
#define CHECK_NT_STATUS(ret) check_nt_status(ret, __func__, __FILE__, __LINE__)

/*
  Return Bcrypt algorithm ID (wchar string) for MariaDB numeric ID
*/
static LPCWSTR ma_hash_get_algorithm(unsigned int alg)
{
  switch (alg)
  {
  case MA_HASH_SHA1:
    return BCRYPT_SHA1_ALGORITHM;
  case MA_HASH_SHA256:
    return BCRYPT_SHA256_ALGORITHM;
  case MA_HASH_SHA384:
    return BCRYPT_SHA384_ALGORITHM;
  case MA_HASH_SHA512:
    return BCRYPT_SHA512_ALGORITHM;
  default:
    return NULL;
  }
}

/* Cached algorithm provides handles. */
static BCRYPT_ALG_HANDLE cached_alg_handles[MA_HASH_MAX];

/*
  Cleanup cached algorithm handles. It runs either on process exit,
  or when DLL is unloaded (see _onexit() documentation)
*/
static int win_crypt_onexit(void)
{
  int i;
  for (i= 0; i < MA_HASH_MAX; i++)
  {
    if (cached_alg_handles[i])
      BCryptCloseAlgorithmProvider(cached_alg_handles[i], 0);
  }
  return 0;
}

static void register_cleanup_onexit_once()
{
  static LONG onexit_called;
  if (!InterlockedCompareExchange(&onexit_called, 1, 0))
    _onexit(win_crypt_onexit);
}

/*
  Given algorithm ID, return BCRYPT provider handle.
  Uses or populates algorithm provider handle cache.
*/
static BCRYPT_ALG_HANDLE ma_hash_get_algorithm_handle(unsigned int alg)
{
  static SRWLOCK lock= SRWLOCK_INIT;
  BCRYPT_ALG_HANDLE handle= NULL;
  const wchar_t *name;

  if ((handle= cached_alg_handles[alg]) != NULL)
    return handle;

  name= ma_hash_get_algorithm(alg);
  if (!name)
    return NULL;

  AcquireSRWLockExclusive(&lock);
  if ((handle= cached_alg_handles[alg]) == NULL)
  {
    if (BCryptOpenAlgorithmProvider(&handle, name, NULL, 0) == 0)
      cached_alg_handles[alg]= handle;
    else
      handle= NULL;
  }
  ReleaseSRWLockExclusive(&lock);

  if (handle)
    register_cleanup_onexit_once();
  return handle;
}

MA_HASH_CTX *ma_hash_new(unsigned int algorithm)
{
  BCRYPT_HASH_HANDLE hash_handle;
  BCRYPT_ALG_HANDLE alg_handle= ma_hash_get_algorithm_handle(algorithm);

  if (!alg_handle)
    return NULL;

  if (BCryptCreateHash(alg_handle, &hash_handle, NULL, 0, NULL, 0, 0))
    return NULL;

  return hash_handle;
}

void ma_hash_free(MA_HASH_CTX *ctx)
{
  NTSTATUS status;
  if (!ctx)
    return;
  status= BCryptDestroyHash(ctx);
  CHECK_NT_STATUS(status);
}

void ma_hash_input(MA_HASH_CTX *ctx, const unsigned char *buffer, size_t len)
{
  NTSTATUS status= BCryptHashData(ctx, (PUCHAR) buffer, (ULONG) len, 0);
  CHECK_NT_STATUS(status);
}

void ma_hash_result(MA_HASH_CTX *ctx, unsigned char *digest)
{
  DWORD hash_length;
  DWORD data_length;
  NTSTATUS status=
      BCryptGetProperty(ctx, BCRYPT_HASH_LENGTH, (PBYTE) &hash_length,
                        sizeof(DWORD), &data_length, 0);
  CHECK_NT_STATUS(status);

  status= BCryptFinishHash(ctx, digest, (ULONG) hash_length, 0);
  CHECK_NT_STATUS(status);
}
