/****************************************************************************
   Copyright (C) 2012 Monty Program AB

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
*****************************************************************************/

/* The implementation for prepared statements was ported from PHP's mysqlnd
   extension, written by Andrey Hristov, Georg Richter and Ulf Wendel

   Original file header:
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 2006-2011 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Georg Richter <georg@mysql.com>                             |
  |          Andrey Hristov <andrey@mysql.com>                           |
  |          Ulf Wendel <uwendel@mysql.com>                              |
  +----------------------------------------------------------------------+
*/

#include "ma_global.h"
#include <ma_sys.h>
#include <ma_string.h>
#include <mariadb_ctype.h>
#include "mysql.h"
#include <math.h> /* ceil() */
#include <limits.h>
#include <stdint.h>

#ifdef WIN32
#include <malloc.h>
#endif

#define MYSQL_SILENT

/* ranges for C-binding */
#define UINT_MAX32      0xFFFFFFFFL
#define UINT_MAX24      0x00FFFFFF
#define UINT_MAX16      0xFFFF
#ifndef INT_MIN8
#define INT_MIN8        (~0x7F)
#define INT_MAX8        0x7F
#endif
#define UINT_MAX8       0xFF

 #define MAX_DOUBLE_STRING_REP_LENGTH 300
#if defined(HAVE_LONG_LONG) && !defined(LONGLONG_MIN)
#define LONGLONG_MIN    ((long long) 0x8000000000000000LL)
#define LONGLONG_MAX    ((long long) 0x7FFFFFFFFFFFFFFFLL)
#endif

#define MAX_DBL_STR (3 + DBL_MANT_DIG - DBL_MIN_EXP)

#if defined(HAVE_LONG_LONG) && !defined(ULONGLONG_MAX)
/* First check for ANSI C99 definition: */
#ifdef ULLONG_MAX
#define ULONGLONG_MAX  ULLONG_MAX
#else
#define ULONGLONG_MAX ((unsigned long long)(~0ULL))
#endif
#endif /* defined (HAVE_LONG_LONG) && !defined(ULONGLONG_MAX)*/

#define YY_PART_YEAR 70

MYSQL_PS_CONVERSION mysql_ps_fetch_functions[MYSQL_TYPE_GEOMETRY + 1];
my_bool mysql_ps_subsystem_initialized= 0;


#define NUMERIC_TRUNCATION(val,min_range, max_range)\
  ((((val) > (max_range)) || ((val) < (min_range)) ? 1 : 0))


void ma_bmove_upp(register char *dst, register const char *src, register size_t len)
{
  while (len-- != 0) *--dst = *--src;
}

/* {{{ ps_fetch_from_1_to_8_bytes */
void ps_fetch_from_1_to_8_bytes(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
                unsigned char **row, unsigned int byte_count)
{
  my_bool is_unsigned= test(field->flags & UNSIGNED_FLAG);
  r_param->buffer_length= byte_count;
  switch (byte_count) {
    case 1:
      *(uchar *)r_param->buffer= **row;
      *r_param->error= is_unsigned != r_param->is_unsigned && *(uchar *)r_param->buffer > INT_MAX8;
      break;
    case 2:
      shortstore(r_param->buffer, ((ushort) sint2korr(*row)));
      *r_param->error= is_unsigned != r_param->is_unsigned && *(ushort *)r_param->buffer > INT_MAX16;
      break;
    case 4:
    {
      longstore(r_param->buffer, ((uint32)sint4korr(*row)));
      *r_param->error= is_unsigned != r_param->is_unsigned && *(uint32 *)r_param->buffer > INT_MAX32;
    }
    break;
    case 8:
      {
        ulonglong val= (ulonglong)sint8korr(*row);
        longlongstore(r_param->buffer, val);
        *r_param->error= is_unsigned != r_param->is_unsigned && val > LONGLONG_MAX ;
      }
      break;
    default:
      r_param->buffer_length= 0;
      break;
  }
  (*row)+= byte_count;
}
/* }}} */

static unsigned long long my_strtoull(const char *str, size_t len, const char **end, int *err)
{
  unsigned long long val = 0;
  const char *p = str;
  const char *end_str = p + len;

  for (; p < end_str; p++)
  {
    if (*p < '0' || *p > '9')
      break;

    if (val > ULONGLONG_MAX /10 || val*10 > ULONGLONG_MAX - (*p - '0'))
    {
      *err = ERANGE;
      break;
    }
    val = val * 10 + *p -'0';
  }

  if (p == str)
    /* Did not parse anything.*/
    *err = ERANGE;

  *end = p;
  return val;
}

static long long my_strtoll(const char *str, size_t len, const char **end, int *err)
{
  unsigned long long uval = 0;
  const char *p = str;
  const char *end_str = p + len;
  int neg;

  while (p < end_str && isspace(*p))
    p++;

  if (p == end_str)
  {
    *end = p;
    *err = ERANGE;
    return 0;
  }

  neg = *p == '-';
  if (neg)
    p++;

  uval = my_strtoull(p, (end_str - p), &p, err);
  *end = p;
  if (*err)
    return uval;

  if (!neg)
  {
    /* Overflow of the long long range. */
    if (uval > LONGLONG_MAX)
    {
      *end = p - 1;
      uval = LONGLONG_MAX;
      *err = ERANGE;
    }
    return uval;
  }

  if (uval == (unsigned long long) LONGLONG_MIN)
    return LONGLONG_MIN;

  if (uval > LONGLONG_MAX)
  {
    *end = p - 1;
    uval = LONGLONG_MIN;
    *err = ERANGE;
  }

  return -1LL * uval;
}


static long long my_atoll(const char *str, const char *end_str, int *error)
{
  const char *p=str;
  const char *end;
  long long ret;
  while (p < end_str && isspace(*p))
    p++;

  ret = my_strtoll(p, end_str - p, &end, error);

  while(end < end_str && isspace(*end))
   end++;

  if(end != end_str)
    *error= 1;

  return ret;
}


static unsigned long long my_atoull(const char *str, const char *end_str, int *error)
{
  const char *p = str;
  const char *end;
  unsigned long long ret;

  while (p < end_str && isspace(*p))
    p++;

  ret = my_strtoull(p, end_str - p, &end, error);

  while(end < end_str && isspace(*end))
   end++;

  if(end != end_str)
    *error= 1;

  return ret;
}

double my_atod(const char *number, const char *end, int *error)
{
  double val= 0.0;
  char buffer[MAX_DBL_STR + 1];
  int len= (int)(end - number);

  *error= errno= 0;

  if (len > MAX_DBL_STR)
  {
    *error= 1;
    len= MAX_DBL_STR;
  }

  memcpy(buffer, number, len);
  buffer[len]= '\0';

  val= strtod(buffer, NULL);

  if (errno)
    *error= errno;

  return val;
}


/*
  strtoui() version, that works for non-null terminated strings
*/
static unsigned int my_strtoui(const char *str, size_t len, const char **end, int *err)
{
  unsigned long long ull = my_strtoull(str, len, end, err);
  if (ull > UINT_MAX)
    *err = ERANGE;
  return (unsigned int)ull;
}

/*
  Parse time, in MySQL format.

  the input string needs is in form "hour:minute:second[.fraction]"
  hour, minute and second can have leading zeroes or not,
  they are not necessarily 2 chars.

  Hour must be < 838, minute < 60, second < 60
  Only 6 places of fraction are considered, the value is truncated after 6 places.
*/
static const unsigned int frac_mul[] = { 1000000,100000,10000,1000,100,10 };

static int parse_time(const char *str, size_t length, const char **end_ptr, MYSQL_TIME *tm)
{
  int err= 0;
  const char *p = str;
  const char *end = str + length;
  size_t frac_len;
  int ret=1;

  tm->hour = my_strtoui(p, end-p, &p, &err);
  if (err || tm->hour > 838 || p == end || *p != ':' )
    goto end;

  p++;
  tm->minute = my_strtoui(p, end-p, &p, &err);
  if (err || tm->minute > 59 || p == end || *p != ':')
    goto end;

  p++;
  tm->second = my_strtoui(p, end-p, &p, &err);
  if (err || tm->second > 59)
    goto end;

  ret = 0;
  tm->second_part = 0;

  if (p == end)
    goto end;

  /* Check for fractional part*/
  if (*p != '.')
    goto end;

  p++;
  frac_len = MIN(6,end-p);

  tm->second_part = my_strtoui(p, frac_len, &p, &err);
  if (err)
    goto end;

  if (frac_len < 6)
    tm->second_part *= frac_mul[frac_len];

  ret = 0;

  /* Consume whole fractional part, even after 6 digits.*/
  p += frac_len;
  while(p < *end_ptr)
  {
    if (*p < '0' || *p > '9')
      break;
    p++;
  }
end:
  *end_ptr = p;
  return ret;
}


/*
  Parse date, in MySQL format.

  The input string needs is in form "year-month-day"
  year, month and day can have leading zeroes or not,
  they do not have fixed length.

  Year must be < 10000, month < 12, day < 32

  Years with 2 digits, are converted to values 1970-2069 according to
  usual rules:

  00-69 is converted to 2000-2069.
  70-99 is converted to 1970-1999.
*/
static int parse_date(const char *str, size_t length, const char **end_ptr, MYSQL_TIME *tm)
{
  int err = 0;
  const char *p = str;
  const char *end = str + length;
  int ret = 1;

  tm->year = my_strtoui(p, end - p, &p, &err);
  if (err || tm->year > 9999 || p == end || *p != '-')
    goto end;

  if (p - str == 2) // 2-digit year
    tm->year += (tm->year >= 70) ? 1900 : 2000;

  p++;
  tm->month = my_strtoui(p,end -p, &p, &err);
  if (err || tm->month > 12 || p == end || *p != '-')
    goto end;

  p++;
  tm->day = my_strtoui(p, end -p , &p, &err);
  if (err || tm->day > 31)
    goto end;

  ret = 0;

end:
  *end_ptr = p;
  return ret;
}

/*
  Parse (not null terminated) string representing
  TIME, DATE, or DATETIME into MYSQL_TIME structure

  The supported formats by this functions are
  - TIME : [-]hours:minutes:seconds[.fraction]
  - DATE : year-month-day
  - DATETIME : year-month-day<space>hours:minutes:seconds[.fraction]

  cf https://dev.mysql.com/doc/refman/8.0/en/datetime.html

  Whitespaces are trimmed from the start and end of the string.
  The function ignores junk at the end of the string.

  Parts of date of time do not have fixed length, so that parsing is compatible with server.
  However server supports additional formats, e.g YYYYMMDD, HHMMSS, which this function does
  not support.

*/
int str_to_TIME(const char *str, size_t length, MYSQL_TIME *tm)
{
  const char *p = str;
  const char *end = str + length;
  int is_time = 0;

  if (!p)
    goto error;

  while (p < end && isspace(*p))
    p++;
  while (p < end && isspace(end[-1]))
    end--;

  if (end -p < 5)
    goto error;

  if (*p == '-')
  {
    tm->neg = 1;
    /* Only TIME can't be negative.*/
    is_time = 1;
    p++;
  }
  else
  {
    int i;
    tm->neg = 0;
    /*
      Date parsing (in server) accepts leading zeroes, thus position of the delimiters
      is not fixed. Scan the string to find out what we need to parse.
    */
    for (i = 1; p + i < end; i++)
    {
      if(p[i] == '-' || p [i] == ':')
      {
        is_time = p[i] == ':';
        break;
      }
    }
  }

  if (is_time)
  {
    if (parse_time(p, end - p, &p, tm))
      goto error;

    tm->year = tm->month = tm->day = 0;
    tm->time_type = MYSQL_TIMESTAMP_TIME;
    return 0;
  }

  if (parse_date(p, end - p, &p, tm))
    goto error;

  if (p == end || p[0] != ' ')
  {
    tm->hour = tm->minute = tm->second = tm->second_part = 0;
    tm->time_type = MYSQL_TIMESTAMP_DATE;
    return 0;
  }

  /* Skip space. */
  p++;
  if (parse_time(p, end - p, &p, tm))
    goto error;

  /* In DATETIME, hours must be < 24.*/
  if (tm->hour > 23)
   goto error;

  tm->time_type = MYSQL_TIMESTAMP_DATETIME;
  return 0;

error:
  memset(tm, 0, sizeof(*tm));
  tm->time_type = MYSQL_TIMESTAMP_ERROR;
  return 1;
}


static void convert_froma_string(MYSQL_BIND *r_param, char *buffer, size_t len)
{
  int error= 0;
  switch (r_param->buffer_type)
  {
    case MYSQL_TYPE_TINY:
    {
      longlong val= my_atoll(buffer, buffer + len, &error);
      *r_param->error= error ? 1 : r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX8) : NUMERIC_TRUNCATION(val, INT_MIN8, INT_MAX8) || error > 0;
      int1store(r_param->buffer, (uchar) val);
      r_param->buffer_length= sizeof(uchar);
    }
    break;
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
    {
      longlong val= my_atoll(buffer, buffer + len, &error);
      *r_param->error= error ? 1 : r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX16) : NUMERIC_TRUNCATION(val, INT_MIN16, INT_MAX16) || error > 0;
      shortstore(r_param->buffer, (short)val);
      r_param->buffer_length= sizeof(short);
    }
    break;
    case MYSQL_TYPE_LONG:
    {
      longlong val= my_atoll(buffer, buffer + len, &error);
      *r_param->error=error ? 1 : r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX32) : NUMERIC_TRUNCATION(val, INT_MIN32, INT_MAX32) || error > 0;
      longstore(r_param->buffer, (int32)val);
      r_param->buffer_length= sizeof(uint32);
    }
    break;
    case MYSQL_TYPE_LONGLONG:
    {
      longlong val= r_param->is_unsigned ? (longlong)my_atoull(buffer, buffer + len, &error) : my_atoll(buffer, buffer + len, &error);
      *r_param->error= error > 0; /* no need to check for truncation */
      longlongstore(r_param->buffer, val);
      r_param->buffer_length= sizeof(longlong);
    }
    break;
    case MYSQL_TYPE_DOUBLE:
    {
      double val= my_atod(buffer, buffer + len, &error);
      *r_param->error= error > 0; /* no need to check for truncation */
      doublestore((uchar *)r_param->buffer, val);
      r_param->buffer_length= sizeof(double);
    }
    break;
    case MYSQL_TYPE_FLOAT:
    {
      float val= (float)my_atod(buffer, buffer + len, &error);
      *r_param->error= error > 0; /* no need to check for truncation */
      floatstore((uchar *)r_param->buffer, val);
      r_param->buffer_length= sizeof(float);
    }
    break;
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
    {
      MYSQL_TIME *tm= (MYSQL_TIME *)r_param->buffer;
      str_to_TIME(buffer, len, tm);
      break;
    }
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
    default:
    {
      if (len >= r_param->offset)
      {
        char *start= buffer + r_param->offset; /* stmt_fetch_column sets offset */
        char *end= buffer + len;
        size_t copylen= 0;

        if (start < end)
        {
          copylen= end - start;
          if (r_param->buffer_length)
            memcpy(r_param->buffer, start, MIN(copylen, r_param->buffer_length));
        }
        if (copylen < r_param->buffer_length)
          ((char *)r_param->buffer)[copylen]= 0;
        *r_param->error= (copylen > r_param->buffer_length);

      }
      *r_param->length= (ulong)len;
    }
    break;
  }
}

static void convert_from_long(MYSQL_BIND *r_param, const MYSQL_FIELD *field, longlong val, my_bool is_unsigned)
{
  switch (r_param->buffer_type) {
    case MYSQL_TYPE_TINY:
      *(uchar *)r_param->buffer= (uchar)(val & 0xff);
      *r_param->error= r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX8) : NUMERIC_TRUNCATION(val, INT_MIN8, INT_MAX8);
      r_param->buffer_length= 1;
      break;
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_YEAR:
      shortstore(r_param->buffer, (short)(val & 0xffff));
      *r_param->error= r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX16) : NUMERIC_TRUNCATION(val, INT_MIN16, INT_MAX16);
      r_param->buffer_length= 2;
      break;
    case MYSQL_TYPE_LONG:
      longstore(r_param->buffer, (int32)(val & 0xffffffff));
      *r_param->error= r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX32) : NUMERIC_TRUNCATION(val, INT_MIN32, INT_MAX32);
      r_param->buffer_length= 4;
      break;
    case MYSQL_TYPE_LONGLONG:
      *r_param->error= (val < 0 && r_param->is_unsigned != is_unsigned);
      longlongstore(r_param->buffer, val);
      r_param->buffer_length= 8;
      break;
    case MYSQL_TYPE_DOUBLE:
    {
      volatile double dbl;

      dbl= (is_unsigned) ? ulonglong2double((ulonglong)val) : (double)val;
      doublestore(r_param->buffer, dbl);

      if (dbl != ceil(dbl))
        *r_param->error= 1;
      else if (is_unsigned)
        *r_param->error= dbl < 0 ||
                          dbl >= (double)ULONGLONG_MAX ||
                          (ulonglong)dbl != (ulonglong)val;
      else
        *r_param->error= dbl < (double)LONGLONG_MIN ||
                          dbl >= (double)LONGLONG_MAX ||
                          (longlong)dbl != (longlong)val;

      r_param->buffer_length= 8;
      break;
    }
    case MYSQL_TYPE_FLOAT:
    {
      volatile float fval;
      fval= is_unsigned ? (float)(ulonglong)(val) : (float)val;
      floatstore((uchar *)r_param->buffer, fval);

      if (fval != ceilf(fval))
        *r_param->error= 1;
      else if (is_unsigned)
        *r_param->error= fval < 0 ||
                          fval >= (float)ULONGLONG_MAX ||
                          (ulonglong)fval != (ulonglong)val;
      else
        *r_param->error= fval < (float)LONGLONG_MIN ||
                          fval >= (float)LONGLONG_MAX ||
                          (longlong)fval != val;

      r_param->buffer_length= 4;
    }
    break;
    default:
    {
      char *buffer;
      char *endptr;
      uint len;
      my_bool zf_truncated= 0;

      buffer= alloca(MAX(field->length, 22));
      endptr= ma_ll2str(val, buffer, is_unsigned ? 10 : -10);
      len= (uint)(endptr - buffer);

      /* check if field flag is zerofill */
      if (field->flags & ZEROFILL_FLAG)
      {
        uint display_width= MAX(field->length, len);
        if (display_width < r_param->buffer_length)
        {
          ma_bmove_upp(buffer + display_width, buffer + len, len);
          /* coverity[bad_memset] */
          memset((void*) buffer, (int) '0', display_width - len);
          len= display_width;
        }
        else
          zf_truncated= 1;
      }
      convert_froma_string(r_param, buffer, len);
      *r_param->error+= zf_truncated;
    }
    break;
  }
}


/* {{{ ps_fetch_null */
static
void ps_fetch_null(MYSQL_BIND *r_param __attribute__((unused)),
                   const MYSQL_FIELD * field __attribute__((unused)),
                   unsigned char **row __attribute__((unused)))
{
  /* do nothing */
}
/* }}} */

#define GET_LVALUE_FROM_ROW(is_unsigned, data, ucast, scast)\
  (is_unsigned) ? (longlong)(ucast) *(longlong *)(data) : (longlong)(scast) *(longlong *)(data)
/* {{{ ps_fetch_int8 */
static
void ps_fetch_int8(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
           unsigned char **row)
{
  switch(r_param->buffer_type) {
    case MYSQL_TYPE_TINY:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 1);
      break;
    default:
    {
      uchar val= **row;
      longlong lval= field->flags & UNSIGNED_FLAG ? (longlong) val : (longlong)(signed char)val;
      convert_from_long(r_param, field, lval, field->flags & UNSIGNED_FLAG);
      (*row) += 1;
    }
    break;
  }
}
/* }}} */


/* {{{ ps_fetch_int16 */
static
void ps_fetch_int16(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
           unsigned char **row)
{
  switch (r_param->buffer_type) {
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 2);
    break;
    default:
    {
      short sval= sint2korr(*row);
      longlong lval= field->flags & UNSIGNED_FLAG ? (longlong)(ushort) sval : (longlong)sval;
      convert_from_long(r_param, field, lval, field->flags & UNSIGNED_FLAG);
      (*row) += 2;
    }
    break;
  }
}
/* }}} */


/* {{{ ps_fetch_int32 */
static
void ps_fetch_int32(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
           unsigned char **row)
{
  switch (r_param->buffer_type) {
/*    case MYSQL_TYPE_TINY:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 1);
      break;
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 2);
      break; */
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONG:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 4);
    break;
    default:
    {
      int32 sval= sint4korr(*row);
      longlong lval= field->flags & UNSIGNED_FLAG ? (longlong)(uint32) sval : (longlong)sval;
      convert_from_long(r_param, field, lval, field->flags & UNSIGNED_FLAG);
      (*row) += 4;
    }
    break;
  }
}
/* }}} */


/* {{{ ps_fetch_int64 */
static
void ps_fetch_int64(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
           unsigned char **row)
{
  switch(r_param->buffer_type)
  {
/*    case MYSQL_TYPE_TINY:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 1);
      break;
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 2);
      break;
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONG:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 4);
      break; */
    case MYSQL_TYPE_LONGLONG:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 8);
    break;
    default:
    {
      longlong sval= (longlong)sint8korr(*row);
      longlong lval= field->flags & UNSIGNED_FLAG ? (longlong)(ulonglong) sval : (longlong)sval;
      convert_from_long(r_param, field, lval, field->flags & UNSIGNED_FLAG);
      (*row) += 8;
    }
    break;
  }
}
/* }}} */

static void convert_from_float(MYSQL_BIND *r_param, const MYSQL_FIELD *field, float val, int size __attribute__((unused)))
{
  double check_trunc_val= (val > 0) ? floor(val) : -floor(-val);
  char *buf= (char *)r_param->buffer;
  switch (r_param->buffer_type)
  {
    case MYSQL_TYPE_TINY:
      *buf= (r_param->is_unsigned) ? (uint8)val : (int8)val;
      *r_param->error= check_trunc_val != (r_param->is_unsigned ? (double)((uint8)*buf) :
                                          (double)((int8)*buf));
      r_param->buffer_length= 1;
    break;
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_YEAR:
    {
      if (r_param->is_unsigned)
      {
        ushort sval= (ushort)val;
        shortstore(buf, sval);
        *r_param->error= check_trunc_val != (double)sval;
      } else {
        short sval= (short)val;
        shortstore(buf, sval);
        *r_param->error= check_trunc_val != (double)sval;
      }
      r_param->buffer_length= 2;
    }
    break;
    case MYSQL_TYPE_LONG:
    {
      if (r_param->is_unsigned)
      {
        uint32 lval= (uint32)val;
        longstore(buf, lval);
        *r_param->error= (check_trunc_val != (double)lval);
      } else {
        int32 lval= (int32)val;
        longstore(buf, lval);
        *r_param->error= (check_trunc_val != (double)lval);
      }
      r_param->buffer_length= 4;
    }
    break;
    case MYSQL_TYPE_LONGLONG:
    {
      if (r_param->is_unsigned)
      {
        ulonglong llval= (ulonglong)val;
        longlongstore(buf, llval);
        *r_param->error= (check_trunc_val != (double)llval);
      } else {
        longlong llval= (longlong)val;
        longlongstore(buf, llval);
        *r_param->error= (check_trunc_val != (double)llval);
      }
      r_param->buffer_length= 8;
    }
    break;
    case MYSQL_TYPE_DOUBLE:
    {
      double dval= (double)val;
      memcpy(buf, &dval, sizeof(double));
      r_param->buffer_length= 8;
    }
    break;
    default:
    {
      char buff[MAX_DOUBLE_STRING_REP_LENGTH];
      size_t length;

      length= MIN(MAX_DOUBLE_STRING_REP_LENGTH - 1, r_param->buffer_length);

      if (field->decimals >= NOT_FIXED_DEC)
      {
        length= ma_gcvt(val, MY_GCVT_ARG_FLOAT, (int)length, buff, NULL);
      }
      else
      {
        length= ma_fcvt(val, field->decimals, buff, NULL);
      }

      /* check if ZEROFILL flag is active */
      if (field->flags & ZEROFILL_FLAG)
      {
        /* enough space available ? */
        if (field->length < length || field->length > MAX_DOUBLE_STRING_REP_LENGTH - 1)
          break;
        ma_bmove_upp(buff + field->length, buff + length, length);
        /* coverity[bad_memset] */
        memset((void*) buff, (int) '0', field->length - length);
        length= field->length;
      }

      convert_froma_string(r_param, buff, length);
    }
    break;
  }
}

static void convert_from_double(MYSQL_BIND *r_param, const MYSQL_FIELD *field, double val, int size __attribute__((unused)))
{
  double check_trunc_val= (val > 0) ? floor(val) : -floor(-val);
  char *buf= (char *)r_param->buffer;
  switch (r_param->buffer_type)
  {
    case MYSQL_TYPE_TINY:
      *buf= (r_param->is_unsigned) ? (uint8)val : (int8)val;
      *r_param->error= check_trunc_val != (r_param->is_unsigned ? (double)((uint8)*buf) :
                                          (double)((int8)*buf));
      r_param->buffer_length= 1;
    break;
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_YEAR:
    {
      if (r_param->is_unsigned)
      {
        ushort sval= (ushort)val;
        shortstore(buf, sval);
        *r_param->error= check_trunc_val != (double)sval;
      } else {
        short sval= (short)val;
        shortstore(buf, sval);
        *r_param->error= check_trunc_val != (double)sval;
      }
      r_param->buffer_length= 2;
    }
    break;
    case MYSQL_TYPE_LONG:
    {
      if (r_param->is_unsigned)
      {
        uint32 lval= (uint32)val;
        longstore(buf, lval);
        *r_param->error= (check_trunc_val != (double)lval);
      } else {
        int32 lval= (int32)val;
        longstore(buf, lval);
        *r_param->error= (check_trunc_val != (double)lval);
      }
      r_param->buffer_length= 4;
    }
    break;
    case MYSQL_TYPE_LONGLONG:
    {
      if (r_param->is_unsigned)
      {
        ulonglong llval= (ulonglong)val;
        longlongstore(buf, llval);
        *r_param->error= (check_trunc_val != (double)llval);
      } else {
        longlong llval= (longlong)val;
        longlongstore(buf, llval);
        *r_param->error= (check_trunc_val != (double)llval);
      }
      r_param->buffer_length= 8;
    }
    break;
    case MYSQL_TYPE_FLOAT:
    {
      float fval= (float)val;
      memcpy(buf, &fval, sizeof(float));
      *r_param->error= (*(float*)buf != fval);
      r_param->buffer_length= 4;
    }
    break;
    default:
    {
     char buff[MAX_DOUBLE_STRING_REP_LENGTH];
     size_t length;

     length= MIN(MAX_DOUBLE_STRING_REP_LENGTH - 1, r_param->buffer_length);

     if (field->decimals >= NOT_FIXED_DEC)
     {
       length= ma_gcvt(val, MY_GCVT_ARG_DOUBLE, (int)length, buff, NULL);
     }
     else
     {
       length= ma_fcvt(val, field->decimals, buff, NULL);
     }

     /* check if ZEROFILL flag is active */
     if (field->flags & ZEROFILL_FLAG)
     {
       /* enough space available ? */
       if (field->length < length || field->length > MAX_DOUBLE_STRING_REP_LENGTH - 1)
         break;
       ma_bmove_upp(buff + field->length, buff + length, length);
       /* coverity[bad_memset] */
       memset((void*) buff, (int) '0', field->length - length);
       length= field->length;
     }
     convert_froma_string(r_param, buff, length);
    }
    break;
  }
}


/* {{{ ps_fetch_double */
static
void ps_fetch_double(MYSQL_BIND *r_param, const MYSQL_FIELD * field , unsigned char **row)
{
  switch (r_param->buffer_type)
  {
    case MYSQL_TYPE_DOUBLE:
    {
      double *value= (double *)r_param->buffer;
      float8get(*value, *row);
      r_param->buffer_length= 8;
    }
    break;
    default:
    {
      double value;
      float8get(value, *row);
      convert_from_double(r_param, field, value, sizeof(double));
    }
    break;
  }
  (*row)+= 8;
}
/* }}} */

/* {{{ ps_fetch_float */
static
void ps_fetch_float(MYSQL_BIND *r_param, const MYSQL_FIELD * field, unsigned char **row)
{
  switch(r_param->buffer_type)
  {
    case MYSQL_TYPE_FLOAT:
    {
      float *value= (float *)r_param->buffer;
      float4get(*value, *row);
      r_param->buffer_length= 4;
      *r_param->error= 0;
    }
    break;
    default:
    {
      float value;
      memcpy(&value, *row, sizeof(float));
      float4get(value, (char *)*row);
      convert_from_float(r_param, field, value, sizeof(float));
    }
    break;
  }
  (*row)+= 4;
}
/* }}} */

static void convert_to_datetime(MYSQL_TIME *t, unsigned char **row, uint len, enum enum_field_types type)
{
  memset(t, 0, sizeof(MYSQL_TIME));

  /* binary protocol for datetime:
     4-bytes:  DATE
     7-bytes:  DATE + TIME
     >7 bytes: DATE + TIME with second_part
  */
  if (len)
  {
    unsigned char *to= *row;
    int has_date= 0;
    uint offset= 7;

    if (type == MYSQL_TYPE_TIME)
    {
      t->neg= to[0];
      t->day= (ulong) sint4korr(to + 1);
      t->time_type= MYSQL_TIMESTAMP_TIME;
      offset= 8;
      to++;
    } else
    {
      t->year= (uint) sint2korr(to);
      t->month= (uint) to[2];
      t->day= (uint) to[3];
      t->time_type= MYSQL_TIMESTAMP_DATE;
      if (type == MYSQL_TYPE_DATE)
        return;
      has_date= 1;
    }

    if (len > 4)
    {
      t->hour= (uint) to[4];
      if (type == MYSQL_TYPE_TIME)
        t->hour+= t->day * 24;
      t->minute= (uint) to[5];
      t->second= (uint) to[6];
      if (has_date)
        t->time_type= MYSQL_TIMESTAMP_DATETIME;
    }
    if (len > offset)
    {
      t->second_part= (ulong)sint4korr(to+7);
    }
  }
}

static const uint32_t sec_part_digits[]= {1000000, 100000, 10000, 1000, 100, 10, 1};

/* {{{ ps_fetch_datetime */
static
void ps_fetch_datetime(MYSQL_BIND *r_param, const MYSQL_FIELD * field,
                       unsigned char **row)
{
  MYSQL_TIME *t= (MYSQL_TIME *)r_param->buffer;
  unsigned int len= net_field_length(row);

  switch (r_param->buffer_type) {
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
      convert_to_datetime(t, row, len, field->type);
      break;
    case MYSQL_TYPE_DATE:
      convert_to_datetime(t, row, len, field->type);
      break;
    case MYSQL_TYPE_TIME:
      convert_to_datetime(t, row, len, field->type);
      t->year= t->day= t->month= 0;
      break;
    case MYSQL_TYPE_YEAR:
    {
      MYSQL_TIME tm;
      convert_to_datetime(&tm, row, len, field->type);
      shortstore(r_param->buffer, tm.year);
      break;
    }
    default:
    {
      char dtbuffer[60];
      MYSQL_TIME tm;
      size_t length;
      convert_to_datetime(&tm, row, len, field->type);

      switch(field->type) {
      case MYSQL_TYPE_DATE:
        length= sprintf(dtbuffer, "%04u-%02u-%02u", tm.year, tm.month, tm.day);
        break;
      case MYSQL_TYPE_TIME:
        if (field->decimals && (field->decimals <= SEC_PART_DIGITS ||
                               (field->decimals == AUTO_SEC_PART_DIGITS && tm.second_part)))
        {
          uint8_t decimals= (field->decimals == AUTO_SEC_PART_DIGITS) ? SEC_PART_DIGITS : field->decimals;
          length= sprintf(dtbuffer, "%s%02u:%02u:%02u.%0*u", (tm.neg ? "-" : ""), tm.hour, tm.minute, tm.second,
                          decimals, (uint32_t)(tm.second_part / sec_part_digits[decimals]));
        } else
          length= sprintf(dtbuffer, "%s%02u:%02u:%02u", (tm.neg ? "-" : ""), tm.hour, tm.minute, tm.second);
        break;
      case MYSQL_TYPE_DATETIME:
      case MYSQL_TYPE_TIMESTAMP:
        if (field->decimals && (field->decimals <= SEC_PART_DIGITS ||
                               (field->decimals == AUTO_SEC_PART_DIGITS && tm.second_part)))
        {
          uint8_t decimals= (field->decimals == AUTO_SEC_PART_DIGITS) ? SEC_PART_DIGITS : field->decimals;
          length= sprintf(dtbuffer, "%04u-%02u-%02u %02u:%02u:%02u.%0*u", tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second,
                          decimals, (uint32_t)(tm.second_part / sec_part_digits[decimals]));
        } else
          length= sprintf(dtbuffer, "%04u-%02u-%02u %02u:%02u:%02u", tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second);
        break;
      default:
        dtbuffer[0]= 0;
        length= 0;
        break;
      }
      convert_froma_string(r_param, dtbuffer, length);
      break;
    }
  }
  (*row) += len;
}
/* }}} */

/* {{{ ps_fetch_string */
static
void ps_fetch_string(MYSQL_BIND *r_param,
                     const MYSQL_FIELD *field __attribute__((unused)),
                     unsigned char **row)
{
  /* C-API differs from PHP. While PHP just converts string to string,
     C-API needs to convert the string to the defined type with in
     the result bind buffer.
   */
  ulong field_length= net_field_length(row);

  convert_froma_string(r_param, (char *)*row, field_length);
  (*row) += field_length;
}
/* }}} */

/* {{{ ps_fetch_bin */
static
void ps_fetch_bin(MYSQL_BIND *r_param,
             const MYSQL_FIELD *field,
             unsigned char **row)
{
  if (field->charsetnr == 63)
  {
    ulong field_length= *r_param->length= net_field_length(row);
    uchar *current_pos= (*row) + r_param->offset,
          *end= (*row) + field_length;
    size_t copylen= 0;

    if (current_pos < end)
    {
      copylen= end - current_pos;
      if (r_param->buffer_length)
        memcpy(r_param->buffer, current_pos, MIN(copylen, r_param->buffer_length));
    }
    if (copylen < r_param->buffer_length &&
        (r_param->buffer_type == MYSQL_TYPE_STRING ||
         r_param->buffer_type == MYSQL_TYPE_JSON))
      ((char *)r_param->buffer)[copylen]= 0;
    *r_param->error= copylen > r_param->buffer_length;
    (*row)+= field_length;
  }
  else
    ps_fetch_string(r_param, field, row);
}
/* }}} */

/* {{{ _mysqlnd_init_ps_subsystem */
void mysql_init_ps_subsystem(void)
{
  memset(mysql_ps_fetch_functions, 0, sizeof(mysql_ps_fetch_functions));
  mysql_ps_fetch_functions[MYSQL_TYPE_NULL].func= ps_fetch_null;
  mysql_ps_fetch_functions[MYSQL_TYPE_NULL].pack_len  = 0;
  mysql_ps_fetch_functions[MYSQL_TYPE_NULL].max_len  = 0;

  mysql_ps_fetch_functions[MYSQL_TYPE_TINY].func    = ps_fetch_int8;
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY].pack_len  = 1;
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY].max_len  = 4;

  mysql_ps_fetch_functions[MYSQL_TYPE_SHORT].func    = ps_fetch_int16;
  mysql_ps_fetch_functions[MYSQL_TYPE_SHORT].pack_len  = 2;
  mysql_ps_fetch_functions[MYSQL_TYPE_SHORT].max_len  = 6;

  mysql_ps_fetch_functions[MYSQL_TYPE_YEAR].func    = ps_fetch_int16;
  mysql_ps_fetch_functions[MYSQL_TYPE_YEAR].pack_len  = 2;
  mysql_ps_fetch_functions[MYSQL_TYPE_YEAR].max_len  = 4;

  mysql_ps_fetch_functions[MYSQL_TYPE_INT24].func    = ps_fetch_int32;
  mysql_ps_fetch_functions[MYSQL_TYPE_INT24].pack_len  = 4;
  mysql_ps_fetch_functions[MYSQL_TYPE_INT24].max_len  = 8;

  mysql_ps_fetch_functions[MYSQL_TYPE_LONG].func    = ps_fetch_int32;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONG].pack_len  = 4;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONG].max_len  = 11;

  mysql_ps_fetch_functions[MYSQL_TYPE_LONGLONG].func  = ps_fetch_int64;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONGLONG].pack_len= 8;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONGLONG].max_len  = 20;

  mysql_ps_fetch_functions[MYSQL_TYPE_FLOAT].func    = ps_fetch_float;
  mysql_ps_fetch_functions[MYSQL_TYPE_FLOAT].pack_len  = 4;
  mysql_ps_fetch_functions[MYSQL_TYPE_FLOAT].max_len  = MAX_DOUBLE_STRING_REP_LENGTH;

  mysql_ps_fetch_functions[MYSQL_TYPE_DOUBLE].func    = ps_fetch_double;
  mysql_ps_fetch_functions[MYSQL_TYPE_DOUBLE].pack_len  = 8;
  mysql_ps_fetch_functions[MYSQL_TYPE_DOUBLE].max_len  = MAX_DOUBLE_STRING_REP_LENGTH;

  mysql_ps_fetch_functions[MYSQL_TYPE_TIME].func  = ps_fetch_datetime;
  mysql_ps_fetch_functions[MYSQL_TYPE_TIME].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_TIME].max_len  = 17;

  mysql_ps_fetch_functions[MYSQL_TYPE_DATE].func  = ps_fetch_datetime;
  mysql_ps_fetch_functions[MYSQL_TYPE_DATE].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_DATE].max_len  = 10;

  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDATE].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDATE].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDATE].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_DATETIME].func  = ps_fetch_datetime;
  mysql_ps_fetch_functions[MYSQL_TYPE_DATETIME].pack_len= MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_DATETIME].max_len  = 30;

  mysql_ps_fetch_functions[MYSQL_TYPE_TIMESTAMP].func  = ps_fetch_datetime;
  mysql_ps_fetch_functions[MYSQL_TYPE_TIMESTAMP].pack_len= MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_TIMESTAMP].max_len  = 30;

  mysql_ps_fetch_functions[MYSQL_TYPE_TINY_BLOB].func  = ps_fetch_bin;
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY_BLOB].pack_len= MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY_BLOB].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_BLOB].func    = ps_fetch_bin;
  mysql_ps_fetch_functions[MYSQL_TYPE_BLOB].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_BLOB].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_MEDIUM_BLOB].func  = ps_fetch_bin;
  mysql_ps_fetch_functions[MYSQL_TYPE_MEDIUM_BLOB].pack_len= MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_MEDIUM_BLOB].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_LONG_BLOB].func    = ps_fetch_bin;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONG_BLOB].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONG_BLOB].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_BIT].func  = ps_fetch_bin;
  mysql_ps_fetch_functions[MYSQL_TYPE_BIT].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_BIT].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_VAR_STRING].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_VAR_STRING].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_VAR_STRING].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_VARCHAR].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_VARCHAR].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_VARCHAR].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_STRING].func      = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_STRING].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_STRING].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_JSON].func      = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_JSON].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_JSON].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_DECIMAL].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_DECIMAL].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_DECIMAL].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDECIMAL].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDECIMAL].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDECIMAL].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_ENUM].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_ENUM].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_ENUM].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_SET].func      = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_SET].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_SET].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_GEOMETRY].func  = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_GEOMETRY].pack_len= MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_GEOMETRY].max_len  = -1;

  mysql_ps_subsystem_initialized= 1;
}
/* }}} */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

