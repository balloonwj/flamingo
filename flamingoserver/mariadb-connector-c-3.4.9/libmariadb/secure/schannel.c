/************************************************************************************
  Copyright (C) 2014 MariaDB Corporation Ab

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

#define SCHANNEL_USE_BLACKLISTS
#include <windows.h>
#include <winternl.h>

#include "ma_schannel.h"
#include "schannel_certs.h"
#include <string.h>
#include <ma_crypt.h>
#include <wincrypt.h>
#include <bcrypt.h>

extern my_bool ma_tls_initialized;
char tls_library_version[] = "Schannel";

#define PROT_SSL3   SP_PROT_SSL3_CLIENT
#define PROT_TLS1_0 SP_PROT_TLS1_0_CLIENT
#define PROT_TLS1_1 SP_PROT_TLS1_1_CLIENT
#define PROT_TLS1_2 SP_PROT_TLS1_2_CLIENT
#define PROT_TLS1_3 SP_PROT_TLS1_3_CLIENT


static struct
{
  DWORD cipher_id;
  DWORD protocol;
  const char *iana_name;
  const char *openssl_name;
  ALG_ID algs[4]; /* exchange, encryption, hash, signature */
}
cipher_map[] =
{
  {
    0x0002,
    PROT_TLS1_0 |  PROT_TLS1_2 | PROT_SSL3,
    "TLS_RSA_WITH_NULL_SHA", "NULL-SHA",
    { CALG_RSA_KEYX, 0, CALG_SHA1, CALG_RSA_SIGN }
  },
  {
    0x0004,
    PROT_TLS1_0 |  PROT_TLS1_2 | PROT_SSL3,
    "TLS_RSA_WITH_RC4_128_MD5", "RC4-MD5",
    { CALG_RSA_KEYX, CALG_RC4, CALG_MD5, CALG_RSA_SIGN }
  },
  {
    0x0005,
    PROT_TLS1_0 |  PROT_TLS1_2 | PROT_SSL3,
    "TLS_RSA_WITH_RC4_128_SHA", "RC4-SHA",
    { CALG_RSA_KEYX, CALG_RC4, CALG_SHA1, CALG_RSA_SIGN }
  },
  {
    0x000A,
    PROT_SSL3,
    "TLS_RSA_WITH_3DES_EDE_CBC_SHA", "DES-CBC3-SHA",
    {CALG_RSA_KEYX, CALG_3DES, CALG_SHA1, CALG_DSS_SIGN}
  },
  {
    0x0013,
    PROT_TLS1_0 |  PROT_TLS1_2 | PROT_SSL3,
    "TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA", "EDH-DSS-DES-CBC3-SHA",
    { CALG_DH_EPHEM, CALG_3DES, CALG_SHA1, CALG_DSS_SIGN }
  },
  {
    0x002F,
    PROT_SSL3 | PROT_TLS1_0 | PROT_TLS1_2,
    "TLS_RSA_WITH_AES_128_CBC_SHA", "AES128-SHA",
    { CALG_RSA_KEYX, CALG_AES_128, CALG_SHA, CALG_RSA_SIGN}
  },
  {
    0x0032,
    PROT_TLS1_0 |  PROT_TLS1_2,
    "TLS_DHE_DSS_WITH_AES_128_CBC_SHA", "DHE-DSS-AES128-SHA",
    { CALG_DH_EPHEM, CALG_AES_128, CALG_SHA1, CALG_RSA_SIGN }
  },
  {
    0x0033,
    PROT_TLS1_0 |  PROT_TLS1_2,
    "TLS_DHE_RSA_WITH_AES_128_CBC_SHA", "DHE-RSA-AES128-SHA",
    { CALG_DH_EPHEM, CALG_AES_128, CALG_SHA1, CALG_RSA_SIGN }
  },
  {
    0x0035,
    PROT_TLS1_0 |  PROT_TLS1_2,
    "TLS_RSA_WITH_AES_256_CBC_SHA", "AES256-SHA",
    { CALG_RSA_KEYX, CALG_AES_256, CALG_SHA1, CALG_RSA_SIGN }
  },
  {
    0x0038,
    PROT_TLS1_0 |  PROT_TLS1_2,
    "TLS_DHE_DSS_WITH_AES_256_CBC_SHA", "DHE-DSS-AES256-SHA",
    { CALG_DH_EPHEM, CALG_AES_256, CALG_SHA1, CALG_DSS_SIGN }
  },
  {
    0x0039,
    PROT_TLS1_0 |  PROT_TLS1_2,
    "TLS_DHE_RSA_WITH_AES_256_CBC_SHA", "DHE-RSA-AES256-SHA",
    { CALG_DH_EPHEM, CALG_AES_256, CALG_SHA1, CALG_RSA_SIGN }
  },
  {
    0x003B,
    PROT_TLS1_2,
    "TLS_RSA_WITH_NULL_SHA256", "NULL-SHA256",
    { CALG_RSA_KEYX, 0, CALG_SHA_256, CALG_RSA_SIGN }
  },
  {
    0x003C,
    PROT_TLS1_2,
    "TLS_RSA_WITH_AES_128_CBC_SHA256", "AES128-SHA256",
    { CALG_RSA_KEYX, CALG_AES_128, CALG_SHA_256, CALG_RSA_SIGN }
  },
  {
    0x003D,
    PROT_TLS1_2,
    "TLS_RSA_WITH_AES_256_CBC_SHA256", "AES256-SHA256",
    { CALG_RSA_KEYX, CALG_AES_256, CALG_SHA_256, CALG_RSA_SIGN }
  },
  {
    0x0040,
    PROT_TLS1_2,
    "TLS_DHE_DSS_WITH_AES_128_CBC_SHA256", "DHE-DSS-AES128-SHA256",
    { CALG_DH_EPHEM, CALG_AES_128, CALG_SHA_256, CALG_DSS_SIGN }
  },
  {
    0x009C,
    PROT_TLS1_2,
    "TLS_RSA_WITH_AES_128_GCM_SHA256", "AES128-GCM-SHA256",
    { CALG_RSA_KEYX, CALG_AES_128, CALG_SHA_256, CALG_RSA_SIGN }
  },
  {
    0x009D,
    PROT_TLS1_2,
    "TLS_RSA_WITH_AES_256_GCM_SHA384", "AES256-GCM-SHA384",
    { CALG_RSA_KEYX, CALG_AES_256, CALG_SHA_384, CALG_RSA_SIGN }
  },
  {
    0x009E,
    PROT_TLS1_2,
    "TLS_DHE_RSA_WITH_AES_128_GCM_SHA256", "DHE-RSA-AES128-GCM-SHA256",
    { CALG_DH_EPHEM, CALG_AES_128, CALG_SHA_256, CALG_RSA_SIGN }
  },
  {
    0x009F,
    PROT_TLS1_2,
    "TLS_DHE_RSA_WITH_AES_256_GCM_SHA384", "DHE-RSA-AES256-GCM-SHA384",
    { CALG_DH_EPHEM, CALG_AES_256, CALG_SHA_384, CALG_RSA_SIGN }
  },
  {
    0xC027,
    PROT_TLS1_2,
    "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256", "ECDHE-RSA-AES128-SHA256",
    { CALG_ECDH, CALG_AES_128, CALG_SHA_256, CALG_RSA_SIGN }
  },
  {
    0xC028,
    PROT_TLS1_2,
    "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384", "ECDHE-RSA-AES256-SHA384",
    { CALG_ECDH, CALG_AES_256, CALG_SHA_384, CALG_RSA_SIGN }
  }
};

#define MAX_ALG_ID 50

extern void ma_schannel_set_sec_error(MARIADB_PVIO *pvio, DWORD ErrorNo);

/*
  Initializes SSL and allocate global
  context SSL_context

  SYNOPSIS
    ma_tls_start

  RETURN VALUES
    0  success
    1  error
*/
int ma_tls_start(char *errmsg, size_t errmsg_len)
{
  ma_tls_initialized = TRUE;
  return 0;
}

/*
   Release SSL and free resources
   Will be automatically executed by 
   mysql_server_end() function

   SYNOPSIS
     ma_tls_end()
       void

   RETURN VALUES
     void
*/
void ma_tls_end()
{
  return;
}

/* {{{ static int ma_tls_set_client_certs(MARIADB_TLS *ctls) */
static int ma_tls_set_client_certs(MARIADB_TLS *ctls, client_cert_handle *cert_handle)
{
  MYSQL *mysql= ctls->pvio->mysql;
  char *certfile= mysql->options.ssl_cert,
       *keyfile= mysql->options.ssl_key;
  MARIADB_PVIO *pvio= ctls->pvio;
  char errmsg[256];
  SECURITY_STATUS status;

  if (!certfile && keyfile)
    certfile= keyfile;
  if (!keyfile && certfile)
    keyfile= certfile;

  if (!certfile)
    return 0;

  status = schannel_create_cert_context(certfile, keyfile, cert_handle, errmsg, sizeof(errmsg));
  if (status)
  {
    pvio->set_error(pvio->mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, 0, errmsg);
    return 1;
  }

  return 0;
}
/* }}} */

/* {{{ void *ma_tls_init(MARIADB_TLS *ctls, MYSQL *mysql) */
void *ma_tls_init(MYSQL *mysql)
{
  SC_CTX *sctx = (SC_CTX *)LocalAlloc(LMEM_ZEROINIT, sizeof(SC_CTX));
  if (sctx)
  {
    SecInvalidateHandle(&sctx->CredHdl);
    SecInvalidateHandle(&sctx->hCtxt);
  }
  return sctx;
}
/* }}} */


/* 
  Maps between openssl suite names and schannel alg_ids.
  Every suite has 4 algorithms (for exchange, encryption, hash and signing).
  
  The input string is a set of suite names (openssl),  separated 
  by ':'
  
  The output is written into the array 'arr' of size 'arr_size'
  The function returns number of elements written to the 'arr'.
*/

static struct _tls_version {
  const char *tls_version;
  DWORD protocol;
} tls_version[]= {
    {"TLSv1.0", 0},
    {"TLSv1.1", PROT_TLS1_1},
    {"TLSv1.2", PROT_TLS1_2},
    {"TLSv1.3", PROT_TLS1_3},
    {"SSLv3",   PROT_SSL3}
};

/* The following list was produced with OpenSSL 1.1.1j
   by executing `openssl ciphers -V`.  */
static struct {
  DWORD dwCipherSuite;
  const char *openssl_name;
} openssl_ciphers[] = {
  {0x002F, "AES128-SHA"},
  {0x0033, "DHE-RSA-AES128-SHA"},
  {0x0035, "AES256-SHA"},
  {0x0039, "DHE-RSA-AES256-SHA"},
  {0x003C, "AES128-SHA256"},
  {0x003D, "AES256-SHA256"},
  {0x0067, "DHE-RSA-AES128-SHA256"},
  {0x006B, "DHE-RSA-AES256-SHA256"},
  {0x008C, "PSK-AES128-CBC-SHA"},
  {0x008D, "PSK-AES256-CBC-SHA"},
  {0x0090, "DHE-PSK-AES128-CBC-SHA"},
  {0x0091, "DHE-PSK-AES256-CBC-SHA"},
  {0x0094, "RSA-PSK-AES128-CBC-SHA"},
  {0x0095, "RSA-PSK-AES256-CBC-SHA"},
  {0x009C, "AES128-GCM-SHA256"},
  {0x009D, "AES256-GCM-SHA384"},
  {0x009E, "DHE-RSA-AES128-GCM-SHA256"},
  {0x009F, "DHE-RSA-AES256-GCM-SHA384"},
  {0x00A8, "PSK-AES128-GCM-SHA256"},
  {0x00A9, "PSK-AES256-GCM-SHA384"},
  {0x00AA, "DHE-PSK-AES128-GCM-SHA256"},
  {0x00AB, "DHE-PSK-AES256-GCM-SHA384"},
  {0x00AC, "RSA-PSK-AES128-GCM-SHA256"},
  {0x00AD, "RSA-PSK-AES256-GCM-SHA384"},
  {0x00AE, "PSK-AES128-CBC-SHA256"},
  {0x00AF, "PSK-AES256-CBC-SHA384"},
  {0x00B2, "DHE-PSK-AES128-CBC-SHA256"},
  {0x00B3, "DHE-PSK-AES256-CBC-SHA384"},
  {0x00B6, "RSA-PSK-AES128-CBC-SHA256"},
  {0x00B7, "RSA-PSK-AES256-CBC-SHA384"},
  {0x1301, "TLS_AES_128_GCM_SHA256"},
  {0x1302, "TLS_AES_256_GCM_SHA384"},
  {0x1303, "TLS_CHACHA20_POLY1305_SHA256"},
  {0xC009, "ECDHE-ECDSA-AES128-SHA"},
  {0xC00A, "ECDHE-ECDSA-AES256-SHA"},
  {0xC013, "ECDHE-RSA-AES128-SHA"},
  {0xC014, "ECDHE-RSA-AES256-SHA"},
  {0xC01D, "SRP-AES-128-CBC-SHA"},
  {0xC01E, "SRP-RSA-AES-128-CBC-SHA"},
  {0xC020, "SRP-AES-256-CBC-SHA"},
  {0xC021, "SRP-RSA-AES-256-CBC-SHA"},
  {0xC023, "ECDHE-ECDSA-AES128-SHA256"},
  {0xC024, "ECDHE-ECDSA-AES256-SHA384"},
  {0xC027, "ECDHE-RSA-AES128-SHA256"},
  {0xC028, "ECDHE-RSA-AES256-SHA384"},
  {0xC02B, "ECDHE-ECDSA-AES128-GCM-SHA256"},
  {0xC02C, "ECDHE-ECDSA-AES256-GCM-SHA384"},
  {0xC02F, "ECDHE-RSA-AES128-GCM-SHA256"},
  {0xC030, "ECDHE-RSA-AES256-GCM-SHA384"},
  {0xC035, "ECDHE-PSK-AES128-CBC-SHA"},
  {0xC036, "ECDHE-PSK-AES256-CBC-SHA"},
  {0xC037, "ECDHE-PSK-AES128-CBC-SHA256"},
  {0xC038, "ECDHE-PSK-AES256-CBC-SHA384"},
  {0xCCA8, "ECDHE-RSA-CHACHA20-POLY1305"},
  {0xCCA9, "ECDHE-ECDSA-CHACHA20-POLY1305"},
  {0xCCAA, "DHE-RSA-CHACHA20-POLY1305"},
  {0xCCAB, "PSK-CHACHA20-POLY1305"},
  {0xCCAC, "ECDHE-PSK-CHACHA20-POLY1305"},
  {0xCCAD, "DHE-PSK-CHACHA20-POLY1305"},
  {0xCCAE, "RSA-PSK-CHACHA20-POLY1305"}
};

static size_t set_cipher(char * cipher_str, DWORD protocol, ALG_ID *arr , size_t arr_size)
{
  char *token = strtok(cipher_str, ":");
  size_t pos = 0;

  while (token)
  {
    size_t i;

    for(i = 0; i < sizeof(cipher_map)/sizeof(cipher_map[0]) ; i++)
    {
      if((pos + 4 < arr_size && strcmp(cipher_map[i].openssl_name, token) == 0) ||
        (cipher_map[i].protocol <= protocol))
      {
        memcpy(arr + pos, cipher_map[i].algs, sizeof(ALG_ID)* 4);
        pos += 4;
        break;
      }
    }
    token = strtok(NULL, ":");
  }
  return pos;
}

static LONG ma_RtlGetVersion(RTL_OSVERSIONINFOEXW *osvi)
{
  typedef LONG (WINAPI * func_RtlGetVersion)(RTL_OSVERSIONINFOEXW *);
  static func_RtlGetVersion pRtlGetVersion;
  if (!pRtlGetVersion)
      pRtlGetVersion = (func_RtlGetVersion) (void*) GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                          "RtlGetVersion");
  if (pRtlGetVersion)
  {
    return pRtlGetVersion(osvi);
  }
  return STATUS_ENTRYPOINT_NOT_FOUND;
}

/** Check if OS is modern enough to support SCH_CREDENTIALS struct */
static BOOL os_version_greater_equal(DWORD major, DWORD minor, DWORD build)
{
  RTL_OSVERSIONINFOEXW osvi;
  osvi.dwOSVersionInfoSize = sizeof(osvi);
  if (!ma_RtlGetVersion(&osvi))
  {
    return (osvi.dwMajorVersion > major)
      || (osvi.dwMajorVersion == major && osvi.dwMinorVersion > minor)
      || (osvi.dwMajorVersion == major && osvi.dwMinorVersion == minor && osvi.dwBuildNumber >= build);
  }
  return FALSE;
}

typedef struct _MA_SCHANNEL_CREDENTIALS {
  BOOL use_old_cred_structure;
  SCHANNEL_CRED schannel_cred;
  SCH_CREDENTIALS sch_credentials;
  ALG_ID AlgId[MAX_ALG_ID];
  TLS_PARAMETERS tls_parameters;
} MA_SCHANNEL_CREDENTIALS;

/**
  Initialize authentication data for the client side, before passing
  it to AcquireCredentialsHandle()

  Take care of specific TLS versions and cipher suites.

  This function chooses between the legacy and new credential structures
  (SCHANNEL_CRED rsp SCH_CREDENTIALS) based on the OS version and the
  requested cipher suite.

  The new SCH_CREDENTIALS structure is not used, if a cipher suite is
  requested, specific TLS protocol prior to TLSv1.3 is requested, or before
  Windows 11 / Windows Server 2022.

  @param ma_cred  Pointer to the MA_SCHANNEL_CREDENTIALS structure
  @param ssl_cipher  The requested cipher suite
  @param tls_ver     The requested TLS version(s), comma separated

  @return SEC_E_OK on success,
      SEC_E_ALGORITHM_MISMATCH, if the requested  cipher suite is not known,
      SEC_E_UNSUPPORTED_FUNCTION,. TLSv1.3 is requested on an older Windows
*/
static SECURITY_STATUS init_auth_data(MA_SCHANNEL_CREDENTIALS *ma_cred,
                                      char *ssl_cipher,
                                      const char *tls_ver)
{
  ma_cred->use_old_cred_structure = FALSE;
  SCHANNEL_CRED *schannel_cred= &ma_cred->schannel_cred;
  SCH_CREDENTIALS *sch_credentials= &ma_cred->sch_credentials;
  DWORD protocol= 0;
  BOOL ssl_cipher_is_protocol= FALSE;

  if (ssl_cipher || tls_ver)
  {
    for (int i= 0; i < sizeof(tls_version) / sizeof(tls_version[0]); i++)
    {
      const char *v= tls_version[i].tls_version;
      if (ssl_cipher && (_stricmp(ssl_cipher, v) == 0))
      {
        ssl_cipher_is_protocol= TRUE;
        protocol|= tls_version[i].protocol;
      }
      else if (tls_ver && strstr(tls_ver, v))
      {
        protocol|= tls_version[i].protocol;
      }
    }
  }

  if (ssl_cipher && !ssl_cipher_is_protocol)
  {
    ma_cred->use_old_cred_structure= TRUE;
  }

  /* CONC-778: Windows 11 starts with build 22000, Windows Server 2022 with 20348 */
  if (!os_version_greater_equal(10, 0, 20348))
  {
    ma_cred->use_old_cred_structure= TRUE;
  }

  if(protocol == PROT_TLS1_3 && ma_cred->use_old_cred_structure)
  {
    return SEC_E_UNSUPPORTED_FUNCTION;
  }

  if ((protocol != 0) && (protocol != PROT_TLS1_3))
  {
    ma_cred->use_old_cred_structure= TRUE;
  }

  if (ma_cred->use_old_cred_structure)
  {
    memset(schannel_cred, 0, sizeof(*schannel_cred));
    schannel_cred->dwVersion= SCHANNEL_CRED_VERSION;
    /*
      check if a protocol was specified as a cipher:
      In this case don't allow cipher suites which belong to newer protocols
      Please note: There are no cipher suites for TLS1.1
     */
    if (ssl_cipher)
    {
      memset(ma_cred->AlgId, 0, sizeof(ma_cred->AlgId));
      schannel_cred->cSupportedAlgs=
          (DWORD) set_cipher(ssl_cipher, protocol, ma_cred->AlgId, MAX_ALG_ID);
      if (schannel_cred->cSupportedAlgs)
      {
        schannel_cred->palgSupportedAlgs= ma_cred->AlgId;
      }
      else if (!ssl_cipher_is_protocol)
      {
        /* Don't know those protocols. */
        return SEC_E_ALGORITHM_MISMATCH;
      }
    }
    schannel_cred->grbitEnabledProtocols= protocol & ~PROT_TLS1_3;
    schannel_cred->dwFlags= SCH_CRED_NO_SERVERNAME_CHECK | SCH_CRED_NO_DEFAULT_CREDS |
                  SCH_CRED_MANUAL_CRED_VALIDATION;
  }
  else
  {
    memset(sch_credentials, 0, sizeof(*sch_credentials));
    sch_credentials->dwVersion= SCH_CREDENTIALS_VERSION;
    sch_credentials->dwFlags= SCH_CRED_NO_SERVERNAME_CHECK | SCH_CRED_NO_DEFAULT_CREDS |
                              SCH_CRED_MANUAL_CRED_VALIDATION;
    if (protocol)
    {
      TLS_PARAMETERS *tls_parameters= &ma_cred->tls_parameters;
      memset(tls_parameters, 0, sizeof(*tls_parameters));

      tls_parameters->grbitDisabledProtocols= ~protocol;
      sch_credentials->pTlsParameters= tls_parameters;
      sch_credentials->cTlsParameters= 1;
    }
  }
  return SEC_E_OK;
}

static void set_auth_data_cert(MA_SCHANNEL_CREDENTIALS *ma_cred, PCCERT_CONTEXT *cert)
{
  if (!cert)
   return;
  if (ma_cred->use_old_cred_structure)
  {
   ma_cred->schannel_cred.cCreds= 1;
   ma_cred->schannel_cred.paCred= cert;
  }
  else
  {
    ma_cred->sch_credentials.cCreds= 1;
    ma_cred->sch_credentials.paCred= cert;
  }
}

my_bool ma_tls_connect(MARIADB_TLS *ctls)
{
  MYSQL *mysql;
  MA_SCHANNEL_CREDENTIALS ma_cred;
  void *auth_data;

  MARIADB_PVIO *pvio;
  my_bool rc= 1;
  SC_CTX *sctx;
  SECURITY_STATUS sRet;
  client_cert_handle cert_handle= {0};
  DWORD protocol = 0;
  const CERT_CONTEXT* cert_context = NULL;

  if (!ctls)
    return 1;

  pvio= ctls->pvio;
  sctx= (SC_CTX *)ctls->ssl;
  if (!pvio || !sctx)
    return 1;

  mysql= pvio->mysql;
  if (!mysql)
    return 1;

  sRet= init_auth_data(&ma_cred, mysql->options.ssl_cipher, 
      mysql->options.extension->tls_version);
  switch(sRet)
  {
    case SEC_E_UNSUPPORTED_FUNCTION:
     pvio->set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, 0,
       "TLS1.3 is not supported on Windows before Windows 11 or Windows Server 2022");
     goto end;
    case SEC_E_ALGORITHM_MISMATCH:
      pvio->set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, 0,
                     "Unknown cipher suite");
      goto end;
    case SEC_E_OK:
      break;
    default:
      assert(0);
      pvio->set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, 0,
                      "Unknown error before the handshake");
      goto end;
  }

  if (ma_tls_set_client_certs(ctls, &cert_handle))
    goto end;
  set_auth_data_cert(&ma_cred, cert_handle.cert?&cert_handle.cert:NULL);
  auth_data= ma_cred.use_old_cred_structure ? (void*) &ma_cred.schannel_cred :(void*) &ma_cred.sch_credentials;

  sRet= AcquireCredentialsHandle(NULL, UNISP_NAME, SECPKG_CRED_OUTBOUND,
                                 NULL, auth_data, NULL, NULL, &sctx->CredHdl, NULL);

  /* We do not need to keep certificates after this point */
  schannel_free_cert_context(&cert_handle);
  if (sRet)
  {
    ma_schannel_set_sec_error(pvio, sRet);
    goto end;
  }
  if (ma_schannel_client_handshake(ctls) != SEC_E_OK)
    goto end;

  rc = 0;

end:
  if (cert_handle.cert)
    schannel_free_cert_context(&cert_handle);
  return rc;
}

ssize_t ma_tls_read(MARIADB_TLS *ctls, const uchar* buffer, size_t length)
{
  SC_CTX *sctx= (SC_CTX *)ctls->ssl;
  MARIADB_PVIO *pvio= ctls->pvio;
  DWORD dlength= 0;
  SECURITY_STATUS status;
  SecBuffer tmp_extra_buf= {0};

retry:
  status= ma_schannel_read_decrypt(pvio, &sctx->hCtxt, &dlength,
                                  (uchar *) buffer, (DWORD) length);
  if (tmp_extra_buf.cbBuffer)
  {
    /*
      This memory was allocated in renegotiation processing
      below, free it.
    */
    LocalFree(tmp_extra_buf.pvBuffer);
    tmp_extra_buf.cbBuffer= 0;
  }
  switch (status) {
  case SEC_E_OK:
    return (ssize_t) dlength;
  case SEC_I_CONTEXT_EXPIRED:
    /* Other side shut down the connection. */
    return 0;
  case SEC_I_RENEGOTIATE:
    /* Rerun handshake steps */
    tmp_extra_buf= sctx->extraBuf;
    tmp_extra_buf.BufferType= SECBUFFER_TOKEN;
    sctx->extraBuf.cbBuffer= 0;
    sctx->extraBuf.pvBuffer= NULL;
    status= ma_schannel_handshake_loop(pvio, FALSE, &tmp_extra_buf);
    sctx->extraBuf= tmp_extra_buf;
    if (status != SEC_E_OK)
      return -1;
    /*
      If decrypt returned some decrypted bytes prior to
      renegotiation,  return them.
      Otherwise, retry the read-decrypt again
    */
    if (dlength)
      return dlength;

    goto retry;

  default:
    return -1;
  }
}

ssize_t ma_tls_write(MARIADB_TLS *ctls, const uchar* buffer, size_t length)
{ 
  MARIADB_PVIO *pvio= ctls->pvio;
  ssize_t rc, wlength= 0;
  ssize_t remain= length;

  while (remain > 0)
  {
    if ((rc= ma_schannel_write_encrypt(pvio, (uchar *)buffer + wlength, remain)) <= 0)
      return rc;
    wlength+= rc;
    remain-= rc;
  }
  return length;
}

/* {{{ my_bool ma_tls_close(MARIADB_PVIO *pvio) */
my_bool ma_tls_close(MARIADB_TLS *ctls)
{
  SC_CTX *sctx= (SC_CTX *)ctls->ssl; 
  
  if (sctx)
  {
    LocalFree(sctx->IoBuffer);

    if (SecIsValidHandle(&sctx->CredHdl))
      FreeCredentialHandle(&sctx->CredHdl);

    if (SecIsValidHandle(&sctx->hCtxt))
      DeleteSecurityContext(&sctx->hCtxt);
  }
  LocalFree(sctx);
  LocalFree(ctls->cert_info.issuer);
  LocalFree(ctls->cert_info.subject);
  return 0;
}
/* }}} */

int ma_tls_verify_server_cert(MARIADB_TLS *ctls, unsigned int verify_flags)
{
  MYSQL *mysql;
  if (!ctls || !ctls->ssl || !ctls->pvio || !ctls->pvio->mysql)
    return 1;

  mysql= ctls->pvio->mysql;
  return ma_schannel_verify_certs(ctls, verify_flags);
}

static const char *cipher_name(const SecPkgContext_CipherInfo *CipherInfo)
{
  size_t i;

  for(i = 0; i < sizeof(openssl_ciphers)/sizeof(openssl_ciphers[0]) ; i++)
  {
    if (CipherInfo->dwCipherSuite == openssl_ciphers[i].dwCipherSuite)
      return openssl_ciphers[i].openssl_name;
  }
  return "";
};

const char *ma_tls_get_cipher(MARIADB_TLS *ctls)
{
  SecPkgContext_CipherInfo CipherInfo = { SECPKGCONTEXT_CIPHERINFO_V1 };
  SECURITY_STATUS sRet;
  SC_CTX *sctx;

  if (!ctls || !ctls->ssl)
    return NULL;

  sctx= (SC_CTX *)ctls->ssl;
  sRet= QueryContextAttributesA(&sctx->hCtxt, SECPKG_ATTR_CIPHER_INFO, (PVOID)&CipherInfo);

  if (sRet != SEC_E_OK)
    return NULL;

  return cipher_name(&CipherInfo);
}

char *ma_cert_blob_to_str(PCERT_NAME_BLOB cnblob)
{
  DWORD type= CERT_X500_NAME_STR;
  DWORD size= CertNameToStrA(X509_ASN_ENCODING, cnblob, type, NULL, 0);
  char *str= NULL;
 
  if (!size)
    return NULL;

  str= (char *)LocalAlloc(LMEM_ZEROINIT,size);
  CertNameToStrA(X509_ASN_ENCODING, cnblob, type, str, size);
  return str;
}


static void ma_systime_to_tm(SYSTEMTIME sys_tm, struct tm *tm)
{
  memset(tm, 0, sizeof(struct tm));
  tm->tm_year= sys_tm.wYear - 1900;
  tm->tm_mon= sys_tm.wMonth - 1;
  tm->tm_mday= sys_tm.wDay;
  tm->tm_hour = sys_tm.wHour;
  tm->tm_min = sys_tm.wMinute;
}

unsigned int ma_tls_get_peer_cert_info(MARIADB_TLS *ctls, unsigned int hash_size)
{
  PCCERT_CONTEXT pCertCtx= NULL;
  SC_CTX *sctx;
  PCERT_INFO pci= NULL;
  SYSTEMTIME tm;
  char fp[129];
  unsigned int hash_alg;

  if (!ctls || !ctls->ssl || !ctls->pvio || !ctls->pvio->mysql)
    return 1;

  sctx= (SC_CTX *)ctls->ssl;

  switch (hash_size) {
    case 0:
    case 256:
      hash_alg= MA_HASH_SHA256;
      break;
    case 384:
      hash_alg= MA_HASH_SHA384;
      break;
    case 512:
      hash_alg= MA_HASH_SHA512;
      break;
    default:
      my_set_error(ctls->pvio->mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                   ER(CR_SSL_CONNECTION_ERROR),
                   "Cannot detect hash algorithm for fingerprint verification");
      return 1;
  }

  /* Did we already read peer cert information ? */
  if (!ctls->cert_info.version)
  {
    if (QueryContextAttributes(&sctx->hCtxt, SECPKG_ATTR_REMOTE_CERT_CONTEXT, (PVOID)&pCertCtx) != SEC_E_OK)
      return 1;

    pci= pCertCtx->pCertInfo;

    ctls->cert_info.version= pci->dwVersion + 1;
    ctls->cert_info.subject = ma_cert_blob_to_str(&pci->Subject);
    ctls->cert_info.issuer = ma_cert_blob_to_str(&pci->Issuer);

    FileTimeToSystemTime(&pci->NotBefore, &tm);
    ma_systime_to_tm(tm, &ctls->cert_info.not_before);
    FileTimeToSystemTime(&pci->NotAfter, &tm);
    ma_systime_to_tm(tm, &ctls->cert_info.not_after);
  }
  ma_tls_get_finger_print(ctls, hash_alg, fp, sizeof(fp));
  mysql_hex_string(ctls->cert_info.fingerprint, fp, (unsigned long)ma_hash_digest_size(hash_alg));

  return 0; 
}


unsigned int ma_tls_get_finger_print(MARIADB_TLS *ctls, uint hash_type, char *fp, unsigned int len)
{
  MA_HASH_CTX* hash_ctx;

  SC_CTX *sctx= (SC_CTX *)ctls->ssl;
  PCCERT_CONTEXT pRemoteCertContext = NULL;

  if (hash_type == MA_HASH_SHA224)
  {
    MYSQL *mysql = ctls->pvio->mysql;
    my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
      ER(CR_SSL_CONNECTION_ERROR),
      "SHA224 hash for fingerprint verification is not supported in Schannel");
    return 0;
  }

  if (QueryContextAttributes(&sctx->hCtxt, SECPKG_ATTR_REMOTE_CERT_CONTEXT, (PVOID)&pRemoteCertContext) != SEC_E_OK)
    return 0;

  hash_ctx = ma_hash_new(hash_type);
  if (!hash_ctx)
  {
    CertFreeCertificateContext(pRemoteCertContext);
    return 0;
  }
  ma_hash_input(hash_ctx, pRemoteCertContext->pbCertEncoded, pRemoteCertContext->cbCertEncoded);
  ma_hash_result(hash_ctx, (unsigned char *)fp);
  ma_hash_free(hash_ctx);

  CertFreeCertificateContext(pRemoteCertContext);
  return (uint)ma_hash_digest_size(hash_type);
}

void ma_tls_set_connection(MYSQL *mysql __attribute__((unused)))
{
  return;
}
