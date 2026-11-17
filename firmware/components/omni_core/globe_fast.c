#include "omni/blend565.h"
#include "omni/globe.h"

/* omni_globe_span_fast: omni_globe_span_ref's arithmetic, bit for bit, written for the ESP32-S3's compiler.
 * span_ref reads every plane through lut-> on each pixel; its dst (uint16_t) may alias the int16 planes, so GCC
 * reloads the plane pointers after each store and spills to the (PSRAM) stack: about 200 instructions a pixel.
 * Here the planes are restrict locals and interior pixels (alpha 255: alpha' 256, so the blend gives the shaded
 * colour exactly, (c*256 + d*0) >> 8 = c) skip the base read and the blend. Held to span_ref by
 * omni_globe_kernel_bitexact on the host (every golden case) and by `globe selftest` on the device. */

void omni_globe_span_fast(const omni_globe_frame_t *f, const omni_globe_span_t *sp, int32_t uoff_q4,
                          uint16_t *restrict dst)
{
    const omni_globe_lut_t *lut = f->lut;
    const uint32_t off = sp->off;
    const int n = sp->n;
    const int16_t *restrict pu = lut->u + off, *restrict pa = lut->a + off, *restrict pb = lut->b + off,
                  *restrict pc = lut->c + off;
    const uint16_t *restrict prow = lut->row + off;
    const uint8_t *restrict plimb = lut->limb + off, *restrict prim = lut->rim + off, *restrict palpha = lut->alpha + off;
    const omni_shade_e_t *restrict shade = f->shade->e;
    const uint32_t *restrict tex = f->tex->px;
    const int32_t sd = f->sun.sd, k1 = f->sun.k1, k2 = f->sun.k2;
    const uint32_t mask = (uint32_t)f->tex->w - 1;
    unsigned tshift = 0;
    while ((1u << tshift) < f->tex->w) tshift++;

    for (int j = 0; j < n; j++) {
        const int alpha = palpha[j];
        if (!alpha) continue;
        int32_t i = ((((int32_t)pa[j] * sd) >> 14) + (((int32_t)pb[j] * k1) >> 14) - (((int32_t)pc[j] * k2) >> 14) +
                     16384) >> 6;
        i = i < 0 ? 0 : i;
        i = i > OMNI_SHADE_N - 1 ? OMNI_SHADE_N - 1 : i;
        const omni_shade_e_t *e = &shade[i];
        const uint32_t x = (uint32_t)((pu[j] + uoff_q4) >> 4) & mask;
        const uint32_t t = tex[((uint32_t)prow[j] << tshift) + x];
        const int limb = plimb[j], rimat = prim[j] * e->at;
        int r = ((((omni_ex5((t >> 11) & 31) * e->dr + omni_ex5(t >> 27) * e->nr) >> 8) * limb) >> 8) + ((60 * rimat) >> 16);
        int g = ((((omni_ex6((t >> 5) & 63) * e->dg + omni_ex6((t >> 21) & 63) * e->ng) >> 8) * limb) >> 8) + ((135 * rimat) >> 16);
        int b = ((((omni_ex5(t & 31) * e->db + omni_ex5((t >> 16) & 31) * e->nb) >> 8) * limb) >> 8) + ((255 * rimat) >> 16);
        r = r > 255 ? 255 : r;
        g = g > 255 ? 255 : g;
        b = b > 255 ? 255 : b;
        if (alpha != 255) {
            const uint16_t base = dst[j];
            const int ap = omni_amul(alpha);
            r = omni_blend8(r, omni_ex5(base >> 11), ap);
            g = omni_blend8(g, omni_ex6((base >> 5) & 63), ap);
            b = omni_blend8(b, omni_ex5(base & 31), ap);
        }
        dst[j] = omni_pack565(r, g, b);
    }
}
