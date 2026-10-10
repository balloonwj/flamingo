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

 /*
  This module contain X509 certificate handling on Windows.
  PEM parsing, loading client certificate and key, server certificate validation
 */

 /*
  CERT_CHAIN_ENGINE_CONFIG has additional members in Windows 8.1
  To allow client to be work on pre-8.1 Windows, compile
  with corresponding _WIN32_WINNT
 */
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "schannel_certs.h"
#include "ma_schannel.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <winhttp.h>
#include <limits.h>
#include <assert.h>
#include "win32_errmsg.h"

 /*
   Return GetLastError(), or, if this unexpectedly gives success,
   return ERROR_INTERNAL_ERROR.

   Background - in several cases in this module we return GetLastError()
   after an Windows function fails. However, we do not want the function to
   return success, even if GetLastError() was suddenly 0.
 */
static DWORD get_last_error()
{
  DWORD ret = GetLastError();
  if (ret)
    return ret;

  // We generally expect last error to be set  API fails.
  // thus  the debug assertion-
  assert(0);
  return ERROR_INTERNAL_ERROR;
}

#define FAIL(...) \
   do{\
     status = get_last_error();\
     ma_format_win32_error(errmsg, errmsg_len, status, __VA_ARGS__);\
     goto cleanup;\
  } while (0)

/*
  Load file into memory. Add null terminator at the end, so it will be a valid C string.
*/
static char* pem_file_to_string(const char* file, char* errmsg, size_t errmsg_len)
{
  LARGE_INTEGER file_size;
  size_t file_bufsize = 0;
  size_t total_bytes_read = 0;
  char* file_buffer = NULL;
  SECURITY_STATUS status = SEC_E_OK;

  HANDLE file_handle = CreateFile(file, GENERIC_READ, FILE_SHARE_READ, NULL,
    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file_handle == INVALID_HANDLE_VALUE)
  {
    FAIL("failed to open file '%s'", file);
  }

  if (!GetFileSizeEx(file_handle, &file_size))
  {
    FAIL("GetFileSizeEx failed on '%s'", file);
  }

  if (file_size.QuadPart > ULONG_MAX - 1)
  {
    SetLastError(SEC_E_INVALID_PARAMETER);
    FAIL("file '%s' too large", file);
  }

  file_bufsize = (size_t)file_size.QuadPart;
  file_buffer = (char*)LocalAlloc(0,file_bufsize + 1);
  if (!file_buffer)
  {
    FAIL("LocalAlloc(0,%zu) failed", file_bufsize + 1);
  }

  while (total_bytes_read < file_bufsize)
  {
    DWORD bytes_to_read = (DWORD)(file_bufsize - total_bytes_read);
    DWORD bytes_read = 0;

    if (!ReadFile(file_handle, file_buffer + total_bytes_read,
      bytes_to_read, &bytes_read, NULL))
    {
      FAIL("ReadFile() failed to read  file '%s'", file);
    }
    if (bytes_read == 0)
    {
      /* Premature EOF -- adjust the bufsize to the new value */
      file_bufsize = total_bytes_read;
    }
    else
    {
      total_bytes_read += bytes_read;
    }
  }

  /* Null terminate the buffer */
  file_buffer[file_bufsize] = '\0';

cleanup:
  if (file_handle != INVALID_HANDLE_VALUE)
  {
    CloseHandle(file_handle);
  }
  if (status)
  {
    /* Some error happened. */
    LocalFree(file_buffer);
    file_buffer = NULL;
  }
  return file_buffer;
}


// Structure for parsing BEGIN/END sections inside pem.
typedef struct _pem_type_desc
{
  const char* begin_tag;
  size_t begin_tag_len;
  const char* end_tag;
  size_t end_tag_len;
} pem_type_desc;

#define BEGIN_TAG(x) "-----BEGIN " x  "-----"
#define END_TAG(x) "\n-----END " x  "-----"
#define PEM_SECTION(tag) {BEGIN_TAG(tag), sizeof(BEGIN_TAG(tag))-1, END_TAG(tag), sizeof(END_TAG(tag))-1}

typedef enum {
  PEM_TYPE_CERTIFICATE = 0,
  PEM_TYPE_X509_CRL,
  PEM_TYPE_RSA_PRIVATE_KEY,
  PEM_TYPE_PRIVATE_KEY
} PEM_TYPE;

static const pem_type_desc pem_sections[] = {
  PEM_SECTION("CERTIFICATE"),
  PEM_SECTION("X509 CRL"),
  PEM_SECTION("RSA PRIVATE KEY"),
  PEM_SECTION("PRIVATE KEY")
};

/*
  Locate a substring in pem for given type,
  e.g section between BEGIN CERTIFICATE and END CERTIFICATE
  in PEMs base64 format, with header and footer.

  output parameters 'begin' and 'end' are set upon return.
  it is possible that functions returns 'begin' != NULL but
  'end' = NULL. This is generally a format error, meaning that
  the end tag was not found
*/
void pem_locate(char* pem_str,
  PEM_TYPE type,
  char** begin,
  char** end)
{
  *begin = NULL;
  *end = NULL;
  char c;

  const pem_type_desc* desc = &pem_sections[type];
  *begin = strstr(pem_str, desc->begin_tag);
  if (!(*begin))
    return;

  // We expect newline after the
  // begin tag, LF or CRLF
  c = (*begin)[desc->begin_tag_len];

  if (c != '\r' && c != '\n')
  {
    *begin = NULL;
    return;
  }

  *end = strstr(*begin + desc->begin_tag_len + 1, desc->end_tag);
  if (!*end)
    return; // error, end marker not found

  (*end) += desc->end_tag_len;
  return;
}


/*
  Add certificates, or CRLs from a PEM file to Wincrypt store
*/
static SECURITY_STATUS add_certs_to_store(
  HCERTSTORE  trust_store,
  const char* file,
  PEM_TYPE type,
  char* errmsg,
  size_t      errmsg_len)
{
  char* file_buffer = NULL;
  char* cur = NULL;
  SECURITY_STATUS status = SEC_E_OK;
  CRL_CONTEXT* crl_context = NULL;
  CERT_CONTEXT* cert_context = NULL;
  char* begin;
  char* end;

  file_buffer = pem_file_to_string(file, errmsg, errmsg_len);
  if (!file_buffer)
  {
    status = ERROR_FILE_NOT_FOUND;
    goto cleanup;
  }

  for (cur = file_buffer; ; cur = end)
  {
    pem_locate(cur, type, &begin, &end);

    if (!begin)
      break;

    if (!end)
    {
      SetLastError(SEC_E_INVALID_PARAMETER);
      FAIL("Invalid PEM file '%s', missing end marker corresponding to begin marker '%s' at offset %zu",
        file, pem_sections[type].begin_tag, (size_t)(begin - file_buffer));
    }
    CERT_BLOB cert_blob;
    void* context = NULL;
    DWORD actual_content_type = 0;

    cert_blob.pbData = (BYTE*)begin;
    cert_blob.cbData = (DWORD)(end - begin);
    if (!CryptQueryObject(
      CERT_QUERY_OBJECT_BLOB, &cert_blob,
      CERT_QUERY_CONTENT_FLAG_CERT | CERT_QUERY_CONTENT_FLAG_CRL,
      CERT_QUERY_FORMAT_FLAG_ALL, 0, NULL, &actual_content_type,
      NULL, NULL, NULL, (const void**)&context))
    {
      FAIL("failed to extract certificate from PEM file '%s'",file);
    }

    if (!context)
    {
      SetLastError(SEC_E_INTERNAL_ERROR);
      FAIL("unexpected result from CryptQueryObject(),cert_context is NULL"
        " after successful completion, file '%s'",
        file);
    }

    if (actual_content_type == CERT_QUERY_CONTENT_CERT)
    {
      CERT_CONTEXT* cert_context = (CERT_CONTEXT*)context;
      if (!CertAddCertificateContextToStore(
        trust_store, cert_context,
        CERT_STORE_ADD_ALWAYS, NULL))
      {
        FAIL("CertAddCertificateContextToStore failed");
      }
    }
    else if (actual_content_type == CERT_QUERY_CONTENT_CRL)
    {
      CRL_CONTEXT* crl_context = (CRL_CONTEXT*)context;
      if (!CertAddCRLContextToStore(
        trust_store, crl_context,
        CERT_STORE_ADD_ALWAYS, NULL))
      {
        FAIL("CertAddCRLContextToStore() failed");
      }
    }
  }
cleanup:
  LocalFree(file_buffer);
  if (cert_context)
    CertFreeCertificateContext(cert_context);
  if (crl_context)
    CertFreeCRLContext(crl_context);
  return status;
}

/*
Add a directory to store, i.e try to load all files.
(extract certificates and add them to store)

@return 0 on success, error only if directory is invalid.
*/
SECURITY_STATUS add_dir_to_store(HCERTSTORE trust_store, const char* dir,
  PEM_TYPE type, char* errmsg, size_t errmsg_len)
{
  WIN32_FIND_DATAA ffd;
  char path[MAX_PATH];
  char pattern[MAX_PATH];
  DWORD dwAttr;
  HANDLE hFind = INVALID_HANDLE_VALUE;
  SECURITY_STATUS status = SEC_E_OK;

  if ((dwAttr = GetFileAttributes(dir)) == INVALID_FILE_ATTRIBUTES)
  {
    SetLastError(SEC_E_INVALID_PARAMETER);
    FAIL("directory '%s' does not exist", dir);
  }
  if (!(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
  {
    SetLastError(SEC_E_INVALID_PARAMETER);
    FAIL("'%s' is not a directory", dir);
  }
  sprintf_s(pattern, sizeof(pattern), "%s\\*", dir);
  hFind = FindFirstFile(pattern, &ffd);
  if (hFind == INVALID_HANDLE_VALUE)
  {
    FAIL("FindFirstFile(%s) failed",pattern);
  }
  do
  {
    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      continue;
    sprintf_s(path, sizeof(path), "%s\\%s", dir, ffd.cFileName);

    // ignore error from add_certs_to_store(), not all file
    // maybe PEM.
    add_certs_to_store(trust_store, path, type, errmsg,
      errmsg_len);
  } while (FindNextFile(hFind, &ffd) != 0);

cleanup:
  if (hFind != INVALID_HANDLE_VALUE)
    FindClose(hFind);

  return status;
}

/* Count certificates in store. */
static int count_certificates(HCERTSTORE store)
{
  int num_certs = 0;
  PCCERT_CONTEXT c = NULL;

  while ((c = CertEnumCertificatesInStore(store, c)))
    num_certs++;

  return num_certs;
}

/**
  Creates certificate store with user defined CA chain and/or CRL.
  Loads PEM certificate from files or directories.

  If only CRLFile/CRLPath is defined, the "system" store is duplicated,
  and new CRLs are added to it.

  If CAFile/CAPAth is defined, then new empty store is created, and CAs
  (and CRLs, if defined), are added to it.

  The function throws an error, if none of the files in CAFile/CAPath have a valid certificate.
  It is also an error if CRLFile does not exist.
*/
SECURITY_STATUS schannel_create_store(
  const char* CAFile,
  const char* CAPath,
  const char* CRLFile,
  const char* CRLPath,
  HCERTSTORE* out_store,
  char* errmsg,
  size_t      errmsg_len)
{

  HCERTSTORE store = NULL;
  HCERTSTORE system_store = NULL;
  int status = SEC_E_OK;

  *out_store = NULL;
  if (!CAFile && !CAPath && !CRLFile && !CRLPath)
  {
    /* Nothing to do, caller will use default store*/
    *out_store = NULL;
    return SEC_E_OK;
  }
  if (CAFile || CAPath)
  {
    /* Open the certificate store */
    store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, (HCRYPTPROV)NULL,
      CERT_STORE_CREATE_NEW_FLAG, NULL);
    if (!store)
    {
      FAIL("CertOpenStore failed for memory store");
    }
  }
  else if (CRLFile || CRLPath)
  {
    /* Only CRL was provided, copy system store, add revocation list to
     * it. */
    system_store =
      CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, (HCRYPTPROV_LEGACY)NULL,
        CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
    if (!system_store)
    {
       FAIL("CertOpenStore failed for system store");
    }

    store = CertDuplicateStore(system_store);
    if (!store)
    {
      FAIL("CertDuplicateStore failed");
    }
  }

  if (CAFile)
  {
    status = add_certs_to_store(store, CAFile,
      PEM_TYPE_CERTIFICATE, errmsg, errmsg_len);
    if (status)
      goto cleanup;
  }
  if (CAPath)
  {
    status = add_dir_to_store(store, CAPath,
      PEM_TYPE_CERTIFICATE, errmsg, errmsg_len);
    if (status)
      goto cleanup;
  }

  if ((CAFile || CAPath) && store && !count_certificates(store))
  {
    SetLastError(SEC_E_INVALID_PARAMETER);
    FAIL("no valid certificates were found, CAFile='%s', CAPath='%s'",
      CAFile ? CAFile : "<not set>", CAPath ? CAPath : "<not set>");
  }

  if (CRLFile)
  {
    status = add_certs_to_store(store, CRLFile, PEM_TYPE_X509_CRL,
      errmsg, errmsg_len);
  }
  if (CRLPath)
  {
    status = add_dir_to_store(store, CRLPath, PEM_TYPE_X509_CRL,
      errmsg, errmsg_len);
  }

cleanup:
  if (system_store)
    CertCloseStore(system_store, 0);
  if (status && store)
  {
    CertCloseStore(store, 0);
    store = NULL;
  }
  *out_store = store;
  return status;
}

/*
  The main verification logic.
  Taken almost completely from Windows 2003 Platform SDK 2003
  (Samples\Security\SSPI\SSL\WebClient.c)

  The only difference here is is usage of custom store
  and chain engine.
*/
static SECURITY_STATUS VerifyServerCertificate(
  PCCERT_CONTEXT  pServerCert,
  HCERTSTORE      hStore,
  LPWSTR          pwszServerName,
  DWORD           dwCertChainFlags,
  DWORD           dwVerifyFlags,
  LPSTR           errmsg,
  size_t          errmsg_len)
{
  SSL_EXTRA_CERT_CHAIN_POLICY_PARA  polExtra;
  CERT_CHAIN_POLICY_PARA   PolicyPara;
  CERT_CHAIN_POLICY_STATUS PolicyStatus;
  CERT_CHAIN_PARA          ChainPara;
  HCERTCHAINENGINE         hChainEngine = NULL;
  PCCERT_CHAIN_CONTEXT     pChainContext = NULL;
  LPSTR rgszUsages[] = { szOID_PKIX_KP_SERVER_AUTH,
                          szOID_SERVER_GATED_CRYPTO,
                          szOID_SGC_NETSCAPE };
  DWORD cUsages = sizeof(rgszUsages) / sizeof(LPSTR);
  SECURITY_STATUS status = SEC_E_OK;

  if (pServerCert == NULL)
  {
    SetLastError(SEC_E_WRONG_PRINCIPAL);
    FAIL("Invalid parameter pServerCert passed to VerifyServerCertificate");
  }

  ZeroMemory(&ChainPara, sizeof(ChainPara));
  ChainPara.cbSize = sizeof(ChainPara);
  ChainPara.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
  ChainPara.RequestedUsage.Usage.cUsageIdentifier = cUsages;
  ChainPara.RequestedUsage.Usage.rgpszUsageIdentifier = rgszUsages;

  if (hStore)
  {
    CERT_CHAIN_ENGINE_CONFIG EngineConfig = { 0 };
    EngineConfig.cbSize = sizeof(EngineConfig);
    EngineConfig.hExclusiveRoot = hStore;
    if (!CertCreateCertificateChainEngine(&EngineConfig, &hChainEngine))
    {
      FAIL("CertCreateCertificateChainEngine failed");
    }
  }

  if (!CertGetCertificateChain(
    hChainEngine,
    pServerCert,
    NULL,
    pServerCert->hCertStore,
    &ChainPara,
    dwCertChainFlags,
    NULL,
    &pChainContext))
  {
    FAIL("CertGetCertificateChain failed");
    goto cleanup;
  }

  // Validate certificate chain.
  ZeroMemory(&polExtra, sizeof(SSL_EXTRA_CERT_CHAIN_POLICY_PARA));
  polExtra.cbStruct = sizeof(SSL_EXTRA_CERT_CHAIN_POLICY_PARA);
  polExtra.dwAuthType = AUTHTYPE_SERVER;
  polExtra.fdwChecks = dwVerifyFlags;
  polExtra.pwszServerName = pwszServerName;

  memset(&PolicyPara, 0, sizeof(PolicyPara));
  PolicyPara.cbSize = sizeof(PolicyPara);
  PolicyPara.pvExtraPolicyPara = &polExtra;
  PolicyPara.dwFlags= CERT_CHAIN_POLICY_IGNORE_ALL_REV_UNKNOWN_FLAGS;

  memset(&PolicyStatus, 0, sizeof(PolicyStatus));
  PolicyStatus.cbSize = sizeof(PolicyStatus);

  if (!CertVerifyCertificateChainPolicy(
    CERT_CHAIN_POLICY_SSL,
    pChainContext,
    &PolicyPara,
    &PolicyStatus))
  {
    FAIL("CertVerifyCertificateChainPolicy failed");
  }

  if (PolicyStatus.dwError)
  {
    SetLastError(PolicyStatus.dwError);
    FAIL("Server certificate validation failed");
  }

cleanup:
  if (hChainEngine)
  {
    CertFreeCertificateChainEngine(hChainEngine);
  }
  if (pChainContext)
  {
    CertFreeCertificateChain(pChainContext);
  }
  return status;
}


void schannel_free_store(HCERTSTORE store)
{
  if (store)
    CertCloseStore(store, 0);
}


/*
Verify server certificate against a wincrypt store
@return 0 - success, otherwise error occurred.
*/
SECURITY_STATUS schannel_verify_server_certificate(
  const CERT_CONTEXT* cert,
  HCERTSTORE store,
  const char *server_name,
  unsigned int verify_flags,
  char* errmsg,
  size_t errmsg_len)
{
  SECURITY_STATUS status = SEC_E_OK;
  wchar_t* wserver_name = NULL;
  DWORD dwVerifyFlags;
  DWORD dwCertChainFlags;

  if (!verify_flags)
    return status;

  if (verify_flags & MARIADB_TLS_VERIFY_HOST)
  {
    int cchServerName = (int)strlen(server_name) + 1;
    wserver_name = (wchar_t*)LocalAlloc(0,sizeof(wchar_t) * cchServerName);
    if (!wserver_name)
    {
      FAIL("LocalAlloc() failed");
    }
    if (MultiByteToWideChar(CP_UTF8, 0, server_name, cchServerName, wserver_name, cchServerName) < 0)
    {
      FAIL("MultiByteToWideChar() failed");
    }
  }

  dwVerifyFlags = 0;
  dwCertChainFlags = CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL|CERT_CHAIN_CACHE_END_CERT;
  if (verify_flags & MARIADB_TLS_VERIFY_REVOKED)
    dwCertChainFlags |= CERT_CHAIN_REVOCATION_CHECK_END_CERT|CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY;
  if (!(verify_flags & MARIADB_TLS_VERIFY_HOST))
    dwVerifyFlags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
  if (!(verify_flags & MARIADB_TLS_VERIFY_PERIOD))
    dwVerifyFlags |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
  if (!(verify_flags & MARIADB_TLS_VERIFY_TRUST))
    dwVerifyFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;

  status = VerifyServerCertificate(cert, store, wserver_name ? wserver_name : L"SERVER_NAME",
    dwCertChainFlags, dwVerifyFlags, errmsg, errmsg_len);

cleanup:
  LocalFree(wserver_name);
  return status;
}

/*
  Generate a random key container name.
  Used to store private key in Windows key store.
*/
#define KEY_CONTAINER_NAME_PREFIX L"MariaDB-Connector-C-"
static void generate_key_container_name(wchar_t* key_container_name, size_t key_container_name_len)
{
   LARGE_INTEGER now;
   QueryPerformanceCounter(&now);
   swprintf_s(key_container_name, key_container_name_len,
              L"MariaDB-Connector-C-%u-%u-%lld",GetCurrentProcessId(),GetCurrentThreadId(),now.QuadPart);
}

/**
  Execute CryptAcquireContext().
  The preference order is persistent user key container with unique name,
  with fallback machine key container with uniquename.

  @param cert_handle - structure to store provider handle, key handle, and
  certificate
  @return TRUE on success, FALSE on failure.

  @note If the function returns TRUE, the key_container_name and flags fields
  of cert_handle are set.
*/
static BOOL crypt_acquire_context(client_cert_handle *cert_handle)
{
  DWORD all_flags[]= {CRYPT_NEWKEYSET, CRYPT_NEWKEYSET | CRYPT_MACHINE_KEYSET};
  size_t i;
  generate_key_container_name(cert_handle->key_container_name,
      sizeof(cert_handle->key_container_name) / sizeof(wchar_t));
  for (i= 0; i < ARRAYSIZE(all_flags); i++)
  {
    DWORD fl= all_flags[i];
    if (CryptAcquireContextW(&cert_handle->prov, cert_handle->key_container_name,
                             MS_ENHANCED_PROV_W, PROV_RSA_FULL, fl))
    {
      cert_handle->flags= fl;
      return TRUE;
    }
    if (GetLastError() != ERROR_ACCESS_DENIED)
      break;
    cert_handle->prov= 0;
  }
  cert_handle->key_container_name[0]= 0;
  cert_handle->flags= 0;
  return FALSE;
}


/* Attach private key (in PEM format) to client certificate */
static SECURITY_STATUS load_private_key(client_cert_handle *cert_handle, char *private_key_str,
                                        size_t len, char *errmsg,
                                        size_t errmsg_len)
{
  DWORD derlen = (DWORD)len;
  BYTE* derbuf = NULL;
  DWORD keyblob_len = 0;
  BYTE* keyblob = NULL;
  PCRYPT_PRIVATE_KEY_INFO  pki = NULL;
  DWORD pki_len = 0;
  SECURITY_STATUS status = SEC_E_OK;

  derbuf = LocalAlloc(0, derlen);
  if (!derbuf)
  {
    FAIL("LocalAlloc failed");
  }

  if (!CryptStringToBinaryA(private_key_str, (DWORD)len, CRYPT_STRING_BASE64HEADER, derbuf, &derlen, NULL, NULL))
  {
    FAIL("Failed to convert BASE64 private key");
  }

  /*
   To accommodate for both "BEGIN PRIVATE KEY" vs "BEGIN RSA PRIVATE KEY"
   sections in PEM, we try to decode with PKCS_PRIVATE_KEY_INFO first,
   and, if it fails, with PKCS_RSA_PRIVATE_KEY flag.
  */
  if (CryptDecodeObjectEx(
    X509_ASN_ENCODING,
    PKCS_PRIVATE_KEY_INFO,
    derbuf, derlen,
    CRYPT_DECODE_ALLOC_FLAG,
    NULL, &pki, &pki_len))
  {
    // convert private key info to RSA private key blob
    if (!CryptDecodeObjectEx(
      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
      PKCS_RSA_PRIVATE_KEY,
      pki->PrivateKey.pbData,
      pki->PrivateKey.cbData,
      CRYPT_DECODE_ALLOC_FLAG,
      NULL, &keyblob, &keyblob_len))
    {
      FAIL("Failed to parse private key");
    }
  }
  else if (!CryptDecodeObjectEx(
    X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
    PKCS_RSA_PRIVATE_KEY,
    derbuf, derlen,
    CRYPT_DECODE_ALLOC_FLAG, NULL,
    &keyblob, &keyblob_len))
  {
    FAIL("Failed to parse private key");
  }

  if (!crypt_acquire_context(cert_handle))
  {
    FAIL("CryptAcquireContext failed");
  }

  if (!CryptImportKey(cert_handle->prov, keyblob, keyblob_len, 0, 0,
                      &cert_handle->key))
  {
    FAIL("CryptImportKey failed");
  }
  cert_handle->flags &= ~CRYPT_NEWKEYSET;

  // Link the private key to the certificate
  CRYPT_KEY_PROV_INFO keyProvInfo= {0};
  keyProvInfo.pwszContainerName= cert_handle->key_container_name;
  keyProvInfo.pwszProvName= NULL;
  keyProvInfo.dwProvType= PROV_RSA_FULL;
  keyProvInfo.dwFlags= cert_handle->flags;
  keyProvInfo.cProvParam= 0;
  keyProvInfo.rgProvParam= NULL;
  keyProvInfo.dwKeySpec= AT_KEYEXCHANGE;
  if (!CertSetCertificateContextProperty(cert_handle->cert, CERT_KEY_PROV_INFO_PROP_ID, 0, &keyProvInfo))
  {
    FAIL("CertSetCertificateContextProperty failed");
  }

cleanup:
  LocalFree(derbuf);
  LocalFree(keyblob);
  LocalFree(pki);
  return status;
}

/*
 Given PEM strings for certificate and private key,
 create a client certificate*
*/
static SECURITY_STATUS create_client_certificate_mem(
  char* cert_file_content,
  char* key_file_content,
  client_cert_handle* cert_handle,
  char* errmsg,
  size_t errmsg_len)
{
  char* begin;
  char* end;
  CERT_BLOB cert_blob;
  DWORD actual_content_type = 0;
  SECURITY_STATUS status = SEC_E_OK;

  /* Parse certificate */
  pem_locate(cert_file_content, PEM_TYPE_CERTIFICATE,
    &begin, &end);

  if (!begin || !end)
  {
    SetLastError(SEC_E_INVALID_PARAMETER);
    FAIL("Client certificate not found in PEM file");
  }

  cert_blob.pbData = (BYTE*)begin;
  cert_blob.cbData = (DWORD)(end - begin);
  if (!CryptQueryObject(
    CERT_QUERY_OBJECT_BLOB, &cert_blob,
    CERT_QUERY_CONTENT_FLAG_CERT,
    CERT_QUERY_FORMAT_FLAG_ALL, 0, NULL, &actual_content_type,
    NULL, NULL, NULL, (const void**)&cert_handle->cert))
  {
    FAIL("Can't parse client certficate");
  }

  /* Parse key */
  PEM_TYPE types[] = { PEM_TYPE_RSA_PRIVATE_KEY, PEM_TYPE_PRIVATE_KEY };
  for (int i = 0; i < sizeof(types) / sizeof(types[0]); i++)
  {
    pem_locate(key_file_content, types[i], &begin, &end);
    if (begin && end)
    {
      /* Assign key to certificate.*/
      status = load_private_key(cert_handle, begin, (end - begin), errmsg, errmsg_len);
      goto cleanup;
    }
  }

  if (!begin || !end)
  {
    SetLastError(SEC_E_INVALID_PARAMETER);
    FAIL("Client private key not found in PEM");
  }

cleanup:
  return status;
}


/* Given cert and key, as PEM file names, create a client certificate */
SECURITY_STATUS schannel_create_cert_context(char* cert_file, char* key_file,
    client_cert_handle *cert_handle,
    char* errmsg, size_t errmsg_len)
{
  char* key_file_content = NULL;
  char* cert_file_content = NULL;
  SECURITY_STATUS status= SEC_E_INTERNAL_ERROR;

  memset(cert_handle, 0, sizeof(client_cert_handle));

  cert_file_content = pem_file_to_string(cert_file, errmsg, errmsg_len);

  if (!cert_file_content)
    goto cleanup;

  if (cert_file == key_file)
  {
    key_file_content = cert_file_content;
  }
  else
  {
    key_file_content = pem_file_to_string(key_file, errmsg, errmsg_len);
    if (!key_file_content)
      goto cleanup;
  }

  status = create_client_certificate_mem(cert_file_content, key_file_content, cert_handle, errmsg, errmsg_len);

cleanup:
  LocalFree(cert_file_content);
  if (cert_file != key_file)
    LocalFree(key_file_content);
  if (status)
    schannel_free_cert_context(cert_handle);
  return status;
}

/*
  Free certificate, and all resources, created by schannel_create_cert_context()
*/
void schannel_free_cert_context(client_cert_handle* cert_handle)
{
  if (cert_handle->key)
  {
    CryptDestroyKey(cert_handle->key);
    cert_handle->key = 0;
  }
  if (cert_handle->prov)
  {
    CryptReleaseContext(cert_handle->prov, 0);
    cert_handle->prov = 0;
  }
  if (cert_handle->cert)
  {
    CertFreeCertificateContext(cert_handle->cert);
    cert_handle->cert = 0;
  }
  if (cert_handle->key_container_name[0])
  {
    if (!CryptAcquireContextW(&cert_handle->prov, cert_handle->key_container_name,
                              MS_ENHANCED_PROV_W, PROV_RSA_FULL,
                              CRYPT_DELETEKEYSET|cert_handle->flags))
    {
      assert(GetLastError() == NTE_BAD_KEYSET);
    }
    cert_handle->key_container_name[0] = 0;
  }
  cert_handle->flags= 0;
}
