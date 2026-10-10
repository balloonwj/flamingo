/************************************************************************************
  Copyright (C) 2019 MariaDB

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

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
  Format Windows error, with optional text.

  For "known" errors we also output their symbolic error constant, e.g
  CERT_E_CN_NO_MATCH in addition to numeric value.

  We also try to output English text for all error messages, as not to  mess up
  with encodings.
*/
void ma_format_win32_error(char* buf, size_t buflen, DWORD code, _Printf_format_string_ const char* fmt, ...)
{
  char* cur = buf;
  char* end = cur + buflen;
  *cur = 0;
  if (fmt)
  {
    va_list vargs;
    va_start(vargs, fmt);
    cur += vsnprintf_s(cur, end - cur, _TRUNCATE, fmt, vargs);
    va_end(vargs);
  }

  if (code == 0)
    return;

  static struct map_entry
  {
    DWORD code;
    const char* sym;
    const char* msg;
  }
  map[] =
  {
#define ENTRY(x, y) {x,#x, y}
    ENTRY(SEC_E_WRONG_PRINCIPAL, "The target principal name is incorrect"),
    ENTRY(CERT_E_CN_NO_MATCH,"The certificate's CN name does not match the passed value"),
    ENTRY(SEC_E_UNTRUSTED_ROOT,"The certificate chain was issued by an authority that is not trusted"),
    ENTRY(TRUST_E_CERT_SIGNATURE,"The signature of the certificate cannot be verified"),
    ENTRY(SEC_E_CERT_EXPIRED,"The received certificate has expired"),
    ENTRY(CERT_E_EXPIRED,"A required certificate is not within its validity period when verifying against the current system clock or the timestamp in the signed file"),
    ENTRY(CRYPT_E_NO_REVOCATION_CHECK, "The revocation function was unable to check revocation for the certificate"),
    ENTRY(CRYPT_E_REVOCATION_OFFLINE,"The revocation function was unable to check revocation because the revocation server was offline"),
    ENTRY(CRYPT_E_REVOKED,"The certificate is revoked"),
    ENTRY(SEC_E_CERT_UNKNOWN,"An unknown error occurred while processing the certificate"),
    ENTRY(CERT_E_ROLE," A certificate that can only be used as an end-entity is being used as a CA or vice versa"),
    ENTRY(CERT_E_WRONG_USAGE,"The certificate is not valid for the requested usage"),
    ENTRY(SEC_E_ILLEGAL_MESSAGE, "The message received was unexpected or badly formatted"),
    ENTRY(CERT_E_VALIDITYPERIODNESTING,"The validity periods of the certification chain do not nest correctly"),
    ENTRY(CERT_E_PATHLENCONST,"A path length constraint in the certification chain has been violated"),
    ENTRY(CERT_E_CRITICAL,"A certificate contains an unknown extension that is marked 'critical'"),
    ENTRY(CERT_E_PURPOSE,"A certificate being used for a purpose other than the ones specified by its CA"),
    ENTRY(CERT_E_ISSUERCHAINING,"A parent of a given certificate in fact did not issue that child certificate"),
    ENTRY(CERT_E_MALFORMED, "A certificate is missing or has an empty value for an important field, such as a subject or issuer name"),
    ENTRY(CERT_E_CHAINING,"A certificate chain could not be built to a trusted root authority"),
    ENTRY(TRUST_E_FAIL," Generic trust failure"),
    ENTRY(CERT_E_UNTRUSTEDTESTROOT,"The certification path terminates with the test root which is not trusted with the current policy settings"),
    ENTRY(CERT_E_UNTRUSTEDROOT,"A certificate chain processed, but terminated in a root certificate which is not trusted by the trust provider"),
    ENTRY(CERT_E_REVOCATION_FAILURE,"The revocation process could not continue - the certificate(s) could not be checked"),
    ENTRY(SEC_E_ILLEGAL_MESSAGE, "The message received was unexpected or badly formatted"),
    ENTRY(SEC_E_UNTRUSTED_ROOT, "Untrusted root certificate"),
    ENTRY(SEC_E_BUFFER_TOO_SMALL, "Buffer too small"),
    ENTRY(SEC_E_CRYPTO_SYSTEM_INVALID, "Cipher is not supported"),
    ENTRY(SEC_E_INSUFFICIENT_MEMORY, "Out of memory"),
    ENTRY(SEC_E_OUT_OF_SEQUENCE, "Invalid message sequence"),
    ENTRY(SEC_E_DECRYPT_FAILURE, "The specified data could not be decrypted"),
    ENTRY(SEC_I_INCOMPLETE_CREDENTIALS, "Incomplete credentials"),
    ENTRY(SEC_E_ENCRYPT_FAILURE, "The specified data could not be encrypted"),
    ENTRY(SEC_I_CONTEXT_EXPIRED, "The context has expired and can no longer be used"),
    ENTRY(SEC_E_ALGORITHM_MISMATCH, "no cipher match"),
    ENTRY(SEC_E_NO_CREDENTIALS, "no credentials"),
    ENTRY(SEC_E_INVALID_TOKEN, "The token supplied to function is invalid"),
    ENTRY(SEC_E_UNSUPPORTED_FUNCTION,"The function requested is not supported")
  };

  struct map_entry* entry = NULL;

  if (cur > buf && cur[-1] != ' ' && cur[-1] != '.')
  {
    strncpy_s(cur,end-cur, ". ", _TRUNCATE);
    cur += 2;
  }

  for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
  {
    if (code == map[i].code)
    {
      entry = &map[i];
      break;
    }
  }
  if (cur > end - 20)
    return;
  if (entry)
  {
    snprintf(cur, end - cur, "%s. Error 0x%08lX(%s)", entry->msg, code, entry->sym);
  }
  else
  {
    cur += FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
      cur, (DWORD)(end - cur), NULL);
    while (cur > buf &&  (*cur == '\0' || *cur == '\n' || *cur == '\r' || *cur == '.'))
      cur--;
    if (*cur)
    {
      cur++;
      *cur = 0;
    }
    snprintf(cur, end - cur, ". Error %lu/0x%08lX", code, code);
  }
  end[-1] = 0;
}

