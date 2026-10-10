/************************************************************************************
    Copyright (C) 2015 Georg Richter and MariaDB Corporation AB
   
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

/* MariaDB virtual IO plugin for Windows named pipe communication */

#ifdef _WIN32

#include <ma_global.h>
#include <ma_sys.h>
#include <errmsg.h>
#include <mysql.h>
#include <mysql/client_plugin.h>
#include <string.h>
#include <ma_string.h>

/* Function prototypes */
my_bool pvio_npipe_set_timeout(MARIADB_PVIO *pvio, enum enum_pvio_timeout type, int timeout);
int pvio_npipe_get_timeout(MARIADB_PVIO *pvio, enum enum_pvio_timeout type);
ssize_t pvio_npipe_read(MARIADB_PVIO *pvio, uchar *buffer, size_t length);
ssize_t pvio_npipe_write(MARIADB_PVIO *pvio, const uchar *buffer, size_t length);

my_bool pvio_npipe_connect(MARIADB_PVIO *pvio, MA_PVIO_CINFO *cinfo);
my_bool pvio_npipe_close(MARIADB_PVIO *pvio);
int pvio_npipe_fast_send(MARIADB_PVIO *pvio);
int pvio_npipe_keepalive(MARIADB_PVIO *pvio);
my_bool pvio_npipe_get_handle(MARIADB_PVIO *pvio, void *handle);
my_bool pvio_npipe_is_blocking(MARIADB_PVIO *pvio);
int pvio_npipe_shutdown(MARIADB_PVIO *pvio);
my_bool pvio_npipe_is_alive(MARIADB_PVIO *pvio);

struct st_ma_pvio_methods pvio_npipe_methods= {
  pvio_npipe_set_timeout,
  pvio_npipe_get_timeout,
  pvio_npipe_read,
  NULL,
  pvio_npipe_write,
  NULL,
  NULL,
  NULL,
  pvio_npipe_connect,
  pvio_npipe_close,
  pvio_npipe_fast_send,
  pvio_npipe_keepalive,
  pvio_npipe_get_handle,
  pvio_npipe_is_blocking,
  pvio_npipe_is_alive,
  NULL,
  pvio_npipe_shutdown
};

#ifndef PLUGIN_DYNAMIC
MARIADB_PVIO_PLUGIN pvio_npipe_client_plugin =
#else
MARIADB_CLIENT_PLUGIN_EXPORT MARIADB_PVIO_PLUGIN
    _mysql_client_plugin_declaration_=
#endif
{
  MARIADB_CLIENT_PVIO_PLUGIN,
  MARIADB_CLIENT_PVIO_PLUGIN_INTERFACE_VERSION,
  "pvio_npipe",
  "Georg Richter",
  "MariaDB virtual IO plugin for named pipe connection",
  {1, 0, 0},
  "LGPL",
  NULL,
  NULL,
  NULL,
  NULL,
  &pvio_npipe_methods
};

struct st_pvio_npipe {
  HANDLE pipe;
  OVERLAPPED overlapped;
  MYSQL *mysql;
};

my_bool pvio_npipe_set_timeout(MARIADB_PVIO *pvio, enum enum_pvio_timeout type, int timeout)
{
  int timeout_ms;

  if (!pvio)
    return 1;
  if (timeout > INT_MAX/1000)
    timeout_ms= -1;
  else if (timeout <=0)
    timeout_ms= -1;
  else
    timeout_ms = timeout*1000;

  pvio->timeout[type]= timeout_ms;
  return 0;
}

int pvio_npipe_get_timeout(MARIADB_PVIO *pvio, enum enum_pvio_timeout type)
{
  if (!pvio)
    return -1;
  return pvio->timeout[type] / 1000;
}

static BOOL complete_io(HANDLE file, OVERLAPPED *ov, BOOL ret, DWORD timeout, DWORD *size)
{
  if (ret)
    timeout = 0; /* IO completed successfully, do not WaitForSingleObject */
  else
  {
    assert(timeout);
    if (GetLastError() != ERROR_IO_PENDING)
      return FALSE;
  }

  if (timeout)
  {
    HANDLE wait_handle= ov->hEvent;
    assert(wait_handle && (wait_handle != INVALID_HANDLE_VALUE));

    DWORD wait_ret= WaitForSingleObject(wait_handle, timeout);
    switch (wait_ret)
    {
      case WAIT_OBJECT_0:
        break;
      case WAIT_TIMEOUT:
        CancelIoEx(file, ov);
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
      default:
        /* WAIT_ABANDONED or WAIT_FAILED unexpected. */
        assert(0);
        return FALSE;
    }
  }

  return GetOverlappedResult(file, ov, size, FALSE);
}

/*
  Disable posting IO completion event to the port.
  Handle can be bound to IOCP outside of the connector for other purposes
  (e.g polling functionality)
*/

static inline void disable_iocp_notification(HANDLE *h)
{
  *h= (HANDLE) ((ULONG_PTR) *h | 1);
}

static inline void enable_iocp_notification(HANDLE *h)
{
  *h= (HANDLE) ((ULONG_PTR) *h & ~1);
}

ssize_t pvio_npipe_read(MARIADB_PVIO *pvio, uchar *buffer, size_t length)
{
  BOOL ret;
  ssize_t r= -1;
  struct st_pvio_npipe *cpipe= NULL;
  DWORD size;
  HANDLE *h;

  if (!pvio || !pvio->data)
    return -1;

  cpipe= (struct st_pvio_npipe *)pvio->data;
  h= &cpipe->overlapped.hEvent;
  disable_iocp_notification(h);
  ret= ReadFile(cpipe->pipe, buffer, (DWORD)length, NULL, &cpipe->overlapped);
  enable_iocp_notification(h);
  ret= complete_io(cpipe->pipe, &cpipe->overlapped, ret, pvio->timeout[PVIO_READ_TIMEOUT], &size);
  r= ret? (ssize_t) size:-1;

  return r;
}

ssize_t pvio_npipe_write(MARIADB_PVIO *pvio, const uchar *buffer, size_t length)
{
  ssize_t r= -1;
  struct st_pvio_npipe *cpipe= NULL;
  BOOL ret;
  DWORD size;
  HANDLE *h;
  if (!pvio || !pvio->data)
    return -1;

  cpipe= (struct st_pvio_npipe *)pvio->data;
  h= &cpipe->overlapped.hEvent;
  disable_iocp_notification(h);
  ret= WriteFile(cpipe->pipe, buffer, (DWORD)length, NULL , &cpipe->overlapped);
  enable_iocp_notification(h);
  ret= complete_io(cpipe->pipe, &cpipe->overlapped, ret, pvio->timeout[PVIO_WRITE_TIMEOUT], &size);
  r= ret ? (ssize_t)size : -1;
  return r;
}


int pvio_npipe_keepalive(MARIADB_PVIO *pvio)
{
  /* keep alive is used for TCP/IP connections only */
  return 0;
}

int pvio_npipe_fast_send(MARIADB_PVIO *pvio)
{
  /* not supported */
  return 0;
}
my_bool pvio_npipe_connect(MARIADB_PVIO *pvio, MA_PVIO_CINFO *cinfo)
{
  struct st_pvio_npipe *cpipe= NULL;

  if (!pvio || !cinfo)
    return 1;

  /* if connect timeout is set, we will overwrite read/write timeout */
  if (pvio->timeout[PVIO_CONNECT_TIMEOUT])
  {
    pvio->timeout[PVIO_READ_TIMEOUT]= pvio->timeout[PVIO_WRITE_TIMEOUT]= pvio->timeout[PVIO_CONNECT_TIMEOUT];
  }

  if (!(cpipe= (struct st_pvio_npipe *)LocalAlloc(LMEM_ZEROINIT, sizeof(struct st_pvio_npipe))))
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_OUT_OF_MEMORY, "HY000", 0, "");
    return 1;
  }
  pvio->data= (void *)cpipe;
  cpipe->pipe= INVALID_HANDLE_VALUE;
  pvio->mysql= cinfo->mysql;
  pvio->type= cinfo->type;

  if (cinfo->type == PVIO_TYPE_NAMEDPIPE)
  {
    char szPipeName[MAX_PATH];
    ULONGLONG deadline;
    LONGLONG wait_ms;
    DWORD backoff= 0; /* Avoid busy wait if ERROR_PIPE_BUSY.*/
    if ( ! cinfo->unix_socket || (cinfo->unix_socket)[0] == 0x00)
      cinfo->unix_socket = MARIADB_NAMEDPIPE;
    if (!cinfo->host || !strcmp(cinfo->host,LOCAL_HOST))
      cinfo->host=LOCAL_HOST_NAMEDPIPE;

    szPipeName[MAX_PATH - 1]= 0;
    snprintf(szPipeName, MAX_PATH - 1, "\\\\%s\\pipe\\%s", cinfo->host, cinfo->unix_socket);

    if (pvio->timeout[PVIO_CONNECT_TIMEOUT] > 0)
      deadline = GetTickCount64() + pvio->timeout[PVIO_CONNECT_TIMEOUT];
    else
      deadline = INFINITE;

    while (1)
    {
      if ((cpipe->pipe = CreateFile(szPipeName,
                                    GENERIC_READ |
                                    GENERIC_WRITE,
                                    0,               /* no sharing */
                                    NULL,            /* default security attributes */
                                    OPEN_EXISTING,
                                    FILE_FLAG_OVERLAPPED,
                                    NULL)) != INVALID_HANDLE_VALUE)
        break;

      if (GetLastError() != ERROR_PIPE_BUSY)
      {
        pvio->set_error(pvio->mysql, CR_NAMEDPIPEOPEN_ERROR, "HY000", 0,
                       cinfo->host, cinfo->unix_socket, GetLastError());
        goto end;
      }

      Sleep(backoff);
      if (!backoff)
        backoff = 1;

      wait_ms = deadline - GetTickCount64();
      if (wait_ms > INFINITE)
        wait_ms = INFINITE;

      if ((wait_ms <= 0) || !WaitNamedPipe(szPipeName, (DWORD)wait_ms))
      {
        pvio->set_error(pvio->mysql, CR_NAMEDPIPEWAIT_ERROR, "HY000", 0,
                       cinfo->host, cinfo->unix_socket, ERROR_TIMEOUT);
        goto end;
      }
    }


    if (!(cpipe->overlapped.hEvent= CreateEvent(NULL, FALSE, FALSE, NULL)))
    {
      pvio->set_error(pvio->mysql, CR_EVENT_CREATE_FAILED, "HY000", 0,
                     GetLastError());
      goto end;
    }
    return 0;
  }
end:
  if (cpipe)
  {
    if (cpipe->pipe != INVALID_HANDLE_VALUE)
      CloseHandle(cpipe->pipe);
    LocalFree(cpipe);
    pvio->data= NULL;
  }
  return 1;
}

my_bool pvio_npipe_close(MARIADB_PVIO *pvio)
{
  struct st_pvio_npipe *cpipe= NULL;
  int r= 0;

  if (!pvio)
    return 1;

  if (pvio->data)
  {
    cpipe= (struct st_pvio_npipe *)pvio->data;
    CloseHandle(cpipe->overlapped.hEvent);
    if (cpipe->pipe != INVALID_HANDLE_VALUE)
    {
      CloseHandle(cpipe->pipe);
      cpipe->pipe= INVALID_HANDLE_VALUE;
    }
    LocalFree(pvio->data);
    pvio->data= NULL;
  }
  return r;
}

my_bool pvio_npipe_get_handle(MARIADB_PVIO *pvio, void *handle)
{
  if (pvio && pvio->data)
  {
    *(HANDLE *)handle= ((struct st_pvio_npipe *)pvio->data)->pipe;
    return 0;
  }
  return 1;
} 

my_bool pvio_npipe_is_blocking(MARIADB_PVIO *pvio)
{
  return 1;
}

int pvio_npipe_shutdown(MARIADB_PVIO *pvio)
{
  HANDLE h;
  if (pvio_npipe_get_handle(pvio, &h) == 0)
  {
    return(CancelIoEx(h, NULL) ? 0 : 1);
  }
  return 1;
}

my_bool pvio_npipe_is_alive(MARIADB_PVIO *pvio)
{
  HANDLE handle;
  if (!pvio || !pvio->data)
    return FALSE;

  handle= ((struct st_pvio_npipe *)pvio->data)->pipe;
  /* Copy data from named pipe without removing it */
  if (PeekNamedPipe(handle, NULL, 0, NULL, NULL, NULL))
    return TRUE;
  return test(GetLastError() != ERROR_BROKEN_PIPE);
}
#endif
