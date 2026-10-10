/************************************************************************************
    Copyright (C) 2000, 2012 MySQL AB & MySQL Finland AB & TCX DataKonsult AB,
                 Monty Program AB

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

#ifndef _ma_hashtbl_h
#define _ma_hashtbl_h
#ifdef	__cplusplus
extern "C" {
#endif

typedef uchar *(*hash_get_key)(const uchar *,uint*,my_bool);
typedef void (*hash_free_key)(void *);

  /* flags for hash_init */
#define MA_HASHTBL_CASE_INSENSITIVE	1

typedef struct st_hash_info {
  uint next;					/* index to next key */
  uchar *data;					/* data for current entry */
} MA_HASHTBL_LINK;

typedef struct st_hash {
  uint key_offset,key_length;		/* Length of key if const length */
  uint records,blength,current_record;
  uint flags;
  DYNAMIC_ARRAY array;				/* Place for hash_keys */
  hash_get_key get_key;
  void (*free)(void *);
  uint (*calc_hashnr)(const uchar *key,uint length);
} MA_HASHTBL;

#define ma_hashtbl_init(A,B,C,D,E,F,G) _ma_hashtbl_init(A,B,C,D,E,F,G CALLER_INFO)
my_bool _ma_hashtbl_init(MA_HASHTBL *hash,uint default_array_elements, uint key_offset,
		  uint key_length, hash_get_key get_key,
		  void (*free_element)(void*), uint flags CALLER_INFO_PROTO);
void ma_hashtbl_free(MA_HASHTBL *tree);
uchar *ma_hashtbl_element(MA_HASHTBL *hash,uint idx);
void * ma_hashtbl_search(MA_HASHTBL *info,const uchar *key,uint length);
void * ma_hashtbl_next(MA_HASHTBL *info,const uchar *key,uint length);
my_bool ma_hashtbl_insert(MA_HASHTBL *info,const uchar *data);
my_bool ma_hashtbl_delete(MA_HASHTBL *hash,uchar *record);
my_bool ma_hashtbl_update(MA_HASHTBL *hash,uchar *record,uchar *old_key,uint old_key_length);
my_bool ma_hashtbl_check(MA_HASHTBL *hash);			/* Only in debug library */

#define ma_hashtbl_clear(H) memset((char*) (H), 0,sizeof(*(H)))
#define ma_hashtbl_inited(H) ((H)->array.buffer != 0)

#ifdef	__cplusplus
}
#endif
#endif
