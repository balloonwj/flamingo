#include <string.h>
#include "crypto_sign.h"
#include "crypto_hash_sha512.h"
#include "ge.h"
#include "sc.h"

int ma_crypto_sign(
  unsigned char *sm, unsigned char *pk,
  const unsigned char *m,unsigned long long mlen,
  const unsigned char *pw,unsigned long long pwlen
)
{
  unsigned char az[64];
  unsigned char nonce[64];
  unsigned char hram[64];
  ge_p3 A, R;

  if (crypto_hash_sha512(az,pw,pwlen))
    return -1;
  az[0] &= 248;
  az[31] &= 63;
  az[31] |= 64;

  memmove(sm + 64,m,mlen);
  memmove(sm + 32,az + 32,32);
  if (crypto_hash_sha512(nonce,sm + 32,mlen + 32))
    return -1;

  ge_scalarmult_base(&A,az);
  ge_p3_tobytes(sm + 32,&A);
  memmove(pk, sm + 32, 32);

  sc_reduce(nonce);
  ge_scalarmult_base(&R,nonce);
  ge_p3_tobytes(sm,&R);

  if (crypto_hash_sha512(hram,sm,mlen + 64))
    return -1;
  sc_reduce(hram);
  sc_muladd(sm + 32,hram,az,nonce);

  return 0;
}
