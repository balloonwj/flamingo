/************************************************************************************
    Copyright (C) 2000, 2012 MySQL AB & MySQL Finland AB & TCX DataKonsult AB,
                 Monty Program AB
                  2013, 2022 MariaDB Corporation AB

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

   Part of this code includes code from the PHP project which
   is freely available from http://www.php.net
*************************************************************************************/

#include <ma_global.h>

#include <ma_sys.h>
#include <ma_string.h>
#include <mariadb_ctype.h>
#include <ma_common.h>
#include "ma_priv.h"
#include "ma_context.h"
#include "mysql.h"
#include "mariadb_version.h"
#include "ma_server_error.h"
#include <mariadb/ma_io.h>
#include "errmsg.h"
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <mariadb_dyncol.h>
#include <mariadb_rpl.h>

#ifndef __has_feature
# define __has_feature(x) 0
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif
#if !defined(_WIN32)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef HAVE_SELECT_H
#  include <select.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#endif
#ifdef HAVE_SYS_UN_H
#  include <sys/un.h>
#endif
#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif
#ifndef _WIN32
#include <poll.h>
#endif
#include <ma_pvio.h>
#ifdef HAVE_TLS
#include <ma_tls.h>
#endif
#include <mysql/client_plugin.h>
#ifdef _WIN32
#include "shlwapi.h"
#include "ws2tcpip.h"
#define strncasecmp _strnicmp
#endif

#define ASYNC_CONTEXT_DEFAULT_STACK_SIZE (256*1024)
#define MA_RPL_VERSION_HACK "5.5.5-"

#define CHARSET_NAME_LEN 64

#undef max_allowed_packet
#undef net_buffer_length
extern ulong max_allowed_packet; /* net.c */
extern ulong net_buffer_length;  /* net.c */

static MYSQL_PARAMETERS mariadb_internal_parameters= {&max_allowed_packet, &net_buffer_length, 0};
static my_bool mysql_client_init=0;
static void mysql_close_options(MYSQL *mysql);
static void ma_clear_session_state(MYSQL *mysql);
static int parse_connection_string(MYSQL *mysql, const char *unused, const char *conn_str, ssize_t len);
extern void release_configuration_dirs();
extern char **get_default_configuration_dirs();
extern my_bool  ma_init_done;
extern my_bool  mysql_ps_subsystem_initialized;
extern my_bool mysql_handle_local_infile(MYSQL *mysql, const char *filename, my_bool can_local_infile);
extern const MARIADB_CHARSET_INFO * mysql_find_charset_nr(uint charsetnr);
extern const MARIADB_CHARSET_INFO * mysql_find_charset_name(const char * const name);
extern my_bool set_default_charset_by_name(const char *cs_name, myf flags __attribute__((unused)));
extern int run_plugin_auth(MYSQL *mysql, char *data, uint data_len,
                           const char *data_plugin, const char *db);
extern int net_add_multi_command(NET *net, uchar command, const uchar *packet,
                                 size_t length);
extern unsigned char* ma_stmt_execute_generate_request(MYSQL_STMT *stmt, size_t *request_len, my_bool internal);

extern LIST *pvio_callback;

/* prepare statement methods from my_stmt.c */
extern my_bool mthd_supported_buffer_type(enum enum_field_types type);
extern my_bool mthd_stmt_read_prepare_response(MYSQL_STMT *stmt);
extern my_bool mthd_stmt_get_param_metadata(MYSQL_STMT *stmt);
extern my_bool mthd_stmt_get_result_metadata(MYSQL_STMT *stmt);
extern int mthd_stmt_read_execute_response(MYSQL_STMT *stmt);
extern int mthd_stmt_fetch_row(MYSQL_STMT *stmt, unsigned char **row);
extern int mthd_stmt_fetch_to_bind(MYSQL_STMT *stmt, unsigned char *row);
extern int mthd_stmt_read_all_rows(MYSQL_STMT *stmt);
extern void mthd_stmt_flush_unbuffered(MYSQL_STMT *stmt);
extern my_bool _mariadb_read_options(MYSQL *mysql, const char *dir, const char *config_file, const char *group, unsigned int recursion);
extern unsigned char *mysql_net_store_length(unsigned char *packet, ulonglong length);

extern void
my_context_install_suspend_resume_hook(struct mysql_async_context *b,
                                       void (*hook)(my_bool, void *),
                                       void *user_data);

uint mysql_port=0;
my_string mysql_unix_port=0;

#define CONNECT_TIMEOUT 0

struct st_mariadb_methods MARIADB_DEFAULT_METHODS;

#if defined(_WIN32)
// socket_errno is defined in ma_global.h for all platforms
#define perror(A)
#else
#include <errno.h>
#define SOCKET_ERROR -1
#endif /* _WIN32 */

#include <mysql/client_plugin.h>

#define IS_CONNHDLR_ACTIVE(mysql)\
  ((mysql)->extension && (mysql)->extension->conn_hdlr)

static void end_server(MYSQL *mysql);
static void mysql_close_memory(MYSQL *mysql);
void read_user_name(char *name);
my_bool STDCALL mariadb_reconnect(MYSQL *mysql);
static int cli_report_progress(MYSQL *mysql, uchar *packet, uint length);

extern int mysql_client_plugin_init();
extern void mysql_client_plugin_deinit();

/* Helper function to detect possible buffer over- or underflow */
my_bool ma_check_buffer_boundaries(MYSQL *mysql, uchar *current_pos,
                                  ulong packet_size, size_t required)
{
  if ( (packet_size < (ulong)(current_pos - mysql->net.read_pos)) ||
       ((size_t)(packet_size - (current_pos - mysql->net.read_pos)) < required))
    return 1;
  return 0;
}

my_bool ma_is_ip_address(const char *s)
{
  struct in_addr  v4;
  struct in6_addr v6;

  if (inet_pton(AF_INET, s, &v4) == 1 ||
      inet_pton(AF_INET6, s, &v6) == 1)
    return 1;

  return 0;
}

/* net_get_error */
void net_get_error(char *buf, size_t buf_len,
       char *error, size_t error_len,
       unsigned int *error_no,
       char *sqlstate)
{
  char *p= buf;
  size_t error_msg_len= 0;

  if (buf_len > 2)
  {
    *error_no= uint2korr(p);
    p+= 2;

    /* since 4.1 sqlstate is following */
    if (*p == '#')
    {
      memcpy(sqlstate, ++p, SQLSTATE_LENGTH);
      p+= SQLSTATE_LENGTH;
    }
    error_msg_len= buf_len - (p - buf);
    error_msg_len= MIN(error_msg_len, error_len - 1);
    memcpy(error, p, error_msg_len);
  }
  else
  {
    *error_no= CR_UNKNOWN_ERROR;
    memcpy(sqlstate, SQLSTATE_UNKNOWN, SQLSTATE_LENGTH);
  }
}

/*****************************************************************************
** read a packet from server. Give error message if socket was down
** or packet is an error message
*****************************************************************************/

ulong
ma_net_safe_read(MYSQL *mysql)
{
  NET *net= &mysql->net;
  ulong len=0;

restart:
  if (net->pvio != 0)
    len=ma_net_read(net);

  if (len == packet_error || len == 0)
  {
    end_server(mysql);
#ifdef HAVE_TLS
    /* don't overwrite possible tls protocol errors */
    if (net->last_errno != CR_SSL_CONNECTION_ERROR)
#endif
    {
      my_set_error(mysql, net->last_errno == ER_NET_PACKET_TOO_LARGE ?
                   CR_NET_PACKET_TOO_LARGE:
                   CR_SERVER_LOST,
                   SQLSTATE_UNKNOWN, 0, errno);
    }
    return(packet_error);
  }
  if (net->read_pos[0] == 255)
  {
    if (len > 3)
    {
      char *pos=(char*) net->read_pos+1;
      uint last_errno=uint2korr(pos);
      pos+=2;
      len-=2;

      if (last_errno== 65535 &&
          ((mariadb_connection(mysql) && (mysql->server_capabilities & CLIENT_PROGRESS)) ||
           (!(mysql->extension->mariadb_server_capabilities & MARIADB_CLIENT_PROGRESS << 32))))
      {
        if (cli_report_progress(mysql, (uchar *)pos, (uint) (len-1)))
        {
          /* Wrong packet */
          my_set_error(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
          return (packet_error);
        }
        goto restart;
      }
      if (IS_MYSQL_ERROR(last_errno) || IS_MARIADB_ERROR(last_errno))
      {
        /* The server appears to have sent an error code within the
         * range(s) of error codes that should only be generated
         * client-side.
         */
        my_set_error(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
      }
      else
      {
        net->last_errno= last_errno;
        if (pos[0]== '#')
        {
          ma_strmake(net->sqlstate, pos+1, SQLSTATE_LENGTH);
          pos+= SQLSTATE_LENGTH + 1;
        }
        else
        {
          strncpy(net->sqlstate, SQLSTATE_UNKNOWN, SQLSTATE_LENGTH);
        }
        ma_strmake(net->last_error,(char*) pos,
                min(len,sizeof(net->last_error)-1));
      }
      /* MDEV-35935: if server sends error packet without error, we have to
         set error manually */
      if (!net->last_errno) {
        my_set_error(mysql, CR_ERR_MISSING_ERROR_INFO, SQLSTATE_UNKNOWN, 0);
      }
    }
    else
    {
      my_set_error(mysql, CR_UNKNOWN_ERROR, SQLSTATE_UNKNOWN, 0);
    }

    mysql->server_status&= ~SERVER_MORE_RESULTS_EXIST;

    return(packet_error);
  }
  return len;
}

/*
  Report progress to the client

  RETURN VALUES
    0  ok
    1  error
*/
static int cli_report_progress(MYSQL *mysql, uchar *packet, uint length)
{
  uint stage, max_stage, proc_length;
  double progress;
  uchar *start= packet;

  if (length < 5)
    return 1;                         /* Wrong packet */

  if (!(mysql->options.extension && mysql->options.extension->report_progress))
    return 0;                         /* No callback, ignore packet */

  packet++;                           /* Ignore number of strings */
  stage= (uint) *packet++;
  max_stage= (uint) *packet++;
  progress= uint3korr(packet)/1000.0;
  packet+= 3;
  proc_length= net_field_length(&packet);
  if (packet + proc_length > start + length)
    return 1;                         /* Wrong packet */
  (*mysql->options.extension->report_progress)(mysql, stage, max_stage,
                                               progress, (char*) packet,
                                               proc_length);
  return 0;
}

/* Get the length of next field. Change parameter to point at field start */
ulong
net_field_length(uchar **packet)
{
  reg1 uchar *pos= *packet;
  if (*pos < 251)
  {
    (*packet)++;
    return (ulong) *pos;
  }
  if (*pos == 251)
  {
    (*packet)++;
    return NULL_LENGTH;
  }
  if (*pos == 252)
  {
    (*packet)+=3;
    return (ulong) uint2korr(pos+1);
  }
  if (*pos == 253)
  {
    (*packet)+=4;
    return (ulong) uint3korr(pos+1);
  }
  (*packet)+=9;					/* Must be 254 when here */
  return (ulong) uint4korr(pos+1);
}

/* Same as above, but returns ulonglong values */

static unsigned long long
net_field_length_ll(uchar **packet)
{
  reg1 uchar *pos= *packet;
  if (*pos < 251)
  {
    (*packet)++;
    return (unsigned long long) *pos;
  }
  if (*pos == 251)
  {
    (*packet)++;
    return (unsigned long long) NULL_LENGTH;
  }
  if (*pos == 252)
  {
    (*packet)+=3;
    return (unsigned long long) uint2korr(pos+1);
  }
  if (*pos == 253)
  {
    (*packet)+=4;
    return (unsigned long long) uint3korr(pos+1);
  }
  (*packet)+=9;					/* Must be 254 when here */
#ifdef NO_CLIENT_LONGLONG
  return (unsigned long long) uint4korr(pos+1);
#else
  return (unsigned long long) uint8korr(pos+1);
#endif
}


void free_rows(MYSQL_DATA *cur)
{
  if (cur)
  {
    ma_free_root(&cur->alloc,MYF(0));
    free(cur);
  }
}

int
mthd_my_send_cmd(MYSQL *mysql,enum enum_server_command command, const char *arg,
	       size_t length, my_bool skip_check, void *opt_arg)
{
  NET *net= &mysql->net;
  int result= -1;

  /* CONC-589: If reconnect option was specified, we have to check if the connection
               (socket) is still available */
  if (command != COM_QUIT && mysql->options.reconnect && !ma_pvio_is_alive(mysql->net.pvio))
  {
    ma_pvio_close(mysql->net.pvio);
    mysql->net.pvio= NULL;
    mysql->net.error= 1;
  }

  if (mysql->net.pvio == 0)
  {
    /* Do reconnect if possible */
    if (mariadb_reconnect(mysql))
      return(1);
  }
  if (mysql->status != MYSQL_STATUS_READY ||
      mysql->server_status & SERVER_MORE_RESULTS_EXIST)
  {
    SET_CLIENT_ERROR(mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    goto end;
  }

  if (IS_CONNHDLR_ACTIVE(mysql))
  {
    result= mysql->extension->conn_hdlr->plugin->set_connection(mysql, command, arg, length, skip_check, opt_arg);
    if (result== -1)
      return(result);
  }

  CLEAR_CLIENT_ERROR(mysql);

  if (command == COM_QUERY ||
      command == COM_STMT_PREPARE)
  {
    if ((mysql->options.client_flag & CLIENT_LOCAL_FILES) &&
         mysql->options.extension && mysql->extension->auto_local_infile == WAIT_FOR_QUERY &&
         arg && (*arg == 'l' || *arg == 'L'))
    {
      if (strncasecmp(arg, "load", 4) == 0)
        mysql->extension->auto_local_infile= ACCEPT_FILE_REQUEST;
    }
  }

  mysql->info=0;
  mysql->affected_rows= ~(unsigned long long) 0;
  ma_net_clear(net);			/* Clear receive buffer */
  if (!arg)
    arg="";

  if (net->extension->multi_status== COM_MULTI_ENABLED)
  {
    return net_add_multi_command(net, command, (const uchar *)arg, length);
  }

  if (ma_net_write_command(net,(uchar) command,arg,
			length ? length : (ulong) strlen(arg), 0))
  {
    if (net->last_errno == ER_NET_PACKET_TOO_LARGE)
    {
      my_set_error(mysql, CR_NET_PACKET_TOO_LARGE, SQLSTATE_UNKNOWN, 0);
      goto end;
    }
    end_server(mysql);
    if (mariadb_reconnect(mysql))
      goto end;
    if (ma_net_write_command(net,(uchar) command,arg,
			  length ? length : (ulong) strlen(arg), 0))
    {
      my_set_error(mysql, CR_SERVER_GONE_ERROR, SQLSTATE_UNKNOWN, 0);
      goto end;
    }
  }
  result=0;

  if (net->extension->multi_status > COM_MULTI_OFF)
    skip_check= 1;

  if (!skip_check)
  {
    result= ((mysql->packet_length=ma_net_safe_read(mysql)) == packet_error ?
	     1 : 0);
  }
 end:
  return(result);
}

int
ma_simple_command(MYSQL *mysql,enum enum_server_command command, const char *arg,
	       size_t length, my_bool skip_check, void *opt_arg)
{
  return mysql->methods->db_command(mysql, command, arg, length, skip_check, opt_arg);
}

int ma_multi_command(MYSQL *mysql, enum enum_multi_status status)
{
  NET *net= &mysql->net;

  switch (status) {
  case COM_MULTI_OFF:
    ma_net_clear(net);
    net->extension->multi_status= status;
    return 0;
  case COM_MULTI_ENABLED:
    if (net->extension->multi_status > COM_MULTI_DISABLED)
      return 1;
    ma_net_clear(net);
    net->extension->multi_status= status;
    return 0;
  case COM_MULTI_DISABLED:
    /* Opposite to COM_MULTI_OFF we don't clear net buffer,
       next command or com_nulti_end will flush entire buffer */
    net->extension->multi_status= status;
    return 0;
  case COM_MULTI_END:
  {
    net->extension->multi_status= COM_MULTI_OFF;
    return ma_net_flush(net);
  }
  case COM_MULTI_CANCEL:
    ma_net_clear(net);
    net->extension->multi_status= COM_MULTI_OFF;
    return 0;
  default:
    return 1;
  }
}

static void free_old_query(MYSQL *mysql)
{
  if (mysql->fields)
    ma_free_root(&mysql->field_alloc,MYF(0));
  ma_init_alloc_root(&mysql->field_alloc,8192,0);	/* Assume rowlength < 8192 */
  mysql->fields=0;
  mysql->field_count=0;				/* For API */
  mysql->info= 0;
  return;
}

#if defined(HAVE_GETPWUID) && defined(NO_GETPWUID_DECL)
struct passwd *getpwuid(uid_t);
char* getlogin(void);
#endif

#if !defined(_WIN32)
void read_user_name(char *name)
{
  if (geteuid() == 0)
    strcpy(name,"root");		/* allow use of surun */
  else
  {
#ifdef HAVE_GETPWUID
    struct passwd *skr;
    const char *str;
    if ((skr=getpwuid(geteuid())) != NULL)
    {
      str=skr->pw_name;
    } else if ((str=getlogin()) == NULL)
    {
      if (!(str=getenv("USER")) && !(str=getenv("LOGNAME")) &&
               !(str=getenv("LOGIN")))
        str="UNKNOWN_USER";
    }
    ma_strmake(name,str,USERNAME_LENGTH);
#elif defined(HAVE_CUSERID)
    (void) cuserid(name);
#else
    ma_strmake(name,"UNKNOWN_USER", USERNAME_LENGTH);
#endif
  }
  return;
}

#else /* WIN32 */

void read_user_name(char *name)
{
  char *str=getenv("USERNAME");		/* ODBC will send user variable */
  ma_strmake(name,str ? str : "ODBC", USERNAME_LENGTH);
}

#endif


/**************************************************************************
** Shut down connection
**************************************************************************/

static void
end_server(MYSQL *mysql)
{
  /* if net->error 2 and reconnect is activated, we need to inform
     connection handler */
  if (mysql->net.pvio != 0)
  {
    ma_pvio_close(mysql->net.pvio);
    mysql->net.pvio= 0;    /* Marker */
  }
  ma_net_end(&mysql->net);
  free_old_query(mysql);
  return;
}

void mthd_my_skip_result(MYSQL *mysql)
{
  ulong pkt_len;

  do {
    pkt_len= ma_net_safe_read(mysql);
    if (pkt_len == packet_error)
      break;
  } while (pkt_len > 8 || mysql->net.read_pos[0] != 254);
  return;
}

void STDCALL
mysql_free_result(MYSQL_RES *result)
{
  if (result)
  {
    if (result->handle && result->handle->status == MYSQL_STATUS_USE_RESULT)
    {
      result->handle->methods->db_skip_result(result->handle);
      result->handle->status=MYSQL_STATUS_READY;
    }
    free_rows(result->data);
    if (result->fields)
      ma_free_root(&result->field_alloc,MYF(0));
    if (result->row)
      free(result->row);
    free(result);
  }
  return;
}


/****************************************************************************
** Get options from my.cnf
****************************************************************************/
enum enum_option_type {
  MARIADB_OPTION_NONE,
  MARIADB_OPTION_BOOL,
  MARIADB_OPTION_INT,
  MARIADB_OPTION_SIZET,
  MARIADB_OPTION_STR,
  MARIADB_OPTION_FUNC,
};

struct st_default_options {
  union {
    enum mysql_option option;
    int (*option_func)(MYSQL *mysql, const char *key, const char *value, ssize_t len);
  } u;
  enum enum_option_type type;
  const char *conf_key;
};

struct st_default_options mariadb_defaults[] =
{
  {{MARIADB_OPT_PORT}, MARIADB_OPTION_INT,"port"},
  {{MARIADB_OPT_UNIXSOCKET}, MARIADB_OPTION_STR, "socket"},
  {{MYSQL_OPT_COMPRESS}, MARIADB_OPTION_BOOL, "compress"},
  {{MARIADB_OPT_PASSWORD}, MARIADB_OPTION_STR, "password"},
  {{MYSQL_OPT_NAMED_PIPE}, MARIADB_OPTION_BOOL, "pipe"},
  {{MYSQL_OPT_CONNECT_TIMEOUT}, MARIADB_OPTION_INT, "timeout"},
  {{MARIADB_OPT_USER}, MARIADB_OPTION_STR, "user"},
  {{MYSQL_INIT_COMMAND}, MARIADB_OPTION_STR, "init-command"},
  {{MARIADB_OPT_HOST}, MARIADB_OPTION_STR, "host"},
  {{MARIADB_OPT_SCHEMA}, MARIADB_OPTION_STR, "database"},
  {{MARIADB_OPT_DEBUG}, MARIADB_OPTION_STR, "debug"},
  {{MARIADB_OPT_FOUND_ROWS}, MARIADB_OPTION_NONE, "return-found-rows"},
  {{MYSQL_OPT_SSL_KEY}, MARIADB_OPTION_STR, "ssl-key"},
  {{MYSQL_OPT_SSL_CERT}, MARIADB_OPTION_STR,"ssl-cert"},
  {{MYSQL_OPT_SSL_CA}, MARIADB_OPTION_STR,"ssl-ca"},
  {{MYSQL_OPT_SSL_CAPATH}, MARIADB_OPTION_STR,"ssl-capath"},
  {{MYSQL_OPT_SSL_CRL}, MARIADB_OPTION_STR,"ssl-crl"},
  {{MYSQL_OPT_SSL_CRLPATH}, MARIADB_OPTION_STR,"ssl-crlpath"},
  {{MYSQL_OPT_SSL_VERIFY_SERVER_CERT}, MARIADB_OPTION_BOOL,"ssl-verify-server-cert"},
  {{MYSQL_SET_CHARSET_DIR}, MARIADB_OPTION_STR, "character-sets-dir"},
  {{MYSQL_SET_CHARSET_NAME}, MARIADB_OPTION_STR, "default-character-set"},
  {{MARIADB_OPT_INTERACTIVE}, MARIADB_OPTION_NONE, "interactive-timeout"},
  {{MYSQL_OPT_CONNECT_TIMEOUT}, MARIADB_OPTION_INT, "connect-timeout"},
  {{MYSQL_OPT_LOCAL_INFILE}, MARIADB_OPTION_BOOL, "local-infile"},
  {{0}, 0 ,"disable-local-infile",},
  {{MYSQL_OPT_SSL_CIPHER}, MARIADB_OPTION_STR, "ssl-cipher"},
  {{MYSQL_OPT_MAX_ALLOWED_PACKET}, MARIADB_OPTION_SIZET, "max-allowed-packet"},
  {{MYSQL_OPT_NET_BUFFER_LENGTH}, MARIADB_OPTION_SIZET, "net-buffer-length"},
  {{MYSQL_OPT_PROTOCOL}, MARIADB_OPTION_INT, "protocol"},
  {{MYSQL_SHARED_MEMORY_BASE_NAME}, MARIADB_OPTION_STR,"shared-memory-base-name"},
  {{MARIADB_OPT_MULTI_RESULTS}, MARIADB_OPTION_BOOL, "multi-results"},
  {{MARIADB_OPT_MULTI_STATEMENTS}, MARIADB_OPTION_BOOL, "multi-statements"},
  {{MARIADB_OPT_MULTI_STATEMENTS}, MARIADB_OPTION_BOOL, "multi-queries"},
  {{MYSQL_SECURE_AUTH}, MARIADB_OPTION_BOOL, "secure-auth"},
  {{MYSQL_REPORT_DATA_TRUNCATION}, MARIADB_OPTION_BOOL, "report-data-truncation"},
  {{MYSQL_OPT_RECONNECT}, MARIADB_OPTION_BOOL, "reconnect"},
  {{MYSQL_PLUGIN_DIR}, MARIADB_OPTION_STR, "plugin-dir"},
  {{MYSQL_DEFAULT_AUTH}, MARIADB_OPTION_STR, "default-auth"},
  {{MARIADB_OPT_SSL_FP}, MARIADB_OPTION_STR, "ssl-fp"},
  {{MARIADB_OPT_SSL_FP_LIST}, MARIADB_OPTION_STR, "ssl-fp-list"},
  {{MARIADB_OPT_SSL_FP_LIST}, MARIADB_OPTION_STR, "ssl-fplist"},
  {{MARIADB_OPT_TLS_PASSPHRASE}, MARIADB_OPTION_STR, "ssl-passphrase"},
  {{MARIADB_OPT_TLS_VERSION}, MARIADB_OPTION_STR, "tls-version"},
  {{MYSQL_SERVER_PUBLIC_KEY}, MARIADB_OPTION_STR, "server-public-key"},
  {{MYSQL_OPT_BIND}, MARIADB_OPTION_STR, "bind-address"},
  {{MYSQL_OPT_SSL_ENFORCE}, MARIADB_OPTION_BOOL, "ssl-enforce"},
  {{MARIADB_OPT_RESTRICTED_AUTH}, MARIADB_OPTION_STR, "restricted-auth"},
  {{.option_func=parse_connection_string}, MARIADB_OPTION_FUNC, "connection"},
  {{MARIADB_OPT_BULK_UNIT_RESULTS}, MARIADB_OPTION_BOOL, "bulk-unit-results"},
  /* Aliases */
  {{MARIADB_OPT_SCHEMA}, MARIADB_OPTION_STR, "db"},
  {{MARIADB_OPT_UNIXSOCKET}, MARIADB_OPTION_STR, "unix_socket"},
  {{MARIADB_OPT_HOST}, MARIADB_OPTION_STR, "servername"},
  {{MARIADB_OPT_PASSWORD}, MARIADB_OPTION_STR, "passwd"},
  {{MARIADB_OPT_SSL_FP}, MARIADB_OPTION_STR, "tls-fp"},
  {{MARIADB_OPT_SSL_FP_LIST}, MARIADB_OPTION_STR, "tls-fplist"},
  {{MYSQL_OPT_SSL_KEY}, MARIADB_OPTION_STR, "tls-key"},
  {{MYSQL_OPT_SSL_CERT}, MARIADB_OPTION_STR,"tls-cert"},
  {{MYSQL_OPT_SSL_CA}, MARIADB_OPTION_STR,"tls-ca"},
  {{MYSQL_OPT_SSL_CAPATH}, MARIADB_OPTION_STR,"tls-capath"},
  {{MYSQL_OPT_SSL_CRL}, MARIADB_OPTION_STR,"tls-crl"},
  {{MYSQL_OPT_SSL_CRLPATH}, MARIADB_OPTION_STR,"tls-crlpath"},
  {{MYSQL_OPT_SSL_CIPHER}, MARIADB_OPTION_STR, "tls-cipher"},
  {{MARIADB_OPT_TLS_PASSPHRASE}, MARIADB_OPTION_STR, "tls-passphrase"},
  {{MYSQL_OPT_SSL_ENFORCE}, MARIADB_OPTION_BOOL, "tls-enforce"},
  {{MYSQL_OPT_SSL_VERIFY_SERVER_CERT}, MARIADB_OPTION_BOOL,"tls-verify-peer"},
  {{MARIADB_OPT_RESTRICTED_AUTH}, MARIADB_OPTION_STR, "restricted-auth"},
  {{MYSQL_OPT_ZSTD_COMPRESSION_LEVEL}, MARIADB_OPTION_INT, "zstd-compression-level"},
  {{0}, 0, NULL}
};

#ifdef DEFAULT_SSL_VERIFY_SERVER_CERT
#define FIX_SSL_VERIFY_SERVER_CERT(OPTS)\
(OPTS)->extension->tls_allow_invalid_server_cert= (getenv("MARIADB_TLS_DISABLE_PEER_VERIFICATION") != NULL)
#else
#define FIX_SSL_VERIFY_SERVER_CERT(OPTS) (OPTS)->extension->tls_allow_invalid_server_cert=1
#endif

#define CHECK_OPT_EXTENSION_SET(OPTS)\
    if (!(OPTS)->extension)                                     \
    {                                                           \
      (OPTS)->extension= (struct st_mysql_options_extension *)  \
        calloc(1, sizeof(struct st_mysql_options_extension));   \
      FIX_SSL_VERIFY_SERVER_CERT(OPTS);                         \
    }

#define OPT_SET_EXTENDED_VALUE_BIN(OPTS, KEY, KEY_LEN, VAL, LEN)\
    CHECK_OPT_EXTENSION_SET(OPTS)                                \
    free((gptr)(OPTS)->extension->KEY);                          \
    if((VAL) && (LEN)) {                                         \
      if (((OPTS)->extension->KEY= malloc((LEN)))) {             \
        memcpy((OPTS)->extension->KEY, (VAL), (LEN));            \
        (OPTS)->extension->KEY_LEN= (LEN);                       \
      }                                                          \
    }                                                            \
    else                                                         \
      (OPTS)->extension->KEY= NULL

#define OPT_SET_EXTENDED_VALUE_STR(OPTS, KEY, VAL)               \
    CHECK_OPT_EXTENSION_SET(OPTS)                                \
    free((gptr)(OPTS)->extension->KEY);                          \
    if((VAL))                                                    \
      (OPTS)->extension->KEY= strdup((char *)(VAL));             \
    else                                                         \
      (OPTS)->extension->KEY= NULL

#define OPT_SET_EXTENDED_VALUE(OPTS, KEY, VAL)                \
    CHECK_OPT_EXTENSION_SET(OPTS)                                 \
    (OPTS)->extension->KEY= (VAL)

#define OPT_SET_EXTENDED_VALUE_INT(A,B,C) OPT_SET_EXTENDED_VALUE(A,B,C)

#define OPT_SET_VALUE_STR(OPTS, KEY, VAL)                        \
    free((OPTS)->KEY);                                           \
    if((VAL))                                                   \
      (OPTS)->KEY= strdup((char *)(VAL));                        \
    else                                                         \
      (OPTS)->KEY= NULL

#define OPT_SET_VALUE_INT(OPTS, KEY, VAL)                         \
    (OPTS)->KEY= (VAL)

static void options_add_initcommand(struct st_mysql_options *options,
                                     const char *init_cmd)
{
  char *insert= strdup(init_cmd);
  if (!options->init_command)
  {
    options->init_command= (DYNAMIC_ARRAY*)malloc(sizeof(DYNAMIC_ARRAY));
    ma_init_dynamic_array(options->init_command, sizeof(char*), 5, 5);
  }

  if (ma_insert_dynamic(options->init_command, (gptr)&insert))
    free(insert);
}

my_bool _mariadb_set_conf_option(MYSQL *mysql, const char *config_option, const char *config_value)
{
    if (config_option)
    {
        int i;
        char *c;
        char *mutable_option = strdup(config_option);

        if (!mutable_option)
            return 1;

        /* CONC-395: replace underscore "_" by dash "-" */
        while ((c = strchr(mutable_option, '_')))
            *c = '-';

        for (i = 0; mariadb_defaults[i].conf_key; i++)
        {
            if (!strcmp(mariadb_defaults[i].conf_key, mutable_option))
            {
                my_bool val_bool;
                int val_int;
                size_t val_sizet;
                int rc;
                void *option_val = NULL;

                switch (mariadb_defaults[i].type) {
                case MARIADB_OPTION_FUNC:
                {
                    int ret= mariadb_defaults[i].u.option_func(mysql, mutable_option, config_value, -1);
                    free(mutable_option);
                    return ret;
                }
                case MARIADB_OPTION_BOOL:
                    val_bool = 0;
                    if (config_value)
                        val_bool = atoi(config_value);
                    option_val = &val_bool;
                    break;
                case MARIADB_OPTION_INT:
                    val_int = 0;
                    if (config_value)
                        val_int = atoi(config_value);
                    option_val = &val_int;
                    break;
                case MARIADB_OPTION_SIZET:
                    val_sizet = 0;
                    if (config_value)
                        val_sizet = strtol(config_value, NULL, 10);
                    option_val = &val_sizet;
                    break;
                case MARIADB_OPTION_STR:
                    if (config_value && !config_value[0])
                        option_val = NULL;
                    else
                        option_val = (void*)config_value;
                    break;
                case MARIADB_OPTION_NONE:
                    break;
                }

                rc = mysql_optionsv(mysql, mariadb_defaults[i].u.option, option_val);
                free(mutable_option);
                return test(rc);
            }
        }

        free(mutable_option);
    }

    /* unknown key */
    return 1;
}

/**
 * @brief: simple connection string parser
 *
 * key/value pairs (or key only) are separated by semicolons.
 * If a semicolon is part of a value, it must be enclosed in
 * curly braces.
 *
 * Unknown keys will be ignored.
 */
static int parse_connection_string(MYSQL *mysql, const char *unused __attribute__((unused)),
                                   const char *conn_str, ssize_t len)
{
  char *conn_save,
       *end, *pos,
       *key= NULL, *val= NULL;
  my_bool in_curly_brace= 0;

  if (len == -1)
    len= strlen(conn_str);

  /* don't modify original dsn */
  conn_save= (char *)malloc(len + 1);
  if (!conn_save)
  {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return 1;
  }
  memcpy(conn_save, conn_str, len);
  conn_save[len]= 0;

  /* start and end */
  pos= conn_save;
  end= conn_save + len;

  while (pos <= end)
  {
    /* ignore white space, unless it is between curly braces */
    if (isspace(*pos) && !in_curly_brace)
    {
      pos++;
      continue;
    }

    switch (*pos) {
      case '{':
        if (!key)
          goto error;
        if (!in_curly_brace)
        {
          in_curly_brace= 1;
          if (pos < end)
          {
            pos++;
            val= pos;
            continue;
          }
        }
        break;
      case '}':
        if (in_curly_brace)
        {
          if (!key)
            goto error;
          if (pos < end && *(pos + 1) == '}')
          {
            memmove(pos, pos + 1, end - pos - 1);
            end--;
            pos += 2;
            continue;
          }
          in_curly_brace= 0;
          *pos++= 0;
          continue;
        }
        break;
      case '=':
        if (in_curly_brace)
        {
          pos++;
          continue;
        }
        if (!key)
          goto error;
        *pos++= 0;
        if (pos <= end)
          val= pos;
        continue;
      case ';':
        if (in_curly_brace)
        {
           pos++;
           continue;
        }
        if (!key)
          goto error;
        *pos++= 0;
        if (key && strcasecmp(key, "connection") != 0)
          _mariadb_set_conf_option(mysql, key, val);
        key= val= NULL;
        continue;
    }
    if (!key && *pos)
      key= pos;
    pos++;
  }
  if (key && strcasecmp(key, "connection") != 0)
    _mariadb_set_conf_option(mysql, key, val);
  free(conn_save);
  return 0;

error:
  my_set_error(mysql, CR_CONNSTR_PARSE_ERROR, SQLSTATE_UNKNOWN, 0, pos - conn_save);
  free(conn_save);
  return 1;
}


static MARIADB_CONST_STRING null_const_string= {0,0};

/***************************************************************************
** Allocate a string copy on memroot
***************************************************************************/
MARIADB_CONST_STRING ma_const_string_copy_root(MA_MEM_ROOT *memroot,
                                                      const char *str,
                                                      size_t length)
{
  MARIADB_CONST_STRING res;
  if (!str || !(res.str= ma_memdup_root(memroot, str, length)))
    return null_const_string;
  res.length= length;
  return res;
}


/***************************************************************************
** Allocate and initialize MA_FIELD_EXTENSION
***************************************************************************/
MA_FIELD_EXTENSION *new_ma_field_extension(MA_MEM_ROOT *memroot)
{
  MA_FIELD_EXTENSION *ext= ma_alloc_root(memroot, sizeof(MA_FIELD_EXTENSION));
  if (ext)
    memset((void *) ext, 0, sizeof(*ext));
  return ext;
}


/***************************************************************************
** Populate field extension from a type info packet
***************************************************************************/

static void ma_field_extension_init_type_info(MA_MEM_ROOT *memroot,
                                              MA_FIELD_EXTENSION *ext,
                                              const char *ptr, size_t length)
{
  const char *end= ptr + length;
  for ( ;  ptr < end; )
  {
    uint itype= (uchar) *ptr++;
    uint len= (uchar) *ptr++;
    if (ptr + len > end  || len > 127)
      break; /*Badly encoded data*/
    if (itype <= 127 && itype <= MARIADB_FIELD_ATTR_LAST)
      ext->metadata[itype]= ma_const_string_copy_root(memroot, ptr, len);
    ptr+= len;
  }
}


/***************************************************************************
** Allocate a field extension deep copy
***************************************************************************/

MA_FIELD_EXTENSION *ma_field_extension_deep_dup(MA_MEM_ROOT *memroot,
                                                const MA_FIELD_EXTENSION *from)
{
  MA_FIELD_EXTENSION *ext= new_ma_field_extension(memroot);
  uint i;
  if (!ext)
    return NULL;
  for (i= 0; i < MARIADB_FIELD_ATTR_LAST; i++)
  {
    if (from->metadata[i].str)
      ext->metadata[i]= ma_const_string_copy_root(memroot,
                                                  from->metadata[i].str,
                                                  from->metadata[i].length);
  }
  return ext;
}

/***************************************************************************
** Change field rows to field structs
***************************************************************************/

static size_t rset_field_offsets[]= {
  OFFSET(MYSQL_FIELD, catalog),
  OFFSET(MYSQL_FIELD, catalog_length),
  OFFSET(MYSQL_FIELD, db),
  OFFSET(MYSQL_FIELD, db_length),
  OFFSET(MYSQL_FIELD, table),
  OFFSET(MYSQL_FIELD, table_length),
  OFFSET(MYSQL_FIELD, org_table),
  OFFSET(MYSQL_FIELD, org_table_length),
  OFFSET(MYSQL_FIELD, name),
  OFFSET(MYSQL_FIELD, name_length),
  OFFSET(MYSQL_FIELD, org_name),
  OFFSET(MYSQL_FIELD, org_name_length)
};

/* calculate lengths for field metadata:
   returns zero on success, 1 if null_length was
   detected */
static my_bool ma_get_rset_field_lengths(MYSQL_ROW row, unsigned int field_count,
                               unsigned long *lengths)
{
  unsigned long *last_length= 0;
  char *pos= 0;
  MYSQL_ROW end= row + field_count + 1;
  my_bool rc= 0;

  while (row != end)
  {
    if (*row)
    {
      if (pos)
        *last_length= (ulong)(*row - pos - 1);
      pos= *row;
    } else {
      /* NULL_LENGTH (see also CONC-709) */
      rc= 1;
      *last_length= 0;
    }
    last_length= lengths++;
    row++;
  }
  return rc;
}

MYSQL_FIELD *
unpack_fields(const MYSQL *mysql,
              MYSQL_DATA *data, MA_MEM_ROOT *alloc, uint fields,
	      my_bool default_value)
{
  MYSQL_ROWS	*row;
  MYSQL_FIELD	*field,*result;
  char    *p;
  unsigned int i, field_count= sizeof(rset_field_offsets)/sizeof(size_t)/2;

  field=result=(MYSQL_FIELD*) ma_alloc_root(alloc,sizeof(MYSQL_FIELD)*fields);
  if (!result)
    return(0);

  for (row=data->data; row ; row = row->next,field++)
  {
    unsigned long lengths[9];

    if (field >= result + fields)
      goto error;

    if (ma_get_rset_field_lengths(row->data, field_count, lengths))
      goto error;

    for (i=0; i < field_count; i++)
    {
      *(char **)(((char *)field) + rset_field_offsets[i*2])=
        ma_strdup_root(alloc, (char *)row->data[i]);
      *(unsigned int *)(((char *)field) + rset_field_offsets[i*2+1])= lengths[i];
    }

    field->extension= NULL;
    if (ma_has_extended_type_info(mysql))
    {
      if (row->data[i+1] - row->data[i] > 1)
      {
        size_t len= row->data[i+1] - row->data[i] - 1;
        MA_FIELD_EXTENSION *ext= new_ma_field_extension(alloc);
        if ((field->extension= ext))
          ma_field_extension_init_type_info(alloc, ext, row->data[i], len);
      }
      i++;
    }

    p= (char *)row->data[i];
    /* filler */
    field->charsetnr= uint2korr(p);
    p+= 2;
    field->length= (uint) uint4korr(p);
    p+= 4;
    field->type=   (enum enum_field_types)uint1korr(p);
    p++;
    field->flags= uint2korr(p);
    p+= 2;
    field->decimals= (uint) p[0];
    p++;

    /* filler */
    p+= 2;

    if (INTERNAL_NUM_FIELD(field))
      field->flags|= NUM_FLAG;

    i++;
    /* This is used by deprecated function mysql_list_fields only,
       however the reported length is not correct, so we always zero it */
    if (default_value && row->data[i])
      field->def=ma_strdup_root(alloc,(char*) row->data[i]);
    else
      field->def=0;
    field->def_length= 0;

    field->max_length= 0;
  }
  if (field < result + fields)
    goto error;
  free_rows(data);				/* Free old data */
  return(result);
error:
  free_rows(data);
  ma_free_root(alloc, MYF(0));
  return(0);
}


/* Read all rows (fields or data) from server */

MYSQL_DATA *mthd_my_read_rows(MYSQL *mysql,MYSQL_FIELD *mysql_fields,
			     uint fields)
{
  uint	field;
  ulong pkt_len;
  ulong len;
  uchar *cp;
  char	*to, *end_to;
  MYSQL_DATA *result;
  MYSQL_ROWS **prev_ptr,*cur;
  NET *net = &mysql->net;

  if ((pkt_len= ma_net_safe_read(mysql)) == packet_error)
    return(0);
  if (!(result=(MYSQL_DATA*) calloc(1, sizeof(MYSQL_DATA))))
  {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return(0);
  }
  ma_init_alloc_root(&result->alloc,8192,0);	/* Assume rowlength < 8192 */
  result->alloc.min_malloc=sizeof(MYSQL_ROWS);
  prev_ptr= &result->data;
  result->rows=0;
  result->fields=fields;

  while (*(cp=net->read_pos) != 254 || pkt_len >= 8)
  {
    result->rows++;
    if (!(cur= (MYSQL_ROWS*) ma_alloc_root(&result->alloc,
					    sizeof(MYSQL_ROWS))) ||
	      !(cur->data= ((MYSQL_ROW)
		      ma_alloc_root(&result->alloc,
				     (fields+1)*sizeof(char *)+fields+pkt_len))))
    {
      free_rows(result);
      SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      return(0);
    }
    *prev_ptr=cur;
    prev_ptr= &cur->next;
    to= (char*) (cur->data+fields+1);
    end_to=to+fields+pkt_len-1;
    for (field=0 ; field < fields ; field++)
    {
      if ((len=(ulong) net_field_length(&cp)) == NULL_LENGTH)
      {						/* null field */
        cur->data[field] = 0;
      }
      else
      {
        cur->data[field] = to;
        if (len > (ulong)(end_to - to) || to > end_to)
        {
          free_rows(result);
          SET_CLIENT_ERROR(mysql, CR_UNKNOWN_ERROR, SQLSTATE_UNKNOWN, 0);
          return(0);
        }
        memcpy(to,(char*) cp,len); to[len]=0;
        to+=len+1;
        cp+=len;
        if (mysql_fields)
        {
          if (mysql_fields[field].max_length < len)
            mysql_fields[field].max_length=len;
         }
      }
    }
    cur->data[field]=to;			/* End of last field */
    if ((pkt_len=ma_net_safe_read(mysql)) == packet_error)
    {
      free_rows(result);
      return(0);
    }
  }
  *prev_ptr=0;					/* last pointer is null */
  /* save status */
  if (pkt_len > 1)
  {
    unsigned int last_status= mysql->server_status;
    cp++;
    mysql->warning_count= uint2korr(cp);
    cp+= 2;
    mysql->server_status= uint2korr(cp);
    ma_status_callback(mysql, last_status)
  }
  return(result);
}


/*
** Read one row. Uses packet buffer as storage for fields.
** When next packet is read, the previous field values are destroyed
*/


int mthd_my_read_one_row(MYSQL *mysql,uint fields,MYSQL_ROW row, ulong *lengths)
{
  uint field;
  ulong pkt_len,len;
  uchar *pos,*prev_pos, *end_pos;

  if ((pkt_len=(uint) ma_net_safe_read(mysql)) == packet_error)
    return -1;

  if (pkt_len <= 8 && mysql->net.read_pos[0] == 254)
  {
    unsigned int last_status= mysql->server_status;
    mysql->warning_count= uint2korr(mysql->net.read_pos + 1);
    mysql->server_status= uint2korr(mysql->net.read_pos + 3);
    ma_status_callback(mysql, last_status);
    return 1;				/* End of data */
  }
  prev_pos= 0;				/* allowed to write at packet[-1] */
  pos=mysql->net.read_pos;
  end_pos=pos+pkt_len;
  for (field=0 ; field < fields ; field++)
  {
    if ((len=(ulong) net_field_length(&pos)) == NULL_LENGTH)
    {						/* null field */
      row[field] = 0;
      *lengths++=0;
    }
    else
    {
      if (len > (ulong) (end_pos - pos) || pos > end_pos)
      {
        mysql->net.last_errno=CR_UNKNOWN_ERROR;
        strncpy(mysql->net.last_error,ER(mysql->net.last_errno),
                MYSQL_ERRMSG_SIZE - 1);
        return -1;
      }
      row[field] = (char*) pos;
      pos+=len;
      *lengths++=len;
    }
    if (prev_pos)
      *prev_pos=0;				/* Terminate prev field */
    prev_pos=pos;
  }
  row[field]=(char*) prev_pos+1;		/* End of last field */
  *prev_pos=0;					/* Terminate last field */
  return 0;
}

/****************************************************************************
** Init MySQL structure or allocate one
****************************************************************************/

MYSQL * STDCALL
mysql_init(MYSQL *mysql)
{
  if (mysql_server_init(0, NULL, NULL))
    return NULL;
  if (!mysql)
  {
    if (!(mysql=(MYSQL*) calloc(1, sizeof(MYSQL))))
      return 0;
    mysql->free_me=1;
    mysql->net.pvio= 0;
    mysql->net.extension= 0;
  }
  else
  {
    memset(mysql, 0, sizeof(MYSQL));
    mysql->net.pvio= 0;
    mysql->free_me= 0;
    mysql->net.extension= 0;
  }

  if (!(mysql->net.extension= (struct st_mariadb_net_extension *)
                               calloc(1, sizeof(struct st_mariadb_net_extension))) ||
      !(mysql->extension= (struct st_mariadb_extension *)
                          calloc(1, sizeof(struct st_mariadb_extension))))
    goto error;
  mysql->options.report_data_truncation= 1;
  mysql->options.connect_timeout=CONNECT_TIMEOUT;
  mysql->charset= mysql_find_charset_name(MARIADB_DEFAULT_CHARSET);
  mysql->methods= &MARIADB_DEFAULT_METHODS;
  strcpy(mysql->net.sqlstate, "00000");
  mysql->net.last_error[0]= mysql->net.last_errno= mysql->net.extension->extended_errno= 0;

  if (ENABLED_LOCAL_INFILE != LOCAL_INFILE_MODE_OFF)
    mysql->options.client_flag|= CLIENT_LOCAL_FILES;
  mysql->extension->auto_local_infile= ENABLED_LOCAL_INFILE == LOCAL_INFILE_MODE_AUTO
                                       ? WAIT_FOR_QUERY : ALWAYS_ACCEPT;
  mysql->options.reconnect= 0;
  return mysql;
error:
  if (mysql->free_me)
  {
    if (mysql->net.extension)
      free(mysql->net.extension);
    free(mysql);
  }
  return 0;
}

int STDCALL
mysql_ssl_set(MYSQL *mysql __attribute__((unused)),
              const char *key __attribute__((unused)),
              const char *cert __attribute__((unused)),
              const char *ca __attribute__((unused)),
              const char *capath __attribute__((unused)),
              const char *cipher __attribute__((unused)))
{
#ifdef HAVE_TLS
  char enable= 1;
  return (mysql_optionsv(mysql, MYSQL_OPT_SSL_ENFORCE, &enable) |
          mysql_optionsv(mysql, MYSQL_OPT_SSL_KEY, key) |
          mysql_optionsv(mysql, MYSQL_OPT_SSL_CERT, cert) |
          mysql_optionsv(mysql, MYSQL_OPT_SSL_CA, ca) |
          mysql_optionsv(mysql, MYSQL_OPT_SSL_CAPATH, capath) |
          mysql_optionsv(mysql, MYSQL_OPT_SSL_CIPHER, cipher)) ? 1 : 0;
#else
  return 0;
#endif
}

/**************************************************************************
**************************************************************************/

const char * STDCALL
mysql_get_ssl_cipher(MYSQL *mysql __attribute__((unused)))
{
#ifdef HAVE_TLS
  if (mysql->net.pvio && mysql->net.pvio->ctls)
  {
    return ma_pvio_tls_cipher(mysql->net.pvio->ctls);
  }
#endif
  return(NULL);
}

/**************************************************************************
** Free strings in the SSL structure and clear 'use_ssl' flag.
** NB! Errors are not reported until you do mysql_real_connect.
**************************************************************************/

char *ma_send_connect_attr(MYSQL *mysql, unsigned char *buffer)
{
  if (mysql->server_capabilities & CLIENT_CONNECT_ATTRS)
  {
    buffer= (unsigned char *)mysql_net_store_length((unsigned char *)buffer, (mysql->options.extension) ?
                             mysql->options.extension->connect_attrs_len : 0);
    if (mysql->options.extension &&
        ma_hashtbl_inited(&mysql->options.extension->connect_attrs))
    {
      uint i;
      for (i=0; i < mysql->options.extension->connect_attrs.records; i++)
      {
        size_t len;
        uchar *p= ma_hashtbl_element(&mysql->options.extension->connect_attrs, i);

        len= strlen((char *)p);
        buffer= mysql_net_store_length(buffer, len);
        memcpy(buffer, p, len);
        buffer+= (len);
        p+= (len + 1);
        len= strlen((char *)p);
        buffer= mysql_net_store_length(buffer, len);
        memcpy(buffer, p, len);
        buffer+= len;
      }
    }
  }
  return (char *)buffer;
}

/** set some default attributes */
static my_bool
ma_set_connect_attrs(MYSQL *mysql, const char *host)
{
  char buffer[255];
  int rc= 0;

  rc= mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_DELETE, "_client_name") +
      mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_DELETE, "_client_version") +
      mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_DELETE, "_os") +
      mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_DELETE, "_server_host") +
#ifdef _WIN32
      mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_DELETE, "_thread") +
#endif
      mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_DELETE, "_pid") +
      mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_DELETE, "_platform");

  rc+= mysql_optionsv(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "_client_name", "libmariadb")
       + mysql_optionsv(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "_client_version", MARIADB_PACKAGE_VERSION)
       + mysql_optionsv(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "_os", MARIADB_SYSTEM_TYPE);

  if (host && *host)
    rc+= mysql_optionsv(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "_server_host", host);

#ifdef _WIN32
  snprintf(buffer, 255, "%lu", (ulong) GetCurrentThreadId());
  rc+= mysql_optionsv(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "_thread", buffer);
  snprintf(buffer, 255, "%lu", (ulong) GetCurrentProcessId());
#else
  snprintf(buffer, 255, "%lu", (ulong) getpid());
#endif
  rc+= mysql_optionsv(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "_pid", buffer);

  rc+= mysql_optionsv(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "_platform", MARIADB_MACHINE_TYPE);
  return(test(rc>0));
}

/*
** Note that the mysql argument must be initialized with mysql_init()
** before calling mysql_real_connect !
*/

MYSQL * STDCALL
mysql_real_connect(MYSQL *mysql, const char *host, const char *user,
		   const char *passwd, const char *db,
		   uint port, const char *unix_socket,unsigned long client_flag)
{
  const char *end= NULL;
  char *connection_handler= (mysql->options.extension) ?
                            mysql->options.extension->connection_handler : 0;

  if (!mysql->options.extension || !mysql->options.extension->tls_verification_callback)
    mysql_optionsv(mysql, MARIADB_OPT_TLS_VERIFICATION_CALLBACK, ma_pvio_tls_verify_server_cert);

  if ((client_flag & CLIENT_ALLOWED_FLAGS) != client_flag)
  {
    my_set_error(mysql, CR_INVALID_CLIENT_FLAG, SQLSTATE_UNKNOWN,
                 ER(CR_INVALID_CLIENT_FLAG),
                 client_flag, CLIENT_ALLOWED_FLAGS);
    return NULL;
  }

  if (!mysql->methods)
    mysql->methods= &MARIADB_DEFAULT_METHODS;

  /* set default */
  if (!mysql->options.extension || !mysql->options.extension->status_callback)
    mysql_optionsv(mysql, MARIADB_OPT_STATUS_CALLBACK, NULL, NULL);

  reset_tls_error(mysql);

  /* if host contains a semicolon or equal sign, we need to parse connection string */
  if (host && (strchr(host, ';') || strchr(host, '=')))
  {
    if (parse_connection_string(mysql, NULL, host, strlen(host)))
      return NULL;
    /* if host was passed in connection string, it is now
       stored in mysql->options.host */
    host= NULL;
  } else if (connection_handler ||
      (host && (end= strstr(host, "://"))))
  {
    MARIADB_CONNECTION_PLUGIN *plugin;
    char plugin_name[64];

    if (!connection_handler || !connection_handler[0])
    {
      memset(plugin_name, 0, 64);
      ma_strmake(plugin_name, host, MIN(end - host, 63));
      end+= 3;
    }
    else
      ma_strmake(plugin_name, connection_handler, MIN(63, strlen(connection_handler)));

    if (!(plugin= (MARIADB_CONNECTION_PLUGIN *)mysql_client_find_plugin(mysql, plugin_name, MARIADB_CLIENT_CONNECTION_PLUGIN)))
      return NULL;

    if (!(mysql->extension->conn_hdlr= (MA_CONNECTION_HANDLER *)calloc(1, sizeof(MA_CONNECTION_HANDLER))))
    {
      SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      return NULL;
    }

    /* save URL for reconnect */
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, url, host);

    mysql->extension->conn_hdlr->plugin= plugin;

    if (plugin && plugin->connect)
    {
      MYSQL *my= plugin->connect(mysql, end, user, passwd, db, port, unix_socket, client_flag);
      if (!my)
      {
        free(mysql->extension->conn_hdlr);
        mysql->extension->conn_hdlr= NULL;
      }
      return my;
    }
  }
  return mysql->methods->db_connect(mysql, host, user, passwd,
                                    db, port, unix_socket, client_flag);
}

struct st_host {
  char *host;
  int port;
};

static void ma_get_host_list(char *host_list, struct st_host *host_info, int default_port)
{
  char *token, *start, *save;
  int host_nr= 0;

  start= host_list;
  while ((token= strtok_r(start, ",", &save)))
  {
    char *p;

    /* ipv6 hostname */
    if ((p= strchr(token, ']')))
    {
      host_info[host_nr].host= token + 1;
      *p++= 0;
      token= p;
    }
    else
      host_info[host_nr].host= token;
    /* check if port was specified */
    if ((p= strchr(token, ':')))
    {
      *p++= 0;
      host_info[host_nr].port= atoi(p);
    }
    else
      host_info[host_nr].port= default_port;
    host_nr++;
    start= NULL;
  }
  return;
}

MYSQL *mthd_my_real_connect(MYSQL *mysql, const char *host, const char *user,
		   const char *passwd, const char *db,
		   uint port, const char *unix_socket, unsigned long client_flag)
{
  char		buff[NAME_LEN+USERNAME_LENGTH+100];
  char		*end, *end_pkt, *host_info;
  MA_PVIO_CINFO  cinfo= {NULL, NULL, 0, -1, NULL};
  MARIADB_PVIO   *pvio= NULL;
  char    *scramble_data;
  const char *scramble_plugin;
  uint pkt_length, scramble_len, pkt_scramble_len= 0;
  NET	*net= &mysql->net;
  my_bool is_multi= 0;
  char *host_copy= NULL;
  struct st_host *host_list= NULL;
  int connect_attempts= 0;

  if (!mysql->methods)
    mysql->methods= &MARIADB_DEFAULT_METHODS;

  if (net->pvio)  /* check if we are already connected */
  {
    SET_CLIENT_ERROR(mysql, CR_ALREADY_CONNECTED, SQLSTATE_UNKNOWN, 0);
    return(NULL);
  }

  /* use default options */
  if (mysql->options.my_cnf_file || mysql->options.my_cnf_group)
  {
    _mariadb_read_options(mysql, NULL,
			  (mysql->options.my_cnf_file ?
			   mysql->options.my_cnf_file : NULL),
			   mysql->options.my_cnf_group, 0);
    free(mysql->options.my_cnf_file);
    free(mysql->options.my_cnf_group);
    mysql->options.my_cnf_file=mysql->options.my_cnf_group=0;
  }

  if (!port)
    port=mysql->options.port;
  if (!host || !host[0])
    host= mysql->options.host;

  /* check if we have multi hosts */
  if (host && host[0] && strchr(host, ','))
  {
    char *p;
    size_t host_count= 1;

    is_multi= 1;

    /* don't change original entry, so let's make a copy */
    if (!(host_copy= strdup(host)))
    {
      SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      goto error;
    }

    p= host_copy;

    while ((p = strchr(p, ',')))
    {
      host_count++;
      p++;
    }

    if (!(host_list= (struct st_host *)calloc(host_count + 1, sizeof(struct st_host))))
    {
      SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      goto error;
    }

    ma_get_host_list(host_copy, host_list, port);
  }

restart:
  /* check if we reached end of list */
  if (is_multi)
  {
    if (!host_list[connect_attempts].host)
      goto error;

    host= host_list[connect_attempts].host;
    port= host_list[connect_attempts].port;
  }

  ma_set_connect_attrs(mysql, host);

#ifndef WIN32
  if (mysql->options.protocol > MYSQL_PROTOCOL_SOCKET)
  {
    SET_CLIENT_ERROR(mysql, CR_CONN_UNKNOWN_PROTOCOL, SQLSTATE_UNKNOWN, 0);
    return(NULL);
  }
#endif

  /* Some empty-string-tests are done because of ODBC */
  if (!user || !user[0])
    user=mysql->options.user;
  if (!passwd)
  {
    passwd=mysql->options.password;
#ifndef DONT_USE_MYSQL_PWD
    if (!passwd)
      passwd=getenv("MYSQL_PWD");  /* get it from environment (haneke) */
    if (!passwd)
      passwd= "";
#endif
  }
  if (!db || !db[0])
    db=mysql->options.db;
  if (!unix_socket)
    unix_socket=mysql->options.unix_socket;

  mysql->server_status=SERVER_STATUS_AUTOCOMMIT;

  /* try to connect via pvio_init */
  cinfo.host= host;
  cinfo.unix_socket= unix_socket;
  cinfo.port= port;
  cinfo.mysql= mysql;

  /*
  ** Grab a socket and connect it to the server
  */
#ifndef _WIN32
#if defined(HAVE_SYS_UN_H)
  if ((!host ||  strcmp(host,LOCAL_HOST) == 0) &&
      mysql->options.protocol != MYSQL_PROTOCOL_TCP &&
      (unix_socket || mysql_unix_port))
  {
    cinfo.host= LOCAL_HOST;
    cinfo.unix_socket= (unix_socket) ? unix_socket : mysql_unix_port;
    cinfo.type= PVIO_TYPE_UNIXSOCKET;
    sprintf(host_info=buff,ER(CR_LOCALHOST_CONNECTION),cinfo.host);
  }
  else
#endif
#else
  if (mysql->options.protocol == MYSQL_PROTOCOL_MEMORY)
  {
    cinfo.host= mysql->options.shared_memory_base_name;
    cinfo.type= PVIO_TYPE_SHAREDMEM;
    sprintf(host_info=buff,ER(CR_SHARED_MEMORY_CONNECTION), cinfo.host ? cinfo.host : SHM_DEFAULT_NAME);
  }
   /* named pipe */
  else if (mysql->options.protocol == MYSQL_PROTOCOL_PIPE ||
	  (host && strcmp(host,LOCAL_HOST_NAMEDPIPE) == 0))
  {
    cinfo.type= PVIO_TYPE_NAMEDPIPE;
    sprintf(host_info=buff,ER(CR_NAMEDPIPE_CONNECTION),cinfo.host);
  }
  else
#endif
  {
    cinfo.unix_socket=0;				/* This is not used */
    if (!port)
      port=mysql_port;
    if (!host)
      host=LOCAL_HOST;
    cinfo.host= host;
    cinfo.port= port;
    cinfo.type= PVIO_TYPE_SOCKET;
    sprintf(host_info=buff,ER(CR_TCP_CONNECTION), cinfo.host);
  }
  /* Initialize and load pvio plugin */
  if (!(pvio= ma_pvio_init(&cinfo)))
    goto error;

  /* try to connect */
  if (ma_pvio_connect(pvio, &cinfo) != 0)
  {
    ma_pvio_close(pvio);
    if (mysql->options.extension && mysql->options.extension->async_context &&
        mysql->options.extension->async_context->pvio)
    {
      /* pvio delegated to mysql->net.pvio by ma_net_init().
       * invalidate the pvio pointer in the async context */
      mysql->options.extension->async_context->pvio = NULL;
    }

    if (is_multi)
    {
      connect_attempts++;
      goto restart;
    }
    goto error;
  }

  if (ma_net_init(net, pvio))
  {
    ma_pvio_close(pvio);
    goto error;
  }

  if (mysql->options.extension && mysql->options.extension->async_context && mysql->options.extension->async_context->pvio)
  {
    /* pvio delegated to mysql->net.pvio by ma_net_init().
     * invalidate the pvio pointer in the async context */
    mysql->options.extension->async_context->pvio = NULL;
  }


  if (mysql->options.max_allowed_packet)
    net->max_packet_size= mysql->options.max_allowed_packet;

  ma_pvio_keepalive(net->pvio);
  strcpy(mysql->net.sqlstate, "00000");

  /* Get version info */
  mysql->protocol_version= PROTOCOL_VERSION;	/* Assume this */
/*
  if (ma_pvio_wait_io_or_timeout(net->pvio, FALSE, 0) < 1)
  {
    my_set_error(mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                 ER(CR_SERVER_LOST_EXTENDED),
                 "handshake: waiting for initial communication packet",
                 errno);
    goto error;
  }
 */
  if ((pkt_length=ma_net_safe_read(mysql)) == packet_error)
  {

    unsigned int code= mysql->net.last_errno;
    if (code == CR_SERVER_LOST)
      my_set_error(mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                 ER(CR_SERVER_LOST_EXTENDED),
                 "handshake: reading initial communication packet",
                 errno);
    else if (IS_MYSQL_ERROR(code) || IS_MARIADB_ERROR(code))
      ; /* not forged - generated on the client side */
    else if (mysql->options.use_ssl)
    {
      char last_error[sizeof(mysql->net.last_error)];
      strcpy(last_error, mysql->net.last_error);
      my_set_error(mysql, CR_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                   "Received error packet before completion of TLS handshake. "
                   "The authenticity of the following error cannot be verified: %d - %s",
                   code, last_error);
    }

    goto error;
  }
  end= (char *)net->read_pos;
  end_pkt= (char *)net->read_pos + pkt_length;

  /* Check if version of protocol matches current one */

  mysql->protocol_version= end[0];
  end++;

  if (mysql->protocol_version <  PROTOCOL_VERSION)
  {
    net->last_errno= CR_VERSION_ERROR;
    sprintf(net->last_error, ER(CR_VERSION_ERROR), mysql->protocol_version,
	    PROTOCOL_VERSION);
    goto error;
  }
  /* Save connection information */
  if (!user) user="";

  if (!(mysql->host_info= strdup(host_info)) ||
      !(mysql->host= strdup(cinfo.host ? cinfo.host : "")) ||
      !(mysql->user=strdup(user)) ||
      !(mysql->passwd=strdup(passwd)))
  {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    goto error;
  }
  if (cinfo.unix_socket)
    mysql->unix_socket= strdup(cinfo.unix_socket);
  else
    mysql->unix_socket=0;
  mysql->port=port;
  client_flag|=mysql->options.client_flag;

  if (strncmp(end, MA_RPL_VERSION_HACK, sizeof(MA_RPL_VERSION_HACK) - 1) == 0)
  {
    mysql->server_version= strdup(end + sizeof(MA_RPL_VERSION_HACK) - 1);
  }
  else
  {
    if (!(mysql->server_version= strdup(end)))
    {
      SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      goto error;
    }
  }
  end+= strlen(end) + 1;

  mysql->thread_id=uint4korr(end);
  end+=4;

  /* This is the first part of scramble packet. In 4.1 and later
     a second package will follow later */
  scramble_data= end;
  scramble_len= SCRAMBLE_LENGTH_323 + 1;
  scramble_plugin= old_password_plugin_name;
  end+= SCRAMBLE_LENGTH_323;

  /* 1st pad */
  end++;

  if (end + 1<= end_pkt)
  {
    mysql->server_capabilities=uint2korr(end);
  }

  /* mysql 5.5 protocol */
  if (end + 18 <= end_pkt)
  {
    mysql->server_language= uint1korr(end + 2);
    mysql->server_status= uint2korr(end + 3);
    mysql->server_capabilities|= (unsigned int)(uint2korr(end + 5)) << 16;
    pkt_scramble_len= uint1korr(end + 7);

    /* check if MariaDB-specific capabilities are available */
    if (mariadb_connection(mysql) && !(mysql->server_capabilities & CLIENT_MYSQL))
    {
      mysql->extension->mariadb_server_capabilities= (ulonglong) uint4korr(end + 14);
    }
  }

  /* pad 2 */
  end+= 18;

  /* second scramble package */
  if (end + SCRAMBLE_LENGTH - SCRAMBLE_LENGTH_323 + 1 <= end_pkt)
  {
    memcpy(end - SCRAMBLE_LENGTH_323, scramble_data, SCRAMBLE_LENGTH_323);
    scramble_data= end - SCRAMBLE_LENGTH_323;
    if (mysql->server_capabilities & CLIENT_PLUGIN_AUTH)
    {
      scramble_len= pkt_scramble_len;
      scramble_plugin= scramble_data + scramble_len;
      if (scramble_data + scramble_len > end_pkt)
      {
        SET_CLIENT_ERROR(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
        goto error;
      }
    } else
    {
      scramble_len= (uint)(end_pkt - scramble_data);
      scramble_plugin= native_password_plugin_name;
    }
  } else
  {
    mysql->server_capabilities&= ~CLIENT_SECURE_CONNECTION;
    if (mysql->options.secure_auth)
    {
      SET_CLIENT_ERROR(mysql, CR_SECURE_AUTH, SQLSTATE_UNKNOWN, 0);
      goto error;
    }
  }

  /* Set character set */
  if (mysql->options.charset_name)
    mysql->charset= mysql_find_charset_name(mysql->options.charset_name);
  else
    mysql->charset=mysql_find_charset_name(MARIADB_DEFAULT_CHARSET);

  if (!mysql->charset)
  {
    net->last_errno=CR_CANT_READ_CHARSET;
    sprintf(net->last_error,ER(net->last_errno),
      mysql->options.charset_name ? mysql->options.charset_name : 
                                    MARIADB_DEFAULT_CHARSET,
      "compiled_in");
    goto error;
  }

  mysql->client_flag= client_flag;

  if (run_plugin_auth(mysql, scramble_data, scramble_len,
                             scramble_plugin, db))
    goto error;

  if (mysql->client_flag & CLIENT_COMPRESS ||
      mysql->client_flag & CLIENT_ZSTD_COMPRESSION)
  {
    if (!compression_plugin(net) ||
        (!(compression_ctx(net) = compression_plugin(net)->init_ctx(COMPRESSION_LEVEL_DEFAULT))))
    {
      int alg= (mysql->client_flag & CLIENT_ZSTD_COMPRESSION) ? 
               COMPRESSION_ZSTD : COMPRESSION_ZLIB;
      compression_plugin(net)= NULL;
      my_set_error(mysql, CR_ERR_LOAD_PLUGIN, SQLSTATE_UNKNOWN, NULL,
                   _mariadb_compression_algorithm_str(alg));
      goto error;
    }
    net->compress= 1;
  }

  /* last part: select default db */
  if (!(mysql->server_capabilities & CLIENT_CONNECT_WITH_DB) &&
      (db && !mysql->db))
  {
    if (mysql_select_db(mysql, db))
    {
      my_set_error(mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                          ER(CR_SERVER_LOST_EXTENDED),
                          "Setting intital database",
                          errno);
      goto error;
    }
  }

  if (mysql->options.init_command)
  {
    char **begin= (char **)mysql->options.init_command->buffer;
    char **end= begin + mysql->options.init_command->elements;

    /* Avoid reconnect in mysql_real_connect */
    my_bool save_reconnect= mysql->options.reconnect;
    mysql->options.reconnect= 0;

    for (;begin < end; begin++)
    {
      if (mysql_real_query(mysql, *begin, (unsigned long)strlen(*begin)))
        goto error;

    /* check if query produced a result set */
      do {
        MYSQL_RES *res;
        if ((res= mysql_use_result(mysql)))
          mysql_free_result(res);
      } while (!mysql_next_result(mysql));
    }
    mysql->options.reconnect= save_reconnect;
  }

  strcpy(mysql->net.sqlstate, "00000");

  /* connection established, apply timeouts */
  ma_pvio_set_timeout(mysql->net.pvio, PVIO_READ_TIMEOUT, mysql->options.read_timeout);
  ma_pvio_set_timeout(mysql->net.pvio, PVIO_WRITE_TIMEOUT, mysql->options.write_timeout);

  free(host_list);
  free(host_copy);

  return(mysql);

error:
  /* Free allocated memory */
  free(host_list);
  free(host_copy);
  end_server(mysql);
  /* only free the allocated memory, user needs to call mysql_close */
  mysql_close_memory(mysql);
  if (!(client_flag & CLIENT_REMEMBER_OPTIONS) &&
      !(IS_MYSQL_ASYNC(mysql)))
    mysql_close_options(mysql);

  /* CONC-703: If no error was set, we set CR_SERVER_LOST by default */
  if (!mysql_errno(mysql))
    my_set_error(mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                        "Can't connect to server (%d).",
                        errno);
  return(0);
}

struct my_hook_data {
  MYSQL *orig_mysql;
  MYSQL *new_mysql;
  /* This is always NULL currently, but restoring does not hurt just in case. */
  MARIADB_PVIO *orig_pvio;
};
/*
  Callback hook to make the new VIO accessible via the old MYSQL to calling
  application when suspending a non-blocking call during automatic reconnect.
*/
static void
my_suspend_hook(my_bool suspend, void *data)
{
  struct my_hook_data *hook_data= (struct my_hook_data *)data;
  if (suspend)
  {
    hook_data->orig_pvio= hook_data->orig_mysql->net.pvio;
    hook_data->orig_mysql->net.pvio= hook_data->new_mysql->net.pvio;
  }
  else
    hook_data->orig_mysql->net.pvio= hook_data->orig_pvio;
}

my_bool STDCALL mariadb_reconnect(MYSQL *mysql)
{
  MYSQL tmp_mysql;
  struct my_hook_data hook_data;
  struct mysql_async_context *ctxt= NULL;
  LIST *li_stmt= mysql->stmts;

  /* check if connection handler is active */
  if (IS_CONNHDLR_ACTIVE(mysql))
  {
    if (mysql->extension->conn_hdlr->plugin && mysql->extension->conn_hdlr->plugin->reconnect)
      return(mysql->extension->conn_hdlr->plugin->reconnect(mysql));
  }

  if (!mysql->options.reconnect ||
      (mysql->server_status & SERVER_STATUS_IN_TRANS) || !mysql->host_info)
  {
   /* Allow reconnect next time */
    mysql->server_status&= ~SERVER_STATUS_IN_TRANS;
    my_set_error(mysql, CR_SERVER_GONE_ERROR, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (!mysql_init(&tmp_mysql))
  {
    /* extensions may have failed to allocate */
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    tmp_mysql.free_me= 0;
    mysql_close(&tmp_mysql);
    return(1);
  }
  tmp_mysql.free_me= 0;
  tmp_mysql.options=mysql->options;
  if (mysql->extension->conn_hdlr)
  {
    tmp_mysql.extension->conn_hdlr= mysql->extension->conn_hdlr;
    mysql->extension->conn_hdlr= 0;
  }

  /* don't reread options from configuration files */
  tmp_mysql.options.my_cnf_group= tmp_mysql.options.my_cnf_file= NULL;
  if (IS_MYSQL_ASYNC_ACTIVE(mysql))
  {
    ctxt= mysql->options.extension->async_context;
    hook_data.orig_mysql= mysql;
    hook_data.new_mysql= &tmp_mysql;
    hook_data.orig_pvio= mysql->net.pvio;
    my_context_install_suspend_resume_hook(ctxt, my_suspend_hook, &hook_data);
  }

  if (!mysql_real_connect(&tmp_mysql,
        mysql->options.host ? NULL : mysql->host,
        mysql->user,mysql->passwd,
			  mysql->db, mysql->port, mysql->unix_socket,
			  mysql->client_flag | CLIENT_REMEMBER_OPTIONS) ||
      mysql_set_character_set(&tmp_mysql, mysql->charset->csname))
  {
    if (ctxt)
      my_context_install_suspend_resume_hook(ctxt, NULL, NULL);
    /* don't free options (CONC-118) */
    memset(&tmp_mysql.options, 0, sizeof(struct st_mysql_options));
    my_set_error(mysql, tmp_mysql.net.last_errno,
                        tmp_mysql.net.sqlstate,
                        tmp_mysql.net.last_error);
    mysql_close(&tmp_mysql);
    return(1);
  }

  for (;li_stmt;li_stmt= li_stmt->next)
  {
    MYSQL_STMT *stmt= (MYSQL_STMT *)li_stmt->data;

    if (stmt->state != MYSQL_STMT_INITTED)
    {
      stmt->state= MYSQL_STMT_INITTED;
      stmt_set_error(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    }
  }

  tmp_mysql.free_me= mysql->free_me;
  tmp_mysql.stmts= mysql->stmts;
  mysql->stmts= NULL;

  if (ctxt)
    my_context_install_suspend_resume_hook(ctxt, NULL, NULL);
  /* Don't free options, we moved them to tmp_mysql */
  memset(&mysql->options, 0, sizeof(mysql->options));
  mysql->free_me=0;
  mysql_close(mysql);
  *mysql=tmp_mysql;
  mysql->net.pvio->mysql= mysql;
#ifdef HAVE_TLS
  /* CONC-604: Set new connection handle */
  if (mysql_get_ssl_cipher(mysql))
    ma_pvio_tls_set_connection(mysql);
#endif
  ma_net_clear(&mysql->net);
  mysql->affected_rows= ~(unsigned long long) 0;
  mysql->info= 0;
  return(0);
}

void ma_invalidate_stmts(MYSQL *mysql, const char *function_name)
{
  if (mysql->stmts)
  {
    LIST *li_stmt= mysql->stmts;

    for (; li_stmt; li_stmt= li_stmt->next)
    {
      MYSQL_STMT *stmt= (MYSQL_STMT *)li_stmt->data;
      stmt->mysql= NULL;
      stmt_set_error(stmt, CR_STMT_CLOSED, SQLSTATE_UNKNOWN, 0, function_name);
    }
    mysql->stmts= NULL;
  }
}

/*
  Legacy support of the MariaDB 5.5 version, where timeouts where only in
  seconds resolution. Applications that use this will be asked to set a timeout
  at the nearest higher whole-seconds value.
*/
unsigned int STDCALL
mysql_get_timeout_value(const MYSQL *mysql)
{
  unsigned int timeout= 0;

  if (mysql->options.extension && mysql->options.extension->async_context)
    timeout= mysql->options.extension->async_context->timeout_value;
  /* Avoid overflow. */
  if (timeout > UINT_MAX - 999)
    return (timeout - 1)/1000 + 1;
  else
    return (timeout+999)/1000;
}


unsigned int STDCALL
mysql_get_timeout_value_ms(const MYSQL *mysql)
{
  if (mysql->options.extension && mysql->options.extension->async_context)
    return mysql->options.extension->async_context->timeout_value;
  return 0;
}

/**************************************************************************
** Change user and database
**************************************************************************/

my_bool	STDCALL mysql_change_user(MYSQL *mysql, const char *user,
				  const char *passwd, const char *db)
{
  const MARIADB_CHARSET_INFO *s_cs= mysql->charset;
  char *s_user= mysql->user,
       *s_passwd= mysql->passwd,
       *s_db= mysql->db;
  int rc;

  if (mysql->options.charset_name)
    mysql->charset= mysql_find_charset_name(mysql->options.charset_name);
  else
    mysql->charset=mysql_find_charset_name(MARIADB_DEFAULT_CHARSET);

  mysql->user= strdup(user ? user : "");
  mysql->passwd= strdup(passwd ? passwd : "");

  /* db will be set in run_plugin_auth */
  mysql->db= 0;
  rc= run_plugin_auth(mysql, 0, 0, 0, db);

  /* COM_CHANGE_USER always releases prepared statements, so we need to invalidate them */
  ma_invalidate_stmts(mysql, "mysql_change_user()");

  if (rc==0)
  {
    free(s_user);
    free(s_passwd);
    free(s_db);

    if (!mysql->db && db && !(mysql->db= strdup(db)))
    {
      SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      rc= 1;
    }
  } else
  {
    free(mysql->user);
    free(mysql->passwd);
    free(mysql->db);

    mysql->user= s_user;
    mysql->passwd= s_passwd;
    mysql->db= s_db;
    mysql->charset= s_cs;
  }
  return(rc);
}


/**************************************************************************
** Set current database
**************************************************************************/

int STDCALL
mysql_select_db(MYSQL *mysql, const char *db)
{
  int error;

  if (!db)
    return 1;

  if ((error=ma_simple_command(mysql, COM_INIT_DB, db,
                               (uint) strlen(db),0,0)))
    return(error);
  free(mysql->db);
  mysql->db=strdup(db);
  return(0);
}


/*************************************************************************
** Send a QUIT to the server and close the connection
** If handle is allocated by mysql connect free it.
*************************************************************************/

static void mysql_close_options(MYSQL *mysql)
{
  if (mysql->options.init_command)
  {
    char **begin= (char **)mysql->options.init_command->buffer;
    char **end= begin + mysql->options.init_command->elements;

    for (;begin < end; begin++)
      free(*begin);
    ma_delete_dynamic(mysql->options.init_command);
    free(mysql->options.init_command);
  }
  free(mysql->options.user);
  free(mysql->options.host);
  free(mysql->options.password);
  free(mysql->options.unix_socket);
  free(mysql->options.db);
  free(mysql->options.my_cnf_file);
  free(mysql->options.my_cnf_group);
  free(mysql->options.charset_dir);
  free(mysql->options.charset_name);
  free(mysql->options.bind_address);
  free(mysql->options.ssl_key);
  free(mysql->options.ssl_cert);
  free(mysql->options.ssl_ca);
  free(mysql->options.ssl_capath);
  free(mysql->options.ssl_cipher);

  if (mysql->options.extension)
  {
    struct mysql_async_context *ctxt;
    if ((ctxt = mysql->options.extension->async_context))
    {
      my_context_destroy(&ctxt->async_context);
      free(ctxt);
      mysql->options.extension->async_context= 0;
    }
    free(mysql->options.extension->plugin_dir);
    free(mysql->options.extension->default_auth);
    free(mysql->options.extension->db_driver);
    free(mysql->options.extension->ssl_crl);
    free(mysql->options.extension->ssl_crlpath);
    free(mysql->options.extension->tls_fp);
    free(mysql->options.extension->tls_fp_list);
    free(mysql->options.extension->tls_pw);
    free(mysql->options.extension->tls_version);
    free(mysql->options.extension->url);
    free(mysql->options.extension->connection_handler);
    free(mysql->options.extension->proxy_header);
    if(ma_hashtbl_inited(&mysql->options.extension->connect_attrs))
      ma_hashtbl_free(&mysql->options.extension->connect_attrs);
    if (ma_hashtbl_inited(&mysql->options.extension->userdata))
      ma_hashtbl_free(&mysql->options.extension->userdata);
    free(mysql->options.extension->restricted_auth);
    free(mysql->options.extension->rpl_host);

  }
  free(mysql->options.extension);
  /* clear all pointer */
  memset(&mysql->options, 0, sizeof(mysql->options));
}

static void mysql_close_memory(MYSQL *mysql)
{
  ma_clear_session_state(mysql);
  free(mysql->host_info);
  free(mysql->host);
  free(mysql->user);
  free(mysql->passwd);
  free(mysql->db);
  free(mysql->unix_socket);
  free(mysql->server_version);
  mysql->host_info= mysql->host= mysql->unix_socket=
                    mysql->server_version=mysql->user=mysql->passwd=mysql->db=0;
}

void my_set_error(MYSQL *mysql,
                  unsigned int error_nr,
                  const char *sqlstate,
                  const char *format,
                  ...)
{
  va_list ap;

  const char *errmsg= format;

  mysql->net.last_errno= error_nr;
  ma_strmake(mysql->net.sqlstate, sqlstate, SQLSTATE_LENGTH);

  if (!format)
  {
    if (IS_MYSQL_ERROR(error_nr) || IS_MARIADB_ERROR(error_nr))
      errmsg= ER(error_nr);
    else {
      snprintf(mysql->net.last_error, MYSQL_ERRMSG_SIZE - 1,
               ER_UNKNOWN_ERROR_CODE, error_nr);
      return;
    }
  }

  va_start(ap, format);
  vsnprintf(mysql->net.last_error, MYSQL_ERRMSG_SIZE - 1, errmsg, ap);
  va_end(ap);
  return;
}

void mysql_close_slow_part(MYSQL *mysql)
{
  free_old_query(mysql);
  mysql->status=MYSQL_STATUS_READY; /* Force command */
  mysql->options.reconnect=0;
  if (mysql->net.pvio && mysql->net.buff)
    ma_simple_command(mysql, COM_QUIT,NullS,0,1,0);
  end_server(mysql);

  /* there is an ongoing async operation */
  if (mysql->options.extension && mysql->options.extension->async_context)
  {
    if (mysql->options.extension->async_context->pending_gai_res)
    {
        freeaddrinfo(mysql->options.extension->async_context->pending_gai_res);
        mysql->options.extension->async_context->pending_gai_res = 0;
    }
    if (mysql->options.extension->async_context->pvio)
    {
      ma_pvio_close(mysql->options.extension->async_context->pvio);
      mysql->options.extension->async_context->pvio = 0;
    }
  }
}

static void ma_clear_session_state(MYSQL *mysql)
{
  uint i;

  if (!mysql || !mysql->extension)
    return;

  for (i= SESSION_TRACK_BEGIN; i <= SESSION_TRACK_END; i++)
  {
    list_free(mysql->extension->session_state[i].list, 0);
  }
  memset(mysql->extension->session_state, 0, sizeof(struct st_mariadb_session_state) * SESSION_TRACK_TYPES);
}

void STDCALL
mysql_close(MYSQL *mysql)
{
  if (mysql)					/* Some simple safety */
  {
    if (mysql->extension && mysql->extension->conn_hdlr)
    {
      MA_CONNECTION_HANDLER *p= mysql->extension->conn_hdlr;
      if (p->plugin->close)
        p->plugin->close(mysql);
      free(p);
      /* Fix for CONC-294: Since we already called plugin->close function
         we need to prevent that mysql_close_slow_part (which sends COM_QUIT
         to the server) will be handled by plugin again. */
      mysql->extension->conn_hdlr= NULL;
    }

    if (mysql->methods)
      mysql->methods->db_close(mysql);

    /* reset the connection in all active statements */
    ma_invalidate_stmts(mysql, "mysql_close()");

    mysql_close_memory(mysql);
    mysql_close_options(mysql);
    ma_clear_session_state(mysql);
    reset_tls_error(mysql);

    if (mysql->net.extension)
    {
      if (compression_plugin(&mysql->net))
        compression_plugin(&mysql->net)->free_ctx(compression_ctx(&mysql->net));
      free(mysql->net.extension);
    }

    mysql->host_info=mysql->user=mysql->passwd=mysql->db=0;

    /* Clear pointers for better safety */
    memset((char*) &mysql->options, 0, sizeof(mysql->options));

    if (mysql->extension)
      free(mysql->extension);

    /* Clear pointers for better safety */
    mysql->net.extension = NULL;
    mysql->extension = NULL;

    mysql->net.pvio= 0;
    if (mysql->free_me)
      free(mysql);
  }
  return;
}


/**************************************************************************
** Do a query. If query returned rows, free old rows.
** Read data by mysql_store_result or by repeating calls to mysql_fetch_row
**************************************************************************/

int STDCALL
mysql_query(MYSQL *mysql, const char *query)
{
  return mysql_real_query(mysql,query, (unsigned long) strlen(query));
}

/*
  Send the query and return so we can do something else.
  Needs to be followed by mysql_read_query_result() when we want to
  finish processing it.
*/



int STDCALL
mysql_send_query(MYSQL* mysql, const char* query, unsigned long length)
{
  return ma_simple_command(mysql, COM_QUERY, query, length, 1,0);
}

void ma_save_session_track_info(void *ptr, enum enum_mariadb_status_info type, ...)
{
  MYSQL *mysql= (MYSQL *)ptr;
  enum enum_session_state_type track_type;
  va_list ap;

  DBUG_ASSERT(mysql != NULL);

  /* We only handle SESSION_TRACK_TYPE here */
  if (type != SESSION_TRACK_TYPE)
    return;

  va_start(ap, type);

  track_type= va_arg(ap, enum enum_session_state_type);

  switch (track_type) {
  case SESSION_TRACK_SCHEMA:
  case SESSION_TRACK_STATE_CHANGE:
  case SESSION_TRACK_TRANSACTION_CHARACTERISTICS:
  case SESSION_TRACK_TRANSACTION_STATE:
  case SESSION_TRACK_GTIDS:
  case SESSION_TRACK_SYSTEM_VARIABLES:
    {
      LIST *session_item;
      MYSQL_LEX_STRING *str;
      char *tmp;
      MARIADB_CONST_STRING *data1= va_arg(ap, MARIADB_CONST_STRING *);

      if (!(session_item= ma_multi_malloc(0,
                          &session_item, sizeof(LIST),
                          &str, sizeof(MYSQL_LEX_STRING),
                          &tmp, data1->length,
                          NULL)))
        goto mem_error;

      str->str= tmp;
      memcpy(str->str, data1->str, data1->length);
      str->length= data1->length;
      session_item->data= str;
      mysql->extension->session_state[track_type].list= list_add(mysql->extension->session_state[track_type].list,
                                                                 session_item);
      if (track_type == SESSION_TRACK_SYSTEM_VARIABLES)
      {
        MARIADB_CONST_STRING *data2= va_arg(ap, MARIADB_CONST_STRING *);
        if (!(session_item= ma_multi_malloc(0,
                          &session_item, sizeof(LIST),
                          &str, sizeof(MYSQL_LEX_STRING),
                          &tmp, data2->length,
                          NULL)))
          goto mem_error;

        str->str= tmp;
        memcpy(str->str, data2->str, data2->length);
        str->length= data2->length;
        session_item->data= str;
        mysql->extension->session_state[track_type].list= list_add(mysql->extension->session_state[track_type].list,
                                                                 session_item);
      }
    }
    break;
  }
  return;

mem_error:
  SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
  return; 
}

int ma_read_ok_packet(MYSQL *mysql, uchar *pos, ulong length)
{
  uchar *end= mysql->net.read_pos+length;
  size_t item_len;
  unsigned int last_status= mysql->server_status;
  mysql->affected_rows= net_field_length_ll(&pos);
  mysql->insert_id=	  net_field_length_ll(&pos);

  if (ma_check_buffer_boundaries(mysql, pos, length, 2))
    goto corrupted;
  mysql->server_status=uint2korr(pos);

  /* clear error */
  mysql->net.last_error[0]= mysql->net.last_errno= mysql->net.extension->extended_errno= 0;

  /* callback */
  ma_status_callback(mysql, last_status);
  pos+=2;
  if (ma_check_buffer_boundaries(mysql, pos, length, 2))
    goto corrupted;
  mysql->warning_count=uint2korr(pos);
  pos+=2;
  if (pos > end)
    goto corrupted;
  if (pos < end)
  {
    if ((item_len= net_field_length(&pos)))
      mysql->info=(char*) pos;
    if (ma_check_buffer_boundaries(mysql, pos, length, item_len))
      goto corrupted;

    /* check if server supports session tracking */
    if (mysql->server_capabilities & CLIENT_SESSION_TRACKING)
    {
      ma_clear_session_state(mysql);
      pos+= item_len;

      if (mysql->server_status & SERVER_SESSION_STATE_CHANGED)
      {
        int i;
        if (pos < end)
        {
          enum enum_session_state_type si_type;
          uchar *old_pos= pos;

          item_len= net_field_length(&pos);  /* length for all items */
          if (ma_check_buffer_boundaries(mysql, pos, length, item_len))
            goto corrupted;
          end= pos + item_len;

          /* length was already set, so make sure that info will be zero terminated */
          if (mysql->info)
            *old_pos= 0;

          while (pos < end)
          {
            size_t plen;
            MARIADB_CONST_STRING data1, data2;
            si_type= (enum enum_session_state_type)net_field_length(&pos);

            switch(si_type) {
            case SESSION_TRACK_SCHEMA:
            case SESSION_TRACK_STATE_CHANGE:
            case SESSION_TRACK_TRANSACTION_CHARACTERISTICS:
            case SESSION_TRACK_SYSTEM_VARIABLES:
            case SESSION_TRACK_TRANSACTION_STATE:
            case SESSION_TRACK_GTIDS:
              if (si_type != SESSION_TRACK_STATE_CHANGE)
              {
                net_field_length(&pos); /* ignore total length, item length will follow next */
              }
              if (si_type == SESSION_TRACK_GTIDS)
              {
                /* skip encoding */
                net_field_length(&pos);
              }
              plen= net_field_length(&pos);
              if (ma_check_buffer_boundaries(mysql, pos, length, plen))
                goto corrupted;

              data1.str= (char *)pos;
              data1.length= plen;
              if (si_type != SESSION_TRACK_SYSTEM_VARIABLES)
              {
                 mysql->options.extension->status_callback(mysql->options.extension->status_data,
                                                           SESSION_TRACK_TYPE, si_type,
                                                           &data1);
                 if (mysql->net.last_errno)
                   goto oom;
              }
              pos+= plen;
              /* in case schema has changed, we have to update mysql->db */
              if (si_type == SESSION_TRACK_SCHEMA)
              {
                free(mysql->db);
                mysql->db= malloc(plen + 1);
                memcpy(mysql->db, data1.str, data1.length);
                mysql->db[data1.length]= 0;
              }
              else if (si_type == SESSION_TRACK_SYSTEM_VARIABLES)
              {
                my_bool set_charset= 0;
                /* make sure that we update charset in case it has changed */
                if (!strncmp(data1.str, "character_set_client", plen))
                  set_charset= 1;
                plen= net_field_length(&pos);
                if (ma_check_buffer_boundaries(mysql, pos, length, plen))
                  goto corrupted;
                data2.str= (char *)pos;
                data2.length= plen;

                mysql->options.extension->status_callback(mysql->options.extension->status_data,
                                                          SESSION_TRACK_TYPE, si_type,
                                                          &data1, &data2);
                 if (mysql->net.last_errno)
                   goto oom;

                pos+= plen;
                if (set_charset && plen < CHARSET_NAME_LEN &&
                    strncmp(mysql->charset->csname, data2.str, data2.length) != 0)
                {
                  char cs_name[CHARSET_NAME_LEN];
                  const MARIADB_CHARSET_INFO *cs_info;
                  memcpy(cs_name, data2.str, data2.length);
                  cs_name[plen]= 0;
                  if ((cs_info = mysql_find_charset_name(cs_name)))
                    mysql->charset= cs_info;
                }
              }
              break;
            default:
              /* not supported yet */
              plen= net_field_length(&pos);
              if (ma_check_buffer_boundaries(mysql, pos, length, plen))
                goto corrupted;
              pos+= plen;
              break;
            }
          }
        }
        for (i= SESSION_TRACK_BEGIN; i <= SESSION_TRACK_END; i++)
        {
          mysql->extension->session_state[i].list= list_reverse(mysql->extension->session_state[i].list);
          mysql->extension->session_state[i].current= mysql->extension->session_state[i].list;
        }
      }
    }
  }
  /* CONC-351: clear session state information */
  else if (mysql->server_capabilities & CLIENT_SESSION_TRACKING)
    ma_clear_session_state(mysql);
  return(0);

oom:
  ma_clear_session_state(mysql);
  SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
  return -1;

corrupted:
  ma_clear_session_state(mysql);
  SET_CLIENT_ERROR(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
  return -1;
}


static int ma_deep_copy_field(const MYSQL_FIELD *src, MYSQL_FIELD *dst,
                              MA_MEM_ROOT *r)
{
#define MA_STRDUP(f)                                                          \
  do                                                                          \
  {                                                                           \
    if (src->f)                                                       \
    {                                                                         \
      if ((dst->f= ma_strdup_root(r, src->f)) == NULL)                          \
        return -1;                                                            \
    }                                                                         \
    else                                                                      \
    {                                                                         \
      dst->f= NULL;                                                           \
    }                                                                         \
  }                                                                           \
  while (0)


  MA_STRDUP(catalog);
  MA_STRDUP(db);
  MA_STRDUP(def);
  MA_STRDUP(name);
  MA_STRDUP(org_name);
  MA_STRDUP(org_table);
  MA_STRDUP(table);
#undef MA_STRDUP

  dst->catalog_length= src->catalog_length;
  dst->charsetnr= src->charsetnr;
  dst->db_length= src->db_length;
  dst->decimals= src->decimals;
  dst->def_length= src->def_length;
  dst->extension=
      src->extension
          ? ma_field_extension_deep_dup(r,
                                        src->extension)
          : NULL;
  dst->flags= src->flags;
  dst->length= src->length;
  dst->max_length = src->max_length;
  dst->name_length= src->name_length;
  dst->org_name_length= src->org_name_length;
  dst->org_table_length= src->org_table_length;
  dst->table_length= src->table_length;
  dst->type= src->type;
  return 0;
}


MYSQL_FIELD *ma_duplicate_resultset_metadata(MYSQL_FIELD *fields, size_t count,
                                             MA_MEM_ROOT *memroot)
{
  size_t i;
  MYSQL_FIELD *result=
      (MYSQL_FIELD *) ma_alloc_root(memroot, sizeof(MYSQL_FIELD) * count);
  if (!result)
    return NULL;

  for (i= 0; i < count; i++)
  {
    if (ma_deep_copy_field(&fields[i], &result[i], memroot))
      return NULL;
  }
  return result;
}


int mthd_my_read_query_result(MYSQL *mysql)
{
  uchar *pos;
  ulong field_count;
  MYSQL_DATA *fields;
  ulong length;
  const uchar *end;
  uchar has_metadata;

  my_bool can_local_infile= (mysql->options.extension) && (mysql->extension->auto_local_infile != WAIT_FOR_QUERY);

  if (mysql->options.extension && mysql->extension->auto_local_infile == ACCEPT_FILE_REQUEST)
    mysql->extension->auto_local_infile= WAIT_FOR_QUERY;

  if ((length = ma_net_safe_read(mysql)) == packet_error)
  {
    return(1);
  }
  free_old_query(mysql);			/* Free old result */
get_info:
  pos=(uchar*) mysql->net.read_pos;
  end= pos + length;
  if ((field_count= net_field_length(&pos)) == 0)
    return ma_read_ok_packet(mysql, pos, length);
  if (field_count == NULL_LENGTH)		/* LOAD DATA LOCAL INFILE */
  {
    int error=mysql_handle_local_infile(mysql, (char *)pos, can_local_infile);

    if ((length=ma_net_safe_read(mysql)) == packet_error || error)
      return(-1);
    goto get_info;				/* Get info packet */
  }

  has_metadata= 1;
  if (ma_supports_cache_metadata(mysql))
  {
    assert(mysql->fields == NULL);
    if (pos < end)
    {
      has_metadata= *pos;
      pos++;
    }
  }

  if (!(mysql->server_status & SERVER_STATUS_AUTOCOMMIT))
    mysql->server_status|= SERVER_STATUS_IN_TRANS;

  if (has_metadata)
  {
    if (!(fields= mysql->methods->db_read_rows(mysql, (MYSQL_FIELD *) 0,
                                               ma_result_set_rows(mysql))))
      return (-1);
    if (!(mysql->fields= unpack_fields(mysql, fields, &mysql->field_alloc,
                                       (uint) field_count, 1)))
      return (-1);
  }
  else
  {
    unsigned int last_status= mysql->server_status;
    /* Read EOF, to get the status and warning count.  */
    if ((length= ma_net_safe_read(mysql)) == packet_error)
    {
      return -1;
    }
    pos= (uchar *) mysql->net.read_pos;
    if (length != 5 || pos[0] != 0xfe)
    {
      return -1;
    }
    mysql->warning_count= uint2korr(pos + 1);
    mysql->server_status= uint2korr(pos + 3);
    ma_status_callback(mysql, last_status);
  }
  mysql->status=MYSQL_STATUS_GET_RESULT;
  mysql->field_count=field_count;
  return(0);
}

int STDCALL mysql_session_track_get_next(MYSQL *mysql, enum enum_session_state_type type,
                                         const char **data, size_t *length)
{
  MYSQL_LEX_STRING *str;
  if (!mysql->extension->session_state[type].current)
    return 1;

  str= (MYSQL_LEX_STRING *)mysql->extension->session_state[type].current->data;
  mysql->extension->session_state[type].current= mysql->extension->session_state[type].current->next;

  *data= str->str ? str->str : NULL;
  *length= str->str ? str->length : 0;
  return 0;
}

int STDCALL mysql_session_track_get_first(MYSQL *mysql, enum enum_session_state_type type,
                                          const char **data, size_t *length)
{
  mysql->extension->session_state[type].current= mysql->extension->session_state[type].list;
  return mysql_session_track_get_next(mysql, type, data, length);
}

my_bool STDCALL
mysql_read_query_result(MYSQL *mysql)
{
  return test(mysql->methods->db_read_query_result(mysql)) ? 1 : 0;
}

int STDCALL
mysql_real_query(MYSQL *mysql, const char *query, unsigned long length)
{
  my_bool skip_result= OPT_EXT_VAL(mysql, multi_command);

  if (length == (unsigned long)-1)
    length= (unsigned long)strlen(query);

  free_old_query(mysql);

  if (ma_simple_command(mysql, COM_QUERY,query,length,1,0))
    return(-1);
  if (!skip_result && !mysql->options.extension->skip_read_response)
    return(mysql->methods->db_read_query_result(mysql));
  return(0);
}

/**************************************************************************
** Alloc result struct for buffered results. All rows are read to buffer.
** mysql_data_seek may be used.
**************************************************************************/

MYSQL_RES * STDCALL
mysql_store_result(MYSQL *mysql)
{
  MYSQL_RES *result;

  if (!mysql->fields)
    return(0);
  if (mysql->status != MYSQL_STATUS_GET_RESULT)
  {
    SET_CLIENT_ERROR(mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(0);
  }
  mysql->status=MYSQL_STATUS_READY;		/* server is ready */
  if (!(result=(MYSQL_RES*) calloc(1, sizeof(MYSQL_RES)+
				       sizeof(ulong)*mysql->field_count)))
  {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return(0);
  }
  result->eof=1;				/* Marker for buffered */
  result->lengths=(ulong*) (result+1);
  if (!(result->data=mysql->methods->db_read_rows(mysql,mysql->fields,mysql->field_count)))
  {
    free(result);
    return(0);
  }
  mysql->affected_rows= result->row_count= result->data->rows;
  result->data_cursor=	result->data->data;
  result->fields=	mysql->fields;
  result->field_alloc=	mysql->field_alloc;
  result->field_count=	mysql->field_count;
  result->current_field=0;
  result->current_row=0;			/* Must do a fetch first */
  mysql->fields=0;				/* fields is now in result */
  return(result);				/* Data fetched */
}


/**************************************************************************
** Alloc struct for use with unbuffered reads. Data is fetched by demand
** when calling to mysql_fetch_row.
** mysql_data_seek is a noop.
**
** No other queries may be specified with the same MYSQL handle.
** There shouldn't be much processing per row because mysql server shouldn't
** have to wait for the client (and will not wait more than 30 sec/packet).
**************************************************************************/

MYSQL_RES * STDCALL
mysql_use_result(MYSQL *mysql)
{
  MYSQL_RES *result;

  if (!mysql->fields)
    return(0);
  if (mysql->status != MYSQL_STATUS_GET_RESULT)
  {
    SET_CLIENT_ERROR(mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(0);
  }
  if (!(result=(MYSQL_RES*) calloc(1, sizeof(*result)+
				      sizeof(ulong)*mysql->field_count)))
  {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return(0);
  }
  result->lengths=(ulong*) (result+1);
  if (!(result->row=(MYSQL_ROW)
	malloc(sizeof(result->row[0])*(mysql->field_count+1))))
  {					/* Ptrs: to one row */
    free(result);
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return(0);
  }
  result->fields=	mysql->fields;
  result->field_alloc=	mysql->field_alloc;
  result->field_count=	mysql->field_count;
  result->current_field=0;
  result->handle=	mysql;
  result->current_row=	0;
  mysql->fields=0;			/* fields is now in result */
  mysql->status=MYSQL_STATUS_USE_RESULT;
  return(result);			/* Data is read to be fetched */
}

/**************************************************************************
** Return next field of the query results
**************************************************************************/
MYSQL_FIELD * STDCALL
mysql_fetch_field(MYSQL_RES *result)
{
  if (result->current_field >= result->field_count)
    return(NULL);
  return &result->fields[result->current_field++];
}


/**************************************************************************
** Return mysql field metadata
**************************************************************************/
int STDCALL
mariadb_field_attr(MARIADB_CONST_STRING *attr,
                   const MYSQL_FIELD *field,
                   enum mariadb_field_attr_t type)
{
  MA_FIELD_EXTENSION *ext= (MA_FIELD_EXTENSION*) field->extension;
  if (!ext || type > MARIADB_FIELD_ATTR_LAST)
  {
    *attr= null_const_string;
    return 1;
  }
  *attr= ext->metadata[type];
  return 0;
}


/**************************************************************************
**  Return next row of the query results
**************************************************************************/
MYSQL_ROW STDCALL
mysql_fetch_row(MYSQL_RES *res)
{
  if (!res)
    return 0;
  if (res->handle)
  {
    if (res->handle->status != MYSQL_STATUS_USE_RESULT &&
        res->handle->status != MYSQL_STATUS_GET_RESULT)
      return 0;
  }
  if (!res->data)
  {						/* Unbufferred fetch */
    if (!res->eof && res->handle)
    {
      if (!(res->handle->methods->db_read_one_row(res->handle,res->field_count,res->row, res->lengths)))
      {
        res->row_count++;
        return(res->current_row=res->row);
      }
      res->eof=1;
      res->handle->status=MYSQL_STATUS_READY;
       /* Don't clear handle in mysql_free_results */
      res->handle=0;
    }
    return((MYSQL_ROW) NULL);
  }
  {
    MYSQL_ROW tmp;
    if (!res->data_cursor)
    {
      return(res->current_row=(MYSQL_ROW) NULL);
    }
    tmp = res->data_cursor->data;
    res->data_cursor = res->data_cursor->next;
    return(res->current_row=tmp);
  }
}

/**************************************************************************
** Get column lengths of the current row
** If one uses mysql_use_result, res->lengths contains the length information,
** else the lengths are calculated from the offset between pointers.
**************************************************************************/

ulong * STDCALL
mysql_fetch_lengths(MYSQL_RES *res)
{
  ulong *lengths,*prev_length;
  char *start;
  MYSQL_ROW column,end;

  if (!(column=res->current_row))
    return 0;					/* Something is wrong */
  if (res->data)
  {
    start=0;
    prev_length=0;				/* Keep gcc happy */
    lengths=res->lengths;
    for (end=column+res->field_count+1 ; column != end ; column++,lengths++)
    {
      if (!*column)
      {
	*lengths=0;				/* Null */
	continue;
      }
      if (start)				/* Found end of prev string */
	*prev_length= (uint) (*column-start-1);
      start= *column;
      prev_length=lengths;
    }
  }
  return res->lengths;
}

/**************************************************************************
** Move to a specific row and column
**************************************************************************/

void STDCALL
mysql_data_seek(MYSQL_RES *result, unsigned long long row)
{
  MYSQL_ROWS	*tmp=0;
  if (result->data)
    for (tmp=result->data->data; row-- && tmp ; tmp = tmp->next) ;
  result->current_row=0;
  result->data_cursor = tmp;
}

/*************************************************************************
** put the row or field cursor one a position one got from mysql_row_tell()
** This doesn't restore any data. The next mysql_fetch_row or
** mysql_fetch_field will return the next row or field after the last used
*************************************************************************/

MYSQL_ROW_OFFSET STDCALL
mysql_row_seek(MYSQL_RES *result, MYSQL_ROW_OFFSET row)
{
  MYSQL_ROW_OFFSET return_value=result->data_cursor;
  result->current_row= 0;
  result->data_cursor= row;
  return return_value;
}


MYSQL_FIELD_OFFSET STDCALL
mysql_field_seek(MYSQL_RES *result, MYSQL_FIELD_OFFSET field_offset)
{
  MYSQL_FIELD_OFFSET return_value=result->current_field;
  result->current_field=field_offset;
  return return_value;
}

/********************************************************
 Warning: mysql_list_dbs is deprecated and will be
          removed. Use SQL statement "SHOW DATABASES"
          instead
 ********************************************************/

/*****************************************************************************
** List all databases
*****************************************************************************/

MYSQL_RES * STDCALL
mysql_list_dbs(MYSQL *mysql, const char *wild)
{
  char buff[255];
  snprintf(buff, 255, "SHOW DATABASES LIKE '%s'", wild ? wild : "%");
  if (mysql_query(mysql,buff))
    return(0);
  return (mysql_store_result(mysql));
}


/********************************************************
 Warning: mysql_list_tables is deprecated and will be
          removed. Use SQL statement "SHOW TABLES"
          instead
 ********************************************************/
/*****************************************************************************
** List all tables in a database
** If wild is given then only the tables matching wild are returned
*****************************************************************************/

MYSQL_RES * STDCALL
mysql_list_tables(MYSQL *mysql, const char *wild)
{
  char buff[255];

  snprintf(buff, 255, "SHOW TABLES LIKE '%s'", wild ? wild : "%");
  if (mysql_query(mysql,buff))
    return(0);
  return (mysql_store_result(mysql));
}


/**************************************************************************
** List all fields in a table
** If wild is given then only the fields matching wild are returned
** Instead of this use query:
** show fields in 'table' like "wild"
**************************************************************************/

MYSQL_RES * STDCALL
mysql_list_fields(MYSQL *mysql, const char *table, const char *wild)
{
  MYSQL_RES *result;
  MYSQL_DATA *query;
  char buff[255];
  int length= 0;

  LINT_INIT(query);

  length= snprintf(buff, 128, "%s%c%s", table, '\0', wild ? wild : "");

  if (ma_simple_command(mysql, COM_FIELD_LIST,buff,length,1,0) ||
      !(query = mysql->methods->db_read_rows(mysql,(MYSQL_FIELD*) 0,
                                             ma_result_set_rows(mysql))))
    return(NULL);

  free_old_query(mysql);
  if (!(result = (MYSQL_RES *) calloc(1, sizeof(MYSQL_RES))))
  {
    free_rows(query);
    return(NULL);
  }
  result->field_alloc=mysql->field_alloc;
  mysql->fields=0;
  result->eof=1;
  result->field_count = (uint) query->rows;
  result->fields= unpack_fields(mysql, query, &result->field_alloc,
				result->field_count, 1);
  if (result->fields)
    return(result);

  free(result);
  return(NULL);
}

/********************************************************
 Warning: mysql_list_processes is deprecated and will be
          removed. Use SQL statement "SHOW PROCESSLIST"
          instead
 ********************************************************/

/* List all running processes (threads) in server */

MYSQL_RES * STDCALL
mysql_list_processes(MYSQL *mysql)
{
  MYSQL_DATA *fields;
  uint field_count;
  uchar *pos;

  LINT_INIT(fields);
  if (ma_simple_command(mysql, COM_PROCESS_INFO,0,0,0,0))
    return(NULL);
  free_old_query(mysql);
  pos=(uchar*) mysql->net.read_pos;
  field_count=(uint) net_field_length(&pos);
  if (!(fields = mysql->methods->db_read_rows(mysql,(MYSQL_FIELD*) 0,7)))
    return(NULL);
  if (!(mysql->fields= unpack_fields(mysql, fields, &mysql->field_alloc,
                                     field_count, 0)))
    return(NULL);
  mysql->status=MYSQL_STATUS_GET_RESULT;
  mysql->field_count=field_count;
  return(mysql_store_result(mysql));
}

/* In 5.0 this version became an additional parameter shutdown_level */
int STDCALL
mysql_shutdown(MYSQL *mysql, enum mysql_enum_shutdown_level shutdown_level)
{
  uchar s_level[2];
  s_level[0]= (uchar)shutdown_level;
  return(ma_simple_command(mysql, COM_SHUTDOWN, (char *)s_level, 1, 0, 0));
}

int STDCALL
mysql_refresh(MYSQL *mysql,uint options)
{
  uchar bits[1];
  bits[0]= (uchar) options;
  return(ma_simple_command(mysql, COM_REFRESH,(char*) bits,1,0,0));
}

int STDCALL
mysql_kill(MYSQL *mysql,ulong pid)
{
  char buff[16];

  /* process id can't be larger than 4-bytes */
  if (pid & (~0xFFFFFFFFUL))
  {
    my_set_error(mysql, CR_CONNECTION_ERROR, SQLSTATE_UNKNOWN, 0);
    return 1;
  }

  snprintf(buff, sizeof buff, "KILL %lu", pid);
  return mysql_real_query(mysql, (char *)buff, (ulong)strlen(buff));
}


int STDCALL
mysql_dump_debug_info(MYSQL *mysql)
{
  return(ma_simple_command(mysql, COM_DEBUG,0,0,0,0));
}

char * STDCALL
mysql_stat(MYSQL *mysql)
{
  if (ma_simple_command(mysql, COM_STATISTICS,0,0,0,0))
    return mysql->net.last_error;
  mysql->net.read_pos[mysql->packet_length]=0;	/* End of stat string */
  if (!mysql->net.read_pos[0])
  {
    SET_CLIENT_ERROR(mysql, CR_WRONG_HOST_INFO , SQLSTATE_UNKNOWN, 0);
    return mysql->net.last_error;
  }
  return((char*) mysql->net.read_pos);
}

int STDCALL
mysql_ping(MYSQL *mysql)
{
  int rc;
  rc= ma_simple_command(mysql, COM_PING, 0, 0, 0, 0);
  if (rc && mysql->options.reconnect)
    rc= ma_simple_command(mysql, COM_PING, 0, 0, 0, 0);
  return rc;
}

char * STDCALL
mysql_get_server_info(MYSQL *mysql)
{
  return((char*) mysql->server_version);
}

static size_t mariadb_server_version_id(MYSQL *mysql)
{
  size_t major, minor, patch;
  char *p;

  if (!(p = mysql->server_version)) {
    return 0;
  }

  major = strtol(p, &p, 10);
  p += 1; /* consume the dot */
  minor = strtol(p, &p, 10);
  p += 1; /* consume the dot */
  patch = strtol(p, &p, 10);

  return (major * 10000L + (unsigned long)(minor * 100L + patch));
}

unsigned long STDCALL mysql_get_server_version(MYSQL *mysql)
{
  return (unsigned long)mariadb_server_version_id(mysql);
}

char * STDCALL
mysql_get_host_info(MYSQL *mysql)
{
  return(mysql->host_info);
}

uint STDCALL
mysql_get_proto_info(MYSQL *mysql)
{
  return (mysql->protocol_version);
}

const char * STDCALL
mysql_get_client_info(void)
{
  return (char*) MARIADB_PACKAGE_VERSION;
}

static size_t get_store_length(size_t length)
{
  #define MAX_STORE_SIZE 9
  unsigned char buffer[MAX_STORE_SIZE], *p;

  /* We just store the length and subtract offset of our buffer
     to determine the length */
  p= mysql_net_store_length(buffer, length);
  return p - buffer;
}

uchar *ma_get_hash_keyval(const uchar *hash_entry,
                       unsigned int *length,
                       my_bool not_used __attribute__((unused)))
{
  /* Hash entry has the following format:
     Offset: 0               key (\0 terminated)
             key_length + 1  value (\0 terminated)
  */
  uchar *p= (uchar *)hash_entry;
  size_t len= strlen((char *)p);
  *length= (unsigned int)len;
  return p;
}

void ma_int_hash_free(void *p)
{
  free(p);
}

int
mysql_optionsv(MYSQL *mysql,enum mysql_option option, ...)
{
  va_list ap;
  void *arg1;
  size_t stacksize;
  struct mysql_async_context *ctxt;

  va_start(ap, option);

  arg1= va_arg(ap, void *);

  switch (option) {
  case MYSQL_OPT_CONNECT_TIMEOUT:
    mysql->options.connect_timeout= *(uint*) arg1;
    break;
  case MYSQL_OPT_COMPRESS:
    mysql->options.compress= 1;			/* Remember for connect */
    mysql->options.client_flag|= CLIENT_COMPRESS;
    break;
  case MYSQL_OPT_NAMED_PIPE:
    mysql->options.named_pipe=1;		/* Force named pipe */
    break;
  case MYSQL_OPT_LOCAL_INFILE:			/* Allow LOAD DATA LOCAL ?*/
    if (!arg1 || test(*(unsigned int*) arg1))
      mysql->options.client_flag|= CLIENT_LOCAL_FILES;
    else
      mysql->options.client_flag&= ~CLIENT_LOCAL_FILES;
    if (arg1) {
      CHECK_OPT_EXTENSION_SET(&mysql->options);
      mysql->extension->auto_local_infile= *(uint*)arg1 == LOCAL_INFILE_MODE_AUTO
                                           ? WAIT_FOR_QUERY : ALWAYS_ACCEPT;
    }
    break;
  case MYSQL_INIT_COMMAND:
    options_add_initcommand(&mysql->options, (char *)arg1);
    break;
  case MYSQL_READ_DEFAULT_FILE:
    OPT_SET_VALUE_STR(&mysql->options, my_cnf_file, (char *)arg1);
    break;
  case MYSQL_READ_DEFAULT_GROUP:
    OPT_SET_VALUE_STR(&mysql->options, my_cnf_group, arg1 ? (char *)arg1 : "");
    break;
  case MYSQL_SET_CHARSET_DIR:
    OPT_SET_VALUE_STR(&mysql->options, charset_dir, arg1);
    break;
  case MYSQL_SET_CHARSET_NAME:
    OPT_SET_VALUE_STR(&mysql->options, charset_name, arg1);
    break;
  case MYSQL_OPT_RECONNECT:
    mysql->options.reconnect= *(my_bool *)arg1;
    break;
  case MYSQL_OPT_PROTOCOL:
    mysql->options.protocol= *((uint *)arg1);
    break;
#ifdef _WIN32
  case MYSQL_SHARED_MEMORY_BASE_NAME:
    OPT_SET_VALUE_STR(&mysql->options, shared_memory_base_name, arg1);
    break;
#endif
  case MYSQL_OPT_READ_TIMEOUT:
    mysql->options.read_timeout= *(uint *)arg1;
    break;
  case MYSQL_OPT_WRITE_TIMEOUT:
    mysql->options.write_timeout= *(uint *)arg1;
    break;
  case MYSQL_REPORT_DATA_TRUNCATION:
    mysql->options.report_data_truncation= *(my_bool *)arg1;
    break;
  case MYSQL_PROGRESS_CALLBACK:
    CHECK_OPT_EXTENSION_SET(&mysql->options);
    if (mysql->options.extension)
      mysql->options.extension->report_progress=
        (void (*)(const MYSQL *, uint, uint, double, const char *, uint)) arg1;
    break;
  case MYSQL_SERVER_PUBLIC_KEY:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, server_public_key, (char *)arg1);
    break;
  case MYSQL_PLUGIN_DIR:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, plugin_dir, (char *)arg1);
    break;
  case MYSQL_DEFAULT_AUTH:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, default_auth, (char *)arg1);
    break;
  case MYSQL_OPT_NONBLOCK:
    if (mysql->options.extension &&
        (ctxt = mysql->options.extension->async_context) != 0)
    {
      /*
        We must not allow changing the stack size while a non-blocking call is
        suspended (as the stack is then in use).
      */
      if (ctxt->suspended)
        goto end;
      my_context_destroy(&ctxt->async_context);
      free(ctxt);
    }
    if (!(ctxt= (struct mysql_async_context *)
          calloc(1, sizeof(*ctxt))))
    {
      SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      goto end;
    }
    stacksize= 0;
    if (arg1)
      stacksize= *(const size_t *)arg1;
    if (!stacksize)
      stacksize= ASYNC_CONTEXT_DEFAULT_STACK_SIZE;
    if (my_context_init(&ctxt->async_context, stacksize))
    {
      free(ctxt);
      goto end;
    }
    if (!mysql->options.extension)
    {
      if(!(mysql->options.extension= (struct st_mysql_options_extension *)
        calloc(1, sizeof(struct st_mysql_options_extension))))
      {
        free(ctxt);
        SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
        goto end;
      }
      FIX_SSL_VERIFY_SERVER_CERT(&mysql->options);
    }
    mysql->options.extension->async_context= ctxt;
    break;
  case MYSQL_OPT_MAX_ALLOWED_PACKET:
    if (mysql)
      mysql->options.max_allowed_packet= (unsigned long)(*(size_t *)arg1);
    else
      max_allowed_packet= (unsigned long)(*(size_t *)arg1);
    break;
  case MYSQL_OPT_NET_BUFFER_LENGTH:
    net_buffer_length= (unsigned long)(*(size_t *)arg1);
    break;
  case MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS:
    if (*(my_bool *)arg1)
      mysql->options.client_flag |= CLIENT_CAN_HANDLE_EXPIRED_PASSWORDS;
    else
      mysql->options.client_flag &= ~CLIENT_CAN_HANDLE_EXPIRED_PASSWORDS;
    break;
  case MYSQL_OPT_SSL_ENFORCE:
    mysql->options.use_ssl= (*(my_bool *)arg1);
    break;
  case MYSQL_OPT_SSL_VERIFY_SERVER_CERT:
    OPT_SET_EXTENDED_VALUE(&mysql->options, tls_allow_invalid_server_cert, !*(my_bool *)arg1);
    break;
  case MYSQL_OPT_SSL_KEY:
    OPT_SET_VALUE_STR(&mysql->options, ssl_key, (char *)arg1);
    break;
  case MYSQL_OPT_SSL_CERT:
    OPT_SET_VALUE_STR(&mysql->options, ssl_cert, (char *)arg1);
    break;
  case MYSQL_OPT_SSL_CA:
    OPT_SET_VALUE_STR(&mysql->options, ssl_ca, (char *)arg1);
    break;
  case MYSQL_OPT_SSL_CAPATH:
    OPT_SET_VALUE_STR(&mysql->options, ssl_capath, (char *)arg1);
    break;
  case MYSQL_OPT_SSL_CIPHER:
    OPT_SET_VALUE_STR(&mysql->options, ssl_cipher, (char *)arg1);
    break;
  case MYSQL_OPT_SSL_CRL:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, ssl_crl, (char *)arg1);
    break;
  case MYSQL_OPT_SSL_CRLPATH:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, ssl_crlpath, (char *)arg1);
    break;
  case MYSQL_OPT_CONNECT_ATTR_DELETE:
    {
      uchar *h;
      CHECK_OPT_EXTENSION_SET(&mysql->options);
      if (ma_hashtbl_inited(&mysql->options.extension->connect_attrs) &&
          (h= (uchar *)ma_hashtbl_search(&mysql->options.extension->connect_attrs, (uchar *)arg1,
                      arg1 ? (uint)strlen((char *)arg1) : 0)))
      {
        uchar *p= h;
        size_t key_len= strlen((char *)p);
        mysql->options.extension->connect_attrs_len-= key_len + get_store_length(key_len);
        p+= key_len + 1;
        key_len= strlen((char *)p);
        mysql->options.extension->connect_attrs_len-= key_len + get_store_length(key_len);
        ma_hashtbl_delete(&mysql->options.extension->connect_attrs, h);
      }

    }
    break;
  case MYSQL_OPT_CONNECT_ATTR_RESET:
    CHECK_OPT_EXTENSION_SET(&mysql->options);
    if (ma_hashtbl_inited(&mysql->options.extension->connect_attrs))
    {
      ma_hashtbl_free(&mysql->options.extension->connect_attrs);
      mysql->options.extension->connect_attrs_len= 0;
    }
    break;
  case MARIADB_OPT_CONNECTION_HANDLER:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, connection_handler, (char *)arg1);
    break;
  case MARIADB_OPT_PORT:
    OPT_SET_VALUE_INT(&mysql->options, port, *((uint *)arg1));
    break;
  case MARIADB_OPT_UNIXSOCKET:
    OPT_SET_VALUE_STR(&mysql->options, unix_socket, arg1);
    break;
  case MARIADB_OPT_USER:
    OPT_SET_VALUE_STR(&mysql->options, user, arg1);
    break;
  case MARIADB_OPT_HOST:
    OPT_SET_VALUE_STR(&mysql->options, host, arg1);
    break;
  case MARIADB_OPT_SCHEMA:
    OPT_SET_VALUE_STR(&mysql->options, db, arg1);
    break;
  case MARIADB_OPT_DEBUG:
    break;
  case MARIADB_OPT_FOUND_ROWS:
    mysql->options.client_flag|= CLIENT_FOUND_ROWS;
    break;
  case MARIADB_OPT_INTERACTIVE:
    mysql->options.client_flag|= CLIENT_INTERACTIVE;
    break;
  case MARIADB_OPT_MULTI_RESULTS:
    mysql->options.client_flag|= CLIENT_MULTI_RESULTS;
    break;
  case MARIADB_OPT_MULTI_STATEMENTS:
    mysql->options.client_flag|= CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS;
    break;
  case MARIADB_OPT_PASSWORD:
    OPT_SET_VALUE_STR(&mysql->options, password, arg1);
    break;
  case MARIADB_OPT_USERDATA:
    {
      void *data= va_arg(ap, void *);
      uchar *buffer, *p;
      char *key= (char *)arg1;

      if (!key || !data)
      {
        SET_CLIENT_ERROR(mysql, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
        goto end;
      }

      CHECK_OPT_EXTENSION_SET(&mysql->options);
      if (!ma_hashtbl_inited(&mysql->options.extension->userdata))
      {
        if (_ma_hashtbl_init(&mysql->options.extension->userdata,
                       0, 0, 0, ma_get_hash_keyval, ma_int_hash_free, 0))
        {
          SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
          goto end;
        }
      }
      /* check if key is already in buffer */
      p= (uchar *)ma_hashtbl_search(&mysql->options.extension->userdata,
                              (uchar *)key,
                              (uint)strlen(key));
      if (p)
      {
        p+= strlen(key) + 1;
        memcpy(p, &data, sizeof(void *));
        break;
      }

      if (!(buffer= (uchar *)malloc(strlen(key) + 1 + sizeof(void *))))
      {
        SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
        goto end;
      }

      p= buffer;
      strcpy((char *)p, key);
      p+= strlen(key) + 1;
      memcpy(p, &data, sizeof(void *));

      if (ma_hashtbl_insert(&mysql->options.extension->userdata, buffer))
      {
        free(buffer);
        SET_CLIENT_ERROR(mysql, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
        goto end;
      }
    }
    break;
  case MYSQL_OPT_CONNECT_ATTR_ADD:
    {
      uchar *buffer;
      void *arg2= va_arg(ap, void *);
      size_t storage_len, key_len= arg1 ? strlen((char *)arg1) : 0,
             value_len= arg2 ? strlen((char *)arg2) : 0;
      if (!key_len || !value_len)
      {
        SET_CLIENT_ERROR(mysql, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
        goto end;
      }
      storage_len= key_len + value_len +
                   get_store_length(key_len) +
                   get_store_length(value_len);

      /* since we store terminating zero character in hash, we need
       * to increase lengths */
      key_len++;
      value_len++;

      CHECK_OPT_EXTENSION_SET(&mysql->options);
      if (!key_len ||
          storage_len + mysql->options.extension->connect_attrs_len > 0xFFFF)
      {
        SET_CLIENT_ERROR(mysql, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
        goto end;
      }

      if (!ma_hashtbl_inited(&mysql->options.extension->connect_attrs))
      {
        if (_ma_hashtbl_init(&mysql->options.extension->connect_attrs,
                       0, 0, 0, ma_get_hash_keyval, ma_int_hash_free, 0))
        {
          SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
          goto end;
        }
      }
      if ((buffer= (uchar *)malloc(key_len + value_len)))
      {
        uchar *p= buffer;
        strcpy((char *)p, arg1);
        p+= (strlen(arg1) + 1);
        if (arg2)
          strcpy((char *)p, arg2);

        if (ma_hashtbl_insert(&mysql->options.extension->connect_attrs, buffer))
        {
          free(buffer);
          SET_CLIENT_ERROR(mysql, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
          goto end;
        }
        mysql->options.extension->connect_attrs_len+= storage_len;
      }
      else
      {
        SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
        goto end;
      }
    }
    break;
  case MYSQL_ENABLE_CLEARTEXT_PLUGIN:
    break;
  case MYSQL_SECURE_AUTH:
    mysql->options.secure_auth= *(my_bool *)arg1;
    break;
  case MYSQL_OPT_BIND:
    OPT_SET_VALUE_STR(&mysql->options, bind_address, arg1);
    break;
  case MARIADB_OPT_TLS_CIPHER_STRENGTH:
    OPT_SET_EXTENDED_VALUE_INT(&mysql->options, tls_cipher_strength, *((unsigned int *)arg1));
    break;
  case MARIADB_OPT_SSL_FP:
  case MARIADB_OPT_TLS_PEER_FP:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, tls_fp, (char *)arg1);
    mysql->options.use_ssl= 1;
    break;
  case MARIADB_OPT_SSL_FP_LIST:
  case MARIADB_OPT_TLS_PEER_FP_LIST:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, tls_fp_list, (char *)arg1);
    mysql->options.use_ssl= 1;
    break;
  case MARIADB_OPT_TLS_PASSPHRASE:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, tls_pw, (char *)arg1);
    break;
  case MARIADB_OPT_CONNECTION_READ_ONLY:
    OPT_SET_EXTENDED_VALUE_INT(&mysql->options, read_only, *(my_bool *)arg1);
    break;
  case MARIADB_OPT_PROXY_HEADER:
    {
    size_t arg2 = va_arg(ap, size_t);
    OPT_SET_EXTENDED_VALUE_BIN(&mysql->options, proxy_header, proxy_header_len, (char *)arg1, arg2);
    }
    break;
  case MARIADB_OPT_TLS_VERSION:
  case MYSQL_OPT_TLS_VERSION:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, tls_version, (char *)arg1);
    break;
  case MARIADB_OPT_IO_WAIT:
    CHECK_OPT_EXTENSION_SET(&mysql->options);
    mysql->options.extension->io_wait = (int(*)(my_socket, my_bool, int))arg1;
    break;
  case MARIADB_OPT_SKIP_READ_RESPONSE:
    OPT_SET_EXTENDED_VALUE_INT(&mysql->options, skip_read_response, *(my_bool *)arg1);
    break;
  case MARIADB_OPT_RESTRICTED_AUTH:
    OPT_SET_EXTENDED_VALUE_STR(&mysql->options, restricted_auth, (char *)arg1);
    break;
  case MARIADB_OPT_RPL_REGISTER_REPLICA:
    {
      unsigned int arg2 = va_arg(ap, unsigned int);
      OPT_SET_EXTENDED_VALUE_STR(&mysql->options, rpl_host,(char *)arg1);
      OPT_SET_EXTENDED_VALUE(&mysql->options, rpl_port, (ushort)arg2);
    }
    break;
  case MARIADB_OPT_STATUS_CALLBACK:
    {
      void *arg2= va_arg(ap, void *);
      if (arg1 || arg2)
      {
        if (arg1) {
           OPT_SET_EXTENDED_VALUE(&mysql->options, status_callback, arg1);
        }
        if (arg2) {
           OPT_SET_EXTENDED_VALUE(&mysql->options, status_data, arg2);
        }
      } else {
        OPT_SET_EXTENDED_VALUE(&mysql->options, status_callback, ma_save_session_track_info);
        OPT_SET_EXTENDED_VALUE(&mysql->options, status_data, mysql);
      }
    }
    break;
  case MARIADB_OPT_BULK_UNIT_RESULTS:
    OPT_SET_EXTENDED_VALUE_INT(&mysql->options, bulk_unit_results, *(my_bool *)arg1);
    break;
  case MARIADB_OPT_TLS_VERIFICATION_CALLBACK:
    if (!arg1)
    {
      OPT_SET_EXTENDED_VALUE(&mysql->options, tls_verification_callback, ma_pvio_tls_verify_server_cert);
    } else {
      OPT_SET_EXTENDED_VALUE(&mysql->options, tls_verification_callback, arg1);
    }
    break;
  case MYSQL_OPT_ZSTD_COMPRESSION_LEVEL:
    OPT_SET_EXTENDED_VALUE(&mysql->options, zstd_compression_level, *((unsigned char *)arg1));
    break;
  default:
    va_end(ap);
    SET_CLIENT_ERROR(mysql, CR_NOT_IMPLEMENTED, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  va_end(ap);
  return(0);
end:
  va_end(ap);
  return(1);
}

int
mysql_get_optionv(MYSQL *mysql, enum mysql_option option, void *arg, ...)
{
  va_list ap;

  va_start(ap, arg);

  switch(option) {
  case MYSQL_OPT_CONNECT_TIMEOUT:
    *((uint *)arg)= mysql->options.connect_timeout;
    break;
  case MYSQL_OPT_COMPRESS:
    *((my_bool *)arg)= mysql->options.compress;
    break;
  case MYSQL_OPT_ZSTD_COMPRESSION_LEVEL:
    *((unsigned char *)arg)= mysql->options.extension->zstd_compression_level;
    break;
  case MYSQL_OPT_NAMED_PIPE:
    *((my_bool *)arg)= mysql->options.named_pipe;
    break;
  case MYSQL_OPT_LOCAL_INFILE:			/* Allow LOAD DATA LOCAL ?*/
    *((uint *)arg)= test(mysql->options.client_flag & CLIENT_LOCAL_FILES);
    break;
  case MYSQL_INIT_COMMAND:
    /* mysql_get_optionsv(mysql, MYSQL_INIT_COMMAND, commands, elements) */
    {
      unsigned int *elements;
      if (arg)
        *((char **)arg)= mysql->options.init_command ? mysql->options.init_command->buffer : NULL;
      if ((elements= va_arg(ap, unsigned int *)))
        *elements= mysql->options.init_command ? mysql->options.init_command->elements : 0;
    }
    break;
  case MYSQL_READ_DEFAULT_FILE:
    *((char **)arg)= mysql->options.my_cnf_file;
    break;
  case MYSQL_READ_DEFAULT_GROUP:
    *((char **)arg)= mysql->options.my_cnf_group;
    break;
  case MYSQL_SET_CHARSET_DIR:
    /* not supported in this version. Since all character sets
       are internally available, we don't throw an error */
    *((char **)arg)= NULL;
    break;
  case MYSQL_SET_CHARSET_NAME:
    if (mysql->charset)
      *((const char **)arg)= mysql->charset->csname;
    else
      *((char **)arg)= mysql->options.charset_name;
    break;
  case MYSQL_OPT_RECONNECT:
    *((my_bool *)arg)= mysql->options.reconnect;
    break;
  case MYSQL_OPT_PROTOCOL:
    *((uint *)arg)= mysql->options.protocol;
    break;
  case MYSQL_OPT_READ_TIMEOUT:
    *((uint *)arg)= mysql->options.read_timeout;
    break;
  case MYSQL_OPT_WRITE_TIMEOUT:
    *((uint *)arg)= mysql->options.write_timeout;
    break;
  case MYSQL_REPORT_DATA_TRUNCATION:
    *((my_bool *)arg)= mysql->options.report_data_truncation;
    break;
  case MYSQL_PROGRESS_CALLBACK:
    *((void (**)(const MYSQL *, uint, uint, double, const char *, uint))arg)=
       mysql->options.extension ?  mysql->options.extension->report_progress : NULL;
    break;
  case MYSQL_SERVER_PUBLIC_KEY:
    *((char **)arg)= mysql->options.extension ?
      mysql->options.extension->server_public_key : NULL;
    break;
  case MYSQL_PLUGIN_DIR:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->plugin_dir : NULL;
    break;
  case MYSQL_DEFAULT_AUTH:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->default_auth : NULL;
    break;
  case MYSQL_OPT_NONBLOCK:
    *((my_bool *)arg)= test(mysql->options.extension && mysql->options.extension->async_context);
    break;
  case MYSQL_OPT_CAN_HANDLE_EXPIRED_PASSWORDS:
    *((my_bool *)arg)= test(mysql->options.client_flag & CLIENT_CAN_HANDLE_EXPIRED_PASSWORDS);
    break;
  case MYSQL_OPT_SSL_ENFORCE:
    *((my_bool *)arg)= mysql->options.use_ssl;
    break;
  case MYSQL_OPT_SSL_VERIFY_SERVER_CERT:
    *((my_bool*)arg) = mysql->options.extension ? !mysql->options.extension->tls_allow_invalid_server_cert: 1;
    break;
  case MYSQL_OPT_SSL_KEY:
    *((char **)arg)= mysql->options.ssl_key;
    break;
  case MYSQL_OPT_SSL_CERT:
    *((char **)arg)= mysql->options.ssl_cert;
    break;
  case MYSQL_OPT_SSL_CA:
    *((char **)arg)= mysql->options.ssl_ca;
    break;
  case MYSQL_OPT_SSL_CAPATH:
    *((char **)arg)= mysql->options.ssl_capath;
    break;
  case MYSQL_OPT_SSL_CIPHER:
    *((char **)arg)= mysql->options.ssl_cipher;
    break;
  case MYSQL_OPT_SSL_CRL:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->ssl_crl : NULL;
    break;
  case MYSQL_OPT_SSL_CRLPATH:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->ssl_crlpath : NULL;
    break;
  case MARIADB_OPT_TLS_VERSION:
  case MYSQL_OPT_TLS_VERSION:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->tls_version : NULL;
    break;
  case MYSQL_OPT_CONNECT_ATTRS:
    /* mysql_get_optionsv(mysql, MYSQL_OPT_CONNECT_ATTRS, keys, vals, elements) */
    {
      unsigned int i, *elements;
      char **key= NULL;
      void *arg1;
      char **val= NULL;

      if (arg)
        key= *(char ***)arg;

      arg1= va_arg(ap, char **);
      if (arg1)
        val= *(char ***)arg1;

      if (!(elements= va_arg(ap, unsigned int *)))
        goto error;

      *elements= 0;

      if (!mysql->options.extension ||
          !ma_hashtbl_inited(&mysql->options.extension->connect_attrs))
        break;

      *elements= mysql->options.extension->connect_attrs.records;

      if (val || key)
      {
        for (i=0; i < *elements; i++)
        {
          uchar *p= ma_hashtbl_element(&mysql->options.extension->connect_attrs, i);
          if (key)
            key[i]= (char *)p;
          p+= strlen((char *)p) + 1;
          if (val)
            val[i]= (char *)p;
        }
      }
    }
    break;
  case MYSQL_OPT_MAX_ALLOWED_PACKET:
    *((unsigned long *)arg)= (mysql) ? mysql->options.max_allowed_packet :
                                       max_allowed_packet;
    break;
  case MYSQL_OPT_NET_BUFFER_LENGTH:
    *((unsigned long *)arg)= net_buffer_length;
    break;
  case MYSQL_SECURE_AUTH:
    *((my_bool *)arg)= mysql->options.secure_auth;
    break;
  case MYSQL_OPT_BIND:
    *((char **)arg)= mysql->options.bind_address;
    break;
  case MARIADB_OPT_TLS_CIPHER_STRENGTH:
    *((unsigned int *)arg) = mysql->options.extension ? mysql->options.extension->tls_cipher_strength : 0;
    break;
  case MARIADB_OPT_SSL_FP:
  case MARIADB_OPT_TLS_PEER_FP:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->tls_fp : NULL;
    break;
  case MARIADB_OPT_SSL_FP_LIST:
  case MARIADB_OPT_TLS_PEER_FP_LIST:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->tls_fp_list : NULL;
    break;
  case MARIADB_OPT_TLS_PASSPHRASE:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->tls_pw : NULL;
    break;
  case MARIADB_OPT_CONNECTION_READ_ONLY:
    *((my_bool *)arg)= mysql->options.extension ? mysql->options.extension->read_only : 0;
    break;
  case MARIADB_OPT_USERDATA:
    /* nysql_get_optionv(mysql, MARIADB_OPT_USERDATA, key, value) */
    {
      uchar *p;
      void *data= va_arg(ap, void *);
      char *key= (char *)arg;
      if (key && data && mysql->options.extension && ma_hashtbl_inited(&mysql->options.extension->userdata) &&
          (p= (uchar *)ma_hashtbl_search(&mysql->options.extension->userdata, (uchar *)key,
                      (uint)strlen((char *)key))))
      {
        p+= strlen(key) + 1;
        memcpy(data, p, sizeof(void *));
        break;
      }
      if (data)
        *((void **)data)= NULL;
    }
    break;
  case MARIADB_OPT_CONNECTION_HANDLER:
    *((char **)arg)= mysql->options.extension ? mysql->options.extension->connection_handler : NULL;
    break;
  case MARIADB_OPT_IO_WAIT:
    *((int(**)(my_socket, my_bool, int))arg) = mysql->options.extension ? mysql->options.extension->io_wait : NULL;
    break;
  case MARIADB_OPT_SKIP_READ_RESPONSE:
    *((my_bool*)arg)= mysql->options.extension ? mysql->options.extension->skip_read_response : 0;
    break;
  case MARIADB_OPT_BULK_UNIT_RESULTS:
    *((my_bool *)arg)= mysql->options.extension ? mysql->options.extension->bulk_unit_results : 0;
    break;
  default:
    va_end(ap);
    SET_CLIENT_ERROR(mysql, CR_NOT_IMPLEMENTED, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  va_end(ap);
  return(0);
error:
  va_end(ap);
  return(1);
}

int STDCALL mysql_get_option(MYSQL *mysql, enum mysql_option option, void *arg)
{
  return mysql_get_optionv(mysql, option, arg);
}

int STDCALL
mysql_options(MYSQL *mysql,enum mysql_option option, const void *arg)
{
  return mysql_optionsv(mysql, option, arg);
}

int STDCALL
mysql_options4(MYSQL *mysql,enum mysql_option option, const void *arg1, const void *arg2)
{
  return mysql_optionsv(mysql, option, arg1, arg2);
}
/****************************************************************************
** Functions to get information from the MySQL structure
** These are functions to make shared libraries more usable.
****************************************************************************/

/* MYSQL_RES */
my_ulonglong STDCALL mysql_num_rows(MYSQL_RES *res)
{
  return res->row_count;
}

unsigned int STDCALL mysql_num_fields(MYSQL_RES *res)
{
  return res->field_count;
}

/* deprecated */
my_bool STDCALL mysql_eof(MYSQL_RES *res)
{
  return res->eof;
}

MYSQL_FIELD * STDCALL mysql_fetch_field_direct(MYSQL_RES *res,uint fieldnr)
{
  return &(res)->fields[fieldnr];
}

MYSQL_FIELD * STDCALL mysql_fetch_fields(MYSQL_RES *res)
{
  return (res)->fields;
}

MYSQL_ROWS * STDCALL mysql_row_tell(MYSQL_RES *res)
{
  return res->data_cursor;
}

uint STDCALL mysql_field_tell(MYSQL_RES *res)
{
  return (res)->current_field;
}

/* MYSQL */

unsigned int STDCALL mysql_field_count(MYSQL *mysql)
{
  return mysql->field_count;
}

my_ulonglong STDCALL mysql_affected_rows(MYSQL *mysql)
{
  return (mysql)->affected_rows;
}

my_bool STDCALL mysql_autocommit(MYSQL *mysql, my_bool mode)
{
  return((my_bool) mysql_real_query(mysql, (mode) ? "SET autocommit=1" :
                                         "SET autocommit=0", 16));
}

my_bool STDCALL mysql_commit(MYSQL *mysql)
{
  return((my_bool)mysql_real_query(mysql, "COMMIT", (unsigned long)strlen("COMMIT")));
}

my_bool STDCALL mysql_rollback(MYSQL *mysql)
{
  return((my_bool)mysql_real_query(mysql, "ROLLBACK", (unsigned long)strlen("ROLLBACK")));
}

my_ulonglong STDCALL mysql_insert_id(MYSQL *mysql)
{
  return (mysql)->insert_id;
}

uint STDCALL mysql_errno(MYSQL *mysql)
{
  return mysql ? mysql->net.last_errno : 0;
}

const char * STDCALL mysql_error(MYSQL *mysql)
{
  return mysql ? (mysql)->net.last_error : (char *)"";
}

const char *STDCALL mysql_info(MYSQL *mysql)
{
  return (mysql)->info;
}

my_bool STDCALL mysql_more_results(MYSQL *mysql)
{
  return(test(mysql->server_status & SERVER_MORE_RESULTS_EXIST));
}

int STDCALL mysql_next_result(MYSQL *mysql)
{

  /* make sure communication is not blocking */
  if (mysql->status != MYSQL_STATUS_READY)
  {
    SET_CLIENT_ERROR(mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  /* clear error, and mysql status variables */
  CLEAR_CLIENT_ERROR(mysql);
  mysql->affected_rows = (ulonglong) ~0;

  if (mysql->server_status & SERVER_MORE_RESULTS_EXIST)
  {
     return(mysql->methods->db_read_query_result(mysql));
  }

  return(-1);
}

ulong STDCALL mysql_thread_id(MYSQL *mysql)
{
  return (mysql)->thread_id;
}

const char * STDCALL mysql_character_set_name(MYSQL *mysql)
{
  return mysql->charset->csname;
}


uint STDCALL mysql_thread_safe(void)
{
#ifdef THREAD
  return 1;
#else
  return 0;
#endif
}

/****************************************************************************
** Some support functions
****************************************************************************/

/*
** Add escape characters to a string (blob?) to make it suitable for a insert
** to should at least have place for length*2+1 chars
** Returns the length of the to string
*/

ulong STDCALL
mysql_escape_string(char *to,const char *from, ulong length)
{
    return (ulong)mysql_cset_escape_slashes(ma_default_charset_info, to, from, length);
}

ulong STDCALL
mysql_real_escape_string(MYSQL *mysql, char *to,const char *from,
			 ulong length)
{
  if (mysql->server_status & SERVER_STATUS_NO_BACKSLASH_ESCAPES)
    return (ulong)mysql_cset_escape_quotes(mysql->charset, to, from, length);
  else
    return (ulong)mysql_cset_escape_slashes(mysql->charset, to, from, length);
}

static void mariadb_get_charset_info(MYSQL *mysql, MY_CHARSET_INFO *cs)
{
  if (!cs)
    return;

  cs->number= mysql->charset->nr;
  cs->csname=  mysql->charset->csname;
  cs->name= mysql->charset->name;
  cs->state= 0;
  cs->comment= NULL;
  cs->dir= NULL;
  cs->mbminlen= mysql->charset->char_minlen;
  cs->mbmaxlen= mysql->charset->char_maxlen;

  return;
}

void STDCALL mysql_get_character_set_info(MYSQL *mysql, MY_CHARSET_INFO *cs)
{
  mariadb_get_charset_info(mysql, cs);
}

int STDCALL mysql_set_character_set(MYSQL *mysql, const char *csname)
{
  const MARIADB_CHARSET_INFO *cs;

  if (!csname)
    goto error;

  if ((cs= mysql_find_charset_name(csname)))
  {
    char buff[64];

    snprintf(buff, 63, "SET NAMES %s", cs->csname);
    if (!mysql_real_query(mysql, buff, (unsigned long)strlen(buff)))
    {
      mysql->charset= cs;
      return(0);
    }
    return(mysql->net.last_errno);
  }

error:
  my_set_error(mysql, CR_CANT_READ_CHARSET, SQLSTATE_UNKNOWN,
               0, csname, "compiled_in");
  return(mysql->net.last_errno);
}

unsigned int STDCALL mysql_warning_count(MYSQL *mysql)
{
  return mysql->warning_count;
}

const char * STDCALL mysql_sqlstate(MYSQL *mysql)
{
  return mysql->net.sqlstate;
}

#ifndef _WIN32
#include <signal.h>
static void ignore_sigpipe()
{
  signal(SIGPIPE, SIG_IGN);
}
#else
#define ignore_sigpipe()
#endif

#ifdef _WIN32
static int mysql_once_init()
#else
static void mysql_once_init()
#endif
{
  ma_init();					/* Will init threads */
  init_client_errs();
  get_default_configuration_dirs();
  set_default_charset_by_name(MARIADB_DEFAULT_CHARSET, 0);
  if (mysql_client_plugin_init())
  {
#ifdef _WIN32
    return 1;
#else
    return;
#endif
  }
  if (!mysql_port)
  {
#if !__has_feature(memory_sanitizer) /* work around MSAN deficiency */
    struct servent *serv_ptr;
#endif
    char *env;

    mysql_port = MARIADB_PORT;
#if !__has_feature(memory_sanitizer) /* work around MSAN deficiency */
    if ((serv_ptr = getservbyname("mysql", "tcp")))
      mysql_port = (uint)ntohs((ushort)serv_ptr->s_port);
#endif
    if ((env = getenv("MYSQL_TCP_PORT")))
      mysql_port =(uint)atoi(env);
  }
  if (!mysql_unix_port)
  {
    char *env;
#ifdef _WIN32
    mysql_unix_port = (char*)MARIADB_NAMEDPIPE;
#else
    mysql_unix_port = (char*)MARIADB_UNIX_ADDR;
#endif
    if ((env = getenv("MYSQL_UNIX_PORT")) ||
      (env = getenv("MARIADB_UNIX_PORT")))
      mysql_unix_port = env;
  }
  if (!mysql_ps_subsystem_initialized)
    mysql_init_ps_subsystem();
#ifdef HAVE_TLS
  ma_tls_start(0, 0);
#endif
  ignore_sigpipe();
  mysql_client_init = 1;
#ifdef _WIN32
  return 0;
#endif
}

#ifdef _WIN32
static INIT_ONCE init_once= INIT_ONCE_STATIC_INIT;
BOOL CALLBACK win_init_once(
  PINIT_ONCE InitOnce,
  PVOID Parameter,
  PVOID *lpContext)
{
  return !mysql_once_init();
  return TRUE;
}
#else
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
#endif

int STDCALL mysql_server_init(int argc __attribute__((unused)),
  char **argv __attribute__((unused)),
  char **groups __attribute__((unused)))
{
#ifdef _WIN32
  BOOL ret = InitOnceExecuteOnce(&init_once, win_init_once, NULL, NULL);
  return ret? 0: 1;
#else
  return pthread_once(&init_once, mysql_once_init);
#endif
}

void STDCALL mysql_server_end(void)
{
  if (!mysql_client_init)
    return;

  release_configuration_dirs();
  mysql_client_plugin_deinit();

  list_free(pvio_callback, 0);
  if (ma_init_done)
    ma_end(0);
#ifdef HAVE_TLS
  ma_pvio_tls_end();
#endif
  mysql_client_init= 0;
  ma_init_done= 0;
#ifdef WIN32
  init_once = (INIT_ONCE)INIT_ONCE_STATIC_INIT;
#else
  init_once = (pthread_once_t)PTHREAD_ONCE_INIT;
#endif
}

my_bool STDCALL mysql_thread_init(void)
{
  return 0;
}

void STDCALL mysql_thread_end(void)
{
}

int STDCALL mysql_set_server_option(MYSQL *mysql,
                                    enum enum_mysql_set_option option)
{
  char buffer[2];
  int2store(buffer, (uint)option);
  return(ma_simple_command(mysql, COM_SET_OPTION, buffer, sizeof(buffer), 0, 0));
}

ulong STDCALL mysql_get_client_version(void)
{
  return MARIADB_PACKAGE_VERSION_ID;
}

ulong STDCALL mysql_hex_string(char *to, const char *from, unsigned long len)
{
  char *start= to;
  char hexdigits[]= "0123456789ABCDEF";

  while (len--)
  {
    *to++= hexdigits[((unsigned char)*from) >> 4];
    *to++= hexdigits[((unsigned char)*from) & 0x0F];
    from++;
  }
  *to= 0;
  return (ulong)(to - start);
}

my_bool STDCALL mariadb_connection(MYSQL *mysql)
{
  return (strstr(mysql->server_version, "MariaDB") ||
          strstr(mysql->server_version, "-maria-"));
}

const char * STDCALL
mysql_get_server_name(MYSQL *mysql)
{
  if (mysql->options.extension &&
      mysql->options.extension->db_driver != NULL)
    return mysql->options.extension->db_driver->name;
  return mariadb_connection(mysql) ? "MariaDB" : "MySQL";
}

static my_socket mariadb_get_socket(MYSQL *mysql)
{
  my_socket sock= INVALID_SOCKET;
  if (mysql->net.pvio)
  {
    ma_pvio_get_handle(mysql->net.pvio, &sock);

  }
  /* if an asynchronous connect is in progress, we need to obtain
     pvio handle from async_context until the connection was
     successfully established.
  */
  else if (mysql->options.extension && mysql->options.extension->async_context &&
           mysql->options.extension->async_context->pvio)
  {
    ma_pvio_get_handle(mysql->options.extension->async_context->pvio, &sock);
  }
  return sock;
}

my_socket STDCALL
mysql_get_socket(MYSQL *mysql)
{
  return mariadb_get_socket(mysql);
}

MARIADB_CHARSET_INFO * STDCALL mariadb_get_charset_by_name(const char *csname)
{
  return (MARIADB_CHARSET_INFO *)mysql_find_charset_name(csname);
}

MARIADB_CHARSET_INFO * STDCALL mariadb_get_charset_by_nr(unsigned int csnr)
{
  return (MARIADB_CHARSET_INFO *)mysql_find_charset_nr(csnr);
}

my_bool mariadb_get_infov(MYSQL *mysql, enum mariadb_value value, void *arg, ...)
{
  va_list ap;

  va_start(ap, arg);

  switch(value) {
#ifdef HAVE_TLS
  case MARIADB_TLS_PEER_CERT_INFO:
    if (mysql->net.pvio->ctls)
    {
      unsigned int size;

      size= va_arg(ap, unsigned int);
      if (!ma_pvio_tls_get_peer_cert_info(mysql->net.pvio->ctls, size))
        *((MARIADB_X509_INFO **)arg)= (MARIADB_X509_INFO *)&mysql->net.pvio->ctls->cert_info;
      va_end(ap);
      return 0;
    }
    *((MARIADB_X509_INFO **)arg)= NULL;
    break;
  case MARIADB_TLS_VERIFY_STATUS:
    *((unsigned int *)arg)= (unsigned int)mysql->extension->tls_validation;
    break;
#endif
  case MARIADB_MAX_ALLOWED_PACKET:
    *((size_t *)arg)= (size_t)max_allowed_packet;
    break;
  case MARIADB_NET_BUFFER_LENGTH:
    *((size_t *)arg)= (size_t)net_buffer_length;
    break;
  case MARIADB_CONNECTION_ERROR_ID:
    if (!mysql)
      goto error;
    *((unsigned int *)arg)= mysql->net.last_errno;
    break;
  case MARIADB_CONNECTION_ERROR:
    if (!mysql)
      goto error;
    *((char **)arg)= mysql->net.last_error;
    break;
  case MARIADB_CONNECTION_SQLSTATE:
    if (!mysql)
      goto error;
    *((char **)arg)= mysql->net.sqlstate;
    break;
  case MARIADB_CONNECTION_TLS_VERSION:
    #ifdef HAVE_TLS
    if (mysql && mysql->net.pvio && mysql->net.pvio->ctls)
      *((char **)arg)= (char *)ma_pvio_tls_get_protocol_version(mysql->net.pvio->ctls);
    else
    #endif
      goto error;
    break;
  case MARIADB_CONNECTION_TLS_VERSION_ID:
    #ifdef HAVE_TLS
    if (mysql && mysql->net.pvio && mysql->net.pvio->ctls)
      *((unsigned int *)arg)= ma_pvio_tls_get_protocol_version_id(mysql->net.pvio->ctls);
    else
    #endif
      goto error;
    break;
  case MARIADB_TLS_LIBRARY:
#ifdef HAVE_TLS
    *((const char **)arg)= tls_library_version;
#else
    *((const char **)arg)= "Off";
#endif
    break;
  case MARIADB_CLIENT_VERSION:
    *((const char **)arg)= MARIADB_CLIENT_VERSION_STR;
    break;
  case MARIADB_CLIENT_VERSION_ID:
    *((size_t *)arg)= MARIADB_VERSION_ID;
    break;
  case MARIADB_CONNECTION_SERVER_VERSION:
    if (mysql)
      *((char **)arg)= mysql->server_version;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_SERVER_TYPE:
    if (mysql)
      *((const char **)arg)= mariadb_connection(mysql) ? "MariaDB" : "MySQL";
    else
      goto error;
    break;
  case MARIADB_CONNECTION_SERVER_VERSION_ID:
    if (mysql)
      *((size_t *)arg)= mariadb_server_version_id(mysql);
    else
      goto error;
    break;
  case MARIADB_CONNECTION_PROTOCOL_VERSION_ID:
    if (mysql)
      *((unsigned int *)arg)= mysql->protocol_version;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_MARIADB_CHARSET_INFO:
    if (mysql)
      mariadb_get_charset_info(mysql, (MY_CHARSET_INFO *)arg);
    else
      goto error;
    break;
  case MARIADB_CONNECTION_SOCKET:
    if (mysql)
      *((my_socket *)arg)= mariadb_get_socket(mysql);
    else
      goto error;
    break;
  case MARIADB_CONNECTION_TYPE:
    if (mysql  && mysql->net.pvio)
      *((int *)arg)= (int)mysql->net.pvio->type;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_ASYNC_TIMEOUT_MS:
    if (mysql && mysql->options.extension && mysql->options.extension->async_context)
      *((unsigned int *)arg)= mysql->options.extension->async_context->timeout_value;
    break;
  case MARIADB_CONNECTION_ASYNC_TIMEOUT:
    if (mysql && mysql->options.extension && mysql->options.extension->async_context)
    {
      unsigned int timeout= mysql->options.extension->async_context->timeout_value;
      if (timeout > UINT_MAX - 999)
        *((unsigned int *)arg)= (timeout - 1)/1000 + 1;
      else
        *((unsigned int *)arg)= (timeout+999)/1000;
    }
    break;
  case MARIADB_CHARSET_NAME:
    {
      char *name;
      name= va_arg(ap, char *);
      if (name)
        *((MARIADB_CHARSET_INFO **)arg)= (MARIADB_CHARSET_INFO *)mysql_find_charset_name(name);
      else
        goto error;
    }
    break;
  case MARIADB_CHARSET_ID:
    {
      unsigned int nr;
      nr= va_arg(ap, unsigned int);
      *((MARIADB_CHARSET_INFO **)arg)= (MARIADB_CHARSET_INFO *)mysql_find_charset_nr(nr);
    }
    break;
  case MARIADB_CONNECTION_SSL_CIPHER:
    #ifdef HAVE_TLS
    if (mysql && mysql->net.pvio && mysql->net.pvio->ctls)
      *((char **)arg)= (char *)ma_pvio_tls_cipher(mysql->net.pvio->ctls);
    else
    #endif
      *((char **)arg)= NULL;
    break;
  case MARIADB_CLIENT_ERRORS:
    *((char ***)arg)= (char **)client_errors;
    break;
  case MARIADB_CONNECTION_INFO:
    if (mysql)
      *((char **)arg)= (char *)mysql->info;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_PVIO_TYPE:
    if (mysql && mysql->net.pvio)
      *((unsigned int *)arg)= (unsigned int)mysql->net.pvio->type;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_SCHEMA:
    if (mysql)
      *((char **)arg)= mysql->db;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_USER:
    if (mysql)
      *((char **)arg)= mysql->user;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_PORT:
    if (mysql)
      *((unsigned int *)arg)= mysql->port;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_UNIX_SOCKET:
    if (mysql)
      *((char **)arg)= mysql->unix_socket;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_HOST:
    if (mysql)
      *((char **)arg)= mysql->host;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_SERVER_STATUS:
    if (mysql)
      *((unsigned int *)arg)= mysql->server_status;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_SERVER_CAPABILITIES:
    if (mysql)
      *((unsigned long *)arg)= mysql->server_capabilities;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_EXTENDED_SERVER_CAPABILITIES:
    if (mysql)
      *((unsigned long *)arg)= mysql->extension->mariadb_server_capabilities;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_CLIENT_CAPABILITIES:
    if (mysql)
      *((unsigned long *)arg)= mysql->client_flag;
    else
      goto error;
    break;
  case MARIADB_CONNECTION_BYTES_READ:
    *((size_t *)arg)= mysql->net.pvio->bytes_read;
    break;
  case MARIADB_CONNECTION_BYTES_SENT:
    *((size_t *)arg)= mysql->net.pvio->bytes_sent;
    break;
  default:
    va_end(ap);
    return(-1);
  }
  va_end(ap);
  return(0);
error:
  va_end(ap);
  return(-1);
}

my_bool STDCALL mariadb_get_info(MYSQL *mysql, enum mariadb_value value, void *arg)
{
  return mariadb_get_infov(mysql, value, arg);
}

/*
  Immediately aborts connection, making all subsequent read/write operations fail.
  Does not invalidate memory used for mysql structure, nor closes any communication
  channels - mysql_close is still needed.
  Useful to break long query, in situation  sending KILL is not possible.
*/
int STDCALL mariadb_cancel(MYSQL *mysql)
{
  if (!mysql || !mysql->net.pvio || !mysql->net.pvio->methods || !mysql->net.pvio->methods->shutdown)
  {
    return 1;
  }
  else
  {
    MARIADB_PVIO *pvio = mysql->net.pvio;
    return pvio->methods->shutdown(pvio);
  }
}

/* compatibility functions for MariaDB */
void STDCALL
mysql_debug(const char *debug __attribute__((unused)))
{
  return;
}

/********************************************************************
  mysql_net_ functions - low-level API to MySQL protocol
*********************************************************************/
ulong STDCALL mysql_net_read_packet(MYSQL *mysql)
{
  return ma_net_safe_read(mysql);
}

ulong STDCALL mysql_net_field_length(uchar **packet)
{
  return net_field_length(packet);
}

my_bool STDCALL mysql_embedded(void)
{
#ifdef EMBEDDED_LIBRARY
  return 1;
#else
  return 0;
#endif
}

MYSQL_PARAMETERS *STDCALL
mysql_get_parameters(void)
{
  return &mariadb_internal_parameters;
}

int STDCALL mysql_reset_connection(MYSQL *mysql)
{
  int rc;

  /* check if connection handler is active */
  if (IS_CONNHDLR_ACTIVE(mysql))
  {
    if (mysql->extension->conn_hdlr->plugin && mysql->extension->conn_hdlr->plugin->reset)
      return(mysql->extension->conn_hdlr->plugin->reset(mysql));
  }

  /* skip result sets */
  if (mysql->status == MYSQL_STATUS_USE_RESULT ||
      mysql->status == MYSQL_STATUS_GET_RESULT)
  {
    mthd_my_skip_result(mysql);
  }

  if (mysql->server_status & SERVER_MORE_RESULTS_EXIST)
  {
    while (mysql_next_result(mysql))
    {
      MYSQL_RES *res= mysql_use_result(mysql);
      mysql_free_result(res);
    }
  }

  mysql->status= MYSQL_STATUS_READY;

  rc= ma_simple_command(mysql, COM_RESET_CONNECTION, 0, 0, 0, 0);
  if (rc && mysql->options.reconnect)
  {
    /* There is no big sense in resetting but we need reconnect */
    rc= ma_simple_command(mysql, COM_RESET_CONNECTION,0,0,0,0);
  }
  if (rc)
    return 1;

  /* reset the connection in all active statements */
  ma_invalidate_stmts(mysql, "mysql_reset_connection()");
  free_old_query(mysql);
  mysql->status= MYSQL_STATUS_READY;
  mysql->affected_rows= ~(my_ulonglong)0;
  mysql->insert_id= 0;
  return 0;
}

#undef STDCALL
/* API functions for usage in dynamic plugins */
struct st_mariadb_api MARIADB_API=
{
  mysql_num_rows,
  mysql_num_fields,
  mysql_eof,
  mysql_fetch_field_direct,
  mysql_fetch_fields,
  mysql_row_tell,
  mysql_field_tell,
  mysql_field_count,
  mysql_more_results,
  mysql_next_result,
  mysql_affected_rows,
  mysql_autocommit,
  mysql_commit,
  mysql_rollback,
  mysql_insert_id,
  mysql_errno,
  mysql_error,
  mysql_info,
  mysql_thread_id,
  mysql_character_set_name,
  mysql_get_character_set_info,
  mysql_set_character_set,
  mariadb_get_infov,
  mariadb_get_info,
  mysql_init,
  mysql_ssl_set,
  mysql_get_ssl_cipher,
  mysql_change_user,
  mysql_real_connect,
  mysql_close,
  mysql_select_db,
  mysql_query,
  mysql_send_query,
  mysql_read_query_result,
  mysql_real_query,
  mysql_shutdown,
  mysql_dump_debug_info,
  mysql_refresh,
  mysql_kill,
  mysql_ping,
  mysql_stat,
  mysql_get_server_info,
  mysql_get_server_version,
  mysql_get_host_info,
  mysql_get_proto_info,
  mysql_list_dbs,
  mysql_list_tables,
  mysql_list_fields,
  mysql_list_processes,
  mysql_store_result,
  mysql_use_result,
  mysql_options,
  mysql_free_result,
  mysql_data_seek,
  mysql_row_seek,
  mysql_field_seek,
  mysql_fetch_row,
  mysql_fetch_lengths,
  mysql_fetch_field,
  mysql_escape_string,
  mysql_real_escape_string,
  mysql_thread_safe,
  mysql_warning_count,
  mysql_sqlstate,
  mysql_server_init,
  mysql_server_end,
  mysql_thread_end,
  mysql_thread_init,
  mysql_set_server_option,
  mysql_get_client_info,
  mysql_get_client_version,
  mariadb_connection,
  mysql_get_server_name,
  mariadb_get_charset_by_name,
  mariadb_get_charset_by_nr,
  mariadb_convert_string,
  mysql_optionsv,
  mysql_get_optionv,
  mysql_get_option,
  mysql_hex_string,
  mysql_get_socket,
  mysql_get_timeout_value,
  mysql_get_timeout_value_ms,
  mariadb_reconnect,
  mysql_stmt_init,
  mysql_stmt_prepare,
  mysql_stmt_execute,
  mysql_stmt_fetch,
  mysql_stmt_fetch_column,
  mysql_stmt_store_result,
  mysql_stmt_param_count,
  mysql_stmt_attr_set,
  mysql_stmt_attr_get,
  mysql_stmt_bind_param,
  mysql_stmt_bind_result,
  mysql_stmt_close,
  mysql_stmt_reset,
  mysql_stmt_free_result,
  mysql_stmt_send_long_data,
  mysql_stmt_result_metadata,
  mysql_stmt_param_metadata,
  mysql_stmt_errno,
  mysql_stmt_error,
  mysql_stmt_sqlstate,
  mysql_stmt_row_seek,
  mysql_stmt_row_tell,
  mysql_stmt_data_seek,
  mysql_stmt_num_rows,
  mysql_stmt_affected_rows,
  mysql_stmt_insert_id,
  mysql_stmt_field_count,
  mysql_stmt_next_result,
  mysql_stmt_more_results,
  mariadb_stmt_execute_direct,
  mysql_reset_connection
};

/*
 * Default methods for a connection. These methods are
 * stored in mysql->methods and can be overwritten by
 * a plugin, e.g. for using another database
 */
struct st_mariadb_methods MARIADB_DEFAULT_METHODS = {
  /* open a connection */
  mthd_my_real_connect,
  /* close connection */
  mysql_close_slow_part,
  /* send command to server */
  mthd_my_send_cmd,
  /* skip result set */
  mthd_my_skip_result,
  /* read response packet */
  mthd_my_read_query_result,
  /* read all rows from a result set */
  mthd_my_read_rows,
  /* read one/next row */
  mthd_my_read_one_row,
  /* check if datatype is supported */
  mthd_supported_buffer_type,
  /* read response packet from prepare */
  mthd_stmt_read_prepare_response,
  /* read response from stmt execute */
  mthd_my_read_query_result,
  /* get result set metadata for a prepared statement */
  mthd_stmt_get_result_metadata,
  /* get param metadata for a prepared statement */
  mthd_stmt_get_param_metadata,
  /* read all rows (buffered) */
  mthd_stmt_read_all_rows,
  /* fetch one row (unbuffered) */
  mthd_stmt_fetch_row,
  /* store values in bind buffer */
  mthd_stmt_fetch_to_bind,
  /* skip unbuffered stmt result */
  mthd_stmt_flush_unbuffered,
  /* set error */
  my_set_error,
  /* invalidate statements */
  ma_invalidate_stmts,
  /* API functions */
  &MARIADB_API,
  /* read execute response */
  mthd_stmt_read_execute_response,
  /* generate execute request */
  ma_stmt_execute_generate_request,
};
