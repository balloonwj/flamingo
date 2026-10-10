/************************************************************************************
   Copyright (C) 2015 MariaDB Corporation AB
   
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
#ifndef _WIN32
#define _GNU_SOURCE 1
#endif

#include <ma_global.h>
#include <mysql.h>
#include <mysql/client_plugin.h>
#include <string.h>
#include <memory.h>

#ifndef WIN32
#include <dlfcn.h>
#endif

#define READ  0
#define WRITE 1

/* function prototypes */
static int trace_init(char *errormsg, 
                      size_t errormsg_size,
                      int unused      __attribute__((unused)), 
                      va_list unused1 __attribute__((unused)));
static int trace_deinit(void);

int (*register_callback)(my_bool register_callback, 
                         void (*callback_function)(int mode, MYSQL *mysql, const uchar *buffer, size_t length));
void trace_callback(int mode, MYSQL *mysql, const uchar *buffer, size_t length);

#ifndef HAVE_TRACE_EXAMPLE_PLUGIN_DYNAMIC
struct st_mysql_client_plugin trace_example_plugin=
#else
MARIADB_CLIENT_PLUGIN_EXPORT  struct st_mysql_client_plugin
    _mysql_client_plugin_declaration_=
#endif
{
  MARIADB_CLIENT_TRACE_PLUGIN,
  MARIADB_CLIENT_TRACE_PLUGIN_INTERFACE_VERSION,
  "trace_example",
  "Georg Richter",
  "Trace example plugin",
  {1,0,0},
  "LGPL",
  NULL,
  &trace_init,
  &trace_deinit,
  NULL
};

static const char *commands[]= {
  "COM_SLEEP",
  "COM_QUIT",
  "COM_INIT_DB",
  "COM_QUERY",
  "COM_FIELD_LIST",
  "COM_CREATE_DB",
  "COM_DROP_DB",
  "COM_REFRESH",
  "COM_SHUTDOWN",
  "COM_STATISTICS",
  "COM_PROCESS_INFO",
  "COM_CONNECT",
  "COM_PROCESS_KILL",
  "COM_DEBUG",
  "COM_PING",
  "COM_TIME",
  "COM_DELAYED_INSERT",
  "COM_CHANGE_USER",
  "COM_BINLOG_DUMP",
  "COM_TABLE_DUMP",
  "COM_CONNECT_OUT",
  "COM_REGISTER_SLAVE",
  "COM_STMT_PREPARE",
  "COM_STMT_EXECUTE",
  "COM_STMT_SEND_LONG_DATA",
  "COM_STMT_CLOSE",
  "COM_STMT_RESET",
  "COM_SET_OPTION",
  "COM_STMT_FETCH",
  "COM_DAEMON",
  "COM_END"
};

typedef struct {
  unsigned long thread_id;
  int last_command; /* COM_* values, -1 for handshake */
  unsigned int max_packet_size;
  unsigned int num_commands;
  size_t total_size[2];
  unsigned int client_flags;
  char *username;
  char *db;
  char *command;
  char *filename;
  unsigned long refid; /* stmt_id, thread_id for kill */
  uchar charset;
  void *next;
  int local_infile;
  unsigned long pkt_length;
} TRACE_INFO;

#define TRACE_STATUS(a) ((!a) ? "ok" : "error")

TRACE_INFO *trace_info= NULL;

static TRACE_INFO *get_trace_info(unsigned long thread_id)
{
  TRACE_INFO *info= trace_info;

  /* search connection */
  while (info)
  {
    if (info->thread_id == thread_id)
      return info;
    else
      info= (TRACE_INFO *)info->next;
  }

  if (!(info= (TRACE_INFO *)calloc(1, sizeof(TRACE_INFO))))
    return NULL;
  info->thread_id= thread_id;
  info->next= trace_info;
  trace_info= info;
  return info;
}

static void delete_trace_info(unsigned long thread_id)
{
  TRACE_INFO *last= NULL, *current;
  current= trace_info;

  while (current)
  {
    if (current->thread_id == thread_id)
    {
      printf("deleting thread %lu\n", thread_id);

      if (last)
        last->next= current->next;
      else
        trace_info= (TRACE_INFO *)current->next;
      if (current->command)
        free(current->command);
      if (current->db)
        free(current->db);
      if (current->username)
        free(current->username);
      if (current->filename)
        free(current->filename);
      free(current);
    }
    last= current;
    current= (TRACE_INFO *)current->next;
  }

}


/* {{{ static int trace_init */
/* 
  Initialization routine

  SYNOPSIS
    trace_init
      unused1
      unused2
      unused3
      unused4

  DESCRIPTION
    Init function registers a callback handler for PVIO interface.

  RETURN
    0           success
*/
static int trace_init(char *errormsg, 
                      size_t errormsg_size,
                      int unused1 __attribute__((unused)), 
                      va_list unused2 __attribute__((unused)))
{
  void *func;

#ifdef WIN32
  if (!(func= GetProcAddress(GetModuleHandle(NULL), "ma_pvio_register_callback")))
#else
  if (!(func= dlsym(RTLD_DEFAULT, "ma_pvio_register_callback")))
#endif
  {
    strncpy(errormsg, "Can't find ma_pvio_register_callback function", errormsg_size);
    return 1;
  }
  register_callback= func;
  register_callback(TRUE, trace_callback);

  return 0;
}
/* }}} */

static int trace_deinit(void)
{
  /* unregister plugin */
  while(trace_info)
  {
    printf("Warning: Connection for thread %lu not properly closed\n", trace_info->thread_id);
    trace_info= (TRACE_INFO *)trace_info->next;
  }
  register_callback(FALSE, trace_callback);
  return 0;
}

static void trace_set_command(TRACE_INFO *info, char *buffer, size_t size)
{
  if (info->command)
    free(info->command);

  info->command= calloc(1, size + 1);
  memcpy(info->command, buffer, size);
}

void dump_buffer(uchar *buffer, size_t len)
{
  uchar *p= buffer;
  while (p < buffer + len)
  {
    printf("%02x ", *p);
    p++;
  }
  printf("\n");
}

static void dump_simple(TRACE_INFO *info, my_bool is_error)
{
  printf("%8lu: %s %s\n", info->thread_id, commands[info->last_command], TRACE_STATUS(is_error));
}

static void dump_reference(TRACE_INFO *info, my_bool is_error)
{
  printf("%8lu: %s(%lu) %s\n", info->thread_id, commands[info->last_command], (long)info->refid, TRACE_STATUS(is_error));
}

static void dump_command(TRACE_INFO *info, my_bool is_error)
{
  size_t i;
  printf("%8lu: %s(",  info->thread_id, commands[info->last_command]);
  for (i= 0; info->command && i < strlen(info->command); i++)
    if (info->command[i] == '\n')
      printf("\\n");
    else if (info->command[i] == '\r')
      printf("\\r");
    else if (info->command[i] == '\t')
      printf("\\t");
    else
      printf("%c", info->command[i]);
  printf(") %s\n", TRACE_STATUS(is_error));
}

void trace_callback(int mode, MYSQL *mysql, const uchar *buffer, size_t length)
{
  unsigned long thread_id= mysql->thread_id;
  TRACE_INFO *info;

  /* check if package is server greeting package,
   * and set thread_id */
  if (!thread_id && mode == READ)
  {
    char *p= (char *)buffer;
    p+= 4; /* packet length */
    if ((uchar)*p != 0xFF) /* protocol version 0xFF indicates error */
    {
      p+= strlen(p + 1) + 2;
      thread_id= uint4korr(p);
    }
    info= get_trace_info(thread_id);
    info->last_command= -1;
  }
  else
  {
    char *p= (char *)buffer;
    info= get_trace_info(thread_id);

    if (info->last_command == -1)
    {
      if (mode == WRITE)
      {
        /* client authentication reply packet:
         * 
         *  ofs description        length
         *  ------------------------
         *  0   length             3
         *  3   packet_no          1
         *  4   client capab.      4
         *  8   max_packet_size    4
         *  12  character set      1
         *  13  reserved          23
         *  ------------------------
         *  36  username (zero terminated)
         *      len (1 byte) + password or
         */

        p+= 4;
        info->client_flags= uint4korr(p);
        p+= 4;
        info->max_packet_size= uint4korr(p);
        p+= 4;
        info->charset= *p;
        p+= 24;
        info->username= strdup(p);
        p+= strlen(p) + 1;
        if (*p) /* we are not interested in authentication data */
          p+= *p;
        p++;
        if (info->client_flags & CLIENT_CONNECT_WITH_DB)
          info->db= strdup(p);
      }
      else
      {
        p++;
        if ((uchar)*p == 0xFF)
          printf("%8lu: CONNECT_ERROR(%d)\n", info->thread_id, uint4korr(p+1));
        else
          printf("%8lu: CONNECT_SUCCESS(host=%s,user=%s,db=%s)\n", info->thread_id, 
                 mysql->host, info->username, info->db ? info->db : "'none'");
        info->last_command= COM_SLEEP;
      }
    }
    else {
      char *p= (char *)buffer;
      int len;

      if (mode == WRITE)
      {
        if (info->pkt_length > 0)
        {
          info->pkt_length-= length;
          return;
        }
        len= uint3korr(p);
        info->pkt_length= len + 4 - length;
        p+= 4;
        info->last_command= *p;
        p++;

        switch (info->last_command) {
        case COM_INIT_DB:
        case COM_DROP_DB:
        case COM_CREATE_DB:
        case COM_DEBUG:
        case COM_QUERY:
        case COM_STMT_PREPARE:
          trace_set_command(info, p, len - 1);
          break;
        case COM_PROCESS_KILL:
          info->refid= uint4korr(p);
          break;
        case COM_QUIT:
          printf("%8lu: COM_QUIT\n", info->thread_id);
          delete_trace_info(info->thread_id);
          break;
        case COM_PING:
          printf("%8lu: COM_PING\n", info->thread_id);
          break;
        case COM_STMT_EXECUTE:
        case COM_STMT_RESET:
        case COM_STMT_CLOSE:
          info->refid= uint4korr(p);
          break;
        case COM_CHANGE_USER:
          break;
        default:
          if (info->local_infile == 1)
          {
            printf("%8lu: SEND_LOCAL_INFILE(%s) ", info->thread_id, info->filename);
            if (len)
              printf("sent %d bytes\n", len);
            else
              printf("- error\n");
            info->local_infile= 2;
          }
          else
            printf("%8lu: UNKNOWN_COMMAND: %d\n", info->thread_id, info->last_command);
          break;
        }
      }
      else
      {
        my_bool is_error;

        len= uint3korr(p);
        p+= 4;

        is_error= (len == -1);

        switch(info->last_command) {
        case COM_STMT_EXECUTE:
        case COM_STMT_RESET:
        case COM_STMT_CLOSE:
        case COM_PROCESS_KILL:
          dump_reference(info, is_error);
          info->refid= 0;
          info->last_command= 0;
          break;
        case COM_QUIT:
          dump_simple(info, is_error);
          break;
        case COM_QUERY:
        case COM_INIT_DB:
        case COM_DROP_DB:
        case COM_CREATE_DB:
        case COM_DEBUG:
        case COM_CHANGE_USER:
          if (info->last_command == COM_QUERY && (uchar)*p == 251)
          {
            info->local_infile= 1;
            p++;
            info->filename= (char *)malloc(len);
            strncpy(info->filename, (char *)p, len);
            dump_command(info, is_error);
            break;
          }
          dump_command(info, is_error);
          if (info->local_infile != 1)
          {
            free(info->command);
            info->command= NULL;
          }
          break;
        case COM_STMT_PREPARE:
          printf("%8lu: COM_STMT_PREPARE(%s) ", info->thread_id, info->command);
          if (!*p)
          {
            unsigned long stmt_id= uint4korr(p+1);
            printf("-> stmt_id(%lu)\n", stmt_id);
          }
          else
            printf("error\n");
          break;
        }
      }
    }
  }
  info->total_size[mode]+= length;
}
