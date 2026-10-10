/************************************************************************************
 * Copyright (C) 2015 - 2018 MariaDB Corporation AB
 * Copyright (c) 2003 Simtec Electronics
 *
 * Re-implemented by Vincent Sanders <vince@kyllikki.org> with extensive
 * reference to original curl example code
 *
 * Rewritten for MariaDB Connector/C by Georg Richter <georg@mariadb.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *************************************************************************************/

/*
  This is a plugin for remote file access via libcurl.

  The following URL types are supported:

  http://
  https://
  ftp://
  sftp://
  ldap://
  smb://
*/

#include <ma_global.h>
#include <ma_sys.h>
#include <mysql.h>
#include <mysql/client_plugin.h>
#include <string.h>
#include <memory.h>

#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <sys/time.h>
#else
#pragma comment(lib, "Ws2_32.lib")
#endif
#include <stdlib.h>
#include <errno.h>
#include <mariadb/ma_io.h>
 
/* Internal file structure */

MA_FILE *ma_rio_open(const char *url,const char *operation);
int ma_rio_close(MA_FILE *file);
int ma_rio_feof(MA_FILE *file);
size_t ma_rio_read(void *ptr, size_t size, size_t nmemb, MA_FILE *file);
char * ma_rio_gets(char *ptr, size_t size, MA_FILE *file);

int ma_rio_init(char *, size_t, int, va_list);
int ma_rio_deinit(void);

struct st_rio_methods ma_rio_methods= {
  ma_rio_open,
  ma_rio_close,
  ma_rio_feof,
  ma_rio_read,
  ma_rio_gets
};

typedef struct
{
  CURL *curl;
  size_t length,
         offset;
  uchar *buffer;
  int in_progress;
} MA_REMOTE_FILE;
 
CURLM *multi_handle= NULL;

#ifndef PLUGIN_DYNAMIC
MARIADB_REMOTEIO_PLUGIN remote_io_client_plugin=
#else
MARIADB_CLIENT_PLUGIN_EXPORT MARIADB_REMOTEIO_PLUGIN
    _mysql_client_plugin_declaration_=
#endif
{
  MARIADB_CLIENT_REMOTEIO_PLUGIN,
  MARIADB_CLIENT_REMOTEIO_PLUGIN_INTERFACE_VERSION,
  "remote_io",
  "Georg Richter",
  "Remote IO plugin",
  {0,1,0},
  "LGPL",
  NULL,
  ma_rio_init, 
  ma_rio_deinit,
  NULL,
  &ma_rio_methods
mysql_end_client_plugin;

/* {{{ ma_rio_init - Plugin initialization */
int ma_rio_init(char *unused1 __attribute__((unused)),
                size_t unused2 __attribute__((unused)),
                int unused3 __attribute__((unused)),
                va_list unused4 __attribute__((unused)))
{
  curl_global_init(CURL_GLOBAL_ALL);
  if (!multi_handle)
    multi_handle = curl_multi_init();
  return 0;
}
/* }}} */

/* {{{ ma_rio_deinit - Plugin deinitialization */
int ma_rio_deinit(void)
{
  if (multi_handle)
  {
    curl_multi_cleanup(multi_handle);
    multi_handle= NULL;
  }
  curl_global_cleanup();
  return 0;
}
/* }}} */

/* {{{ curl_write_callback */
static size_t rio_write_callback(char *buffer,
                                 size_t size,
                                 size_t nitems,
                                 void *ptr)
{
  size_t free_bytes;
  char *tmp;

  MA_FILE *file= (MA_FILE *)ptr; 
  MA_REMOTE_FILE *curl_file = (MA_REMOTE_FILE *)file->ptr;
  size *= nitems;
 
  free_bytes= curl_file->length - curl_file->offset; 

  /* check if we need to allocate more memory */ 
  if (size > free_bytes) {
    tmp= (char *)realloc((gptr)curl_file->buffer, curl_file->length + (size - free_bytes));
    if (!tmp)
      size= free_bytes;
    else {
      curl_file->length+= size - free_bytes;
      curl_file->buffer= (unsigned char *)tmp;
    }
  }

  /* copy buffer into MA_FILE structure */
  memcpy((char *)curl_file->buffer + curl_file->offset, buffer, size);
  curl_file->offset+= size;
 
  return size;
}
/* }}} */
 
/* use to attempt to fill the read buffer up to requested number of bytes */ 
static int fill_buffer(MA_FILE *file, size_t want)
{
  fd_set fdread;
  fd_set fdwrite;
  fd_set fdexcep;
  struct timeval timeout;
  int rc;
  CURLMcode mc; /* curl_multi_fdset() return code */
  MA_REMOTE_FILE *rf= (MA_REMOTE_FILE *)file->ptr;
 
  /* only attempt to fill buffer if transactions still running and buffer
     doesn't exceed required size already */ 
  if (!rf->in_progress || (rf->offset > want))
    return 0;
 
  /* try to fill buffer */ 
  do {
    int maxfd = -1;
    long curl_timeo = -1;
 
    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcep);
 
    /* set a suitable timeout to fail on */ 
    timeout.tv_sec = 20; /* 20 seconds */ 
    timeout.tv_usec = 0;
 
    curl_multi_timeout(multi_handle, &curl_timeo);
    if(curl_timeo >= 0) {
      timeout.tv_sec = curl_timeo / 1000;
      if(timeout.tv_sec > 1)
        timeout.tv_sec = 1;
      else
        timeout.tv_usec = (curl_timeo % 1000) * 1000;
    }
 
    /* get file descriptors from the transfers */ 
    mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);
 
    if(mc != CURLM_OK)
    {
      /* todo: error handling */
      break;
    }
 
    /* On success the value of maxfd is guaranteed to be >= -1. We call
       select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
       no fds ready yet so we call select(0, ...) */ 
 
    if(maxfd == -1) {
      struct timeval wait = { 0, 100 * 1000 }; /* 100ms */ 
      rc = select(0, NULL, NULL, NULL, &wait);
    }
    else {
      rc = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);
    }
 
    switch(rc) {
    case -1:
      /* select error */ 
      break;
 
    case 0:
    default:
      /* timeout or readable/writable sockets */ 
      curl_multi_perform(multi_handle, &rf->in_progress);
      break;
    }
  } while(rf->in_progress && (rf->offset < want));
  return 1;
}
 
/* use to remove want bytes from the front of a files buffer */ 
static int use_buffer(MA_FILE *file,int want)
{
  MA_REMOTE_FILE *rf= (MA_REMOTE_FILE *)file->ptr;
  /* sort out buffer */ 
  if((rf->offset - want) <=0) {
    /* ditch buffer - write will recreate */ 
    if (rf->buffer)
      free(rf->buffer);
 
    rf->buffer=NULL;
    rf->offset=0;
    rf->length=0;
  }
  else {
    /* move rest down make it available for later */ 
    memmove(rf->buffer,
            &rf->buffer[want],
            (rf->offset - want));
 
    rf->offset -= want;
  }
  return 0;
}
 
MA_FILE *ma_rio_open(const char *url,const char *operation)
{
  /* this code could check for URLs or types in the 'url' and
     basically use the real fopen() for standard files */ 
 
  MA_FILE *file;
  MA_REMOTE_FILE *rf;
  (void)operation;
 
  if (!(file = (MA_FILE *)calloc(1, sizeof(MA_FILE))))
    return NULL;
 
  file->type= MA_FILE_REMOTE;
  if (!(file->ptr= rf= (MA_REMOTE_FILE *)calloc(1, sizeof(MA_REMOTE_FILE))))
  {
    free(file);
    return NULL; 
  }
  rf->curl = curl_easy_init();

  if (curl_easy_setopt(rf->curl, CURLOPT_URL, url) ||
      curl_easy_setopt(rf->curl, CURLOPT_WRITEDATA, file) ||
      curl_easy_setopt(rf->curl, CURLOPT_VERBOSE, 0L) ||
      curl_easy_setopt(rf->curl, CURLOPT_WRITEFUNCTION, rio_write_callback))
  {
    free(file);
    free(rf);
    return NULL;
  }

  curl_multi_add_handle(multi_handle, rf->curl);

  /* lets start the fetch */ 
  curl_multi_perform(multi_handle, &rf->in_progress);

  if((rf->offset == 0) && (!rf->in_progress)) {
    /* if in_progress is 0 now, we should return NULL */ 

    /* make sure the easy handle is not in the multi handle anymore */ 
    curl_multi_remove_handle(multi_handle, rf->curl);

    /* cleanup */ 
    curl_easy_cleanup(rf->curl);

    free(file);

    file = NULL;
  }
  return file;
}
 
int ma_rio_close(MA_FILE *file)
{
  int ret=0;/* default is good return */ 
  MA_REMOTE_FILE *rf= (MA_REMOTE_FILE *)file->ptr;

  switch(file->type) {
    case MA_FILE_REMOTE:
    curl_multi_remove_handle(multi_handle, rf->curl);
 
    /* cleanup */
    curl_easy_cleanup(rf->curl);
    break;
 
  default: /* unknown or supported type - oh dear */ 
    ret=EOF;
    errno=EBADF;
    break;
  }
 
  if(rf->buffer)
    free(rf->buffer);/* free any allocated buffer space */ 
 
  free(rf);
  free(file);
 
  return ret;
}
 
int ma_rio_feof(MA_FILE *file)
{
  int ret=0;
  MA_REMOTE_FILE *rf= (MA_REMOTE_FILE *)file->ptr;
 
  switch(file->type) {
  case MA_FILE_REMOTE:
    if((rf->offset == 0) && (!rf->in_progress))
      ret = 1;
    break;
 
  default: /* unknown or supported type - oh dear */ 
    ret=-1;
    errno=EBADF;
    break;
  }
  return ret;
}
 
size_t ma_rio_read(void *ptr, size_t size, size_t nmemb, MA_FILE *file)
{
  size_t want;
  MA_REMOTE_FILE *rf= (MA_REMOTE_FILE *)file->ptr;
 
  switch(file->type) {
  case MA_FILE_REMOTE:
    want = nmemb * size;

    fill_buffer(file,want);
 
    /* check if there's data in the buffer - if not fill_buffer()
     * either errored or EOF */ 
    if(!rf->offset)
      return 0;
 
    /* ensure only available data is considered */ 
    if(rf->offset < want)
      want = rf->offset;
 
    /* xfer data to caller */ 
    memcpy(ptr, rf->buffer, want);
 
    use_buffer(file,want);
 
    want = want / size;     /* number of items */ 
    break;
 
  default: /* unknown or supported type - oh dear */ 
    want=0;
    errno=EBADF;
    break;
 
  }
  return want;
}
 
char *ma_rio_gets(char *ptr, size_t size, MA_FILE *file)
{
  size_t want = size - 1;/* always need to leave room for zero termination */ 
  size_t loop;
 
  switch(file->type) {
  case MA_FILE_REMOTE:
  {
    MA_REMOTE_FILE *rf= (MA_REMOTE_FILE *)file->ptr;
    fill_buffer(file,want);
 
    /* check if there's data in the buffer - if not fill either errored or
     * EOF */ 
    if(!rf->offset)
      return NULL;
 
    /* ensure only available data is considered */ 
    if(rf->offset < want)
      want = rf->offset;
 
    /*buffer contains data */ 
    /* look for newline or eof */ 
    for(loop=0;loop < want;loop++) {
      if(rf->buffer[loop] == '\n') {
        want=loop+1;/* include newline */ 
        break;
      }
    }
 
    /* xfer data to caller */ 
    memcpy(ptr, rf->buffer, want);
    ptr[want]=0;/* always null terminate */ 
 
    use_buffer(file,want);
 
    break;
  }
 
  default: /* unknown or supported type - oh dear */ 
    ptr=NULL;
    errno=EBADF;
    break;
  }
 
  return ptr;/*success */ 
}
