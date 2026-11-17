/* globe_pie8: the compact globe kernel's arithmetic for one block of 8 interior pixels (alpha 255) on the ESP32-S3's
 * PIE unit, bit-exact with omni_globe_cspan_ref (Task 11, O2). globe_pie.c gathers the block's texels and shade
 * entries into the scratch (scalar: they are table lookups) and calls this; see globe_pie.c for the layout and
 * the rounding argument. Instruction semantics: ESP32-S3 TRM v1.8 ch. 1.8 (EE.VMUL.U16: the 32-bit product
 * logically shifted right by SAR, low 16 bits kept; EE.VMULAS.U16.QACC: the product added exactly into a 40-bit
 * lane; EE.SRCMB.S16.QACC: lanes arithmetically shifted, saturated to int16; EE.VUNZIP.16 / EE.VZIP.8). */

    .section .text.globe_pie8,"ax",@progbits
    .align  4
    .global globe_pie8
    .type   globe_pie8,@function

/* void globe_pie8(pie_scratch_t *s, const omni_globe_blk_t *b, const uint16_t *k)
 *   s: +0 tex[8] (day | night << 16)  +32 sh0[8] (dr | dg << 16)  +64 sh1[8] (db | nr << 16)
 *      +96 sh2[8] (ng | nb << 16)  +128 at[8]  +144 out[8]  +160 four 16-byte spill slots; 16-byte aligned
 *   b: the block (limb[8] at +40, rim[8] at +48); k: K[] of globe_pie.c */
globe_pie8:
    entry   a1, 32
    mov     a5, a2
    ee.vld.128.ip q0, a5, 16
    ee.vld.128.ip q1, a5, 16          /* a5 -> sh0 */
    ee.vunzip.16 q0, q1               /* q0 day, q1 night */
    addi    a7, a2, 160
    ee.vldbc.16 q7, a4                /* 1 */
    addi    a6, a4, 4
    ee.vldbc.16 q6, a6                /* 0xf8 */
    /* red: ((x >> 8) & 0xf8) | (x >> 13) */
    ssai    8
    ee.vmul.u16 q2, q0, q7
    ee.vmul.u16 q3, q1, q7
    ee.andq q2, q2, q6
    ee.andq q3, q3, q6
    ssai    13
    ee.vmul.u16 q4, q0, q7
    ee.vmul.u16 q5, q1, q7
    ee.orq  q2, q2, q4
    ee.orq  q3, q3, q5
    ee.vst.128.ip q2, a7, 16          /* day red */
    ee.vst.128.ip q3, a7, 16          /* night red */
    /* blue: ((x << 3) & 0xf8) | ((x >> 2) & 7) */
    addi    a6, a4, 2
    ee.vldbc.16 q5, a6                /* 8 */
    ssai    0
    ee.vmul.u16 q2, q0, q5
    ee.vmul.u16 q3, q1, q5
    ee.andq q2, q2, q6
    ee.andq q3, q3, q6
    ssai    2
    ee.vmul.u16 q4, q0, q7
    ee.vmul.u16 q5, q1, q7
    addi    a6, a4, 10
    ee.vldbc.16 q6, a6                /* 7 */
    ee.andq q4, q4, q6
    ee.andq q5, q5, q6
    ee.orq  q2, q2, q4
    ee.orq  q3, q3, q5
    ee.vst.128.ip q2, a7, 16          /* day blue */
    ee.vst.128.ip q3, a7, 16          /* night blue */
    /* green: ((x >> 3) & 0xfc) | ((x >> 9) & 3) */
    ssai    3
    ee.vmul.u16 q2, q0, q7
    ee.vmul.u16 q3, q1, q7
    addi    a6, a4, 6
    ee.vldbc.16 q6, a6                /* 0xfc */
    ee.andq q2, q2, q6
    ee.andq q3, q3, q6
    ssai    9
    ee.vmul.u16 q4, q0, q7
    ee.vmul.u16 q5, q1, q7
    addi    a6, a4, 8
    ee.vldbc.16 q6, a6                /* 3 */
    ee.andq q4, q4, q6
    ee.andq q5, q5, q6
    ee.orq  q0, q2, q4                /* q0 day green */
    ee.orq  q1, q3, q5                /* q1 night green */
    /* shade factors */
    ee.vld.128.ip q2, a5, 16
    ee.vld.128.ip q3, a5, 16
    ee.vunzip.16 q2, q3               /* q2 dr, q3 dg */
    ee.vld.128.ip q4, a5, 16
    ee.vld.128.ip q5, a5, 16
    ee.vunzip.16 q4, q5               /* q4 db, q5 nr */
    ee.vld.128.ip q6, a5, 16
    ee.vld.128.ip q7, a5, 16          /* a5 -> at */
    ee.vunzip.16 q6, q7               /* q6 ng, q7 nb */
    movi    a8, 8
    /* (day * kd + night * kn) >> 8, exact in the 40-bit lanes */
    ee.zero.qacc
    ee.vmulas.u16.qacc q0, q3
    ee.vmulas.u16.qacc q1, q6
    ee.srcmb.s16.qacc q0, a8, 0       /* q0 green */
    addi    a7, a2, 160
    ee.vld.128.ip q1, a7, 16
    ee.vld.128.ip q3, a7, 16
    ee.zero.qacc
    ee.vmulas.u16.qacc q1, q2
    ee.vmulas.u16.qacc q3, q5
    ee.srcmb.s16.qacc q1, a8, 0       /* q1 red */
    ee.vld.128.ip q2, a7, 16
    ee.vld.128.ip q3, a7, 16
    ee.zero.qacc
    ee.vmulas.u16.qacc q2, q4
    ee.vmulas.u16.qacc q3, q7
    ee.srcmb.s16.qacc q2, a8, 0       /* q2 blue */
    /* limb and rim, u8 -> u16 */
    addi    a9, a3, 40
    ee.vld.l.64.ip q3, a9, 8
    ee.zero.q q4
    ee.vzip.8 q3, q4                  /* q3 limb */
    ee.vld.l.64.ip q5, a9, 0
    ee.zero.q q4
    ee.vzip.8 q5, q4                  /* q5 rim */
    ssai    8
    ee.vmul.u16 q0, q0, q3            /* (sum * limb) >> 8 */
    ee.vmul.u16 q1, q1, q3
    ee.vmul.u16 q2, q2, q3
    ee.vld.128.ip q6, a5, 0           /* at */
    /* atmosphere: ((A * rim) * at) >> 16 == (A * (rim * at)) >> 16; A * rim <= 65025 fits */
    ssai    0
    addi    a6, a4, 14
    ee.vldbc.16 q4, a6                /* 60 */
    ee.vmul.u16 q4, q5, q4
    ssai    16
    ee.vmul.u16 q4, q4, q6
    ee.vadds.s16 q1, q1, q4
    ssai    0
    addi    a6, a4, 16
    ee.vldbc.16 q4, a6                /* 135 */
    ee.vmul.u16 q4, q5, q4
    ssai    16
    ee.vmul.u16 q4, q4, q6
    ee.vadds.s16 q0, q0, q4
    ssai    0
    addi    a6, a4, 12
    ee.vldbc.16 q4, a6                /* 255 */
    ee.vmul.u16 q7, q5, q4
    ssai    16
    ee.vmul.u16 q7, q7, q6
    ee.vadds.s16 q2, q2, q7
    ee.vmin.s16 q0, q0, q4            /* min 255 */
    ee.vmin.s16 q1, q1, q4
    ee.vmin.s16 q2, q2, q4
    /* pack 565: (r << 8) & 0xf800 | (g << 3) & 0x07e0 | b >> 3 */
    ssai    0
    addi    a6, a4, 18
    ee.vldbc.16 q3, a6                /* 256 */
    ee.vmul.u16 q1, q1, q3
    addi    a6, a4, 20
    ee.vldbc.16 q3, a6                /* 0xf800 */
    ee.andq q1, q1, q3
    addi    a6, a4, 2
    ee.vldbc.16 q3, a6                /* 8 */
    ee.vmul.u16 q0, q0, q3
    addi    a6, a4, 22
    ee.vldbc.16 q3, a6                /* 0x07e0 */
    ee.andq q0, q0, q3
    ee.orq  q1, q1, q0
    ssai    3
    ee.vldbc.16 q3, a4                /* 1 */
    ee.vmul.u16 q2, q2, q3
    ee.orq  q1, q1, q2
    addi    a6, a2, 144
    ee.vst.128.ip q1, a6, 0
    retw.n

    .size   globe_pie8, . - globe_pie8

/* void globe_pie_over8(pie_over_t *s, const uint16_t *k): the overlay rule over 8 pixels.
 *   s: +0 argb[8] (0xAARRGGBB)  +32 dst[8] (RGB565, in: the pixel under, out: composited); 16-byte aligned
 *   out = pack((src alpha' + under (256 - alpha')) >> 8) per channel, alpha' = a + (a >> 7): QACC keeps the sum
 *   (<= 65280) exact, as the C overlay_row_clip2. k: K[] of globe_pie.c (+24: 0x00ff). */
    .section .text.globe_pie_over8,"ax",@progbits
    .align  4
    .global globe_pie_over8
    .type   globe_pie_over8,@function
globe_pie_over8:
    entry   a1, 32
    mov     a4, a3                    /* k: the second argument (the code below shares globe_pie8's a4) */
    mov     a5, a2
    ee.vld.128.ip q0, a5, 16
    ee.vld.128.ip q1, a5, 16          /* a5 -> dst */
    ee.vunzip.8 q0, q1                /* q0 lanes b | r << 8, q1 lanes g | a << 8 */
    ee.vld.128.ip q2, a5, 0           /* q2 under */
    ee.vldbc.16 q7, a4                /* 1 */
    ssai    8
    ee.vmul.u16 q3, q1, q7            /* a */
    ssai    7
    ee.vmul.u16 q4, q3, q7            /* a >> 7 */
    ee.vadds.s16 q3, q3, q4           /* q3 alpha' (<= 256) */
    addi    a6, a4, 18
    ee.vldbc.16 q4, a6                /* 256 */
    ee.vsubs.s16 q4, q4, q3           /* q4 256 - alpha' */
    addi    a6, a4, 24
    ee.vldbc.16 q6, a6                /* 0x00ff */
    ee.andq q5, q0, q6                /* q5 src blue */
    ee.andq q1, q1, q6                /* q1 src green */
    ssai    8
    ee.vmul.u16 q0, q0, q7            /* q0 src red */
    movi    a8, 8
    /* under red: ((x >> 8) & 0xf8) | (x >> 13) */
    ee.vmul.u16 q6, q2, q7            /* SAR 8 */
    addi    a6, a4, 4
    ee.vldbc.16 q7, a6                /* 0xf8 */
    ee.andq q6, q6, q7
    ee.vldbc.16 q7, a4
    ssai    13
    ee.vmul.u16 q7, q2, q7
    ee.orq  q6, q6, q7
    ee.zero.qacc
    ee.vmulas.u16.qacc q0, q3
    ee.vmulas.u16.qacc q6, q4
    ee.srcmb.s16.qacc q0, a8, 0       /* q0 red */
    /* under green: ((x >> 3) & 0xfc) | ((x >> 9) & 3) */
    ee.vldbc.16 q7, a4
    ssai    3
    ee.vmul.u16 q6, q2, q7
    addi    a6, a4, 6
    ee.vldbc.16 q7, a6                /* 0xfc */
    ee.andq q6, q6, q7
    ee.vldbc.16 q7, a4
    ssai    9
    ee.vmul.u16 q7, q2, q7
    addi    a6, a4, 8
    ee.vldbc.16 q2, a6                /* 3 (q2 kept below: reload under) */
    ee.andq q7, q7, q2
    ee.orq  q6, q6, q7
    ee.zero.qacc
    ee.vmulas.u16.qacc q1, q3
    ee.vmulas.u16.qacc q6, q4
    ee.srcmb.s16.qacc q1, a8, 0       /* q1 green */
    /* under blue: ((x << 3) & 0xf8) | ((x >> 2) & 7) */
    addi    a5, a2, 32
    ee.vld.128.ip q2, a5, 0           /* under again */
    addi    a6, a4, 2
    ee.vldbc.16 q7, a6                /* 8 */
    ssai    0
    ee.vmul.u16 q6, q2, q7
    addi    a6, a4, 4
    ee.vldbc.16 q7, a6                /* 0xf8 */
    ee.andq q6, q6, q7
    ee.vldbc.16 q7, a4
    ssai    2
    ee.vmul.u16 q7, q2, q7
    addi    a6, a4, 10
    ee.vldbc.16 q2, a6                /* 7 */
    ee.andq q7, q7, q2
    ee.orq  q6, q6, q7
    ee.zero.qacc
    ee.vmulas.u16.qacc q5, q3
    ee.vmulas.u16.qacc q6, q4
    ee.srcmb.s16.qacc q5, a8, 0       /* q5 blue */
    /* pack 565 */
    ssai    0
    addi    a6, a4, 18
    ee.vldbc.16 q3, a6                /* 256 */
    ee.vmul.u16 q0, q0, q3
    addi    a6, a4, 20
    ee.vldbc.16 q3, a6                /* 0xf800 */
    ee.andq q0, q0, q3
    addi    a6, a4, 2
    ee.vldbc.16 q3, a6                /* 8 */
    ee.vmul.u16 q1, q1, q3
    addi    a6, a4, 22
    ee.vldbc.16 q3, a6                /* 0x07e0 */
    ee.andq q1, q1, q3
    ee.orq  q0, q0, q1
    ssai    3
    ee.vldbc.16 q3, a4
    ee.vmul.u16 q5, q5, q3
    ee.orq  q0, q0, q5
    addi    a6, a2, 32
    ee.vst.128.ip q0, a6, 0
    retw.n

    .size   globe_pie_over8, . - globe_pie_over8
