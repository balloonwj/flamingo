/* Copyright (C) 2004 Sergei Golubchik

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
*/
  
/*
=======================================================================
  NOTE: this library implements SQL standard "exact numeric" type
  and is not at all generic, but rather intentionally crippled to
  follow the standard :)
=======================================================================
  Quoting the standard
  (SQL:2003, Part 2 Foundations, aka ISO/IEC 9075-2:2003)

4.4.2 Characteristics of numbers, page 27:

  An exact numeric type has a precision P and a scale S. P is a positive
  integer that determines the number of significant digits in a
  particular radix R, where R is either 2 or 10. S is a non-negative
  integer. Every value of an exact numeric type of scale S is of the
  form n*10^{-S}, where n is an integer such that ?-R^P <= n <= R^P.

  [...]

  If an assignment of some number would result in a loss of its most
  significant digit, an exception condition is raised. If least
  significant digits are lost, implementation-defined rounding or
  truncating occurs, with no exception condition being raised.

  [...]

  Whenever an exact or approximate numeric value is assigned to an exact
  numeric value site, an approximation of its value that preserves
  leading significant digits after rounding or truncating is represented
  in the declared type of the target. The value is converted to have the
  precision and scale of the target. The choice of whether to truncate
  or round is implementation-defined.

  [...]

  All numeric values between the smallest and the largest value,
  inclusive, in a given exact numeric type have an approximation
  obtained by rounding or truncation for that type; it is
  implementation-defined which other numeric values have such
  approximations.

5.3 <literal>, page 143

  <exact numeric literal> ::=
    <unsigned integer> [ <period> [ <unsigned integer> ] ]
  | <period> <unsigned integer>

6.1 <data type>, page 165:

  19) The <scale> of an <exact numeric type> shall not be greater than
      the <precision> of the <exact numeric type>.

  20) For the <exact numeric type>s DECIMAL and NUMERIC:

    a) The maximum value of <precision> is implementation-defined.
       <precision> shall not be greater than this value.
    b) The maximum value of <scale> is implementation-defined. <scale>
       shall not be greater than this maximum value.

  21) NUMERIC specifies the data type exact numeric, with the decimal
      precision and scale specified by the <precision> and <scale>.

  22) DECIMAL specifies the data type exact numeric, with the decimal
      scale specified by the <scale> and the implementation-defined
      decimal precision equal to or greater than the value of the
      specified <precision>.

6.26 <numeric value expression>, page 241:

  1) If the declared type of both operands of a dyadic arithmetic
     operator is exact numeric, then the declared type of the result is
     an implementation-defined exact numeric type, with precision and
     scale determined as follows:

   a) Let S1 and S2 be the scale of the first and second operands
      respectively.
   b) The precision of the result of addition and subtraction is
      implementation-defined, and the scale is the maximum of S1 and S2.
   c) The precision of the result of multiplication is
      implementation-defined, and the scale is S1 + S2.
   d) The precision and scale of the result of division are
      implementation-defined.
*/

#include <ma_global.h>
#include <ma_sys.h> /* for my_alloca */
#include <ma_decimal.h>
#include <mysql.h>
#include <mariadb_rpl.h>
#include <string.h>

#ifdef WIN32
#include <malloc.h>
#endif

typedef decimal_digit dec1;
typedef longlong      dec2;

#define unlikely(A) (A)
#define DIG_PER_DEC1 9
#define DIG_MASK     100000000
#define DIG_BASE     1000000000
#define DIG_BASE2    LL(1000000000000000000)
#define ROUND_UP(X)  (((X)+DIG_PER_DEC1-1)/DIG_PER_DEC1)
static const dec1 powers10[DIG_PER_DEC1+1]={
  1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
static const int dig2bytes[DIG_PER_DEC1+1]={0, 1, 1, 2, 2, 3, 3, 4, 4, 4};

#define sanity(d) DBUG_ASSERT((d)->len >0 && ((d)->buf[0] | \
                              (d)->buf[(d)->len-1] | 1))

#define FIX_INTG_FRAC_ERROR(len, intg1, frac1, error)                   \
        do                                                              \
        {                                                               \
          if (unlikely(intg1+frac1 > (len)))                            \
          {                                                             \
            if (unlikely(intg1 > (len)))                                \
            {                                                           \
              intg1=(len);                                              \
              frac1=0;                                                  \
              error=E_DEC_OVERFLOW;                                     \
            }                                                           \
            else                                                        \
            {                                                           \
              frac1=(len)-intg1;                                        \
              error=E_DEC_TRUNCATED;                                    \
            }                                                           \
          }                                                             \
          else                                                          \
            error=E_DEC_OK;                                             \
        } while(0)

#define ADD(to, from1, from2, carry)  /* assume carry <= 1 */           \
        do                                                              \
        {                                                               \
          dec1 a=(from1)+(from2)+(carry);                               \
          if (((carry)= a >= DIG_BASE)) /* no division here! */         \
            a-=DIG_BASE;                                                \
          (to)=a;                                                       \
        } while(0)

#define ADD2(to, from1, from2, carry)                                   \
        do                                                              \
        {                                                               \
          dec1 a=(from1)+(from2)+(carry);                               \
          if (((carry)= a >= DIG_BASE))                                 \
            a-=DIG_BASE;                                                \
          if (unlikely(a >= DIG_BASE))                                  \
          {                                                             \
            a-=DIG_BASE;                                                \
            carry++;                                                    \
          }                                                             \
          (to)=a;                                                       \
        } while(0)

#define SUB(to, from1, from2, carry) /* to=from1-from2 */               \
        do                                                              \
        {                                                               \
          dec1 a=(from1)-(from2)-(carry);                               \
          if (((carry)= a < 0))                                         \
            a+=DIG_BASE;                                                \
          (to)=a;                                                       \
        } while(0)

#define SUB2(to, from1, from2, carry) /* to=from1-from2 */              \
        do                                                              \
        {                                                               \
          dec1 a=(from1)-(from2)-(carry);                               \
          if (((carry)= a < 0))                                         \
            a+=DIG_BASE;                                                \
          if (unlikely(a < 0))                                          \
          {                                                             \
            a+=DIG_BASE;                                                \
            carry++;                                                    \
          }                                                             \
          (to)=a;                                                       \
        } while(0)

/*
  Convert decimal to its printable string representation

  SYNOPSIS
    decimal2string()
      from    - value to convert
      to      - points to buffer where string representation should be stored
      *to_len - in:  size of to buffer
                out: length of the actually written string

  RETURN VALUE
    E_DEC_OK/E_DEC_TRUNCATED/E_DEC_OVERFLOW
*/

int decimal2string(decimal *from, char *to, int *to_len)
{
  int len, intg=from->intg, frac=from->frac, i;
  int error=E_DEC_OK;
  char *s=to;
  dec1 *buf, *buf0=from->buf, tmp;

  DBUG_ASSERT(*to_len >= 2+from->sign);

  /* removing leading zeroes */
  i=((intg-1) % DIG_PER_DEC1)+1;
  while (intg > 0 && *buf0 == 0)
  {
    intg-=i;
    i=DIG_PER_DEC1;
    buf0++;
  }
  if (intg > 0)
  {
    for (i=(intg-1) % DIG_PER_DEC1; *buf0 < powers10[i--]; intg--) {}
    DBUG_ASSERT(intg > 0);
  }
  else
    intg=0;
  if (unlikely(intg+frac==0))
  {
    intg=1;
    tmp=0;
    buf0=&tmp;
  }

  len= from->sign + intg + test(frac) + frac;
  if (unlikely(len > --*to_len)) /* reserve one byte for \0 */
  {
    int i=len-*to_len;
    error= (frac && i <= frac + 1) ? E_DEC_TRUNCATED : E_DEC_OVERFLOW;
    if (frac && i >= frac + 1) i--;
    if (i > frac)
    {
      intg-= i-frac;
      frac= 0;
    }
    else
      frac-=i;
    len= from->sign + intg + test(frac) + frac;
  }
  *to_len=len;
  s[len]=0;

  if (from->sign)
    *s++='-';

  if (frac)
  {
    char *s1=s+intg;
    buf=buf0+ROUND_UP(intg);
    *s1++='.';
    for (; frac>0; frac-=DIG_PER_DEC1)
    {
      dec1 x=*buf++;
      for (i=min(frac, DIG_PER_DEC1); i; i--)
      {
        dec1 y=x/DIG_MASK;
        *s1++='0'+(uchar)y;
        x-=y*DIG_MASK;
        x*=10;
      }
    }
  }

  s+=intg;
  for (buf=buf0+ROUND_UP(intg); intg>0; intg-=DIG_PER_DEC1)
  {
    dec1 x=*--buf;
    for (i=min(intg, DIG_PER_DEC1); i; i--)
    {
      dec1 y=x/10;
      *--s='0'+(uchar)(x-y*10);
      x=y;
    }
  }
  return error;
}

/*
  Convert string to decimal

  SYNOPSIS
    str2decl()
      from    - value to convert
      to      - decimal where where the result will be stored
                to->buf and to->len must be set.
      end     - if not NULL, *end will be set to the char where
                conversion ended
      fixed   - use to->intg, to->frac as limits for input number

  NOTE
    to->intg and to->frac can be modified even when fixed=1
    (but only decreased, in this case)

  RETURN VALUE
    E_DEC_OK/E_DEC_TRUNCATED/E_DEC_OVERFLOW/E_DEC_BAD_NUM/E_DEC_OOM
*/

/*
  Convert decimal to its binary fixed-length representation
  two representations of the same length can be compared with memcmp
  with the correct -1/0/+1 result

  SYNOPSIS
    decimal2bin()
      from    - value to convert
      to      - points to buffer where string representation should be stored
      precision/scale - see decimal_bin_size() below

  NOTE
    the buffer is assumed to be of the size decimal_bin_size(precision, scale)

  RETURN VALUE
    E_DEC_OK/E_DEC_TRUNCATED/E_DEC_OVERFLOW
*/

/*
  Restores decimal from its binary fixed-length representation

  SYNOPSIS
    bin2decimal()
      from    - value to convert
      to      - result
      precision/scale - see decimal_bin_size() below

  NOTE
    see decimal2bin()
    the buffer is assumed to be of the size decimal_bin_size(precision, scale)

  RETURN VALUE
    E_DEC_OK/E_DEC_TRUNCATED/E_DEC_OVERFLOW
*/

int bin2decimal(const char *from, decimal *to, int precision, int scale)
{
  int error=E_DEC_OK,
      intg= precision - scale,
      intg0= intg / DIG_PER_DEC1,
      frac0= scale / DIG_PER_DEC1,
      intg0x= intg - intg0 * DIG_PER_DEC1,
      frac0x= scale - frac0*DIG_PER_DEC1,
      intg1= intg0 + (intg0x > 0),
      frac1= frac0 + (frac0x > 0),
      tmp_size= decimal_bin_size(precision, scale);
  char *tmp;
  dec1 *buf= to->buf,
       mask=(*from & 0x80) ? 0 : -1;
  char *stop;

  /* Initial implementation from Sergei modified "from" buffer, (which errored
     in binlog api when verifying checksum), so we declare from as read only and use
     a stack buffer instead */
  tmp= (char *)alloca(tmp_size);
  memcpy(tmp, from, tmp_size);
  *tmp^= 0x80; /* remove sign bit */
  from= tmp;

  sanity(to);

  FIX_INTG_FRAC_ERROR(to->len, intg1, frac1, error);
  if (unlikely(error))
  {
    if (intg1 < intg0+(intg0x>0))
    {
      from+= dig2bytes[intg0x] + sizeof(dec1)*(intg0 - intg1);
      frac0= frac0x= intg0x= 0;
      intg0= intg1;
    }
    else
    {
      frac0x= 0;
      frac0= frac1;
    }
  }

  to->sign= (mask != 0);
  to->intg= intg0 * DIG_PER_DEC1 + intg0x;
  to->frac= frac0 * DIG_PER_DEC1 + frac0x;

  if (intg0x)
  {
    int i= dig2bytes[intg0x];
    dec1 x= 0;
    switch (i)
    {
      case 1: x=myisam_sint1korr(from); break;
      case 2: x=myisam_sint2korr(from); break;
      case 3: x=myisam_sint3korr(from); break;
      case 4: x=myisam_sint4korr(from); break;
      default: DBUG_ASSERT(0); x= 0;
    }
    from+=i;
    *buf=x ^ mask;
    if (buf > to->buf || *buf != 0)
      buf++;
    else
      to->intg-=intg0x;
  }
  for (stop=(char *)from+intg0*sizeof(dec1); from < stop; from+=sizeof(dec1))
  {
    DBUG_ASSERT(sizeof(dec1) == 4);
    *buf=myisam_sint4korr(from) ^ mask;
    if (buf > to->buf || *buf != 0)
      buf++;
    else
      to->intg-=DIG_PER_DEC1;
  }
  DBUG_ASSERT(to->intg >=0);
  for (stop=(char *)from+frac0*sizeof(dec1); from < stop; from+=sizeof(dec1))
  {
    DBUG_ASSERT(sizeof(dec1) == 4);
    *buf=myisam_sint4korr(from) ^ mask;
    buf++;
  }
  if (frac0x)
  {
    int i=dig2bytes[frac0x];
    dec1 x= 0;
    switch (i)
    {
      case 1: x=myisam_sint1korr(from); break;
      case 2: x=myisam_sint2korr(from); break;
      case 3: x=myisam_sint3korr(from); break;
      case 4: x=myisam_sint4korr(from); break;
      default: DBUG_ASSERT(0); x= 0;
    }
    *buf= (x ^ mask) * powers10[DIG_PER_DEC1 - frac0x];
    buf++;
  }
  return error;
}

/*
  Returns the size of array to hold a decimal with given precision and scale

  RETURN VALUE
    size in dec1
    (multiply by sizeof(dec1) to get the size if bytes)
*/

int decimal_size(int precision, int scale)
{
  DBUG_ASSERT(scale >= 0 && precision > 0 && scale <= precision);
  return ROUND_UP(precision-scale)+ROUND_UP(scale);
}

/*
  Returns the size of array to hold a binary representation of a decimal

  RETURN VALUE
    size in bytes
*/

int decimal_bin_size(int precision, int scale)
{
  int intg=precision-scale,
      intg0=intg/DIG_PER_DEC1, frac0=scale/DIG_PER_DEC1,
      intg0x=intg-intg0*DIG_PER_DEC1, frac0x=scale-frac0*DIG_PER_DEC1;

  DBUG_ASSERT(scale >= 0 && precision > 0 && scale <= precision);
  return intg0*sizeof(dec1)+dig2bytes[intg0x]+
         frac0*sizeof(dec1)+dig2bytes[frac0x];
}
