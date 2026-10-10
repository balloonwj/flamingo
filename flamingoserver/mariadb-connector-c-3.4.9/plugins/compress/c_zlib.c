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

#include <ma_global.h>
#include <ma_sys.h>
#include <mysql.h>
#include <mysql/client_plugin.h>
#include <string.h>
#include <memory.h>

#include <stdio.h>
#include <string.h>
#include <zlib.h>

static ma_compress_ctx *ma_zlib_ctx_init(int compression_level)
{
  ma_compress_ctx *ctx;

  if (!(ctx = (ma_compress_ctx *)calloc(1, sizeof(ma_compress_ctx))))
    return NULL;

  ctx->compression_level= (compression_level == COMPRESSION_LEVEL_DEFAULT) ?
                          Z_DEFAULT_COMPRESSION : compression_level;
  return ctx;
}

static void ma_zlib_ctx_deinit(ma_compress_ctx *ctx)
{
    free(ctx);
}

my_bool ma_zlib_compress(ma_compress_ctx *ctx, void *dst,
                         size_t *dst_len, void *source, size_t source_len)
{
  int rc;
  if (!ctx)
    return 1;

  if ((rc= compress2((Bytef *)dst, (uLongf *)dst_len, (Bytef *)source, (uLong)source_len,
                   ctx->compression_level)) != Z_OK)
    return 1;
  return 0;
}

my_bool ma_zlib_decompress(ma_compress_ctx *ctx, void *dst, size_t *dst_len, 
                           void *source, size_t *source_len)
{
  int rc;
  if (!ctx)
    return 1;

  rc= uncompress((Bytef*) dst, (uLongf *)dst_len, (Bytef*) source, (uLongf)*source_len);

  if (rc != Z_OK)
    return 1;

  return 0;
}

#ifndef PLUGIN_DYNAMIC
MARIADB_COMPRESSION_PLUGIN zlib_client_plugin=
#else
MARIADB_CLIENT_PLUGIN_EXPORT MARIADB_COMPRESSION_PLUGIN
    _mysql_client_plugin_declaration_=
#endif
{
  MARIADB_CLIENT_COMPRESSION_PLUGIN,
  MARIADB_CLIENT_COMPRESSION_PLUGIN_INTERFACE_VERSION,
  "zlib",
  "Georg Richter",
  "zlib compresson plugin",
  {0,1,0},
  "LGPL",
  NULL,
  NULL,
  NULL,
  NULL,
  ma_zlib_ctx_init,
  ma_zlib_ctx_deinit,
  ma_zlib_compress,
  ma_zlib_decompress,
mysql_end_client_plugin;

