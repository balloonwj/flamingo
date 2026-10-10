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
/* MariaDB virtual IO plugin for Windows shared memory communication */

#ifdef _WIN32

#include <ma_global.h>
#include <ma_sys.h>
#include <errmsg.h>
#include <mysql.h>
#include <mysql/client_plugin.h>
#include <string.h>
#include <ma_string.h>

#define PVIO_SHM_BUFFER_SIZE (16000 + 4)

my_bool pvio_shm_set_timeout(MARIADB_PVIO *pvio, enum enum_pvio_timeout type, int timeout);
int pvio_shm_get_timeout(MARIADB_PVIO *pvio, enum enum_pvio_timeout type);
ssize_t pvio_shm_read(MARIADB_PVIO *pvio, uchar *buffer, size_t length);
ssize_t pvio_shm_write(MARIADB_PVIO *pvio, const uchar *buffer, size_t length);
int pvio_shm_wait_io_or_timeout(MARIADB_PVIO *pvio, my_bool is_read, int timeout);
int pvio_shm_blocking(MARIADB_PVIO *pvio, my_bool value, my_bool *old_value);
my_bool pvio_shm_connect(MARIADB_PVIO *pvio, MA_PVIO_CINFO *cinfo);
my_bool pvio_shm_close(MARIADB_PVIO *pvio);
int pvio_shm_shutdown(MARIADB_PVIO *pvio);
my_bool pvio_shm_is_alive(MARIADB_PVIO *pvio);
my_bool pvio_shm_get_handle(MARIADB_PVIO *pvio, void *handle);

struct st_ma_pvio_methods pvio_shm_methods= {
  pvio_shm_set_timeout,
  pvio_shm_get_timeout,
  pvio_shm_read,
  NULL,
  pvio_shm_write,
  NULL,
  pvio_shm_wait_io_or_timeout,
  pvio_shm_blocking,
  pvio_shm_connect,
  pvio_shm_close,
  NULL,
  NULL,
  pvio_shm_get_handle,
  NULL,
  pvio_shm_is_alive,
  NULL,
  pvio_shm_shutdown
};

#ifndef PLUGIN_DYNAMIC
MARIADB_PVIO_PLUGIN pvio_shmem_client_plugin=
#else
MARIADB_CLIENT_PLUGIN_EXPORT MARIADB_PVIO_PLUGIN
    _mysql_client_plugin_declaration_=
#endif
{
  MARIADB_CLIENT_PVIO_PLUGIN,
  MARIADB_CLIENT_PVIO_PLUGIN_INTERFACE_VERSION,
  "pvio_shmem",
  "Georg Richter",
  "MariaDB virtual IO plugin for Windows shared memory communication",
  {1, 0, 0},
  "LGPPL",
  NULL,
  NULL,
  NULL,
  NULL,
  &pvio_shm_methods,
 
};

enum enum_shm_events
{
  PVIO_SHM_SERVER_WROTE= 0,
  PVIO_SHM_SERVER_READ,
  PVIO_SHM_CLIENT_WROTE,
  PVIO_SHM_CLIENT_READ,
  PVIO_SHM_CONNECTION_CLOSED
};

typedef struct {
  HANDLE event[5];
  HANDLE file_map;
  LPVOID *map;
  char *read_pos;
  size_t buffer_size;
} PVIO_SHM;

const char *StrEvent[]= {"SERVER_WROTE", "SERVER_READ", "CLIENT_WROTE", "CLIENT_READ", "CONNECTION_CLOSED"};

struct st_pvio_shm {
  char *shm_name;
};

my_bool pvio_shm_set_timeout(MARIADB_PVIO *pvio, enum enum_pvio_timeout type, int timeout)
{
  if (!pvio)
    return 1;
  pvio->timeout[type]= (timeout > 0) ? timeout * 1000 : INFINITE;
  return 0;
}

int pvio_shm_get_timeout(MARIADB_PVIO *pvio, enum enum_pvio_timeout type)
{
  if (!pvio)
    return -1;
  return pvio->timeout[type] / 1000;
}

ssize_t pvio_shm_read(MARIADB_PVIO *pvio, uchar *buffer, size_t length)
{
  PVIO_SHM *pvio_shm= (PVIO_SHM *)pvio->data;
  size_t copy_size= length;
  HANDLE events[2];
  
  if (!pvio_shm)
    return -1;

  /* we need to wait for write and close events */
  if (!pvio_shm->buffer_size)
  {
    events[0]= pvio_shm->event[PVIO_SHM_CONNECTION_CLOSED];
    events[1]= pvio_shm->event[PVIO_SHM_SERVER_WROTE];

    switch(WaitForMultipleObjects(2, events, 0, pvio->timeout[PVIO_READ_TIMEOUT]))
    {
    case WAIT_OBJECT_0: /* server closed connection */
      SetLastError(ERROR_GRACEFUL_DISCONNECT);
      return -1;
    case WAIT_OBJECT_0 +1: /* server_wrote event */
      break;
    case WAIT_TIMEOUT:
      SetLastError(ETIMEDOUT);
    default:
      return -1;
    }
    /* server sent data */
    pvio_shm->read_pos= (char *)pvio_shm->map;
    pvio_shm->buffer_size= uint4korr(pvio_shm->read_pos);
    pvio_shm->read_pos+= 4;
  }

  if (pvio_shm->buffer_size < copy_size)
    copy_size= pvio_shm->buffer_size;
  
  if (copy_size)
  {
    memcpy(buffer, (uchar *)pvio_shm->read_pos, pvio_shm->buffer_size);
    pvio_shm->read_pos+= copy_size;
    pvio_shm->buffer_size-= copy_size;
  }

  /* we need to read again */
  if (!pvio_shm->buffer_size)
    if (!SetEvent(pvio_shm->event[PVIO_SHM_CLIENT_READ]))
      return -1;

  return (ssize_t)copy_size;
}

ssize_t pvio_shm_write(MARIADB_PVIO *pvio, const uchar *buffer, size_t length)
{
  HANDLE events[2];
  PVIO_SHM *pvio_shm= (PVIO_SHM *)pvio->data;
  size_t bytes_to_write= length;
  uchar *buffer_pos= (uchar *)buffer;
  
  if (!pvio_shm)
    return -1;

  events[0]= pvio_shm->event[PVIO_SHM_CONNECTION_CLOSED];
  events[1]= pvio_shm->event[PVIO_SHM_SERVER_READ];

  while (bytes_to_write)
  {
    size_t pkt_length;
    switch (WaitForMultipleObjects(2, events, 0, pvio->timeout[PVIO_WRITE_TIMEOUT])) {
    case WAIT_OBJECT_0: /* connection closed */
      SetLastError(ERROR_GRACEFUL_DISCONNECT);
      return -1;
    case WAIT_OBJECT_0 + 1: /* server_read */
      break;
    case WAIT_TIMEOUT:
      SetLastError(ETIMEDOUT);
    default:
      return -1;
    }
    pkt_length= MIN(PVIO_SHM_BUFFER_SIZE, length);
    int4store(pvio_shm->map, pkt_length);
    memcpy((uchar *)pvio_shm->map + 4, buffer_pos, length);
    buffer_pos+= length;
    bytes_to_write-= length;

    if (!SetEvent(pvio_shm->event[PVIO_SHM_CLIENT_WROTE]))
      return -1;
  }
  return (ssize_t)length;
}


int pvio_shm_wait_io_or_timeout(MARIADB_PVIO *pvio, my_bool is_read, int timeout)
{
  return 0;
}

int pvio_shm_blocking(MARIADB_PVIO *pvio, my_bool block, my_bool *previous_mode)
{
  /* not supported */
  return 0;
}

int pvio_shm_keepalive(MARIADB_PVIO *pvio)
{
  /* not supported */
  return 0;
}

int pvio_shm_fast_send(MARIADB_PVIO *pvio)
{
  /* not supported */
  return 0;
}

my_bool pvio_shm_connect(MARIADB_PVIO *pvio, MA_PVIO_CINFO *cinfo)
{
  const char *base_memory_name;
  char *prefixes[]= {"", "Global\\", NULL};
  char *shm_name, *shm_suffix, *shm_prefix;
  uchar i= 0;
  int len;
  int cid;
  DWORD dwDesiredAccess= EVENT_MODIFY_STATE | SYNCHRONIZE;
  HANDLE hdlConnectRequest= NULL,
         hdlConnectRequestAnswer= NULL,
         file_map= NULL;
  LPVOID map= NULL;
  PVIO_SHM *pvio_shm= (PVIO_SHM*)LocalAlloc(LMEM_ZEROINIT, sizeof(PVIO_SHM)); 

  if (!pvio_shm)
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_OUT_OF_MEMORY, "HY000", 0, "");
    return 0;
  }

  /* MariaDB server constructs the event name as follows:
     "Global\\base_memory_name" or
     "\\base_memory_name"
   */
 

  base_memory_name= (cinfo->host) ? cinfo->host : SHM_DEFAULT_NAME;

  if (!(shm_name= (char *)LocalAlloc(LMEM_ZEROINIT, strlen(base_memory_name) + 40)))
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_OUT_OF_MEMORY, "HY000", 0, "");
    goto error;
  }

  /* iterate through prefixes */
  while (prefixes[i])
  {
    len= sprintf(shm_name, "%s%s_", prefixes[i], base_memory_name);
    shm_suffix= shm_name + len;
    strcpy(shm_suffix, "CONNECT_REQUEST");
    if ((hdlConnectRequest= OpenEvent(dwDesiredAccess, 0, shm_name)))
    {
      /* save prefix to prevent further loop */
      shm_prefix= prefixes[i];
      break;
    }
    i++;
  }
  if (!hdlConnectRequest)
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Opening CONNECT_REQUEST event failed", GetLastError());
    goto error;
  }

  strcpy(shm_suffix, "CONNECT_ANSWER");
  if (!(hdlConnectRequestAnswer= OpenEvent(dwDesiredAccess, 0, shm_name)))
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Opening CONNECT_ANSWER event failed", GetLastError());
    goto error;
  }
  
  /* get connection id, so we can build the filename used for connection */
  strcpy(shm_suffix, "CONNECT_DATA");
  if (!(file_map= OpenFileMapping(FILE_MAP_WRITE, 0, shm_name)))
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "OpenFileMapping failed", GetLastError());
    goto error;
  }

  /* try to get first 4 bytes, which represents connection_id */
  if (!(map= MapViewOfFile(file_map, FILE_MAP_WRITE, 0, 0, sizeof(cid))))
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Reading connection_id failed", GetLastError());
    goto error;
  }

  /* notify server */
  if (!SetEvent(hdlConnectRequest))
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Failed sending connection request", GetLastError());
    goto error;
  }

  /* Wait for server answer */
  switch(WaitForSingleObject(hdlConnectRequestAnswer, pvio->timeout[PVIO_CONNECT_TIMEOUT])) {
  case WAIT_ABANDONED:
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Mutex was not released in time", GetLastError());
    goto error;
    break;
  case WAIT_FAILED:
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Operation wait failed", GetLastError());
    goto error;
    break;
  case WAIT_TIMEOUT:
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Operation timed out", GetLastError());
    goto error;
    break;
  case WAIT_OBJECT_0:
    break;
  default:
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Wait for server failed", GetLastError());
    break;
  }

  cid= uint4korr(map);

  len= sprintf(shm_name, "%s%s_%d_", shm_prefix, base_memory_name, cid);
  shm_suffix= shm_name + len;
  
  strcpy(shm_suffix, "DATA");
  pvio_shm->file_map= OpenFileMapping(FILE_MAP_WRITE, 0, shm_name);
  if (pvio_shm->file_map == NULL)
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "OpenFileMapping failed", GetLastError());
    goto error;
  }
  if (!(pvio_shm->map= MapViewOfFile(pvio_shm->file_map, FILE_MAP_WRITE, 0, 0, PVIO_SHM_BUFFER_SIZE)))
  {
    PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "MapViewOfFile failed", GetLastError());
    goto error;
  }

  for (i=0; i < 5; i++)
  {
    strcpy(shm_suffix, StrEvent[i]);
    if (!(pvio_shm->event[i]= OpenEvent(dwDesiredAccess, 0, shm_name)))
    {
      PVIO_SET_ERROR(cinfo->mysql, CR_SHARED_MEMORY_CONNECT_ERROR, "HY000", 0, "Couldn't create event", GetLastError());
      goto error;
    }
  }
  /* we will first read from server */
  SetEvent(pvio_shm->event[PVIO_SHM_SERVER_READ]);

error:
  if (hdlConnectRequest)
    CloseHandle(hdlConnectRequest);
  if (hdlConnectRequestAnswer)
    CloseHandle(hdlConnectRequestAnswer);
  if (shm_name)
    LocalFree(shm_name);
  if (map)
    UnmapViewOfFile(map);
  if (file_map)
    CloseHandle(file_map);
  if (pvio_shm)
  {
    /* check if all events are set */
    if (pvio_shm->event[4])
    {
      pvio->data= (void *)pvio_shm;
      pvio->mysql= cinfo->mysql;
      pvio->type= cinfo->type;
      pvio_shm->read_pos= (char *)pvio_shm->map;
      pvio->mysql->net.pvio= pvio;
      return 0;
    }
    for (i=0;i < 5; i++)
      if (pvio_shm->event[i])
        CloseHandle(pvio_shm->event[i]);
    if (pvio_shm->map)
      UnmapViewOfFile(pvio_shm->map);
    if (pvio_shm->file_map)
      CloseHandle(pvio_shm->file_map);
    LocalFree(pvio_shm);
  }
  return 1;

}

my_bool pvio_shm_close(MARIADB_PVIO *pvio)
{
  PVIO_SHM *pvio_shm= (PVIO_SHM *)pvio->data;
  int i;

  if (!pvio_shm)
    return 1;

  /* notify server */
  SetEvent(pvio_shm->event[PVIO_SHM_CONNECTION_CLOSED]);

  UnmapViewOfFile(pvio_shm->map);
  CloseHandle(pvio_shm->file_map);

  for (i=0; i < 5; i++)
    CloseHandle(pvio_shm->event[i]);

  LocalFree(pvio_shm);
  pvio->data= NULL;
  return 0;
}

my_bool pvio_shm_get_socket(MARIADB_PVIO *pvio, void *handle)
{
  return 1;
} 

my_bool pvio_shm_is_blocking(MARIADB_PVIO *pvio)
{
  return 1;
}

int pvio_shm_shutdown(MARIADB_PVIO *pvio)
{
  PVIO_SHM *pvio_shm= (PVIO_SHM *)pvio->data;
  if (pvio_shm)
    return (SetEvent(pvio_shm->event[PVIO_SHM_CONNECTION_CLOSED]) ? 0 : 1);
  return 1;
}

my_bool pvio_shm_is_alive(MARIADB_PVIO *pvio)
{
  PVIO_SHM *pvio_shm;
  if (!pvio || !pvio->data)
    return FALSE;
  pvio_shm= (PVIO_SHM *)pvio->data;
  return WaitForSingleObject(pvio_shm->event[PVIO_SHM_CONNECTION_CLOSED], 0)!=WAIT_OBJECT_0;
}

my_bool pvio_shm_get_handle(MARIADB_PVIO *pvio, void *handle)
{

  *(HANDLE **)handle= 0;
  if (!pvio || !pvio->data)
    return FALSE;
  *(HANDLE **)handle= ((PVIO_SHM*)pvio->data)->event;
  return TRUE;
}
#endif

