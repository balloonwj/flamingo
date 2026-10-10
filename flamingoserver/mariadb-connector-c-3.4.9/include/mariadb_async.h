/* Copyright (C) 2012 MariaDB Services and Kristian Nielsen
                 2015,2020 MariaDB Corporation AB

   This file is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02111-1301, USA */

/* Common definitions for MariaDB non-blocking client library. */

#ifndef MYSQL_ASYNC_H
#define MYSQL_ASYNC_H

extern int my_connect_async(MARIADB_PVIO *pvio, 
                            const struct sockaddr *name, uint namelen,
                            int vio_timeout);
extern ssize_t my_recv_async(MARIADB_PVIO *pvio,
                             unsigned char *buf, size_t size, int timeout);
extern ssize_t my_send_async(MARIADB_PVIO *pvio,
                             const unsigned char *buf, size_t size,
                             int timeout);
extern my_bool my_io_wait_async(struct mysql_async_context *b,
                                enum enum_pvio_io_event event, int timeout);
#ifdef HAVE_TLS
extern ssize_t ma_tls_read_async(MARIADB_PVIO *pvio, const uchar *buf, size_t size);
extern ssize_t ma_tls_write_async(MARIADB_PVIO *pvio, const uchar *buf, size_t size);
#endif

#endif  /* MYSQL_ASYNC_H */
