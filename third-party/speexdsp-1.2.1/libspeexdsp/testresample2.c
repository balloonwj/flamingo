/* Copyright (C) 2007 Jean-Marc Valin

   File: testresample2.c
   Testing the resampling code

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "speex/speex_resampler.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define PERIOD 32
#define INBLOCK 1024
#define RATE 48000

int main()
{
   spx_uint32_t i;
   float *fin, *fout;
   int rate = 1000, off = 0, avail = INBLOCK;
   SpeexResamplerState *st = speex_resampler_init(1, RATE, RATE, 4, NULL);
   speex_resampler_set_rate(st, RATE, rate);
   speex_resampler_skip_zeros(st);

   fin = malloc(INBLOCK*2*sizeof(float));
   for (i=0; i<INBLOCK*2;i++)
     fin[i] = sinf ((float)i/PERIOD * 2 * M_PI) * 0.9;

   fout = malloc(INBLOCK*4*sizeof(float));

   while (1)
   {
      spx_uint32_t in_len;
      spx_uint32_t out_len;

      in_len = avail;
      out_len = (in_len * rate + RATE-1) / RATE;

      fprintf (stderr, "%d %d %d %d -> ", rate, off, in_len, out_len);

      speex_resampler_process_float(st, 0, fin + off, &in_len, fout, &out_len);

      fprintf (stderr, "%d %d\n", in_len, out_len);
      off += in_len;
      avail = avail - in_len + INBLOCK;

      if (off >= INBLOCK)
        off -= INBLOCK;

      fwrite(fout, sizeof(float), out_len, stdout);

      rate += 100;
      if (rate > 128000)
        break;

      speex_resampler_set_rate(st, RATE, rate);
   }
   speex_resampler_destroy(st);
   free(fin);
   free(fout);
   return 0;
}

