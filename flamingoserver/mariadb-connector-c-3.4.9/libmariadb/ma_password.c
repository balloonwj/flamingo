/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB
                 2016,2018 MariaDB Corporation AB
   
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

/* password checking routines */
/*****************************************************************************
  The main idea is that no password are sent between client & server on
  connection and that no password are saved in mysql in a decodable form.

  On connection a random string is generated and sent to the client.
  The client generates a new string with a random generator inited with
  the hash values from the password and the sent string.
  This 'check' string is sent to the server where it is compared with
  a string generated from the stored hash_value of the password and the
  random string.

  The password is saved (in user.password) by using the PASSWORD() function in
  mysql.

  Example:
    update user set password=PASSWORD("hello") where user="test"
  This saves a hashed number as a string in the password field.
*****************************************************************************/

#include <ma_global.h>
#include <ma_sys.h>
#include <ma_string.h>
#include <ma_crypt.h>
#include "mysql.h"


void ma_randominit(struct rand_struct *rand_st,ulong seed1, ulong seed2)
{						/* For mysql 3.21.# */
#ifdef HAVE_purify
  memset((char*) rand_st, 0m sizeof(*rand_st));		/* Avoid UMC warnings */
#endif
  rand_st->max_value= 0x3FFFFFFFL;
  rand_st->max_value_dbl=(double) rand_st->max_value;
  rand_st->seed1=seed1%rand_st->max_value ;
  rand_st->seed2=seed2%rand_st->max_value;
}

double rnd(struct rand_struct *rand_st)
{
  rand_st->seed1=(rand_st->seed1*3+rand_st->seed2) % rand_st->max_value;
  rand_st->seed2=(rand_st->seed1+rand_st->seed2+33) % rand_st->max_value;
  return (((double) rand_st->seed1)/rand_st->max_value_dbl);
}

void ma_hash_password(ulong *result, const char *password, size_t len)
{
  register ulong nr=1345345333L, add=7, nr2=0x12345671L;
  ulong tmp;
  const char *password_end= password + len;
  for (; password < password_end; password++)
  {
    if (*password == ' ' || *password == '\t')
      continue;			/* skip space in password */
    tmp= (ulong) (uchar) *password;
    nr^= (((nr & 63)+add)*tmp)+ (nr << 8);
    nr2+=(nr2 << 8) ^ nr;
    add+=tmp;
  }
  result[0]=nr & (((ulong) 1L << 31) -1L); /* Don't use sign bit (str2int) */;
  result[1]=nr2 & (((ulong) 1L << 31) -1L);
  return;
}

/*
 * Generate a new message based on message and password
 * The same thing is done in client and server and the results are checked.
 */

/* scramble for 4.1 servers
 * Code based on php_nysqlnd_scramble function from PHP's mysqlnd extension,
 * written by Andrey Hristov (andrey@php.net)
 * License: PHP License 3.0
 */
void my_crypt(unsigned char *buffer, const unsigned char *s1, const unsigned char *s2, size_t len)
{
	const unsigned char *s1_end= s1 + len;
	while (s1 < s1_end) {
		*buffer++= *s1++ ^ *s2++;
	}
}

int ma_scramble_41(const unsigned char *buffer, const char *scramble, const char *password)
{
	MA_HASH_CTX *ctx;
	unsigned char sha1[MA_SHA1_HASH_SIZE];
	unsigned char sha2[MA_SHA1_HASH_SIZE];


	/* Phase 1: hash password */
  if (ma_hash(MA_HASH_SHA1, (unsigned char *)password, strlen(password), sha1))
    return 1;

	/* Phase 2: hash sha1 */
  if (ma_hash(MA_HASH_SHA1, (unsigned char *)sha1, MA_SHA1_HASH_SIZE, sha2))
    return 1;

	/* Phase 3: hash scramble + sha2 */
  ctx= ma_hash_new(MA_HASH_SHA1);
  if (!ctx)
    return 1;
  ma_hash_input(ctx, (unsigned char *)scramble, SCRAMBLE_LENGTH);
  ma_hash_input(ctx, (unsigned char *)sha2, MA_SHA1_HASH_SIZE);
  ma_hash_result(ctx, (unsigned char *)buffer);
  ma_hash_free(ctx);

	/* let's crypt buffer now */
	my_crypt((uchar *)buffer, (const unsigned char *)buffer, (const unsigned  char *)sha1, MA_SHA1_HASH_SIZE);
	return 0;
}
/* }}} */

void ma_make_scrambled_password(char *to,const char *password)
{
  ulong hash_res[2];
  ma_hash_password(hash_res,password, strlen(password));
  sprintf(to,"%08lx%08lx",hash_res[0],hash_res[1]);
}

/*
 * Generate a new message based on message and password
 * The same thing is done in client and server and the results are checked.
 */
char *ma_scramble_323(char *to, const char *message, const char *password)
{
  struct rand_struct rand_st;
  ulong hash_pass[2], hash_message[2];

  if (password && password[0])
  {
    char extra, *to_start=to;
    const char *end_scramble323= message + SCRAMBLE_LENGTH_323;
    ma_hash_password(hash_pass,password, (uint) strlen(password));
    /* Don't use strlen, could be > SCRAMBLE_LENGTH_323 ! */
    ma_hash_password(hash_message, message, SCRAMBLE_LENGTH_323);
    ma_randominit(&rand_st, hash_pass[0] ^ hash_message[0],
               hash_pass[1] ^ hash_message[1]);
    for (; message < end_scramble323; message++)
      *to++= (char) (floor(rnd(&rand_st) * 31) + 64);
    extra=(char) (floor(rnd(&rand_st) * 31));
    while (to_start != to)
      *(to_start++)^= extra;
  }
  *to= 0;
  return to;
}
