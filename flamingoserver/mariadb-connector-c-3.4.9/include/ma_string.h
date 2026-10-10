/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB
                 2012 by MontyProgram AB

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02111-1301, USA */

/* defines for the libmariadb library */

#ifndef _ma_string_h_
#define _ma_string_h_

#include <string.h>

typedef enum {
  MY_GCVT_ARG_FLOAT,
  MY_GCVT_ARG_DOUBLE
} my_gcvt_arg_type;

size_t ma_fcvt(double x, int precision, char *to, my_bool *error);
size_t ma_gcvt(double x, my_gcvt_arg_type type, int width, char *to,
               my_bool *error);
char *ma_ll2str(long long val,char *dst, int radix);

#define MAX_ENV_SIZE 1024

static inline my_bool ma_check_env_str(const char *env)
{
  unsigned int i;

  if (!env)
    return 1;

  for (i=0; i < MAX_ENV_SIZE; i++)
  {
    if (env[i] == 0)
      break;
  }
  if (i >= MAX_ENV_SIZE)
    return 1;
  return 0;
}

#endif
