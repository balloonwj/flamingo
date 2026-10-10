/* Copyright (C) 2000 Sergei Golubchik 
*/

#ifndef _decimal_h
#define _decimal_h

typedef enum {TRUNCATE=0, HALF_EVEN, HALF_UP, CEILING, FLOOR} decimal_round_mode;
typedef int32 decimal_digit;

typedef struct st_decimal {
  int    intg, frac, len;
  my_bool sign;
  decimal_digit *buf;
} decimal;

int decimal2string(decimal *from, char *to, int *to_len);
int bin2decimal(const char *from, decimal *to, int precision, int scale);

int decimal_size(int precision, int scale);
int decimal_bin_size(int precision, int scale);
int decimal_result_size(decimal *from1, decimal *from2, char op, int param);


/* set a decimal to zero */

#define decimal_make_zero(dec)        do {                \
                                        (dec)->buf[0]=0;    \
                                        (dec)->intg=1;      \
                                        (dec)->frac=0;      \
                                        (dec)->sign=0;      \
                                      } while(0)

/*
  returns the length of the buffer to hold string representation
  of the decimal (including decimal dot, possible sign and \0)
*/

#define decimal_string_size(dec) ((dec)->intg + (dec)->frac + ((dec)->frac > 0) + 2)

/* negate a decimal */
#define decimal_neg(dec) do { (dec)->sign^=1; } while(0)

/*
  conventions:

    decimal_smth() == 0     -- everything's ok
    decimal_smth() <= 1     -- result is usable, but precision loss is possible
    decimal_smth() <= 2     -- result can be unusable, most significant digits
                               could've been lost
    decimal_smth() >  2     -- no result was generated
*/

#define E_DEC_OK                0
#define E_DEC_TRUNCATED         1
#define E_DEC_OVERFLOW          2
#define E_DEC_DIV_ZERO          4
#define E_DEC_BAD_NUM           8
#define E_DEC_OOM              16

#define E_DEC_ERROR            31
#define E_DEC_FATAL_ERROR      30

#endif

