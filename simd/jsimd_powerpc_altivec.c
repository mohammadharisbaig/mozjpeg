/*
 * AltiVec optimizations for libjpeg-turbo
 *
 * Copyright (C) 2014, D. R. Commander.
 * All rights reserved.
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#define JPEG_INTERNALS
#include "../jinclude.h"
#include "../jpeglib.h"
#include "../jsimd.h"
#include "../jdct.h"
#include "../jsimddct.h"
#include "jsimd.h"
#include <altivec.h>


/* Common code */

#define TRANSPOSE(row, col)  \
{  \
  __vector short row04l, row04h, row15l, row15h,  \
                 row26l, row26h, row37l, row37h;  \
  __vector short col01e, col01o, col23e, col23o,  \
                 col45e, col45o, col67e, col67o;  \
  \
                                       /* transpose coefficients (phase 1) */ \
  row04l = vec_mergeh(row##0, row##4); /* row04l=(00 40 01 41 02 42 03 43) */ \
  row04h = vec_mergel(row##0, row##4); /* row04h=(04 44 05 45 06 46 07 47) */ \
  row15l = vec_mergeh(row##1, row##5); /* row15l=(10 50 11 51 12 52 13 53) */ \
  row15h = vec_mergel(row##1, row##5); /* row15h=(14 54 15 55 16 56 17 57) */ \
  row26l = vec_mergeh(row##2, row##6); /* row26l=(20 60 21 61 22 62 23 63) */ \
  row26h = vec_mergel(row##2, row##6); /* row26h=(24 64 25 65 26 66 27 67) */ \
  row37l = vec_mergeh(row##3, row##7); /* row37l=(30 70 31 71 32 72 33 73) */ \
  row37h = vec_mergel(row##3, row##7); /* row37h=(34 74 35 75 36 76 37 77) */ \
  \
                                       /* transpose coefficients (phase 2) */ \
  col01e = vec_mergeh(row04l, row26l); /* col01e=(00 20 40 60 01 21 41 61) */ \
  col23e = vec_mergel(row04l, row26l); /* col23e=(02 22 42 62 03 23 43 63) */ \
  col45e = vec_mergeh(row04h, row26h); /* col45e=(04 24 44 64 05 25 45 65) */ \
  col67e = vec_mergel(row04h, row26h); /* col67e=(06 26 46 66 07 27 47 67) */ \
  col01o = vec_mergeh(row15l, row37l); /* col01o=(10 30 50 70 11 31 51 71) */ \
  col23o = vec_mergel(row15l, row37l); /* col23o=(12 32 52 72 13 33 53 73) */ \
  col45o = vec_mergeh(row15h, row37h); /* col45o=(14 34 54 74 15 35 55 75) */ \
  col67o = vec_mergel(row15h, row37h); /* col67o=(16 36 56 76 17 37 57 77) */ \
  \
                                       /* transpose coefficients (phase 3) */ \
  col##0 = vec_mergeh(col01e, col01o); /* col0=(00 10 20 30 40 50 60 70) */   \
  col##1 = vec_mergel(col01e, col01o); /* col1=(01 11 21 31 41 51 61 71) */   \
  col##2 = vec_mergeh(col23e, col23o); /* col2=(02 12 22 32 42 52 62 72) */   \
  col##3 = vec_mergel(col23e, col23o); /* col3=(03 13 23 33 43 53 63 73) */   \
  col##4 = vec_mergeh(col45e, col45o); /* col4=(04 14 24 34 44 54 64 74) */   \
  col##5 = vec_mergel(col45e, col45o); /* col5=(05 15 25 35 45 55 65 75) */   \
  col##6 = vec_mergeh(col67e, col67o); /* col6=(06 16 26 36 46 56 66 76) */   \
  col##7 = vec_mergel(col67e, col67o); /* col7=(07 17 27 37 47 57 67 77) */   \
}


/* FAST INTEGER FORWARD DCT
 *
 * This is similar to the SSE2 implementation, except that we left-shift the
 * constants by 1 less bit (the -1 in IFAST_CONST_SHIFT.)  This is because
 * vec_madds(arg1, arg2, arg3) generates the 16-bit saturated sum of:
 *   the elements in arg3 + the most significant 17 bits of
 *     (the elements in arg1 * the elements in arg2).
 */

#define IFAST_F_0_382 98   /* FIX(0.382683433) */
#define IFAST_F_0_541 139  /* FIX(0.541196100) */
#define IFAST_F_0_707 181  /* FIX(0.707106781) */
#define IFAST_F_1_306 334  /* FIX(1.306562965) */

#define IFAST_CONST_BITS 8
#define IFAST_PRE_MULTIPLY_SCALE_BITS 2
#define IFAST_CONST_SHIFT \
  (16 - IFAST_PRE_MULTIPLY_SCALE_BITS - IFAST_CONST_BITS - 1)

static const __vector short jconst_fdct_ifast __attribute__((aligned(16))) =
{
  IFAST_F_0_382 << IFAST_CONST_SHIFT,
  IFAST_F_0_541 << IFAST_CONST_SHIFT,
  IFAST_F_0_707 << IFAST_CONST_SHIFT,
  IFAST_F_1_306 << IFAST_CONST_SHIFT
};

#define DO_FDCT_IFAST()  \
{  \
  /* Even part */  \
  \
  tmp10 = vec_add(tmp0, tmp3);  \
  tmp13 = vec_sub(tmp0, tmp3);  \
  tmp11 = vec_add(tmp1, tmp2);  \
  tmp12 = vec_sub(tmp1, tmp2);  \
  \
  out0  = vec_add(tmp10, tmp11);  \
  out4  = vec_sub(tmp10, tmp11);  \
  \
  z1 = vec_add(tmp12, tmp13);  \
  z1 = vec_sl(z1, PRE_MULTIPLY_SCALE_BITS);  \
  z1 = vec_madds(z1, PW_0707, zero);  \
  \
  out2 = vec_add(tmp13, z1);  \
  out6 = vec_sub(tmp13, z1);  \
  \
  /* Odd part */  \
  \
  tmp10 = vec_add(tmp4, tmp5);  \
  tmp11 = vec_add(tmp5, tmp6);  \
  tmp12 = vec_add(tmp6, tmp7);  \
  \
  tmp10 = vec_sl(tmp10, PRE_MULTIPLY_SCALE_BITS);  \
  tmp12 = vec_sl(tmp12, PRE_MULTIPLY_SCALE_BITS);  \
  z5 = vec_sub(tmp10, tmp12);  \
  z5 = vec_madds(z5, PW_0382, zero);  \
  \
  z2 = vec_madds(tmp10, PW_0541, zero);  \
  z2 = vec_add(z2, z5);  \
  \
  z4 = vec_madds(tmp12, PW_1306, zero);  \
  z4 = vec_add(z4, z5);  \
  \
  tmp11 = vec_sl(tmp11, PRE_MULTIPLY_SCALE_BITS);  \
  z3 = vec_madds(tmp11, PW_0707, zero);  \
  \
  z11 = vec_add(tmp7, z3);  \
  z13 = vec_sub(tmp7, z3);  \
  \
  out5 = vec_add(z13, z2);  \
  out3 = vec_sub(z13, z2);  \
  out1 = vec_add(z11, z4);  \
  out7 = vec_sub(z11, z4);  \
}

void
jsimd_fdct_ifast_altivec (DCTELEM *data)
{
  __vector short row0, row1, row2, row3, row4, row5, row6, row7,
    col0, col1, col2, col3, col4, col5, col6, col7,
    tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp10, tmp11, tmp12, tmp13,
    z1, z2, z3, z4, z5, z11, z13,
    out0, out1, out2, out3, out4, out5, out6, out7;

  /* Constants */
  __vector short zero = vec_splat_s16(0),
    PW_0382 = vec_splat(jconst_fdct_ifast, 0),
    PW_0541 = vec_splat(jconst_fdct_ifast, 1),
    PW_0707 = vec_splat(jconst_fdct_ifast, 2),
    PW_1306 = vec_splat(jconst_fdct_ifast, 3);
  __vector unsigned short PRE_MULTIPLY_SCALE_BITS =
    vec_splat_u16(IFAST_PRE_MULTIPLY_SCALE_BITS);

  /* Pass 1: process rows. */

  row0 = *(__vector short *)&data[0];
  row1 = *(__vector short *)&data[8];
  row2 = *(__vector short *)&data[16];
  row3 = *(__vector short *)&data[24];
  row4 = *(__vector short *)&data[32];
  row5 = *(__vector short *)&data[40];
  row6 = *(__vector short *)&data[48];
  row7 = *(__vector short *)&data[56];

  TRANSPOSE(row, col);

  tmp0 = vec_add(col0, col7);
  tmp7 = vec_sub(col0, col7);
  tmp1 = vec_add(col1, col6);
  tmp6 = vec_sub(col1, col6);
  tmp2 = vec_add(col2, col5);
  tmp5 = vec_sub(col2, col5);
  tmp3 = vec_add(col3, col4);
  tmp4 = vec_sub(col3, col4);

  DO_FDCT_IFAST();

  /* Pass 2: process columns. */

  TRANSPOSE(out, row);

  tmp0 = vec_add(row0, row7);
  tmp7 = vec_sub(row0, row7);
  tmp1 = vec_add(row1, row6);
  tmp6 = vec_sub(row1, row6);
  tmp2 = vec_add(row2, row5);
  tmp5 = vec_sub(row2, row5);
  tmp3 = vec_add(row3, row4);
  tmp4 = vec_sub(row3, row4);

  DO_FDCT_IFAST();

  *(__vector short *)&data[0] = out0;
  *(__vector short *)&data[8] = out1;
  *(__vector short *)&data[16] = out2;
  *(__vector short *)&data[24] = out3;
  *(__vector short *)&data[32] = out4;
  *(__vector short *)&data[40] = out5;
  *(__vector short *)&data[48] = out6;
  *(__vector short *)&data[56] = out7;
}


/* SLOW INTEGER FORWARD DCT */

#define ISLOW_F_0_298 2446   /* FIX(0.298631336) */
#define ISLOW_F_0_390 3196   /* FIX(0.390180644) */
#define ISLOW_F_0_541 4433   /* FIX(0.541196100) */
#define ISLOW_F_0_765 6270   /* FIX(0.765366865) */
#define ISLOW_F_0_899 7373   /* FIX(0.899976223) */
#define ISLOW_F_1_175 9633   /* FIX(1.175875602) */
#define ISLOW_F_1_501 12299  /* FIX(1.501321110) */
#define ISLOW_F_1_847 15137  /* FIX(1.847759065) */
#define ISLOW_F_1_961 16069  /* FIX(1.961570560) */
#define ISLOW_F_2_053 16819  /* FIX(2.053119869) */
#define ISLOW_F_2_562 20995  /* FIX(2.562915447) */
#define ISLOW_F_3_072 25172  /* FIX(3.072711026) */

#define ISLOW_CONST_BITS 13
#define ISLOW_PASS1_BITS 2
#define ISLOW_DESCALE_P1 (ISLOW_CONST_BITS - ISLOW_PASS1_BITS)
#define ISLOW_DESCALE_P2 (ISLOW_CONST_BITS + ISLOW_PASS1_BITS)

static const __vector int jconst_fdct_islow __attribute__((aligned(16))) =
{
  1 << (ISLOW_DESCALE_P1 - 1),
  1 << (ISLOW_DESCALE_P2 - 1)
};

static const __vector short jconst_fdct_islow2 __attribute__((aligned(16))) =
{
  1 << (ISLOW_PASS1_BITS - 1)
};

#define DO_FDCT_ISLOW_COMMON(PASS)  \
{  \
  tmp1312l = vec_mergeh(tmp13, tmp12);  \
  tmp1312h = vec_mergel(tmp13, tmp12);  \
  \
  out2l = vec_msums(tmp1312l, PW_F130_F054, zero);  \
  out2h = vec_msums(tmp1312h, PW_F130_F054, zero);  \
  out6l = vec_msums(tmp1312l, PW_F054_MF130, zero);  \
  out6h = vec_msums(tmp1312h, PW_F054_MF130, zero);  \
  \
  out2l = vec_add(out2l, PD_DESCALE_P##PASS);  \
  out2h = vec_add(out2h, PD_DESCALE_P##PASS);  \
  out2l = vec_sra(out2l, DESCALE_P##PASS);  \
  out2h = vec_sra(out2h, DESCALE_P##PASS);  \
  \
  out6l = vec_add(out6l, PD_DESCALE_P##PASS);  \
  out6h = vec_add(out6h, PD_DESCALE_P##PASS);  \
  out6l = vec_sra(out6l, DESCALE_P##PASS);  \
  out6h = vec_sra(out6h, DESCALE_P##PASS);  \
  \
  out2 = vec_pack(out2l, out2h);  \
  out6 = vec_pack(out6l, out6h);  \
  \
  /* Odd part */  \
  \
  z3 = vec_add(tmp4, tmp6);  \
  z4 = vec_add(tmp5, tmp7);  \
  \
  z34l = vec_mergeh(z3, z4);  \
  z34h = vec_mergel(z3, z4);  \
  \
  z3l = vec_msums(z34l, PW_MF078_F117, zero);  \
  z3h = vec_msums(z34h, PW_MF078_F117, zero);  \
  z4l = vec_msums(z34l, PW_F117_F078, zero);  \
  z4h = vec_msums(z34h, PW_F117_F078, zero);  \
  \
  tmp47l = vec_mergeh(tmp4, tmp7);  \
  tmp47h = vec_mergel(tmp4, tmp7);  \
  \
  tmp4l = vec_msums(tmp47l, PW_MF060_MF089, zero);  \
  tmp4h = vec_msums(tmp47h, PW_MF060_MF089, zero);  \
  tmp7l = vec_msums(tmp47l, PW_MF089_F060, zero);  \
  tmp7h = vec_msums(tmp47h, PW_MF089_F060, zero);  \
  \
  out7l = vec_add(z3l, tmp4l);  \
  out7h = vec_add(z3h, tmp4h);  \
  out1l = vec_add(z4l, tmp7l);  \
  out1h = vec_add(z4h, tmp7h);  \
  \
  out7l = vec_add(out7l, PD_DESCALE_P##PASS);  \
  out7h = vec_add(out7h, PD_DESCALE_P##PASS);  \
  out7l = vec_sra(out7l, DESCALE_P##PASS);  \
  out7h = vec_sra(out7h, DESCALE_P##PASS);  \
  \
  out1l = vec_add(out1l, PD_DESCALE_P##PASS);  \
  out1h = vec_add(out1h, PD_DESCALE_P##PASS);  \
  out1l = vec_sra(out1l, DESCALE_P##PASS);  \
  out1h = vec_sra(out1h, DESCALE_P##PASS);  \
  \
  out7 = vec_pack(out7l, out7h);  \
  out1 = vec_pack(out1l, out1h);  \
  \
  tmp56l = vec_mergeh(tmp5, tmp6);  \
  tmp56h = vec_mergel(tmp5, tmp6);  \
  \
  tmp5l = vec_msums(tmp56l, PW_MF050_MF256, zero);  \
  tmp5h = vec_msums(tmp56h, PW_MF050_MF256, zero);  \
  tmp6l = vec_msums(tmp56l, PW_MF256_F050, zero);  \
  tmp6h = vec_msums(tmp56h, PW_MF256_F050, zero);  \
  \
  out5l = vec_add(tmp5l, z4l);  \
  out5h = vec_add(tmp5h, z4h);  \
  out3l = vec_add(tmp6l, z3l);  \
  out3h = vec_add(tmp6h, z3h);  \
  \
  out5l = vec_add(out5l, PD_DESCALE_P##PASS);  \
  out5h = vec_add(out5h, PD_DESCALE_P##PASS);  \
  out5l = vec_sra(out5l, DESCALE_P##PASS);  \
  out5h = vec_sra(out5h, DESCALE_P##PASS);  \
  \
  out3l = vec_add(out3l, PD_DESCALE_P##PASS);  \
  out3h = vec_add(out3h, PD_DESCALE_P##PASS);  \
  out3l = vec_sra(out3l, DESCALE_P##PASS);  \
  out3h = vec_sra(out3h, DESCALE_P##PASS);  \
  \
  out5 = vec_pack(out5l, out5h);  \
  out3 = vec_pack(out3l, out3h);  \
}

#define DO_FDCT_ISLOW_ROWS()  \
{  \
  /* Even part */  \
  \
  tmp10 = vec_add(tmp0, tmp3);  \
  tmp13 = vec_sub(tmp0, tmp3);  \
  tmp11 = vec_add(tmp1, tmp2);  \
  tmp12 = vec_sub(tmp1, tmp2);  \
  \
  out0  = vec_add(tmp10, tmp11);  \
  out0  = vec_sl(out0, PASS1_BITS);  \
  out4  = vec_sub(tmp10, tmp11);  \
  out4  = vec_sl(out4, PASS1_BITS);  \
  \
  DO_FDCT_ISLOW_COMMON(1);  \
}

#define DO_FDCT_ISLOW_COLS()  \
{  \
  /* Even part */  \
  \
  tmp10 = vec_add(tmp0, tmp3);  \
  tmp13 = vec_sub(tmp0, tmp3);  \
  tmp11 = vec_add(tmp1, tmp2);  \
  tmp12 = vec_sub(tmp1, tmp2);  \
  \
  out0  = vec_add(tmp10, tmp11);  \
  out0  = vec_add(out0, PW_DESCALE_P2X);  \
  out0  = vec_sra(out0, PASS1_BITS);  \
  out4  = vec_sub(tmp10, tmp11);  \
  out4  = vec_add(out4, PW_DESCALE_P2X);  \
  out4  = vec_sra(out4, PASS1_BITS);  \
  \
  DO_FDCT_ISLOW_COMMON(2);  \
}

void
jsimd_fdct_islow_altivec (DCTELEM *data)
{
  __vector short row0, row1, row2, row3, row4, row5, row6, row7,
    col0, col1, col2, col3, col4, col5, col6, col7,
    tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp10, tmp11, tmp12, tmp13,
    tmp47l, tmp47h, tmp56l, tmp56h, tmp1312l, tmp1312h,
    z3, z4, z34l, z34h,
    out0, out1, out2, out3, out4, out5, out6, out7;
  __vector int tmp4l, tmp4h, tmp5l, tmp5h, tmp6l, tmp6h, tmp7l, tmp7h,
    z3l, z3h, z4l, z4h,
    out1l, out1h, out2l, out2h, out3l, out3h, out5l, out5h, out6l, out6h,
    out7l, out7h;

  __vector short PW_F130_F054 = {ISLOW_F_0_541 + ISLOW_F_0_765, ISLOW_F_0_541,
    ISLOW_F_0_541 + ISLOW_F_0_765, ISLOW_F_0_541,
    ISLOW_F_0_541 + ISLOW_F_0_765, ISLOW_F_0_541,
    ISLOW_F_0_541 + ISLOW_F_0_765, ISLOW_F_0_541};
  __vector short PW_F054_MF130 = {ISLOW_F_0_541, ISLOW_F_0_541 - ISLOW_F_1_847,
    ISLOW_F_0_541, ISLOW_F_0_541 - ISLOW_F_1_847,
    ISLOW_F_0_541, ISLOW_F_0_541 - ISLOW_F_1_847,
    ISLOW_F_0_541, ISLOW_F_0_541 - ISLOW_F_1_847};
  __vector short PW_MF078_F117 = {ISLOW_F_1_175 - ISLOW_F_1_961, ISLOW_F_1_175,
    ISLOW_F_1_175 - ISLOW_F_1_961, ISLOW_F_1_175,
    ISLOW_F_1_175 - ISLOW_F_1_961, ISLOW_F_1_175,
    ISLOW_F_1_175 - ISLOW_F_1_961, ISLOW_F_1_175};
  __vector short PW_F117_F078 = {ISLOW_F_1_175, ISLOW_F_1_175 - ISLOW_F_0_390,
    ISLOW_F_1_175, ISLOW_F_1_175 - ISLOW_F_0_390,
    ISLOW_F_1_175, ISLOW_F_1_175 - ISLOW_F_0_390,
    ISLOW_F_1_175, ISLOW_F_1_175 - ISLOW_F_0_390};
  __vector short PW_MF060_MF089 = {
    ISLOW_F_0_298 - ISLOW_F_0_899, -ISLOW_F_0_899,
    ISLOW_F_0_298 - ISLOW_F_0_899, -ISLOW_F_0_899,
    ISLOW_F_0_298 - ISLOW_F_0_899, -ISLOW_F_0_899,
    ISLOW_F_0_298 - ISLOW_F_0_899, -ISLOW_F_0_899};
  __vector short PW_MF089_F060 = {
    -ISLOW_F_0_899, ISLOW_F_1_501 - ISLOW_F_0_899,
    -ISLOW_F_0_899, ISLOW_F_1_501 - ISLOW_F_0_899,
    -ISLOW_F_0_899, ISLOW_F_1_501 - ISLOW_F_0_899,
    -ISLOW_F_0_899, ISLOW_F_1_501 - ISLOW_F_0_899};
  __vector short PW_MF050_MF256 = {
    ISLOW_F_2_053 - ISLOW_F_2_562, -ISLOW_F_2_562,
    ISLOW_F_2_053 - ISLOW_F_2_562, -ISLOW_F_2_562,
    ISLOW_F_2_053 - ISLOW_F_2_562, -ISLOW_F_2_562,
    ISLOW_F_2_053 - ISLOW_F_2_562, -ISLOW_F_2_562};
  __vector short PW_MF256_F050 = {
    -ISLOW_F_2_562, ISLOW_F_3_072 - ISLOW_F_2_562,
    -ISLOW_F_2_562, ISLOW_F_3_072 - ISLOW_F_2_562,
    -ISLOW_F_2_562, ISLOW_F_3_072 - ISLOW_F_2_562,
    -ISLOW_F_2_562, ISLOW_F_3_072 - ISLOW_F_2_562};
  __vector short PW_DESCALE_P2X = vec_splat(jconst_fdct_islow2, 0);

  /* Constants */
  __vector unsigned short PASS1_BITS = vec_splat_u16(ISLOW_PASS1_BITS);
  __vector int zero = vec_splat_s32(0),
    PD_DESCALE_P1 = vec_splat(jconst_fdct_islow, 0),
    PD_DESCALE_P2 = vec_splat(jconst_fdct_islow, 1);
  __vector unsigned int DESCALE_P1 = vec_splat_u32(ISLOW_DESCALE_P1),
    DESCALE_P2 = vec_splat_u32(ISLOW_DESCALE_P2);

  /* Pass 1: process rows. */

  row0 = *(__vector short *)&data[0];
  row1 = *(__vector short *)&data[8];
  row2 = *(__vector short *)&data[16];
  row3 = *(__vector short *)&data[24];
  row4 = *(__vector short *)&data[32];
  row5 = *(__vector short *)&data[40];
  row6 = *(__vector short *)&data[48];
  row7 = *(__vector short *)&data[56];

  TRANSPOSE(row, col);

  tmp0 = vec_add(col0, col7);
  tmp7 = vec_sub(col0, col7);
  tmp1 = vec_add(col1, col6);
  tmp6 = vec_sub(col1, col6);
  tmp2 = vec_add(col2, col5);
  tmp5 = vec_sub(col2, col5);
  tmp3 = vec_add(col3, col4);
  tmp4 = vec_sub(col3, col4);

  DO_FDCT_ISLOW_ROWS();

  /* Pass 2: process columns. */

  TRANSPOSE(out, row);

  tmp0 = vec_add(row0, row7);
  tmp7 = vec_sub(row0, row7);
  tmp1 = vec_add(row1, row6);
  tmp6 = vec_sub(row1, row6);
  tmp2 = vec_add(row2, row5);
  tmp5 = vec_sub(row2, row5);
  tmp3 = vec_add(row3, row4);
  tmp4 = vec_sub(row3, row4);

  DO_FDCT_ISLOW_COLS();

  *(__vector short *)&data[0] = out0;
  *(__vector short *)&data[8] = out1;
  *(__vector short *)&data[16] = out2;
  *(__vector short *)&data[24] = out3;
  *(__vector short *)&data[32] = out4;
  *(__vector short *)&data[40] = out5;
  *(__vector short *)&data[48] = out6;
  *(__vector short *)&data[56] = out7;
}


/* FAST INTEGER INVERSE DCT
 *
 * This is similar to the SSE2 implementation, except that we left-shift the
 * constants by 1 less bit (the -1 in IFAST_CONST_SHIFT.)  This is because
 * vec_madds(arg1, arg2, arg3) generates the 16-bit saturated sum of:
 *   the elements in arg3 + the most significant 17 bits of
 *     (the elements in arg1 * the elements in arg2).
 */

#define F_1_082 277              /* FIX(1.082392200) */
#define F_1_414 362              /* FIX(1.414213562) */
#define F_1_847 473              /* FIX(1.847759065) */
#define F_2_613 669              /* FIX(2.613125930) */
#define F_1_613 (F_2_613 - 256)  /* FIX(2.613125930) - FIX(1) */

#define IFAST_PASS1_BITS 2
#define IFAST_CENTERJSAMPLE 128

static const __vector short jconst_idct_ifast __attribute__((aligned(16))) =
{
  F_1_414 << IFAST_CONST_SHIFT,
  F_1_847 << IFAST_CONST_SHIFT,
  -F_1_613 << IFAST_CONST_SHIFT,
  F_1_082 << IFAST_CONST_SHIFT
};

static const __vector signed char jconst_idct_ifast2
  __attribute__((aligned(16))) =
{
  IFAST_CENTERJSAMPLE
};

#define DO_IDCT_IFAST(in)  \
{  \
  /* Even part */  \
  \
  tmp10 = vec_add(in##0, in##4);  \
  tmp11 = vec_sub(in##0, in##4);  \
  tmp13 = vec_add(in##2, in##6);  \
  \
  tmp12 = vec_sub(in##2, in##6);  \
  tmp12 = vec_sl(tmp12, PRE_MULTIPLY_SCALE_BITS);  \
  tmp12 = vec_madds(tmp12, PW_F1414, zero);  \
  tmp12 = vec_sub(tmp12, tmp13);  \
  \
  tmp0 = vec_add(tmp10, tmp13);  \
  tmp3 = vec_sub(tmp10, tmp13);  \
  tmp1 = vec_add(tmp11, tmp12);  \
  tmp2 = vec_sub(tmp11, tmp12);  \
  \
  /* Odd part */  \
  \
  z13 = vec_add(in##5, in##3);  \
  z10 = vec_sub(in##5, in##3);  \
  z10s = vec_sl(z10, PRE_MULTIPLY_SCALE_BITS);  \
  z11 = vec_add(in##1, in##7);  \
  z12s = vec_sub(in##1, in##7);  \
  z12s = vec_sl(z12s, PRE_MULTIPLY_SCALE_BITS);  \
  \
  tmp11 = vec_sub(z11, z13);  \
  tmp11 = vec_sl(tmp11, PRE_MULTIPLY_SCALE_BITS);  \
  tmp11 = vec_madds(tmp11, PW_F1414, zero);  \
  \
  tmp7 = vec_add(z11, z13);  \
  \
  z5 = vec_add(z10s, z12s);  \
  z5 = vec_madds(z5, PW_F1847, zero);  \
  \
  tmp10 = vec_madds(z12s, PW_F1082, zero);  \
  tmp10 = vec_sub(tmp10, z5);  \
  tmp12 = vec_madds(z10s, PW_MF1613, zero);  \
  tmp12 = vec_sub(tmp12, z10);  \
  tmp12 = vec_add(tmp12, z5);  \
  \
  tmp6 = vec_sub(tmp12, tmp7);  \
  tmp5 = vec_sub(tmp11, tmp6);  \
  tmp4 = vec_add(tmp10, tmp5);  \
  \
  out0 = vec_add(tmp0, tmp7);  \
  out1 = vec_add(tmp1, tmp6);  \
  out2 = vec_add(tmp2, tmp5);  \
  out3 = vec_sub(tmp3, tmp4);  \
  out4 = vec_add(tmp3, tmp4);  \
  out5 = vec_sub(tmp2, tmp5);  \
  out6 = vec_sub(tmp1, tmp6);  \
  out7 = vec_sub(tmp0, tmp7);  \
}

void
jsimd_idct_ifast_altivec (void * dct_table_, JCOEFPTR coef_block,
                          JSAMPARRAY output_buf, JDIMENSION output_col)
{
  short *dct_table = (short *)dct_table_;
  __vector short row0, row1, row2, row3, row4, row5, row6, row7,
    col0, col1, col2, col3, col4, col5, col6, col7,
    quant0, quant1, quant2, quant3, quant4, quant5, quant6, quant7,
    tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp10, tmp11, tmp12, tmp13,
    z5, z10, z10s, z11, z12s, z13,
    out0, out1, out2, out3, out4, out5, out6, out7;
  __vector signed char outb;
  long long *outptr, *outbptr = (long long *)(&outb);

  /* Constants */
  __vector short zero = vec_splat_s16(0),
    PW_F1414 = vec_splat(jconst_idct_ifast, 0),
    PW_F1847 = vec_splat(jconst_idct_ifast, 1),
    PW_MF1613 = vec_splat(jconst_idct_ifast, 2),
    PW_F1082 = vec_splat(jconst_idct_ifast, 3);
  __vector unsigned short
    PRE_MULTIPLY_SCALE_BITS = vec_splat_u16(IFAST_PRE_MULTIPLY_SCALE_BITS),
    PASS1_BITS3 = vec_splat_u16(IFAST_PASS1_BITS + 3);
  __vector signed char PB_CENTERJSAMP = vec_splat(jconst_idct_ifast2, 0);

  /* Pass 1: process columns. */

  col0 = *(__vector short *)&coef_block[0];
  col1 = *(__vector short *)&coef_block[8];
  col2 = *(__vector short *)&coef_block[16];
  col3 = *(__vector short *)&coef_block[24];
  col4 = *(__vector short *)&coef_block[32];
  col5 = *(__vector short *)&coef_block[40];
  col6 = *(__vector short *)&coef_block[48];
  col7 = *(__vector short *)&coef_block[56];

  tmp1 = vec_or(col1, col2);
  tmp2 = vec_or(col3, col4);
  tmp1 = vec_or(tmp1, tmp2);
  tmp3 = vec_or(col5, col6);
  tmp3 = vec_or(tmp3, col7);
  tmp1 = vec_or(tmp1, tmp3);

  quant0 = *(__vector short *)&dct_table[0];
  col0 = vec_mladd(col0, quant0, zero);

  if (vec_all_eq(tmp1, zero)) {
    /* AC terms all zero */

    row0 = vec_splat(col0, 0);
    row1 = vec_splat(col0, 1);
    row2 = vec_splat(col0, 2);
    row3 = vec_splat(col0, 3);
    row4 = vec_splat(col0, 4);
    row5 = vec_splat(col0, 5);
    row6 = vec_splat(col0, 6);
    row7 = vec_splat(col0, 7);

  } else {

    quant1 = *(__vector short *)&dct_table[8];
    quant2 = *(__vector short *)&dct_table[16];
    quant3 = *(__vector short *)&dct_table[24];
    quant4 = *(__vector short *)&dct_table[32];
    quant5 = *(__vector short *)&dct_table[40];
    quant6 = *(__vector short *)&dct_table[48];
    quant7 = *(__vector short *)&dct_table[56];

    col1 = vec_mladd(col1, quant1, zero);
    col2 = vec_mladd(col2, quant2, zero);
    col3 = vec_mladd(col3, quant3, zero);
    col4 = vec_mladd(col4, quant4, zero);
    col5 = vec_mladd(col5, quant5, zero);
    col6 = vec_mladd(col6, quant6, zero);
    col7 = vec_mladd(col7, quant7, zero);

    DO_IDCT_IFAST(col);

    TRANSPOSE(out, row);
  }

  /* Pass 2: process rows. */

  DO_IDCT_IFAST(row);

  out0 = vec_sra(out0, PASS1_BITS3);
  out1 = vec_sra(out1, PASS1_BITS3);
  out2 = vec_sra(out2, PASS1_BITS3);
  out3 = vec_sra(out3, PASS1_BITS3);
  out4 = vec_sra(out4, PASS1_BITS3);
  out5 = vec_sra(out5, PASS1_BITS3);
  out6 = vec_sra(out6, PASS1_BITS3);
  out7 = vec_sra(out7, PASS1_BITS3);

  TRANSPOSE(out, col);

  outb = vec_packs(col0, col1);
  outb = vec_add(outb, PB_CENTERJSAMP);
  outptr = (long long *)(output_buf[0] + output_col);
  *outptr = outbptr[0];
  outptr = (long long *)(output_buf[1] + output_col);
  *outptr = outbptr[1];

  outb = vec_packs(col2, col3);
  outb = vec_add(outb, PB_CENTERJSAMP);
  outptr = (long long *)(output_buf[2] + output_col);
  *outptr = outbptr[0];
  outptr = (long long *)(output_buf[3] + output_col);
  *outptr = outbptr[1];

  outb = vec_packs(col4, col5);
  outb = vec_add(outb, PB_CENTERJSAMP);
  outptr = (long long *)(output_buf[4] + output_col);
  *outptr = outbptr[0];
  outptr = (long long *)(output_buf[5] + output_col);
  *outptr = outbptr[1];

  outb = vec_packs(col6, col7);
  outb = vec_add(outb, PB_CENTERJSAMP);
  outptr = (long long *)(output_buf[6] + output_col);
  *outptr = outbptr[0];
  outptr = (long long *)(output_buf[7] + output_col);
  *outptr = outbptr[1];
}
