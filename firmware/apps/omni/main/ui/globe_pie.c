#include "ui/globe_pie.h"
#include <string.h>

/* The compact kernel with its arithmetic on the PIE unit (Task 11, O2), bit-exact with omni_globe_cspan_ref.
 *
 * Per span: the edge pixels (outside whole interior blocks: alpha < 255, or a block shared with them) go through
 * omni_globe_cspan_range, the C kernel. Each whole block of the interior run takes two phases:
 *  1. scalar: mu, the shade index and the texel address of each pixel exactly as the reference (they are table
 *     lookups), gathering the texel and the shade entry's first 7 fields into the 16-byte aligned scratch;
 *  2. globe_pie8 (globe_pie.S): everything after, 8 pixels a step, written to scratch.out and copied to dst.
 * Why each step equals the reference for every input the reference accepts with shade entries < 16448 (real
 * tables are below 400; the synthetic digest's below 1024):
 *  - 565 expansion: shifts and masks of the 16-bit texel, exact;
 *  - (d kd + n kn) >> 8: products added in QACC's 40-bit lanes, then an arithmetic shift: exact, and the result
 *    (< 2^15) is not saturated;
 *  - (s * limb) >> 8 with s < 2^15, limb < 2^8: the 32-bit product shifted, low 16 bits kept: exact;
 *  - (A * rim * at) >> 16: A * rim <= 255 * 255 fits 16 bits, times at (< 2^16) fits 32: exact;
 *  - the sum (< 2^15, no saturation) clamped by VMIN at 255; the 565 pack by multiply-and-mask: exact.
 * Interior pixels have alpha 255 (alpha' 256), whose blend returns the shaded colour: no base read. */

typedef struct {
    uint32_t tex[8], sh0[8], sh1[8], sh2[8];
    uint16_t at[8], out[8];
    uint16_t spill[4][8];
} __attribute__((aligned(16))) pie_scratch_t;

_Static_assert(sizeof(pie_scratch_t) == 224, "globe_pie.S offsets");
/* the scalar phase reads a shade entry as 4 words: dr | dg << 16, db | nr << 16, ng | nb << 16, at | pad << 16 */
_Static_assert(sizeof(omni_shade_e_t) == 16 && __builtin_offsetof(omni_shade_e_t, dg) == 2 &&
                   __builtin_offsetof(omni_shade_e_t, db) == 4 && __builtin_offsetof(omni_shade_e_t, nr) == 6 &&
                   __builtin_offsetof(omni_shade_e_t, ng) == 8 && __builtin_offsetof(omni_shade_e_t, nb) == 10 &&
                   __builtin_offsetof(omni_shade_e_t, at) == 12,
               "globe_pie.c shade entry layout");
_Static_assert(sizeof(omni_globe_blk_t) == 64 && __builtin_offsetof(omni_globe_blk_t, limb) == 40 &&
                   __builtin_offsetof(omni_globe_blk_t, rim) == 48, "globe_pie.S offsets");

void globe_pie8(pie_scratch_t *s, const omni_globe_blk_t *b, const uint16_t *k);

static const uint16_t K[13] __attribute__((aligned(16))) = { 1, 8, 0xf8, 0xfc, 3, 7, 255, 60, 135, 256, 0xf800, 0x07e0,
                                                             0x00ff };

bool globe_pie_shade_ok(const omni_globe_shade_t *sh)
{
    for (int i = 0; i < OMNI_SHADE_N; i++) {
        const omni_shade_e_t *e = &sh->e[i];
        if (e->dr >= GLOBE_PIE_SHADE_MAX || e->dg >= GLOBE_PIE_SHADE_MAX || e->db >= GLOBE_PIE_SHADE_MAX ||
            e->nr >= GLOBE_PIE_SHADE_MAX || e->ng >= GLOBE_PIE_SHADE_MAX || e->nb >= GLOBE_PIE_SHADE_MAX)
            return false;
    }
    return true;
}

void globe_cspan_pie(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                     uint16_t *dst)
{
    const omni_globe_clut_t *c = f->clut;
    const int kb0 = (sp->ia + 7) / 8, kb1 = sp->ib / 8;   /* whole blocks inside the interior run */
    if (sp->ia >= sp->ib || kb1 <= kb0) {
        omni_globe_cspan_range(f, sp, t, dst, 0, sp->n);
        return;
    }
    omni_globe_cspan_range(f, sp, t, dst, 0, 8 * kb0);
    int32_t b0, step;
    omni_globe_cramp(c, sp, t, &b0, &step);
    const uint32_t *restrict shade = (const uint32_t *)f->shade->e;
    const uint32_t *restrict tex = f->tex->px;
    const uint32_t mask = (uint32_t)f->tex->w - 1;
    unsigned tshift = 0;
    while ((1u << tshift) < f->tex->w) tshift++;
    const int32_t lz = t->lz, uoff = t->uoff_q4;
    pie_scratch_t s;
    const omni_globe_blk_t *blk = c->blk + sp->blk;
    int32_t acc = b0 + 8 * kb0 * step;
    for (int kb = kb0; kb < kb1; kb++) {
        const omni_globe_blk_t *b = &blk[kb];
        for (int l = 0; l < 8; l++, acc += step) {
            int32_t i = ((acc >> 12) + ((b->z[l] * lz) >> 14) + 16384) >> 6;
            i = i < 0 ? 0 : i;
            i = i > OMNI_SHADE_N - 1 ? OMNI_SHADE_N - 1 : i;
            const uint32_t *e = shade + 4 * i;
            s.sh0[l] = e[0];
            s.sh1[l] = e[1];
            s.sh2[l] = e[2];
            s.at[l] = (uint16_t)e[3];
            s.tex[l] = tex[((uint32_t)b->row[l] << tshift) + ((uint32_t)((b->u[l] + uoff) >> 4) & mask)];
        }
        globe_pie8(&s, b, K);
        memcpy(dst + 8 * kb, s.out, sizeof s.out);
    }
    omni_globe_cspan_range(f, sp, t, dst, 8 * kb1, sp->n);
}

/* ---- Overlay runs (omni_globe_frame_t.over_run): 8 pixels a step on the PIE unit, the rest in C; both are
 * globe_render.c's rule exactly: alpha' = a + (a >> 7) <= 256; src alpha' + under (256 - alpha') <= 65280 summed in
 * QACC's 40-bit lanes and shifted by 8; the under pixel expanded and the result packed as the kernel above. */

typedef struct {
    uint32_t argb[8];
    uint16_t dst[8];
} __attribute__((aligned(16))) pie_over_t;

void globe_pie_over8(pie_over_t *s, const uint16_t *k);

static inline int ex5(unsigned v) { return (int)((v << 3) | (v >> 2)); }
static inline int ex6(unsigned v) { return (int)((v << 2) | (v >> 4)); }

void globe_over_pie(const uint32_t *argb, uint16_t *dst, int n)
{
    int j = 0;
    if (n >= 8) {
        pie_over_t s;
        for (; j + 8 <= n; j += 8) {
            memcpy(s.argb, argb + j, sizeof s.argb);
            memcpy(s.dst, dst + j, sizeof s.dst);
            globe_pie_over8(&s, K);
            memcpy(dst + j, s.dst, sizeof s.dst);
        }
    }
    for (; j < n; j++) {
        const uint32_t c = argb[j];
        const int a = (int)(c >> 24), ap = a + (a >> 7), aq = 256 - ap;
        const uint16_t d = dst[j];
        const int r = ((int)(c >> 16 & 255) * ap + ex5(d >> 11) * aq) >> 8;
        const int g = ((int)(c >> 8 & 255) * ap + ex6((d >> 5) & 63) * aq) >> 8;
        const int b = ((int)(c & 255) * ap + ex5(d & 31) * aq) >> 8;
        dst[j] = (uint16_t)(((unsigned)r >> 3) << 11 | ((unsigned)g >> 2) << 5 | ((unsigned)b >> 3));
    }
}
