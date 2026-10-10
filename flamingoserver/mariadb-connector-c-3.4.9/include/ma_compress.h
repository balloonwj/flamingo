/************************************************************************************
    Copyright (C) 2022 MariaDB Corporation AB

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
#ifndef __ma_compress_h__

#include <ma_sys.h>

#define COMPRESSION_LEVEL_DEFAULT INT_MAX

#define compression_plugin(net) (net)->extension->compression_plugin
#define compression_ctx(net) (net)->extension->compression_ctx

typedef struct {
  void *compress_ctx;
  void *decompress_ctx;
  int compression_level;
  void *extra; /* reserved */
} ma_compress_ctx;

enum enum_ma_compression_algorithm {
  COMPRESSION_NONE= 0,
  COMPRESSION_ZLIB,
  COMPRESSION_ZSTD,
  COMPRESSION_UNKNOWN
};

const char *_mariadb_compression_algorithm_str(enum enum_ma_compression_algorithm algorithm);
my_bool _mariadb_compress(NET *net, unsigned char *, size_t *, size_t *);
my_bool _mariadb_uncompress(NET *net, unsigned char *, size_t *, size_t *);
unsigned char *_mariadb_compress_alloc(NET *net, const unsigned char *packet, size_t *len, size_t *complen);

#endif
