/************************************************************************************
  Copyright (C) 2019 MariaDB Corporation Ab

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

#pragma once
#include <windows.h>
#include <wincrypt.h>
typedef struct _client_cert_handle {
  HCRYPTPROV prov; /* provider */
  HCRYPTKEY  key;  /* private key */
  PCCERT_CONTEXT cert; /* client certificate */
  DWORD flags; /* Flags used in CryptAcquireContext, e.g CRYPT_MACHINE_KEYSET*/
  wchar_t key_container_name[64]; /* key container name */
} client_cert_handle;


void schannel_destroy_client_cert_handle(client_cert_handle *cert);

extern SECURITY_STATUS schannel_create_store(
  const char* CAFile,
  const char* CAPath,
  const char* CRLFile,
  const char* CRLPath,
  HCERTSTORE* store,
  char* errmsg,
  size_t errmsg_len
);

extern SECURITY_STATUS schannel_verify_server_certificate(
  const CERT_CONTEXT* cert,
  HCERTSTORE store,
  const char* server_name,
  unsigned int verify_flags,
  char* errmsg,
  size_t errmsg_len);

extern void schannel_free_store(HCERTSTORE store);

extern SECURITY_STATUS schannel_create_cert_context(
  char* cert_file,
  char* key_file,
  client_cert_handle* cert_handle,
  char* errmsg,
  size_t errmsg_len);

extern void schannel_free_cert_context(client_cert_handle *cert_handle);

