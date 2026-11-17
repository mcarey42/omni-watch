#pragma once
/* The one RGB565 alpha blend rule (D5), used by every blend in omni_core (globe, rings, RLE, the compositor):
 * channels expanded to 8 bits by bit replication, alpha 0..255 scaled to 0..256 by omni_amul, then
 * (src * a' + dst * (256 - a')) >> 8 per channel and a truncating pack. Alpha 255 gives the source exactly and alpha 0
 * the destination exactly, so kernels may skip either case and stay bit-exact. */
#include <stdint.h>

static inline int omni_ex5(unsigned v) { return (int)((v << 3) | (v >> 2)); }
static inline int omni_ex6(unsigned v) { return (int)((v << 2) | (v >> 4)); }
static inline int omni_ex_r(uint16_t c) { return omni_ex5(c >> 11); }
static inline int omni_ex_g(uint16_t c) { return omni_ex6((c >> 5) & 63u); }
static inline int omni_ex_b(uint16_t c) { return omni_ex5(c & 31u); }
static inline uint16_t omni_pack565(int r, int g, int b)
{
    return (uint16_t)(((unsigned)r >> 3) << 11 | ((unsigned)g >> 2) << 5 | ((unsigned)b >> 3));
}
static inline int omni_amul(int a) { return a + (a >> 7); }   /* alpha 0..255 -> 0..256 */
/* One 8-bit channel: src over dst at a' = omni_amul(alpha). */
static inline int omni_blend8(int src, int dst, int ap) { return (src * ap + dst * (256 - ap)) >> 8; }
/* rgb565 at alpha a8 over dst: the rule above, red and blue in one 32-bit multiply (two 16-bit lanes; each lane's sum
 * is at most 255 * 256, so nothing carries between them), green in another. */
static inline uint16_t omni_blend565(uint16_t dst, uint16_t rgb565, int a8)
{
    const uint32_t ap = (uint32_t)omni_amul(a8), aq = 256u - ap;
    const uint32_t rb = ((uint32_t)omni_ex_r(rgb565) << 16 | (uint32_t)omni_ex_b(rgb565)) * ap +
                        ((uint32_t)omni_ex_r(dst) << 16 | (uint32_t)omni_ex_b(dst)) * aq;
    const uint32_t g = (uint32_t)omni_ex_g(rgb565) * ap + (uint32_t)omni_ex_g(dst) * aq;
    return (uint16_t)((rb >> 27) << 11 | (g >> 10) << 5 | ((rb >> 11) & 31u));
}
