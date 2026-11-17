#include "omni/blend565.h"
#include "omni/globe_compact.h"
#include <math.h>
#include <string.h>

/* The compact ortho LUT and its reference kernel: see omni/globe_compact.h. */

#define PI 3.14159265358979323846
#define D2R (PI / 180.0)

bool omni_globe_compact_ok(const omni_globe_cfg_t *cfg)
{
    return cfg->proj == OMNI_PROJ_ORTHO && cfg->tex_w <= 512;
}

static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

size_t omni_globe_compact_bytes(const omni_globe_lut_t *lut)
{
    return 63 + (size_t)lut->n / 8 * sizeof(omni_globe_blk_t) + align_up(lut->nspans * sizeof(omni_globe_cspan_t), 64);
}

static int16_t q14d(double v) { return (int16_t)lround((v < -1 ? -1 : v > 1 ? 1 : v) * 16384.0); }

bool omni_globe_compact(omni_globe_clut_t *c, const omni_globe_lut_t *lut, void *mem, size_t bytes)
{
    if (!omni_globe_compact_begin(c, lut, mem, bytes)) return false;
    omni_globe_compact_spans(c, lut, 0, lut->nspans);
    return true;
}

bool omni_globe_compact_begin(omni_globe_clut_t *c, const omni_globe_lut_t *lut, void *mem, size_t bytes)
{
    const omni_globe_cfg_t *cfg = &lut->cfg;
    if (!mem || !omni_globe_compact_ok(cfg) || bytes < omni_globe_compact_bytes(lut) || lut->n % 8) return false;
    uint8_t *p = (uint8_t *)mem + ((64 - ((uintptr_t)mem & 63)) & 63);
    c->cfg = *cfg;
    c->nblk = lut->n / 8;
    c->blk = (omni_globe_blk_t *)p;
    p += (size_t)c->nblk * sizeof(omni_globe_blk_t);
    c->span = (omni_globe_cspan_t *)p;
    c->nspans = lut->nspans;
    const double lat0 = cfg->lat0_deg * D2R, roll = cfg->roll_deg * D2R;
    c->cl = q14d(cos(lat0));
    c->sl = q14d(sin(lat0));
    c->cr = q14d(cos(roll));
    c->sr = q14d(sin(roll));
    for (int i = 0; i < 3; i++) c->light[i] = cfg->studio ? q14d(cfg->light[i]) : 0;
    c->k = (int32_t)lround(1048576.0 / cfg->diam);
    c->bad_runs = 0;
    return true;
}

/* Spans [s0, s1): their blocks (padding lanes all zero: alpha 0) and cspans. z as omni_globe_lut_rows computes it
 * (the same float operations, so the same bits). Disjoint span ranges touch disjoint blocks (two cores split them). */
void omni_globe_compact_spans(omni_globe_clut_t *c, const omni_globe_lut_t *lut, unsigned s0, unsigned s1)
{
    const omni_globe_cfg_t *cfg = &lut->cfg;
    const int D = cfg->diam, box = (cfg->size - cfg->diam) / 2;
    const float R = (float)D / 2, inv_r = 1 / R, edge = 1 - 0.5f / R, edge2 = edge * edge;
    for (unsigned si = s0; si < s1 && si < lut->nspans; si++) {
        const omni_globe_span_t *s = &lut->span[si];
        omni_globe_cspan_t *cs = &c->span[si];
        cs->y = s->y;
        cs->x0 = s->x0;
        cs->n = s->n;
        cs->blk = s->off / 8;
        cs->pad = 0;
        memset(&c->blk[cs->blk], 0, (size_t)((s->n + 7) / 8) * sizeof(omni_globe_blk_t));
        int ia = s->n, ib = s->n;
        const int py = s->y - box, x0 = s->x0 - box;
        const float ys0 = -((float)py + 0.5f - R) * inv_r;
        for (int j = 0; j < s->n; j++) {
            const uint32_t k = s->off + (uint32_t)j;
            omni_globe_blk_t *b = &c->blk[k / 8];
            const int l = (int)(k & 7);
            b->u[l] = lut->u[k];
            b->row[l] = (uint8_t)lut->row[k];
            b->limb[l] = lut->limb[k];
            b->rim[l] = lut->rim[k];
            b->alpha[l] = lut->alpha[k];
            if (lut->alpha[k] == 255) {
                if (ia == s->n) ia = j;
                ib = j + 1;
            }
            const float xs = ((float)(x0 + j) + 0.5f - R) * inv_r;
            float r2 = xs * xs + ys0 * ys0;
            if (r2 > edge2) {
                const float r = sqrtf(r2);
                if (r > 0.9995f) r2 = 0.9995f * 0.9995f;
            }
            const float zz = 1 - r2, z = zz > 0 ? sqrtf(zz) : 0;
            b->z[l] = (int16_t)(z * 16384.0f + 0.5f);
        }
        if (ia == s->n) ia = ib = 0;   /* no interior: every pixel reads the base */
        for (int j = ia; j < ib; j++)   /* a disc row's alpha-255 pixels are one run; if not, no run (exact, slower) */
            if (lut->alpha[s->off + (uint32_t)j] != 255) {
                c->bad_runs++;
                ia = ib = 0;
                break;
            }
        cs->ia = (int16_t)ia;
        cs->ib = (int16_t)ib;
    }
}

void omni_globe_cterms(omni_globe_cterms_t *t, const omni_globe_clut_t *c, const omni_globe_sun_t *sun, double lon0_deg)
{
    int32_t lx, ly, lz;
    if (c->cfg.studio) {
        lx = c->light[0];
        ly = c->light[1];
        lz = c->light[2];
    } else {
        lx = -sun->k2;
        ly = (sun->sd * c->cl - sun->k1 * c->sl) >> 14;
        lz = (sun->sd * c->sl + sun->k1 * c->cl) >> 14;
    }
    t->uoff_q4 = omni_globe_uoff_q4(c->cfg.tex_w, lon0_deg);
    t->ex = (int16_t)((lx * c->cr - ly * c->sr) >> 14);
    t->ey = (int16_t)((lx * c->sr + ly * c->cr) >> 14);
    t->lz = (int16_t)lz;
}

/* ---- Reference kernel: span_ref's arithmetic after mu (see globe_render.c), mu as the header defines. */

static inline int channel(int d, int kd, int n, int kn, int limb, int rimat, int A)
{
    const int v = ((((d * kd + n * kn) >> 8) * limb) >> 8) + ((A * rimat) >> 16);
    return v > 255 ? 255 : v;
}

/* The span's linear sun term: B0 and step of the header. */
void omni_globe_cramp(const omni_globe_clut_t *c, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                      int32_t *b0, int32_t *step)
{
    const int D = c->cfg.diam, box = (c->cfg.size - D) / 2, x = sp->x0 - box, y = sp->y - box;
    *b0 = (int32_t)((((int64_t)(2 * x - D + 1) * t->ex + (int64_t)(D - 1 - 2 * y) * t->ey) * c->k) >> 8);
    *step = (int32_t)(((int64_t)2 * t->ex * c->k) >> 8);
}

void omni_globe_cspan_ref(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                          uint16_t *dst)
{
    const omni_globe_clut_t *c = f->clut;
    const omni_shade_e_t *shade = f->shade->e;
    const uint32_t *tex = f->tex->px;
    const uint32_t tw = f->tex->w, mask = tw - 1;
    int32_t b0, step;
    omni_globe_cramp(c, sp, t, &b0, &step);
    for (int j = 0; j < sp->n; j++) {
        const omni_globe_blk_t *b = &c->blk[sp->blk + (uint32_t)(j >> 3)];
        const int l = j & 7;
        const int alpha = b->alpha[l];
        if (!alpha) continue;
        const int32_t mu = ((b0 + j * step) >> 12) + ((b->z[l] * t->lz) >> 14);
        int32_t i = (mu + 16384) >> 6;
        i = i < 0 ? 0 : i > OMNI_SHADE_N - 1 ? OMNI_SHADE_N - 1 : i;
        const omni_shade_e_t *e = &shade[i];
        const uint32_t x = (uint32_t)((b->u[l] + t->uoff_q4) >> 4) & mask;
        const uint32_t tx = tex[b->row[l] * tw + x];
        const uint16_t day = (uint16_t)tx, night = (uint16_t)(tx >> 16);
        const int limb = b->limb[l], rimat = b->rim[l] * e->at;
        const int r = channel(omni_ex_r(day), e->dr, omni_ex_r(night), e->nr, limb, rimat, 60);
        const int g = channel(omni_ex_g(day), e->dg, omni_ex_g(night), e->ng, limb, rimat, 135);
        const int bl = channel(omni_ex_b(day), e->db, omni_ex_b(night), e->nb, limb, rimat, 255);
        const uint16_t base = dst[j];
        const int ap = omni_amul(alpha);
        dst[j] = omni_pack565(omni_blend8(r, omni_ex_r(base), ap), omni_blend8(g, omni_ex_g(base), ap), omni_blend8(bl, omni_ex_b(base), ap));
    }
}

/* The same, written for the S3's compiler (restrict, interior pixels skip the blend: alpha' 256 gives the colour). */
void omni_globe_cspan_fast(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                           uint16_t *restrict dst)
{
    omni_globe_cspan_range(f, sp, t, dst, 0, sp->n);
}

void omni_globe_cspan_range(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                            uint16_t *restrict dst, int j0, int j1)
{
    const omni_globe_clut_t *c = f->clut;
    const omni_shade_e_t *restrict shade = f->shade->e;
    const uint32_t *restrict tex = f->tex->px;
    const uint32_t mask = (uint32_t)f->tex->w - 1;
    unsigned tshift = 0;
    while ((1u << tshift) < f->tex->w) tshift++;
    int32_t b0, step;
    omni_globe_cramp(c, sp, t, &b0, &step);
    const int32_t lz = t->lz, uoff = t->uoff_q4;
    const omni_globe_blk_t *restrict blk = c->blk + sp->blk;
    int32_t acc = b0 + j0 * step;
    for (int j = j0; j < j1; j++, acc += step) {
        const omni_globe_blk_t *b = &blk[j >> 3];
        const int l = j & 7;
        const int alpha = b->alpha[l];
        if (!alpha) continue;
        int32_t i = ((acc >> 12) + ((b->z[l] * lz) >> 14) + 16384) >> 6;
        i = i < 0 ? 0 : i;
        i = i > OMNI_SHADE_N - 1 ? OMNI_SHADE_N - 1 : i;
        const omni_shade_e_t *e = &shade[i];
        const uint32_t tx = tex[((uint32_t)b->row[l] << tshift) + ((uint32_t)((b->u[l] + uoff) >> 4) & mask)];
        const int limb = b->limb[l], rimat = b->rim[l] * e->at;
        int r = ((((omni_ex5((tx >> 11) & 31) * e->dr + omni_ex5(tx >> 27) * e->nr) >> 8) * limb) >> 8) + ((60 * rimat) >> 16);
        int g = ((((omni_ex6((tx >> 5) & 63) * e->dg + omni_ex6((tx >> 21) & 63) * e->ng) >> 8) * limb) >> 8) + ((135 * rimat) >> 16);
        int bl = ((((omni_ex5(tx & 31) * e->db + omni_ex5((tx >> 16) & 31) * e->nb) >> 8) * limb) >> 8) + ((255 * rimat) >> 16);
        r = r > 255 ? 255 : r;
        g = g > 255 ? 255 : g;
        bl = bl > 255 ? 255 : bl;
        if (alpha != 255) {
            const uint16_t base = dst[j];
            const int ap = omni_amul(alpha);
            r = omni_blend8(r, omni_ex5(base >> 11), ap);
            g = omni_blend8(g, omni_ex6((base >> 5) & 63), ap);
            bl = omni_blend8(bl, omni_ex5(base & 31), ap);
        }
        dst[j] = omni_pack565(r, g, bl);
    }
}

/* ---- Linear in u (R53 step 6a): see the header. */

static inline int lerp16(int a, int b, int f) { return (a * (16 - f) + b * f) >> 4; }

void omni_globe_cspan_lin_ref(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                              uint16_t *dst)
{
    const omni_globe_clut_t *c = f->clut;
    const omni_shade_e_t *shade = f->shade->e;
    const uint32_t *tex = f->tex->px;
    const uint32_t tw = f->tex->w, mask = tw - 1;
    int32_t b0, step;
    omni_globe_cramp(c, sp, t, &b0, &step);
    for (int j = 0; j < sp->n; j++) {
        const omni_globe_blk_t *b = &c->blk[sp->blk + (uint32_t)(j >> 3)];
        const int l = j & 7;
        const int alpha = b->alpha[l];
        if (!alpha) continue;
        const int32_t mu = ((b0 + j * step) >> 12) + ((b->z[l] * t->lz) >> 14);
        int32_t i = (mu + 16384) >> 6;
        i = i < 0 ? 0 : i > OMNI_SHADE_N - 1 ? OMNI_SHADE_N - 1 : i;
        const omni_shade_e_t *e = &shade[i];
        const int32_t p = b->u[l] + t->uoff_q4 - 8;   /* Q4, from the first texel centre */
        const int fr = p & 15;
        const uint32_t x0 = (uint32_t)(p >> 4) & mask, x1 = (x0 + 1) & mask;
        const uint32_t t0 = tex[b->row[l] * tw + x0], t1 = tex[b->row[l] * tw + x1];
        const uint16_t d0 = (uint16_t)t0, d1 = (uint16_t)t1, n0 = (uint16_t)(t0 >> 16), n1 = (uint16_t)(t1 >> 16);
        const int limb = b->limb[l], rimat = b->rim[l] * e->at;
        const int r = channel(lerp16(omni_ex_r(d0), omni_ex_r(d1), fr), e->dr, lerp16(omni_ex_r(n0), omni_ex_r(n1), fr), e->nr, limb,
                              rimat, 60);
        const int g = channel(lerp16(omni_ex_g(d0), omni_ex_g(d1), fr), e->dg, lerp16(omni_ex_g(n0), omni_ex_g(n1), fr), e->ng, limb,
                              rimat, 135);
        const int bl = channel(lerp16(omni_ex_b(d0), omni_ex_b(d1), fr), e->db, lerp16(omni_ex_b(n0), omni_ex_b(n1), fr), e->nb, limb,
                               rimat, 255);
        const uint16_t base = dst[j];
        const int ap = omni_amul(alpha);
        dst[j] = omni_pack565(omni_blend8(r, omni_ex_r(base), ap), omni_blend8(g, omni_ex_g(base), ap), omni_blend8(bl, omni_ex_b(base), ap));
    }
}

/* As cspan_range (restrict, the interior skips the blend), with the two texel reads of lin_ref. */
void omni_globe_cspan_lin_fast(const omni_globe_frame_t *f, const omni_globe_cspan_t *sp, const omni_globe_cterms_t *t,
                               uint16_t *restrict dst)
{
    const omni_globe_clut_t *c = f->clut;
    const omni_shade_e_t *restrict shade = f->shade->e;
    const uint32_t *restrict tex = f->tex->px;
    const uint32_t mask = (uint32_t)f->tex->w - 1;
    unsigned tshift = 0;
    while ((1u << tshift) < f->tex->w) tshift++;
    int32_t b0, step;
    omni_globe_cramp(c, sp, t, &b0, &step);
    const int32_t lz = t->lz, uoff = t->uoff_q4 - 8;
    const omni_globe_blk_t *restrict blk = c->blk + sp->blk;
    int32_t acc = b0;
    for (int j = 0; j < sp->n; j++, acc += step) {
        const omni_globe_blk_t *b = &blk[j >> 3];
        const int l = j & 7;
        const int alpha = b->alpha[l];
        if (!alpha) continue;
        int32_t i = ((acc >> 12) + ((b->z[l] * lz) >> 14) + 16384) >> 6;
        i = i < 0 ? 0 : i;
        i = i > OMNI_SHADE_N - 1 ? OMNI_SHADE_N - 1 : i;
        const omni_shade_e_t *e = &shade[i];
        const int32_t p = b->u[l] + uoff;
        const int fr = p & 15, fl = 16 - fr;
        const uint32_t rowb = (uint32_t)b->row[l] << tshift, x0 = (uint32_t)(p >> 4) & mask;
        const uint32_t t0 = tex[rowb + x0], t1 = tex[rowb + ((x0 + 1) & mask)];
        const int dr = (omni_ex5((t0 >> 11) & 31) * fl + omni_ex5((t1 >> 11) & 31) * fr) >> 4;
        const int dg = (omni_ex6((t0 >> 5) & 63) * fl + omni_ex6((t1 >> 5) & 63) * fr) >> 4;
        const int db = (omni_ex5(t0 & 31) * fl + omni_ex5(t1 & 31) * fr) >> 4;
        const int nr = (omni_ex5(t0 >> 27) * fl + omni_ex5(t1 >> 27) * fr) >> 4;
        const int ng = (omni_ex6((t0 >> 21) & 63) * fl + omni_ex6((t1 >> 21) & 63) * fr) >> 4;
        const int nb = (omni_ex5((t0 >> 16) & 31) * fl + omni_ex5((t1 >> 16) & 31) * fr) >> 4;
        const int limb = b->limb[l], rimat = b->rim[l] * e->at;
        int r = ((((dr * e->dr + nr * e->nr) >> 8) * limb) >> 8) + ((60 * rimat) >> 16);
        int g = ((((dg * e->dg + ng * e->ng) >> 8) * limb) >> 8) + ((135 * rimat) >> 16);
        int bl = ((((db * e->db + nb * e->nb) >> 8) * limb) >> 8) + ((255 * rimat) >> 16);
        r = r > 255 ? 255 : r;
        g = g > 255 ? 255 : g;
        bl = bl > 255 ? 255 : bl;
        if (alpha != 255) {
            const uint16_t base = dst[j];
            const int ap = omni_amul(alpha);
            r = omni_blend8(r, omni_ex5(base >> 11), ap);
            g = omni_blend8(g, omni_ex6((base >> 5) & 63), ap);
            bl = omni_blend8(bl, omni_ex5(base & 31), ap);
        }
        dst[j] = omni_pack565(r, g, bl);
    }
}
