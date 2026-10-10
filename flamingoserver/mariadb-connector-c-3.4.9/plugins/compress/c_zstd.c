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
#include <zstd.h>

#ifndef ZSTD_CLEVEL_DEFAULT
#define ZSTD_CLEVEL_DEFAULT 3
#endif

static void ma_free_ctx(ma_compress_ctx *ctx)
{
  if (ctx)
  {
    if (ctx->compress_ctx)
      ZSTD_freeCCtx(ctx->compress_ctx);
    if (ctx->decompress_ctx)
      ZSTD_freeDCtx(ctx->decompress_ctx);
    free(ctx);
  }
}

static ma_compress_ctx *ma_zstd_ctx_init(int compression_level)
{
  ma_compress_ctx *ctx;

  if (!(ctx = (ma_compress_ctx *)calloc(1, sizeof(ma_compress_ctx))))
    return NULL;

  ctx->compression_level= (compression_level == COMPRESSION_LEVEL_DEFAULT) ?
                          ZSTD_CLEVEL_DEFAULT : compression_level;

  if (!(ctx->compress_ctx= (void *)ZSTD_createCCtx()) ||
      !(ctx->decompress_ctx= (void *)ZSTD_createDCtx()))
    goto end;

  return ctx;
end:
  ma_free_ctx(ctx);
  return NULL;
}

static void ma_zstd_ctx_deinit(ma_compress_ctx *ctx)
{
  ma_free_ctx(ctx);
}

my_bool ma_zstd_compress(ma_compress_ctx *ctx, void *dst,
                         size_t *dst_len, void *source, size_t source_len)
{
  size_t rc;
  if (!ctx)
    return 1;

  rc= ZSTD_compressCCtx(ctx->compress_ctx, dst, *dst_len, source, source_len, ctx->compression_level);
  if (ZSTD_isError(rc))
    return 1;
  *dst_len= rc;
  return 0;
}

my_bool ma_zstd_decompress(ma_compress_ctx *ctx, void *dst, size_t *dst_len, 
                           void *source, size_t *source_len)
{
  size_t rc;
  if (!ctx)
    return 1;

  rc= ZSTD_decompressDCtx(ctx->decompress_ctx, dst, *dst_len, source, *source_len);
  if (ZSTD_isError(rc))
  {
    return 1;
  }

  *dst_len= rc;

  return 0;
}

#ifndef PLUGIN_DYNAMIC
MARIADB_COMPRESSION_PLUGIN zstd_client_plugin=
#else
MARIADB_COMPRESSION_PLUGIN _mysql_client_plugin_declaration_ =
#endif
{
  MARIADB_CLIENT_COMPRESSION_PLUGIN,
  MARIADB_CLIENT_COMPRESSION_PLUGIN_INTERFACE_VERSION,
  "zstd",
  "Georg Richter",
  "ZStandard compresson plugin",
  {0,1,0},
  "LGPL",
  NULL,
  NULL,
  NULL,
  NULL,
  ma_zstd_ctx_init,
  ma_zstd_ctx_deinit,
  ma_zstd_compress,
  ma_zstd_decompress,
mysql_end_client_plugin;

