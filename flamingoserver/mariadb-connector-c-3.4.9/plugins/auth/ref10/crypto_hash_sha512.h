#include <ma_crypt.h>
#define crypto_hash_sha512(DST,SRC,SLEN) ma_hash(MA_HASH_SHA512, SRC, SLEN, DST)
