/************************************************************************************
    Copyright (C) 2015-2018 MariaDB Corporation AB
   
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

/* MariaDB Connection plugin for load balancing  */

#include <ma_global.h>
#include <ma_sys.h>
#include <errmsg.h>
#include <mysql.h>
#include <mysql/client_plugin.h>
#include <string.h>
#include <ma_string.h>
#include <ma_common.h>

#ifndef WIN32
#include <sys/time.h>
#endif

/* function prototypes */
MYSQL *repl_connect(MYSQL *mysql, const char *host, const char *user, const char *passwd,
		    const char *db, unsigned int port, const char *unix_socket, unsigned long clientflag);
void repl_close(MYSQL *mysql);
int repl_command(MYSQL *mysql,enum enum_server_command command, const char *arg,
                      size_t length, my_bool skipp_check, void *opt_arg);
int repl_set_optionsv(MYSQL *mysql, unsigned int option, ...);

#define MARIADB_MASTER 0
#define MARIADB_SLAVE  1

static struct st_mariadb_api *libmariadb_api= NULL;

#ifndef PLUGIN_DYNAMIC
MARIADB_CONNECTION_PLUGIN replication_client_plugin =
#else
MARIADB_CLIENT_PLUGIN_EXPORT MARIADB_CONNECTION_PLUGIN
    _mysql_client_plugin_declaration_=
#endif
{
  MARIADB_CLIENT_CONNECTION_PLUGIN,
  MARIADB_CLIENT_CONNECTION_PLUGIN_INTERFACE_VERSION,
  "replication",
  "Georg Richter",
  "MariaDB connection plugin for load balancing",
  {1, 0, 0},
  "LGPL",
  NULL,
  NULL,
  NULL,
  NULL,
  repl_connect,
  repl_close,
  repl_set_optionsv,
  repl_command,
  NULL,
  NULL
};

typedef struct st_conn_repl {
  MARIADB_PVIO *pvio[2];
  MYSQL *slave_mysql;
  my_bool read_only;
  my_bool round_robin;
  char *url;
  char *host[2];
  unsigned int port[2];
  unsigned int current_type;
} REPL_DATA;

#define SET_SLAVE(mysql, data)\
do {\
  mysql->net.pvio= data->pvio[MARIADB_SLAVE]; \
  data->current_type= MARIADB_SLAVE;\
} while(0)

#define SET_MASTER(mysql, data)\
do {\
  mysql->net.pvio= data->pvio[MARIADB_MASTER];\
  data->current_type= MARIADB_MASTER;\
} while(0)


/* parse url
 * Url has the following format:
 * master[:port],slave1[:port],slave2[:port],..,slaven[:port]
 *
 */

my_bool repl_parse_url(const char *url, REPL_DATA *data)
{
  char *p;
  char *slaves[64];
  int port[64], i,num_slaves= 0;

  if (!url || url[0] == 0)
    return 1;

  memset(slaves, 0, 64 * sizeof(char *));
  memset(&port, 0, 64 * sizeof(int));

  memset(data->host, 0, 2 * sizeof(char *));
  memset(data->port, 0, 2 * sizeof(int));

  if (!data->url)
    data->url= strdup(url);
  data->host[MARIADB_MASTER]= p= data->url;
 
  /* get slaves */ 
  while((p && (p= strchr(p, ','))))
  {
    *p= '\0';
    p++;
    if (*p)
    {
      slaves[num_slaves]= p;
      num_slaves++;
    }
  }

  if (!num_slaves)
    return 0;
  if (num_slaves == 1)
    data->host[MARIADB_SLAVE]= slaves[0];
  else 
  {
    int random_nr;
#ifndef WIN32
    struct timeval tp;
    gettimeofday(&tp,NULL);
    srand(tp.tv_usec / 1000 + tp.tv_sec * 1000);
#else
    srand(GetTickCount());
#endif

    random_nr= rand() % num_slaves;
    data->host[MARIADB_SLAVE]= slaves[random_nr];
  }

  /* check ports */
  for (i=0; i < 2 && data->host[i]; i++)
  {
    /* We need to be aware of IPv6 addresses: According to RFC3986 sect. 3.2.2
       hostnames have to be enclosed in square brackets if a port is given */
    if (data->host[i][0]== '[' && strchr(data->host[i], ':') && (p= strchr(data->host[i],']')))
    {
      /* ignore first square bracket */
      memmove(data->host[i], data->host[i]+1, strlen(data->host[i]) - 1);
      p= strchr(data->host[i],']');
      *p= 0;
      p++;
    }
    else
      p= data->host[i];
    if (p && (p= strchr(p, ':')))
    {
      *p= '\0';
      p++;
      data->port[i]= atoi(p);
    }
  }

  return 0;
}

MYSQL *repl_connect(MYSQL *mysql, const char *host, const char *user, const char *passwd,
		    const char *db, unsigned int port, const char *unix_socket, unsigned long clientflag)
{
  REPL_DATA *data= NULL;
  MA_CONNECTION_HANDLER *hdlr= mysql->extension->conn_hdlr;

  if (!libmariadb_api)
    libmariadb_api= mysql->methods->api;

  if ((data= (REPL_DATA *)hdlr->data))
  {
    data->pvio[MARIADB_MASTER]->methods->close(data->pvio[MARIADB_MASTER]);
    data->pvio[MARIADB_MASTER]= 0;
    repl_close(mysql);
  }

  if (!(data= calloc(1, sizeof(REPL_DATA))))
  {
    mysql->methods->set_error(mysql, CR_OUT_OF_MEMORY, "HY000", 0);
    return NULL;
  }
  memset(data->pvio, 0, 2 * sizeof(MARIADB_PVIO *));

  if (repl_parse_url(host, data))
    goto error;

  /* try to connect to master */
  if (!(libmariadb_api->mysql_real_connect(mysql, data->host[MARIADB_MASTER], user, passwd, db, 
        data->port[MARIADB_MASTER] ? data->port[MARIADB_MASTER] : port, unix_socket, clientflag)))
    goto error;

  data->pvio[MARIADB_MASTER]= mysql->net.pvio;
  hdlr->data= data;
  SET_MASTER(mysql, data);

  /* to allow immediate access without connection delay, we will start
   * connecting to slave(s) in background */

  /* if slave connection will fail, we will not return error but use master instead */
  if (!(data->slave_mysql= libmariadb_api->mysql_init(NULL)) ||
      !(mysql->methods->db_connect(data->slave_mysql, data->host[MARIADB_SLAVE], user, passwd, db, 
                                   data->port[MARIADB_SLAVE] ? data->port[MARIADB_SLAVE] : port, unix_socket, clientflag)))
  {
    if (data->slave_mysql)
      libmariadb_api->mysql_close(data->slave_mysql);
    data->pvio[MARIADB_SLAVE]= NULL;
  }
  else
  {
    data->pvio[MARIADB_SLAVE]= data->slave_mysql->net.pvio;
    data->slave_mysql->net.pvio->mysql= mysql;
  }
  return mysql;
error:
  if (data)
  {
    if (data->url)
      free(data->url);
    free(data);
  }
  return NULL;
}

void repl_close(MYSQL *mysql)
{
  MA_CONNECTION_HANDLER *hdlr= mysql->extension->conn_hdlr;
  REPL_DATA *data= (REPL_DATA *)hdlr->data;

  /* restore master */
  SET_MASTER(mysql, data);

  /* free slave information and close connection */
  if (data->pvio[MARIADB_SLAVE])
  {
    /* restore mysql */
    data->pvio[MARIADB_SLAVE]->mysql= data->slave_mysql;
    libmariadb_api->mysql_close(data->slave_mysql);
    data->pvio[MARIADB_SLAVE]= NULL;
    data->slave_mysql= NULL;
  }

  /* free masrwe information and close connection */
  free(data->url);
  free(data);
  mysql->extension->conn_hdlr->data= NULL;
}

static my_bool is_slave_command(const char *buffer, size_t buffer_len)
{
  const char *buffer_end= buffer + buffer_len;

  for (; buffer < buffer_end; ++buffer)
  {
    char c;
    if (isalpha(c=*buffer))
    {
      if (tolower(c) == 's')
        return 1;
      return 0;
    }
  }
  return 0;
}

static my_bool is_slave_stmt(MYSQL *mysql, const char *buffer)
{
  unsigned long stmt_id= uint4korr(buffer);
  LIST *stmt_list= mysql->stmts;

  for (; stmt_list; stmt_list= stmt_list->next)
  {
    MYSQL_STMT *stmt= (MYSQL_STMT *)stmt_list->data;
    if (stmt->stmt_id == stmt_id)
      return 1;
  }
  return 0;
}


int repl_command(MYSQL *mysql,enum enum_server_command command, const char *arg,
                     size_t length, 
                     my_bool skipp_check __attribute__((unused)), 
                     void *opt_arg __attribute__((unused)))
{
  REPL_DATA *data= (REPL_DATA *)mysql->extension->conn_hdlr->data; 

  /* if we don't have slave or slave became unavailable root traffic to master */
  if (!data->pvio[MARIADB_SLAVE] || !data->read_only)
  {
    SET_MASTER(mysql, data);
    return 0;
  }
  switch(command) {
    case COM_QUERY:
    case COM_STMT_PREPARE:
      if (is_slave_command(arg, length))
        SET_SLAVE(mysql, data)
      else
        SET_MASTER(mysql,data)
      break;
    case COM_STMT_EXECUTE:
    case COM_STMT_FETCH:
      if (data->pvio[MARIADB_SLAVE]->mysql->stmts && is_slave_stmt(data->pvio[MARIADB_SLAVE]->mysql, arg))
        SET_SLAVE(mysql, data)
      else
        SET_MASTER(mysql,data)
      break;

    default:
      SET_MASTER(mysql,data)
      break; 
  }
  return 0;
}

int repl_set_optionsv(MYSQL *mysql, unsigned int option, ...)
{
  REPL_DATA *data= (REPL_DATA *)mysql->extension->conn_hdlr->data;
  va_list ap;
  void *arg1;
  int rc= 0;

  va_start(ap, option);
  arg1= va_arg(ap, void *);

  switch(option) {
  case MARIADB_OPT_CONNECTION_READ_ONLY:
    data->read_only= *(my_bool *)arg1;
    break;
  default:
    rc= -1;
    break;
  }
  va_end(ap);
  return(rc);
}
