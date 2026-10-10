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

  Author: Georg Richter

 *************************************************************************************/
#include "ma_schannel.h"
#include "schannel_certs.h"
#include <assert.h>

#define SC_IO_BUFFER_SIZE 0x4000
#define MAX_SSL_ERR_LEN 100

#define SCHANNEL_PAYLOAD(A) ((A).cbMaximumMessage + (A).cbHeader + (A).cbTrailer)
void ma_schannel_set_win_error(MARIADB_PVIO *pvio, DWORD ErrorNo);




/* {{{ void ma_schannel_set_sec_error */
void ma_schannel_set_sec_error(MARIADB_PVIO* pvio, DWORD ErrorNo)
{
  MYSQL* mysql = pvio->mysql;
  if (ErrorNo != SEC_E_OK)
    mysql->net.extension->extended_errno = ErrorNo;
  if (ErrorNo == SEC_E_INTERNAL_ERROR && GetLastError())
  {
    ma_schannel_set_win_error(pvio, GetLastError());
    return;
  }
  ma_schannel_set_win_error(pvio, ErrorNo);
}
/* }}} */

#include "win32_errmsg.h"
/* {{{ void ma_schnnel_set_win_error */
void ma_schannel_set_win_error(MARIADB_PVIO *pvio, DWORD ErrorNo)
{
  char buffer[256];
  ma_format_win32_error(buffer, sizeof(buffer), ErrorNo, "TLS/SSL error: ");
  pvio->set_error(pvio->mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, buffer);
  return;
}
/* }}} */


/* }}} */

/* {{{ SECURITY_STATUS ma_schannel_handshake_loop(MARIADB_PVIO *pvio, my_bool InitialRead, SecBuffer *pExtraData) */
/*
  perform handshake loop

  SYNOPSIS
    ma_schannel_handshake_loop()
    pvio            Pointer to an Communication/IO structure
    InitialRead    TRUE if it's the very first read
    ExtraData      Pointer to an SecBuffer which contains extra data (sent by application)

    
*/

SECURITY_STATUS ma_schannel_handshake_loop(MARIADB_PVIO *pvio, my_bool InitialRead, SecBuffer *pExtraData)
{
  SecBufferDesc   OutBuffer, InBuffer;
  SecBuffer       InBuffers[2], OutBuffers;
  DWORD           dwSSPIFlags, dwSSPIOutFlags, cbData, cbIoBuffer;
  TimeStamp       tsExpiry;
  SECURITY_STATUS rc;
  PUCHAR          IoBuffer;
  BOOL            fDoRead;
  MARIADB_TLS     *ctls= pvio->ctls;
  MYSQL *mysql=   pvio->mysql;
  SC_CTX          *sctx= (SC_CTX *)ctls->ssl;
  char            *sni_host= NULL;


  dwSSPIFlags = ISC_REQ_SEQUENCE_DETECT |
                ISC_REQ_REPLAY_DETECT |
                ISC_REQ_CONFIDENTIALITY |
                ISC_RET_EXTENDED_ERROR |
                ISC_REQ_ALLOCATE_MEMORY |
                ISC_REQ_STREAM;

  if (mysql->host && !ma_is_ip_address(mysql->host))
    sni_host= mysql->host;

  /* Allocate data buffer */
  if (!(IoBuffer = malloc(SC_IO_BUFFER_SIZE)))
    return SEC_E_INSUFFICIENT_MEMORY;

  cbIoBuffer = 0;

  if (!InitialRead && pExtraData->cbBuffer)
  {
    memcpy(IoBuffer, pExtraData->pvBuffer,pExtraData->cbBuffer);
    cbIoBuffer= pExtraData->cbBuffer;
  }

  fDoRead = InitialRead;

  /* handshake loop: We will leave if handshake is finished
     or an error occurs */

  rc = SEC_I_CONTINUE_NEEDED;

  while (rc == SEC_I_CONTINUE_NEEDED ||
         rc == SEC_E_INCOMPLETE_MESSAGE ||
         rc == SEC_I_INCOMPLETE_CREDENTIALS )
  {
    /* Read data */
    if (rc == SEC_E_INCOMPLETE_MESSAGE ||
        !cbIoBuffer)
    {
      if(fDoRead)
      {
        ssize_t nbytes = pvio->methods->read(pvio, IoBuffer + cbIoBuffer, (size_t)(SC_IO_BUFFER_SIZE - cbIoBuffer));
        if (nbytes <= 0)
        {
          rc = SEC_E_INTERNAL_ERROR;
          break;
        }
        cbData = (DWORD)nbytes;
        cbIoBuffer += cbData;
      }
      else
        fDoRead = TRUE;
    }

    /* input buffers
       First buffer stores data received from server. leftover data
       will be stored in second buffer with BufferType SECBUFFER_EXTRA */

    InBuffers[0].pvBuffer   = IoBuffer;
    InBuffers[0].cbBuffer   = cbIoBuffer;
    InBuffers[0].BufferType = SECBUFFER_TOKEN;

    InBuffers[1].pvBuffer   = NULL;
    InBuffers[1].cbBuffer   = 0;
    InBuffers[1].BufferType = SECBUFFER_EMPTY;

    InBuffer.cBuffers       = 2;
    InBuffer.pBuffers       = InBuffers;
    InBuffer.ulVersion      = SECBUFFER_VERSION;


    /* output buffer */
    OutBuffers.pvBuffer  = NULL;
    OutBuffers.BufferType= SECBUFFER_TOKEN;
    OutBuffers.cbBuffer  = 0;

    OutBuffer.cBuffers      = 1;
    OutBuffer.pBuffers      = &OutBuffers;
    OutBuffer.ulVersion     = SECBUFFER_VERSION;


    rc = InitializeSecurityContextA(&sctx->CredHdl,
                                    &sctx->hCtxt,
                                    sni_host,
                                    dwSSPIFlags,
                                    0,
                                    SECURITY_NATIVE_DREP,
                                    &InBuffer,
                                    0,
                                    NULL,
                                    &OutBuffer,
                                    &dwSSPIOutFlags,
                                    &tsExpiry );


    if (rc == SEC_E_OK  ||
        rc == SEC_I_CONTINUE_NEEDED ||
        (FAILED(rc) && (dwSSPIOutFlags & ISC_RET_EXTENDED_ERROR)))
    {
      if(OutBuffers.cbBuffer && OutBuffers.pvBuffer)
      {
        ssize_t nbytes = pvio->methods->write(pvio, (uchar *)OutBuffers.pvBuffer, (size_t)OutBuffers.cbBuffer);
        if(nbytes <= 0)
        {
          FreeContextBuffer(OutBuffers.pvBuffer);
          DeleteSecurityContext(&sctx->hCtxt);
          return SEC_E_INTERNAL_ERROR;
        }
        cbData= (DWORD)nbytes;
        /* Free output context buffer */
        FreeContextBuffer(OutBuffers.pvBuffer);
        OutBuffers.pvBuffer = NULL;
      }
    }
    /* check if we need to read more data */
    switch (rc) {
    case SEC_E_INCOMPLETE_MESSAGE:
      /* we didn't receive all data, so just continue loop */
      continue;
      break;
    case SEC_E_OK:
      /* handshake completed, but we need to check if extra
         data was sent (which contains encrypted application data) */
      if (InBuffers[1].BufferType == SECBUFFER_EXTRA)
      {
        if (!(pExtraData->pvBuffer= LocalAlloc(0, InBuffers[1].cbBuffer)))
          return SEC_E_INSUFFICIENT_MEMORY;

        MoveMemory(pExtraData->pvBuffer, IoBuffer + (cbIoBuffer - InBuffers[1].cbBuffer), InBuffers[1].cbBuffer );
        pExtraData->BufferType = SECBUFFER_TOKEN;
        pExtraData->cbBuffer   = InBuffers[1].cbBuffer;
      }
      else
      {
        pExtraData->BufferType= SECBUFFER_EMPTY;
        pExtraData->pvBuffer= NULL;
        pExtraData->cbBuffer= 0;
      }
    break;

    case SEC_I_INCOMPLETE_CREDENTIALS:
      /* Provided credentials didn't contain a valid client certificate.
         We will try to connect anonymously, using current credentials */
      fDoRead= FALSE;
      rc= SEC_I_CONTINUE_NEEDED;
      continue;
      break;
    default:
      if (FAILED(rc))
      {
        goto loopend;
      }
      break;
    }

    if ( InBuffers[1].BufferType == SECBUFFER_EXTRA )
    {
      MoveMemory( IoBuffer, IoBuffer + (cbIoBuffer - InBuffers[1].cbBuffer), InBuffers[1].cbBuffer );
      cbIoBuffer = InBuffers[1].cbBuffer;
    }
    else
      cbIoBuffer = 0;
  }
loopend:
  if (FAILED(rc))
  {
    ma_schannel_set_sec_error(pvio, rc);
    DeleteSecurityContext(&sctx->hCtxt);
  }
  free(IoBuffer);

  return rc;
}
/* }}} */

/* {{{ SECURITY_STATUS ma_schannel_client_handshake(MARIADB_TLS *ctls) */
/*
   performs client side handshake 

   SYNOPSIS
     ma_schannel_client_handshake()
     ctls             Pointer to a MARIADB_TLS structure

   DESCRIPTION
     initiates a client/server handshake. This function can be used
     by clients only

   RETURN
     SEC_E_OK         on success
*/

SECURITY_STATUS ma_schannel_client_handshake(MARIADB_TLS *ctls)
{
  MARIADB_PVIO *pvio;
  SECURITY_STATUS sRet;
  DWORD OutFlags;
  SC_CTX *sctx;
  SecBuffer ExtraData;
  DWORD SFlags= ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                ISC_REQ_CONFIDENTIALITY | ISC_RET_EXTENDED_ERROR |
                ISC_REQ_USE_SUPPLIED_CREDS |
                ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

  SecBufferDesc	BufferOut;
  SecBuffer  BuffersOut;

  if (!ctls || !ctls->pvio)
    return 1;

  pvio= ctls->pvio;
  sctx= (SC_CTX *)ctls->ssl;

  /* Initialize security context */
  BuffersOut.BufferType= SECBUFFER_TOKEN;
  BuffersOut.cbBuffer= 0;
  BuffersOut.pvBuffer= NULL;


  BufferOut.cBuffers= 1;
  BufferOut.pBuffers= &BuffersOut;
  BufferOut.ulVersion= SECBUFFER_VERSION;

  sRet = InitializeSecurityContext(&sctx->CredHdl,
                                    NULL,
                                    pvio->mysql->host,
                                    SFlags,
                                    0,
                                    SECURITY_NATIVE_DREP,
                                    NULL,
                                    0,
                                    &sctx->hCtxt,
                                    &BufferOut,
                                    &OutFlags,
                                    NULL);

  if(sRet != SEC_I_CONTINUE_NEEDED)
  {
    ma_schannel_set_sec_error(pvio, sRet);
    return sRet;
  }

  /* send client hello */
  if(BuffersOut.cbBuffer != 0 && BuffersOut.pvBuffer != NULL)
  {
    ssize_t nbytes = (DWORD)pvio->methods->write(pvio, (uchar *)BuffersOut.pvBuffer, (size_t)BuffersOut.cbBuffer);

    if (nbytes <= 0)
    {
      sRet= SEC_E_INTERNAL_ERROR;
      goto end;
    }
  }
  sRet= ma_schannel_handshake_loop(pvio, TRUE, &ExtraData);

  /* allocate IO-Buffer for write operations: After handshake
  was successful, we are able now to calculate payload */
  if ((sRet = QueryContextAttributes(&sctx->hCtxt, SECPKG_ATTR_STREAM_SIZES, &sctx->Sizes )))
    goto end;

  sctx->IoBufferSize= SCHANNEL_PAYLOAD(sctx->Sizes);
  if (!(sctx->IoBuffer= (PUCHAR)LocalAlloc(0, sctx->IoBufferSize)))
  {
    sRet= SEC_E_INSUFFICIENT_MEMORY;
    goto end;
  }
    
  return sRet;
end:
  if (BuffersOut.pvBuffer)
    FreeContextBuffer(BuffersOut.pvBuffer);
  return sRet;
}
/* }}} */

/* {{{ SECURITY_STATUS ma_schannel_read_decrypt(MARIADB_PVIO *pvio, PCredHandle phCreds, CtxtHandle * phContext,
                                                DWORD DecryptLength, uchar *ReadBuffer, DWORD ReadBufferSize) */
/*
  Reads encrypted data from a SSL stream and decrypts it.

  SYNOPSIS
    ma_schannel_read
    pvio              pointer to Communication IO structure
    phContext        a context handle
    DecryptLength    size of decrypted buffer
    ReadBuffer       Buffer for decrypted data
    ReadBufferSize   size of ReadBuffer


  DESCRIPTION
    Reads decrypted data from a SSL stream and encrypts it.

  RETURN
    SEC_E_OK         on success
    SEC_E_*          if an error occurred
*/  

SECURITY_STATUS ma_schannel_read_decrypt(MARIADB_PVIO *pvio,
                                         CtxtHandle * phContext,
                                         DWORD *DecryptLength,
                                         uchar *ReadBuffer,
                                         DWORD ReadBufferSize)
{
  ssize_t nbytes = 0;
  DWORD dwOffset = 0;
  SC_CTX *sctx;
  SECURITY_STATUS sRet = 0;
  SecBufferDesc Msg;
  SecBuffer Buffers[4];
  int i;

  if (!pvio || !pvio->methods || !pvio->methods->read || !pvio->ctls || !DecryptLength)
    return SEC_E_INTERNAL_ERROR;

  sctx = (SC_CTX *)pvio->ctls->ssl;
  *DecryptLength = 0;

  if (sctx->dataBuf.cbBuffer)
  {
    /* Have unread decrypted data from the last time, copy. */
    nbytes = MIN(ReadBufferSize, sctx->dataBuf.cbBuffer);
    memcpy(ReadBuffer, sctx->dataBuf.pvBuffer, nbytes);
    sctx->dataBuf.pvBuffer = (char *)(sctx->dataBuf.pvBuffer) + nbytes;
    sctx->dataBuf.cbBuffer -= (DWORD)nbytes;
    *DecryptLength = (DWORD)nbytes;
    return SEC_E_OK;
  }


  while (1)
  {
    /* Check for any encrypted data returned by last DecryptMessage() in SECBUFFER_EXTRA buffer. */
    if (sctx->extraBuf.cbBuffer)
    {
      memmove(sctx->IoBuffer, sctx->extraBuf.pvBuffer, sctx->extraBuf.cbBuffer);
      dwOffset = sctx->extraBuf.cbBuffer;
      sctx->extraBuf.cbBuffer = 0;
    }

    do {
      assert(sctx->IoBufferSize > dwOffset);
      if (dwOffset == 0 || sRet == SEC_E_INCOMPLETE_MESSAGE)
      {
        nbytes = pvio->methods->read(pvio, sctx->IoBuffer + dwOffset, (size_t)(sctx->IoBufferSize - dwOffset));
        if (nbytes <= 0)
        {
          /* server closed connection, or an error */
          // todo: error 
          return SEC_E_INVALID_HANDLE;
        }
        dwOffset += (DWORD)nbytes;
      }
      ZeroMemory(Buffers, sizeof(SecBuffer) * 4);
      Buffers[0].pvBuffer = sctx->IoBuffer;
      Buffers[0].cbBuffer = dwOffset;

      Buffers[0].BufferType = SECBUFFER_DATA;
      Buffers[1].BufferType = SECBUFFER_EMPTY;
      Buffers[2].BufferType = SECBUFFER_EMPTY;
      Buffers[3].BufferType = SECBUFFER_EMPTY;

      Msg.ulVersion = SECBUFFER_VERSION;    // Version number
      Msg.cBuffers = 4;
      Msg.pBuffers = Buffers;

      sRet = DecryptMessage(phContext, &Msg, 0, NULL);

    } while (sRet == SEC_E_INCOMPLETE_MESSAGE); /* Continue reading until full message arrives */


    if (sRet != SEC_E_OK && sRet != SEC_I_RENEGOTIATE)
    {
      ma_schannel_set_sec_error(pvio, sRet);
      return sRet;
    }

    sctx->extraBuf.cbBuffer = 0;
    sctx->dataBuf.cbBuffer = 0;
    for (i = 0; i < 4; i++)
    {
      if (Buffers[i].BufferType == SECBUFFER_DATA)
        sctx->dataBuf = Buffers[i];
      if (Buffers[i].BufferType == SECBUFFER_EXTRA)
        sctx->extraBuf = Buffers[i];
    }


    if (sctx->dataBuf.cbBuffer || sRet == SEC_I_RENEGOTIATE)
    {
      assert(sctx->dataBuf.pvBuffer);
      /*
        Copy at most ReadBufferSize bytes to output.
        Store the rest (if any) to be processed next time.
      */
      nbytes = MIN(sctx->dataBuf.cbBuffer, ReadBufferSize);
      if (nbytes)
        memcpy((char *)ReadBuffer, sctx->dataBuf.pvBuffer, nbytes);
      sctx->dataBuf.cbBuffer -= (unsigned long)nbytes;
      sctx->dataBuf.pvBuffer = (char *)sctx->dataBuf.pvBuffer + nbytes;

      *DecryptLength = (DWORD)nbytes;
      return sRet;
    }
    // No data buffer, loop
  }
}
/* }}} */
#include "win32_errmsg.h"
unsigned int ma_schannel_verify_certs(MARIADB_TLS *ctls, unsigned int verify_flags)
{
  SECURITY_STATUS status;
  unsigned int verify_status = MARIADB_TLS_VERIFY_ERROR;
  MARIADB_PVIO *pvio= ctls->pvio;
  MYSQL *mysql= pvio->mysql;
  SC_CTX *sctx = (SC_CTX *)ctls->ssl;
  const char *ca_file= mysql->options.ssl_ca;
  const char* ca_path = mysql->options.ssl_capath;
  const char *crl_file= mysql->options.extension ? mysql->options.extension->ssl_crl : NULL;
  const char* crl_path = mysql->options.extension ? mysql->options.extension->ssl_crlpath : NULL;
  PCCERT_CONTEXT pServerCert= NULL;
  char errmsg[256];
  HCERTSTORE store= NULL;
  int ret= 0;

  if (!crl_file && !crl_path) // backward compatible behavior
    verify_flags &= ~MARIADB_TLS_VERIFY_REVOKED;

  if (!verify_flags)
    return 0;

  status = schannel_create_store(ca_file, ca_path, crl_file, crl_path, &store, errmsg, sizeof(errmsg));
  if(status)
    goto end;

  status = QueryContextAttributesA(&sctx->hCtxt, SECPKG_ATTR_REMOTE_CERT_CONTEXT, (PVOID)&pServerCert);
  if (status)
  {
    ma_format_win32_error(errmsg, sizeof(errmsg), GetLastError(),
      "QueryContextAttributes(SECPKG_ATTR_REMOTE_CERT_CONTEXT) failed.");
    goto end;
  }

  status = schannel_verify_server_certificate(
      pServerCert,
      store,
      mysql->host,
      verify_flags,
      errmsg, sizeof(errmsg));

  if (status)
    goto end;

  verify_status= MARIADB_TLS_VERIFY_OK;

  ret= 1;

end:
  if (!ret)
  {
    switch (status) {
      case CERT_E_UNTRUSTEDROOT:
        if ((verify_flags & MARIADB_TLS_VERIFY_TRUST))
        {
          mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_TRUST;
        }
        break;
      case CERT_E_EXPIRED:
        mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_PERIOD;
        break;
      case CRYPT_E_REVOKED:
        mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_REVOKED;
        break;
      case CERT_E_INVALID_NAME:
      case CERT_E_CN_NO_MATCH:
        mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_HOST;
        break;
      default:
        mysql->net.tls_verify_status|= MARIADB_TLS_VERIFY_UNKNOWN;
        break;
    }
    pvio->set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN, 0, errmsg);
  }
  if (pServerCert)
    CertFreeCertificateContext(pServerCert);
  if(store)
    schannel_free_store(store);
  return verify_status;
}


/* {{{ size_t ma_schannel_write_encrypt(MARIADB_PVIO *pvio, PCredHandle phCreds, CtxtHandle * phContext) */
/*
  Decrypts data and write to SSL stream
  SYNOPSIS
    ma_schannel_write_decrypt
    pvio              pointer to Communication IO structure
    phContext        a context handle
    DecryptLength    size of decrypted buffer
    ReadBuffer       Buffer for decrypted data
    ReadBufferSize   size of ReadBuffer

  DESCRIPTION
    Write encrypted data to SSL stream.

  RETURN
    SEC_E_OK         on success
    SEC_E_*          if an error occurred
*/ 
ssize_t ma_schannel_write_encrypt(MARIADB_PVIO *pvio,
                                 uchar *WriteBuffer,
                                 size_t WriteBufferSize)
{
  SECURITY_STATUS scRet;
  SecBufferDesc Message;
  SecBuffer Buffers[4];
  SC_CTX *sctx= (SC_CTX *)pvio->ctls->ssl;
  size_t payload;
  ssize_t nbytes;
  DWORD write_size;

  payload= MIN(WriteBufferSize, sctx->Sizes.cbMaximumMessage);

  memcpy(&sctx->IoBuffer[sctx->Sizes.cbHeader], WriteBuffer, payload);
  
  Buffers[0].pvBuffer     = sctx->IoBuffer;
  Buffers[0].cbBuffer     = sctx->Sizes.cbHeader;
  Buffers[0].BufferType   = SECBUFFER_STREAM_HEADER;    // Type of the buffer

  Buffers[1].pvBuffer     = &sctx->IoBuffer[sctx->Sizes.cbHeader];
  Buffers[1].cbBuffer     = (DWORD)payload;
  Buffers[1].BufferType   = SECBUFFER_DATA;

  Buffers[2].pvBuffer     = &sctx->IoBuffer[sctx->Sizes.cbHeader] + payload;
  Buffers[2].cbBuffer     = sctx->Sizes.cbTrailer;
  Buffers[2].BufferType   = SECBUFFER_STREAM_TRAILER;

  Buffers[3].pvBuffer     = SECBUFFER_EMPTY;
  Buffers[3].cbBuffer     = SECBUFFER_EMPTY;
  Buffers[3].BufferType   = SECBUFFER_EMPTY;

  Message.ulVersion       = SECBUFFER_VERSION;
  Message.cBuffers        = 4;
  Message.pBuffers        = Buffers;
  if ((scRet = EncryptMessage(&sctx->hCtxt, 0, &Message, 0))!= SEC_E_OK)
    return -1;
  write_size = Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer;
  nbytes = pvio->methods->write(pvio, sctx->IoBuffer, write_size);
  return nbytes == write_size ? payload : -1;
}
/* }}} */

extern char *ssl_protocol_version[5];

/* {{{ ma_tls_get_protocol_version(MARIADB_TLS *ctls) */
int ma_tls_get_protocol_version(MARIADB_TLS *ctls)
{
  SC_CTX *sctx;
  SecPkgContext_ConnectionInfo ConnectionInfo;
  if (!ctls->ssl)
    return 1;

  sctx= (SC_CTX *)ctls->ssl;

  if (QueryContextAttributes(&sctx->hCtxt, SECPKG_ATTR_CONNECTION_INFO, &ConnectionInfo) != SEC_E_OK)
    return -1;

  switch(ConnectionInfo.dwProtocol)
  {
  case SP_PROT_SSL3_CLIENT:
    return PROTOCOL_SSLV3;
  case SP_PROT_TLS1_CLIENT:
    return PROTOCOL_TLS_1_0;
  case SP_PROT_TLS1_1_CLIENT:
    return PROTOCOL_TLS_1_1;
  case SP_PROT_TLS1_2_CLIENT:
    return PROTOCOL_TLS_1_2;
  case SP_PROT_TLS1_3_CLIENT:
    return PROTOCOL_TLS_1_3;
  default:
    return -1;
  }
}
/* }}} */
