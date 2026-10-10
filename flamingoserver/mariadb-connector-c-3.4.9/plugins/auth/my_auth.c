#include <ma_global.h>
#include <ma_sys.h>
#include <errmsg.h>
#include <string.h>
#include <ma_common.h>
#include <ma_crypt.h>
#include <mysql/client_plugin.h>

typedef struct st_mysql_client_plugin_AUTHENTICATION auth_plugin_t;
static int client_mpvio_write_packet(struct st_plugin_vio*, const uchar*, int);
static int native_password_auth_client(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql);
static int native_password_hash(MYSQL *mysql, unsigned char *out, size_t *outlen);
static int dummy_fallback_auth_client(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql __attribute__((unused)));
extern void read_user_name(char *name);
extern char *ma_send_connect_attr(MYSQL *mysql, unsigned char *buffer);
extern int ma_read_ok_packet(MYSQL *mysql, uchar *pos, ulong length);
extern unsigned char *mysql_net_store_length(unsigned char *packet, ulonglong length);
extern const char *disabled_plugins;

#define hashing(p)  (p->interface_version >= 0x0101 && p->hash_password_bin)
#define password_and_hashing(m,p) ((m)->passwd && (m)->passwd[0] && hashing((p)))

typedef struct {
  int (*read_packet)(struct st_plugin_vio *vio, uchar **buf);
  int (*write_packet)(struct st_plugin_vio *vio, const uchar *pkt, int pkt_len);
  void (*info)(struct st_plugin_vio *vio, struct st_plugin_vio_info *info);
  /* -= end of MYSQL_PLUGIN_VIO =- */
  MYSQL *mysql;
  auth_plugin_t *plugin;             /**< what plugin we're under */
  const char *db;
  struct {
    uchar *pkt;                      /**< pointer into NET::buff */
    uint pkt_len;
  } cached_server_reply;
  uint packets_read, packets_written; /**< counters for send/received packets */
  my_bool mysql_change_user;          /**< if it's mysql_change_user() */
  int last_read_packet_len;           /**< the length of the last *read* packet */
} MCPVIO_EXT;
/*
#define compile_time_assert(A) \
do {\
  typedef char constraint[(A) ? 1 : -1];\
} while (0);
*/

auth_plugin_t mysql_native_password_client_plugin=
{
  MYSQL_CLIENT_AUTHENTICATION_PLUGIN,
  MYSQL_CLIENT_AUTHENTICATION_PLUGIN_INTERFACE_VERSION,
  native_password_plugin_name,
  "R.J.Silk, Sergei Golubchik",
  "Native MySQL authentication",
  {1, 0, 1},
  "LGPL",
  NULL,
  NULL,
  NULL,
  NULL,
  native_password_auth_client,
  native_password_hash
};

/**
  Checks if self-signed certificate error should be ignored.
*/
static my_bool is_local_connection(MARIADB_PVIO *pvio)
{
  const char *hostname= pvio->mysql->host;
  const char *local_host_names[]= {
#ifdef _WIN32
  /*
   On Unix, we consider TCP connections with "localhost"
   an insecure transport, for the single reason to run tests for
   insecure transport on CI.This is artificial, but should be ok.
   Default client connections use unix sockets anyway, so it
   would not hurt much.

   On Windows, the situation is quite different.
   Default connections type is TCP, default host name is "localhost",
   non-password plugin gssapi is common (every installation)
   In this environment, there would be a lot of faux/disruptive
   "self-signed certificates" errors there. Thus, "localhost" TCP
   needs to be considered secure transport.
  */
  "localhost",
#endif
  "127.0.0.1", "::1", NULL};
  int i;

  if (pvio->type != PVIO_TYPE_SOCKET)
  {
    return TRUE;
  }
  if (!hostname)
    return FALSE;
  for (i= 0; local_host_names[i]; i++)
  {
    if (strcmp(hostname, local_host_names[i]) == 0)
    {
      return TRUE;
    }
  }
  return FALSE;
}


static int native_password_auth_client(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql)
{
  int pkt_len;
  uchar *pkt;

  if (((MCPVIO_EXT *)vio)->mysql_change_user)
  {
    /*
      in mysql_change_user() the client sends the first packet.
      we use the old scramble.
    */
    pkt= (uchar*)mysql->scramble_buff;
    pkt_len= SCRAMBLE_LENGTH + 1;
  }
  else
  {
    /* read the scramble */
    if ((pkt_len= vio->read_packet(vio, &pkt)) < 0)
      return CR_ERROR;

    if (pkt_len != SCRAMBLE_LENGTH + 1)
      return CR_SERVER_HANDSHAKE_ERR;

    /* save it in MYSQL */
    memmove(mysql->scramble_buff, pkt, SCRAMBLE_LENGTH);
    mysql->scramble_buff[SCRAMBLE_LENGTH] = 0;
  }

  if (mysql && mysql->passwd[0])
  {
    char scrambled[SCRAMBLE_LENGTH + 1];
    memset(scrambled, 0, SCRAMBLE_LENGTH + 1);
    if (ma_scramble_41((uchar *)scrambled, (char*)pkt, mysql->passwd))
    {
      SET_CLIENT_ERROR(mysql, CR_AUTH_PLUGIN_ERR, SQLSTATE_UNKNOWN,
                       "SHA1 hash computation failed");
      return CR_ERROR;
    }
    if (vio->write_packet(vio, (uchar*)scrambled, SCRAMBLE_LENGTH))
      return CR_ERROR;
  }
  else
    if (vio->write_packet(vio, 0, 0)) /* no password */
      return CR_ERROR;

  return CR_OK;
}

static int native_password_hash(MYSQL *mysql, unsigned char *out, size_t *out_length)
{
  unsigned char digest[MA_SHA1_HASH_SIZE];

  if (*out_length < MA_SHA1_HASH_SIZE)
    return 1;
  *out_length= MA_SHA1_HASH_SIZE;

  /* would it be better to reuse instead of recalculating here? see ed25519 */
  if (ma_hash(MA_HASH_SHA1, (unsigned char*)mysql->passwd, strlen(mysql->passwd),
              digest))
    return 1;
  if (ma_hash(MA_HASH_SHA1, digest, sizeof(digest), out))
    return 1;

  return 0;
}

auth_plugin_t dummy_fallback_client_plugin=
{
  MYSQL_CLIENT_AUTHENTICATION_PLUGIN,
  MYSQL_CLIENT_AUTHENTICATION_PLUGIN_INTERFACE_VERSION,
  "dummy_fallback_auth",
  "Sergei Golubchik",
  "Dummy fallback plugin",
  {1, 0, 0},
  "LGPL",
  NULL,
  NULL,
  NULL,
  NULL,
  dummy_fallback_auth_client,
  NULL
};


static int dummy_fallback_auth_client(MYSQL_PLUGIN_VIO *vio, MYSQL *mysql __attribute__((unused)))
{
  char last_error[MYSQL_ERRMSG_SIZE];
  unsigned int i, last_errno= ((MCPVIO_EXT *)vio)->mysql->net.last_errno;
  if (last_errno)
  {
    memcpy(last_error, ((MCPVIO_EXT *)vio)->mysql->net.last_error,
            sizeof(last_error) - 1);
    last_error[sizeof(last_error) - 1]= 0;
  }

  /* safety-wise we only do 10 round-trips */
  for (i=0; i < 10; i++)
  {
    uchar *pkt;
    if (vio->read_packet(vio, &pkt) < 0)
      break;
    if (vio->write_packet(vio, 0, 0))
      break;
  }
  if (last_errno)
  {
    MYSQL *mysql= ((MCPVIO_EXT *)vio)->mysql;
    memcpy(mysql->net.last_error, last_error,
            sizeof(mysql->net.last_error) - 1);
    mysql->net.last_error[sizeof(mysql->net.last_error) - 1]= 0;
  }
  return CR_ERROR;
}

static int send_change_user_packet(MCPVIO_EXT *mpvio,
                                   const uchar *data, int data_len)
{
  MYSQL *mysql= mpvio->mysql;
  char *buff, *end;
  int res= 1;
  size_t conn_attr_len= (mysql->options.extension) ? 
                         mysql->options.extension->connect_attrs_len : 0;

  buff= malloc(USERNAME_LENGTH+1 + data_len+1 + NAME_LEN+1 + 2 + NAME_LEN+1 + 9 + conn_attr_len);

  end= ma_strmake(buff, mysql->user, USERNAME_LENGTH) + 1;

  if (!data_len)
    *end++= 0;
  else
  {
    if (mysql->client_flag & CLIENT_SECURE_CONNECTION)
    {
      if (mysql->server_capabilities & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA)
      {
        end= (char *)mysql_net_store_length((uchar *)end, data_len);
      }
      else
      {
        if (data_len > 255)
        {
          my_set_error(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
          goto error;
        }
        *end++= data_len;
      }
    }
    else
    {
      DBUG_ASSERT(data_len == SCRAMBLE_LENGTH_323 + 1);
      DBUG_ASSERT(data[SCRAMBLE_LENGTH_323] == 0);
    }
    memcpy(end, data, data_len);
    end+= data_len;
  }
  end= ma_strmake(end, mpvio->db ? mpvio->db : "", NAME_LEN) + 1;

  if (mysql->server_capabilities & CLIENT_PROTOCOL_41)
  {
    int2store(end, (ushort) mysql->charset->nr);
    end+= 2;
  }

  if (mysql->server_capabilities & CLIENT_PLUGIN_AUTH)
    end= ma_strmake(end, mpvio->plugin->name, NAME_LEN) + 1;

  end= ma_send_connect_attr(mysql, (unsigned char *)end);

  res= ma_simple_command(mysql, COM_CHANGE_USER,
                      buff, (ulong)(end-buff), 1, NULL);

error:
  free(buff);
  return res;
}

#define MARIADB_TLS_VERIFY_AUTO (MARIADB_TLS_VERIFY_HOST | MARIADB_TLS_VERIFY_TRUST)

static int send_client_reply_packet(MCPVIO_EXT *mpvio,
                                    const uchar *data, int data_len)
{
  MYSQL *mysql= mpvio->mysql;
  NET *net= &mysql->net;
  char *buff, *end;
  size_t conn_attr_len= (mysql->options.extension) ? 
                         mysql->options.extension->connect_attrs_len : 0;
  size_t proxy_header_len= 0;
  char *proxy_header=
      (mysql->options.extension) ? mysql->options.extension->proxy_header : NULL;
  if (proxy_header)
    proxy_header_len= mysql->options.extension->proxy_header_len;

  /* see end= buff+32 below, fixed size of the packet is 32 bytes */
  buff= malloc(33 + USERNAME_LENGTH + data_len + NAME_LEN + NAME_LEN + conn_attr_len + 9);
  end= buff;

  mysql->client_flag|= mysql->options.client_flag;
  mysql->client_flag|= CLIENT_CAPABILITIES;

  if (mysql->client_flag & CLIENT_MULTI_STATEMENTS)
    mysql->client_flag|= CLIENT_MULTI_RESULTS;

#if defined(HAVE_TLS) && !defined(EMBEDDED_LIBRARY)
  if (mysql->options.ssl_key || mysql->options.ssl_cert ||
      mysql->options.ssl_ca || mysql->options.ssl_capath ||
      mysql->options.ssl_cipher || mysql->options.use_ssl ||
      mysql->options.extension->tls_fp || mysql->options.extension->tls_fp_list ||
      !mysql->options.extension->tls_allow_invalid_server_cert)
    mysql->options.use_ssl= 1;
  if (mysql->options.use_ssl)
    mysql->client_flag|= CLIENT_SSL;
#endif /* HAVE_TLS && !EMBEDDED_LIBRARY*/
  if (mpvio->db)
    mysql->client_flag|= CLIENT_CONNECT_WITH_DB;
  else
    /* See CONC-490: If no database was specified, we need
       to unset CLIENT_CONNECT_WITH_DB flag */
    mysql->client_flag&= ~CLIENT_CONNECT_WITH_DB;

  /* CONC-635: For connections via named pipe or shared memory the server
               indicates the capability for secure connections (TLS), but
               doesn't support it. */
  if ((mysql->server_capabilities & CLIENT_SSL) &&
      (mysql->net.pvio->type == PVIO_TYPE_NAMEDPIPE ||
       mysql->net.pvio->type == PVIO_TYPE_SHAREDMEM))
  {
    mysql->server_capabilities &= ~(CLIENT_SSL);
    mysql->options.extension->tls_allow_invalid_server_cert= 1;
  }

  /* if server doesn't support SSL and verification of server certificate
     was set to mandatory, we need to return an error */
  if (mysql->options.use_ssl && !(mysql->server_capabilities & CLIENT_SSL))
  {
    if (!mysql->options.extension->tls_allow_invalid_server_cert ||
        mysql->options.extension->tls_fp ||
        mysql->options.extension->tls_fp_list)
    {
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                          ER(CR_SSL_CONNECTION_ERROR), 
                          "SSL is required, but the server does not support it");
      goto error;
    }
  }


  /* Remove options that server doesn't support */
  mysql->client_flag= mysql->client_flag &
                       (~(CLIENT_COMPRESS | CLIENT_ZSTD_COMPRESSION | CLIENT_SSL | CLIENT_PROTOCOL_41) 
                       | mysql->server_capabilities);

  /* save compress for reconnect */
  if (mysql->client_flag & CLIENT_COMPRESS)
    mysql->options.compress= 1;

  if (mysql->options.compress && (mysql->server_capabilities & CLIENT_COMPRESS))
  {
    /* For MySQL 8.0 we will use zstd compression */
    if (mysql->server_capabilities & CLIENT_ZSTD_COMPRESSION)
    {
      if ((compression_plugin(net) = (MARIADB_COMPRESSION_PLUGIN *)mysql_client_find_plugin(mysql, 
                                    _mariadb_compression_algorithm_str(COMPRESSION_ZSTD),
                                    MARIADB_CLIENT_COMPRESSION_PLUGIN)))
      {
        mysql->client_flag|= CLIENT_ZSTD_COMPRESSION;
        mysql->client_flag&= ~CLIENT_COMPRESS;
      }
    }
    /* load zlib compression as default */
    if (!compression_plugin(net))
    {
      if ((compression_plugin(net) = (MARIADB_COMPRESSION_PLUGIN *)mysql_client_find_plugin(mysql, 
                                    _mariadb_compression_algorithm_str(COMPRESSION_ZLIB),
                                    MARIADB_CLIENT_COMPRESSION_PLUGIN)))
      {
        mysql->client_flag|= CLIENT_COMPRESS;
      }
    }
  }

  if (mysql->client_flag & CLIENT_PROTOCOL_41)
  {
    /* 4.1 server and 4.1 client has a 32 byte option flag */
    if (!(mysql->server_capabilities & CLIENT_MYSQL))
      mysql->client_flag&= ~CLIENT_MYSQL;
    int4store(buff,mysql->client_flag);
    int4store(buff+4, net->max_packet_size);
    buff[8]= (char) mysql->charset->nr;
    memset(buff + 9, 0, 32-9);
    if (!(mysql->server_capabilities & CLIENT_MYSQL))
    {
      uint server_extended_cap= mysql->extension->mariadb_server_capabilities;
      ulonglong client_extended_flag = CLIENT_DEFAULT_EXTENDED_FLAGS;
      if (mysql->options.extension && mysql->options.extension->bulk_unit_results)
        client_extended_flag|= MARIADB_CLIENT_BULK_UNIT_RESULTS;
      mysql->extension->mariadb_client_flag=
          server_extended_cap & (long)(client_extended_flag >> 32);
      int4store(buff + 28, mysql->extension->mariadb_client_flag);
    }
    end= buff+32;
  }
  else
  {
    int2store(buff, mysql->client_flag);
    int3store(buff+2, net->max_packet_size);
    end= buff+5;
  }
#ifdef HAVE_TLS
  if (mysql->options.ssl_key ||
      mysql->options.ssl_cert ||
      mysql->options.ssl_ca ||
      mysql->options.ssl_capath ||
      mysql->options.ssl_cipher
#ifdef CRL_IMPLEMENTED
      || (mysql->options.extension &&
       (mysql->options.extension->ssl_crl ||
        mysql->options.extension->ssl_crlpath))
#endif
      )
    mysql->options.use_ssl= 1;
  if (mysql->options.use_ssl &&
      (mysql->client_flag & CLIENT_SSL))
  {
    unsigned int verify_flags= 0;
    /*
      Send mysql->client_flag, max_packet_size - unencrypted otherwise
      the server does not know we want to do SSL
    */
    if (proxy_header_len)
    {
      ma_net_write_buff(net, proxy_header, proxy_header_len);
      /* Reset proxy header */
      proxy_header_len= 0;
      proxy_header= NULL;
    }

    if (ma_net_write(net, (unsigned char *)buff, (size_t) (end-buff)) || ma_net_flush(net))
    {
      my_set_error(mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                          ER(CR_SERVER_LOST_EXTENDED),
                          "sending connection information to server",
                          errno);
      goto error;
    }
    mysql->net.tls_verify_status = 0;
    if (ma_pvio_start_ssl(mysql->net.pvio))
      goto error;

    verify_flags= MARIADB_TLS_VERIFY_PERIOD;

    /* Don't check for revocation if CRL not provided */
    if (mysql->options.extension &&
       (mysql->options.extension->ssl_crl || mysql->options.extension->ssl_crlpath))
    {
      verify_flags|= MARIADB_TLS_VERIFY_REVOKED;
    }

    if (have_fingerprint(mysql))
    {
      verify_flags|= MARIADB_TLS_VERIFY_FINGERPRINT;
    } else {
      /*
        Don't check host name on local (non globally resolvable) addresses
        For local connections, only check CA if CA is given.
      */
      if (!is_local_connection(mysql->net.pvio))
        verify_flags |= MARIADB_TLS_VERIFY_HOST|MARIADB_TLS_VERIFY_TRUST;
      else if (mysql->options.ssl_ca || mysql->options.ssl_capath)
        verify_flags |= MARIADB_TLS_VERIFY_TRUST;
    }

    if (mysql->options.extension->tls_verification_callback(mysql->net.pvio->ctls, verify_flags))
    {
      if (mysql->net.tls_verify_status > MARIADB_TLS_VERIFY_AUTO ||
          (mysql->options.ssl_ca || mysql->options.ssl_capath))
        goto error;
      if (!password_and_hashing(mysql, mpvio->plugin))
        goto error;
    }
  }
#endif /* HAVE_TLS */

  /* This needs to be changed as it's not useful with big packets */
  if (mysql->user && mysql->user[0])
    ma_strmake(end, mysql->user, USERNAME_LENGTH);
  else
    read_user_name(end);

  /* We have to handle different version of handshake here */
  end+= strlen(end) + 1;
  if (data_len)
  {
    if (mysql->server_capabilities & CLIENT_SECURE_CONNECTION)
    {
      if (mysql->server_capabilities & CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA)
      {
        end= (char *)mysql_net_store_length((uchar *)end, data_len);
      }
      else {
        /* Without CLIENT_PLUGIN_AUTH_LENENC_CLIENT_DATA capability password
           length is limited up to 255 chars */
        if (data_len > 0xFF)
          goto error;
        *end++= data_len;
      }
      memcpy(end, data, data_len);
      end+= data_len;
    }
    else
    {
      DBUG_ASSERT(data_len == SCRAMBLE_LENGTH_323 + 1); /* incl. \0 at the end */
      memcpy(end, data, data_len);
      end+= data_len;
    }
  }
  else
    *end++= 0;

  /* Add database if needed */
  if (mpvio->db && (mysql->server_capabilities & CLIENT_CONNECT_WITH_DB))
  {
    end= ma_strmake(end, mpvio->db, NAME_LEN) + 1;
    mysql->db= strdup(mpvio->db);
  }

  if (mysql->server_capabilities & CLIENT_PLUGIN_AUTH)
    end= ma_strmake(end, mpvio->plugin->name, NAME_LEN) + 1;

  end= ma_send_connect_attr(mysql, (unsigned char *)end);

  /* MySQL 8.0: 
     If zstd compresson was specified, the server expects
     1 byte for compression level
  */
  if (mysql->client_flag & CLIENT_ZSTD_COMPRESSION)
  {
    uchar compression_level= 3;
    if (mysql->options.extension &&
        mysql->options.extension->zstd_compression_level >= 1 &&
        mysql->options.extension->zstd_compression_level <= 20)
    {
        compression_level= mysql->options.extension->zstd_compression_level;
    }
    *end++= compression_level;
  }

  if (proxy_header_len)
    ma_net_write_buff(net, proxy_header, proxy_header_len);
  /* Write authentication package */
  if (ma_net_write(net, (unsigned char *)buff, (size_t) (end-buff)) || ma_net_flush(net))
  {
    my_set_error(mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                        ER(CR_SERVER_LOST_EXTENDED),
                        "sending authentication information",
                        errno);
    goto error;
  }
  free(buff);
  return 0;

error:
  free(buff);
  return 1;
}

/**
  vio->read_packet() callback method for client authentication plugins

  This function is called by a client authentication plugin, when it wants
  to read data from the server.
*/

static int client_mpvio_read_packet(struct st_plugin_vio *mpv, uchar **buf)
{
  MCPVIO_EXT *mpvio= (MCPVIO_EXT*)mpv;
  MYSQL *mysql= mpvio->mysql;
  ulong  pkt_len;

  /* there are cached data left, feed it to a plugin */
  if (mpvio->cached_server_reply.pkt)
  {
    *buf= mpvio->cached_server_reply.pkt;
    mpvio->cached_server_reply.pkt= 0;
    mpvio->packets_read++;
    return mpvio->cached_server_reply.pkt_len;
  }

  if (mpvio->packets_read == 0)
  {
    /*
      the server handshake packet came from the wrong plugin,
      or it's mysql_change_user(). Either way, there is no data
      for a plugin to read. send a dummy packet to the server
      to initiate a dialog.
    */
    if (client_mpvio_write_packet(mpv, 0, 0))
      return (int)packet_error;
  }

  /* otherwise read the data */
  if ((pkt_len= ma_net_safe_read(mysql)) == packet_error)
    return (int)packet_error;

  mpvio->last_read_packet_len= pkt_len;
  *buf= mysql->net.read_pos;

  /* was it a request to change plugins ? */
  if (pkt_len && **buf == 254)
    return (int)packet_error; /* if yes, this plugin shan't continue */

  /*
    the server sends \1\255 or \1\254 instead of just \255 or \254 -
    for us to not confuse it with an error or "change plugin" packets.
    We remove this escaping \1 here.

    See also server_mpvio_write_packet() where the escaping is done.
  */
  if (pkt_len && **buf == 1)
  {
    (*buf)++;
    pkt_len--;
  }
  mpvio->packets_read++;
  return pkt_len;
}

/**
  vio->write_packet() callback method for client authentication plugins

  This function is called by a client authentication plugin, when it wants
  to send data to the server.

  It transparently wraps the data into a change user or authentication
  handshake packet, if necessary.
*/

static int client_mpvio_write_packet(struct st_plugin_vio *mpv,
                                     const uchar *pkt, int pkt_len)
{
  int res;
  MCPVIO_EXT *mpvio= (MCPVIO_EXT*)mpv;

  if (mpvio->packets_written == 0)
  {
    if (mpvio->mysql_change_user)
      res= send_change_user_packet(mpvio, pkt, pkt_len);
    else
      res= send_client_reply_packet(mpvio, pkt, pkt_len);
  }
  else
  {
    NET *net= &mpvio->mysql->net;
    if (mpvio->mysql->thd)
      res= 1; /* no chit-chat in embedded */
    else
      res= ma_net_write(net, (unsigned char *)pkt, pkt_len) || ma_net_flush(net);
  }

  if (res)
  {
    /* don't overwrite errors */
    if (!mysql_errno(mpvio->mysql))
      my_set_error(mpvio->mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                                 ER(CR_SERVER_LOST_EXTENDED),
                                 "sending authentication information",
                                 errno);
  }
  mpvio->packets_written++;
  return res;
}

/**
  fills MYSQL_PLUGIN_VIO_INFO structure with the information about the
  connection
*/

void mpvio_info(MARIADB_PVIO *pvio, MYSQL_PLUGIN_VIO_INFO *info)
{
  memset(info, 0, sizeof(*info));
  switch (pvio->type) {
  case PVIO_TYPE_SOCKET:
    info->protocol= MYSQL_VIO_TCP;
    ma_pvio_get_handle(pvio, &info->socket);
    return;
  case PVIO_TYPE_UNIXSOCKET:
    info->protocol= MYSQL_VIO_SOCKET;
    ma_pvio_get_handle(pvio, &info->socket);
    return;
    /*
  case VIO_TYPE_SSL:
    {
      struct sockaddr addr;
      SOCKET_SIZE_TYPE addrlen= sizeof(addr);
      if (getsockname(vio->sd, &addr, &addrlen))
        return;
      info->protocol= addr.sa_family == AF_UNIX ?
        MYSQL_VIO_SOCKET : MYSQL_VIO_TCP;
      info->socket= vio->sd;
      return;
    }
    */
#ifdef _WIN32
    /*
  case VIO_TYPE_NAMEDPIPE:
    info->protocol= MYSQL_VIO_PIPE;
    info->handle= vio->hPipe;
    return;
    */
/* not supported yet
  case VIO_TYPE_SHARED_MEMORY:
    info->protocol= MYSQL_VIO_MEMORY;
    info->handle= vio->handle_file_map; 
    return;
*/
#endif
  default: DBUG_ASSERT(0);
  }
}

static void client_mpvio_info(MYSQL_PLUGIN_VIO *vio,
                              MYSQL_PLUGIN_VIO_INFO *info)
{
  MCPVIO_EXT *mpvio= (MCPVIO_EXT*)vio;
  mpvio_info(mpvio->mysql->net.pvio, info);
}

/**
  Client side of the plugin driver authentication.

  @note this is used by both the mysql_real_connect and mysql_change_user

  @param mysql       mysql
  @param data        pointer to the plugin auth data (scramble) in the
                     handshake packet
  @param data_len    the length of the data
  @param data_plugin a plugin that data were prepared for
                     or 0 if it's mysql_change_user()
  @param db          initial db to use, can be 0

  @retval 0 ok
  @retval 1 error
*/

int run_plugin_auth(MYSQL *mysql, char *data, uint data_len,
                    const char *data_plugin, const char *db)
{
  const char    *auth_plugin_name= NULL;
  auth_plugin_t *auth_plugin;
  MCPVIO_EXT    mpvio;
  ulong		pkt_length;
  int           res;


  /* determine the default/initial plugin to use */
  if (mysql->server_capabilities & CLIENT_PLUGIN_AUTH)
  {
    if (mysql->options.extension && mysql->options.extension->default_auth)
      auth_plugin_name= mysql->options.extension->default_auth;
    else if (data_plugin)
      auth_plugin_name= data_plugin;
  }
  if (!auth_plugin_name)
  {
    if (mysql->server_capabilities & CLIENT_PROTOCOL_41)
       auth_plugin_name= native_password_plugin_name;
    else
       auth_plugin_name= "mysql_old_password";
  }
  if (!(auth_plugin= (auth_plugin_t*) mysql_client_find_plugin(mysql,
                     auth_plugin_name, MYSQL_CLIENT_AUTHENTICATION_PLUGIN)))
    auth_plugin= &dummy_fallback_client_plugin;

  mysql->net.last_errno= 0; /* just in case */

  if (data_plugin && strcmp(data_plugin, auth_plugin_name))
  {
    /* data was prepared for a different plugin, so we don't
       send any data */
    data= 0;
    data_len= 0;
  }

  mpvio.mysql_change_user= data_plugin == 0;
  mpvio.cached_server_reply.pkt= (uchar*)data;
  mpvio.cached_server_reply.pkt_len= data_len;
  mpvio.read_packet= client_mpvio_read_packet;
  mpvio.write_packet= client_mpvio_write_packet;
  mpvio.info= client_mpvio_info;
  mpvio.mysql= mysql;
  mpvio.packets_read= mpvio.packets_written= 0;
  mpvio.db= db;

retry:
  mpvio.plugin= auth_plugin;

  if (auth_plugin_name)
  {
    if ((mysql->options.extension && mysql->options.extension->restricted_auth)
        ? !strstr(mysql->options.extension->restricted_auth, auth_plugin_name)
        : strstr(disabled_plugins, auth_plugin_name) != NULL)
    {
      my_set_error(mysql, CR_PLUGIN_NOT_ALLOWED, SQLSTATE_UNKNOWN, 0, auth_plugin_name);
      return 1;
    }
  }

  mysql->net.read_pos[0]= 0;
  res= auth_plugin->authenticate_user((struct st_plugin_vio *)&mpvio, mysql);

  if ((res == CR_ERROR && !mysql->net.buff) ||
      (res > CR_OK && mysql->net.read_pos[0] != 254))
  {
    /*
      the plugin returned an error. write it down in mysql,
      unless the error code is CR_ERROR and mysql->net.last_errno
      is already set (the plugin has done it)
    */
    if (res > CR_ERROR)
      my_set_error(mysql, res, SQLSTATE_UNKNOWN, 0);
    else
      if (!mysql->net.last_errno) {
        my_set_error(mysql, CR_UNKNOWN_ERROR, SQLSTATE_UNKNOWN, 0);
      }
    return 1;
  }

  /* read the OK packet (or use the cached value in mysql->net.read_pos */
  if (res == CR_OK)
    pkt_length= ma_net_safe_read(mysql);
  else /* res == CR_OK_HANDSHAKE_COMPLETE or an error */
    pkt_length= mpvio.last_read_packet_len;

  if (pkt_length == packet_error)
  {
    if (mysql->net.last_errno == CR_SERVER_LOST)
      my_set_error(mysql, CR_SERVER_LOST, SQLSTATE_UNKNOWN,
                          ER(CR_SERVER_LOST_EXTENDED),
                          "reading authorization packet",
                          errno);
    return 1;
  }
  if (mysql->net.read_pos[0] == 254)
  {
    /* The server asked to use a different authentication plugin */
    if (pkt_length == 1)
    {
      /* old "use short scramble" packet */
      auth_plugin_name= old_password_plugin_name;
      mpvio.cached_server_reply.pkt= (uchar*)mysql->scramble_buff;
      mpvio.cached_server_reply.pkt_len= SCRAMBLE_LENGTH + 1;
    }
    else
    {
      /* new "use different plugin" packet */
      uint len;
      auth_plugin_name= (char*)mysql->net.read_pos + 1;
      len= (uint)strlen(auth_plugin_name); /* safe as ma_net_read always appends \0 */
      mpvio.cached_server_reply.pkt_len= pkt_length - len - 2;
      mpvio.cached_server_reply.pkt= mysql->net.read_pos + len + 2;
    }
    if (!(auth_plugin= (auth_plugin_t *) mysql_client_find_plugin(mysql,
                         auth_plugin_name, MYSQL_CLIENT_AUTHENTICATION_PLUGIN)))
      auth_plugin= &dummy_fallback_client_plugin;

    /* can we use this plugin with this tls server cert ? */
    if ((mysql->net.tls_verify_status & MARIADB_TLS_VERIFY_TRUST) &&
        !password_and_hashing(mysql, auth_plugin))
    {
      my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
                   ER(CR_SSL_CONNECTION_ERROR),
                   "Certificate verification failure: The certificate is NOT trusted.");
      return 1;
    }
    goto retry;
  }
  /*
    net->read_pos[0] should always be 0 here if the server implements
    the protocol correctly
  */
  if (mysql->net.read_pos[0] != 0)
    return 1;
  if (ma_read_ok_packet(mysql, mysql->net.read_pos + 1, pkt_length))
    return -1;

  if (!mysql->net.tls_verify_status)
    return 0;

  assert(mysql->options.use_ssl);
  assert(!mysql->options.extension->tls_allow_invalid_server_cert);
  assert(!mysql->options.ssl_ca);
  assert(!mysql->options.ssl_capath);
  assert(!mysql->options.extension->tls_fp);
  assert(!mysql->options.extension->tls_fp_list);
  assert(hashing(auth_plugin));
  assert(mysql->passwd[0]);
  if (mysql->info && mysql->info[0] == '\1')
  {
    MA_HASH_CTX *ctx = NULL;
    unsigned char buf[1024], digest[MA_SHA256_HASH_SIZE];
    char fp[128], hexdigest[sizeof(digest)*2+1], *hexsig= mysql->info + 1;
    size_t buflen= sizeof(buf) - 1, fplen;

    mysql->info= NULL; /* no need to confuse the client with binary info */

    if (!(fplen= ma_tls_get_finger_print(mysql->net.pvio->ctls, MA_HASH_SHA256,
                                         fp, sizeof(fp))))
      return 1; /* error is already set */

    if (auth_plugin->hash_password_bin(mysql, buf, &buflen) ||
        !(ctx= ma_hash_new(MA_HASH_SHA256)))
    {
      SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      return 1;
    }

    ma_hash_input(ctx, (unsigned char*)buf, buflen);
    ma_hash_input(ctx, (unsigned char*)mysql->scramble_buff, SCRAMBLE_LENGTH);
    ma_hash_input(ctx, (unsigned char*)fp, fplen);
    ma_hash_result(ctx, digest);
    ma_hash_free(ctx);

    mysql_hex_string(hexdigest, (char*)digest, sizeof(digest));
    if (strcmp(hexdigest, hexsig) == 0)
      return 0; /* phew. self-signed certificate is validated! */
  }

  my_set_error(mysql, CR_SSL_CONNECTION_ERROR, SQLSTATE_UNKNOWN,
               ER(CR_SSL_CONNECTION_ERROR),
               "Certificate verification failure: The certificate is NOT trusted.");
  return 1;
}

