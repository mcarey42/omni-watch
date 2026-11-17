#include "omni/blend565.h"
#include "omni/globe.h"
#include "omni/globe_compact.h"
#include <math.h>
#include <string.h>

/* Reference globe renderer: the mockup's Globe.prototype.render (faces-practical.js 195-226) in fixed point,
 * plus its halo() (243-258) in float. Optimised span kernels must match omni_globe_span_ref bit for bit. */

/* ---- RGB565 <-> 8-bit: expand by bit replication, pack by truncation. */

/* ---- Shade table */

/* Single precision (R58: the ESP32-S3 has no double unit; the double build took 11.8 ms per gain change), with
 * only + - * / and sqrtf in a fixed order, so host and device agree; float rounding moves an entry by at most 1
 * (test_shade_curve holds every entry within 1 of the double formula). */
static uint16_t q8(float v)
{
    const float r = floorf(v * 256 + 0.5f);
    return (uint16_t)(r < 0 ? 0 : r > 65535 ? 65535 : r);
}

void omni_globe_shade_build(omni_globe_shade_t *sh, const omni_globe_look_t *look)
{
    const float gain = look->gain, night = look->night * gain, atmo = look->atmo * gain;
    for (int i = 0; i < OMNI_SHADE_N; i++) {
        const float mu = ((float)i + 0.5f) / 256 - 1;
        if (look->kind == 1) {   /* the Omni's: day = (0.32 + 0.78 max(0, mu)) gain, no night, no atmosphere */
            const uint16_t d = q8((0.32f + 0.78f * (mu > 0 ? mu : 0)) * gain);
            omni_shade_e_t *e = &sh->e[i];
            e->dr = e->dg = e->db = d;
            e->nr = e->ng = e->nb = e->at = e->pad = 0;
            continue;
        }
        float x = (mu + 0.09f) * 5.5f;
        x = x < 0 ? 0 : x > 1 ? 1 : x;
        const float s = x * x * (3 - 2 * x);                            /* soft terminator, about 10 deg wide */
        const float lit = s * (0.30f + 0.70f * sqrtf(mu > 0 ? mu : 0)) * gain;
        const float nk = 1 - s, tw = 4 * s * nk;                         /* warm twilight band */
        omni_shade_e_t *e = &sh->e[i];
        e->dr = q8(lit * (1 + 0.22f * tw) + 0.05f * nk * gain);
        e->dg = q8(lit * (1 + 0.04f * tw) + 0.06f * nk * gain);
        e->db = q8(lit * (1 - 0.18f * tw) + 0.09f * nk * gain);
        e->nr = e->ng = q8(nk * night);
        e->nb = q8(0.9f * nk * night);
        e->at = q8((0.18f + 0.82f * s) * atmo);
        e->pad = 0;
    }
}

/* ---- Span kernel */

static inline int channel(int d, int kd, int n, int kn, int limb, int rimat, int A)
{
    const int v = ((((d * kd + n * kn) >> 8) * limb) >> 8) + ((A * rimat) >> 16);
    return v > 255 ? 255 : v;
}

void omni_globe_span_ref(const omni_globe_frame_t *f, const omni_globe_span_t *sp, int32_t uoff_q4, uint16_t *dst)
{
    const omni_globe_lut_t *lut = f->lut;
    const omni_shade_e_t *shade = f->shade->e;
    const uint32_t *tex = f->tex->px;
    const uint32_t tw = f->tex->w, mask = tw - 1;
    for (int j = 0; j < sp->n; j++) {
        const uint32_t k = sp->off + (uint32_t)j;
        const int alpha = lut->alpha[k];
        if (!alpha) continue;   /* the blend would give the base back exactly */
        int32_t i = (omni_globe_mu(lut, k, &f->sun) + 16384) >> 6;
        i = i < 0 ? 0 : i > OMNI_SHADE_N - 1 ? OMNI_SHADE_N - 1 : i;
        const omni_shade_e_t *e = &shade[i];
        const uint32_t x = (uint32_t)((lut->u[k] + uoff_q4) >> 4) & mask;   /* u + uoff_q4 > 0 */
        const uint32_t t = tex[lut->row[k] * tw + x];
        const uint16_t day = (uint16_t)t, night = (uint16_t)(t >> 16);
        const int limb = lut->limb[k], rimat = lut->rim[k] * e->at;
        const int r = channel(omni_ex_r(day), e->dr, omni_ex_r(night), e->nr, limb, rimat, 60);
        const int g = channel(omni_ex_g(day), e->dg, omni_ex_g(night), e->ng, limb, rimat, 135);
        const int b = channel(omni_ex_b(day), e->db, omni_ex_b(night), e->nb, limb, rimat, 255);
        const uint16_t base = dst[j];
        const int ap = omni_amul(alpha);
        dst[j] = omni_pack565(omni_blend8(r, omni_ex_r(base), ap), omni_blend8(g, omni_ex_g(base), ap), omni_blend8(b, omni_ex_b(base), ap));
    }
}

/* Exactly as documented in globe.h. */
int32_t omni_globe_uoff_q4(uint16_t tex_w, double lon0)
{
    const double m = 16.0 * tex_w;
    double v = fmod(floor(lon0 * tex_w / 360.0 * 16.0 + 0.5) + tex_w * 8.0, m);
    if (v < 0) v += m;
    return (int32_t)v + (int32_t)m;
}

/* ---- Frame */

/* The overlay spans of row y, composited over row's columns [a0, b0) and [a1, b1) (disjoint or empty; all: 0, S). */
static void over_run_c(const uint32_t *src, uint16_t *d, int n)
{
    for (int j = 0; j < n; j++) {
        const uint32_t c = src[j];
        const int ap = omni_amul((int)(c >> 24));
        d[j] = omni_pack565(omni_blend8((int)(c >> 16 & 255), omni_ex_r(d[j]), ap), omni_blend8((int)(c >> 8 & 255), omni_ex_g(d[j]), ap),
                    omni_blend8((int)(c & 255), omni_ex_b(d[j]), ap));
    }
}

static void overlay_row_clip2(const omni_overlay_t *ov, uint32_t *oi, uint16_t y, uint16_t *row, int a0, int b0, int a1,
                              int b1, void (*run)(const uint32_t *, uint16_t *, int))
{
    if (!run) run = over_run_c;
    while (*oi < ov->nspans && ov->span[*oi].y < y) (*oi)++;
    for (; *oi < ov->nspans && ov->span[*oi].y == y; (*oi)++) {
        const omni_ospan_t *s = &ov->span[*oi];
        const uint32_t *src = ov->argb + s->off;
        for (int iv = 0; iv < 2; iv++) {
            const int xa = iv ? a1 : a0, xb = iv ? b1 : b0;
            if (xb <= xa) continue;
            const int ja = xa > s->x0 ? xa - s->x0 : 0, jb = xb - s->x0 < s->n ? xb - s->x0 : s->n;
            if (jb > ja) run(src + ja, row + s->x0 + ja, jb - ja);
        }
    }
}

void omni_overlay_row(const omni_overlay_t *ov, uint32_t *oi, uint16_t y, uint16_t *row, int xa, int xb,
                             void (*run)(const uint32_t *, uint16_t *, int))
{
    overlay_row_clip2(ov, oi, y, row, xa, xb, 0, 0, run);
}

void omni_globe_render(const omni_globe_frame_t *f, uint16_t *out, uint16_t y0, uint16_t y1, omni_globe_span_fn fn)
{
    const omni_globe_lut_t *lut = f->lut;
    const uint16_t S = lut->cfg.size;
    if (!fn) fn = omni_globe_span_ref;
    if (y1 > S) y1 = S;
    const int32_t uoff = omni_globe_uoff_q4(f->tex->w, f->lon0_deg);
    const int fa = omni_amul(f->fade);
    unsigned si = 0;
    uint32_t oi = 0;
    while (si < lut->nspans && lut->span[si].y < y0) si++;
    for (uint16_t y = y0; y < y1; y++) {
        uint16_t *row = out + (size_t)y * S;
        if (f->under) memcpy(row, f->under + (size_t)y * S, S * sizeof *row);
        else memset(row, 0, S * sizeof *row);
        if (si < lut->nspans && lut->span[si].y == y) {
            const omni_globe_span_t *sp = &lut->span[si++];
            fn(f, sp, uoff, row + sp->x0);
        }
        if (f->over) omni_overlay_row(f->over, &oi, y, row, 0, S, NULL);
        if (fa != 256)
            for (int x = 0; x < S; x++)
                row[x] = omni_pack565((omni_ex_r(row[x]) * fa) >> 8, (omni_ex_g(row[x]) * fa) >> 8, (omni_ex_b(row[x]) * fa) >> 8);
    }
}

/* ---- Overlay */

void omni_overlay_draw(const omni_overlay_t *ov, uint16_t *box, uint16_t size)
{
    uint32_t oi = 0;
    for (uint16_t y = 0; y < size; y++) omni_overlay_row(ov, &oi, y, box + (size_t)y * size, 0, size, NULL);
}

static void overlay_count(const uint32_t *argb, uint16_t w, uint16_t h, uint16_t stride, uint32_t *ns, uint32_t *np)
{
    *ns = *np = 0;
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t *r = argb + (size_t)y * stride;
        for (uint32_t x = 0; x < w; x++) {
            if (!(r[x] >> 24)) continue;
            (*np)++;
            if (x == 0 || !(r[x - 1] >> 24)) (*ns)++;
        }
    }
}

size_t omni_overlay_bytes(const uint32_t *argb, uint16_t w, uint16_t h, uint16_t stride_px)
{
    uint32_t ns, np;
    overlay_count(argb, w, h, stride_px, &ns, &np);
    return 3 + (size_t)ns * sizeof(omni_ospan_t) + (size_t)np * 4;   /* 3: aligning mem to 4 */
}

bool omni_overlay_build(omni_overlay_t *ov, const uint32_t *argb, uint16_t w, uint16_t h, uint16_t stride_px,
                        void *mem, size_t bytes)
{
    if (!mem || bytes < omni_overlay_bytes(argb, w, h, stride_px)) return false;
    uint32_t ns, np;
    overlay_count(argb, w, h, stride_px, &ns, &np);
    uint8_t *p = (uint8_t *)mem + ((4 - ((uintptr_t)mem & 3)) & 3);
    omni_ospan_t *span = (omni_ospan_t *)p;
    uint32_t *px = (uint32_t *)(p + (size_t)ns * sizeof(omni_ospan_t));   /* sizeof(omni_ospan_t) % 4 == 0 */
    uint32_t si = 0, off = 0;
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t *r = argb + (size_t)y * stride_px;
        for (uint32_t x = 0; x < w;) {
            if (!(r[x] >> 24)) { x++; continue; }
            omni_ospan_t *s = &span[si++];
            s->y = (uint16_t)y;
            s->x0 = (uint16_t)x;
            s->off = off;
            while (x < w && r[x] >> 24) px[off++] = r[x++];
            s->n = (uint16_t)(off - s->off);
        }
    }
    ov->nspans = ns;
    ov->span = span;
    ov->argb = px;
    return true;
}

/* ---- Halo: two canvas radial-gradient rings, source-over in float. Stops are interpolated unpremultiplied,
 * as the mockup's canvas (Skia) does. The rings' inner edges lie under the globe and are point-sampled at pixel
 * centres; the outer edge (1.07 R) is antialiased over +-0.4 px. */

typedef struct { float r, g, b, a; } rgba_t;   /* unpremultiplied: colour 0..255, alpha 0..1 */

static rgba_t rgba(float r, float g, float b, float a) { rgba_t c = { r, g, b, a }; return c; }
static rgba_t lerp4(rgba_t x, rgba_t y, float t)
{
    rgba_t c = { x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t };
    return c;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void over(float px[3], rgba_t c, float cov)
{
    const float a = c.a * cov;
    px[0] = c.r * a + px[0] * (1 - a);
    px[1] = c.g * a + px[1] * (1 - a);
    px[2] = c.b * a + px[2] * (1 - a);
}

/* The halo's per-call terms. Reciprocals and sqrtf replace the divisions and hypotf of the first version (Task 11:
 * 3.7 us a pixel on the device); test_globe_halo.c holds it to the old maths (1 of 90 M pixels differs, by
 * 1) and pins a halo digest. */
typedef struct {
    float cx, cy, R, r_in, r_lobe, r_out, aa, inv_2aa, inv_span, inv_lobe, lx, ly, la;
    rgba_t s0, s1, s2;
    bool lobe;
    int xa, ya, xb, yb;   /* the extent that can be written */
} halo_t;

static void halo_setup(halo_t *h, uint16_t size, float cx, float cy, float R, const float *dir, float strength)
{
    h->cx = cx;
    h->cy = cy;
    h->R = R;
    h->r_in = R * 0.985f;
    h->r_lobe = R * 0.99f;
    h->r_out = R * 1.07f;
    h->aa = 0.4f;
    h->inv_2aa = 1 / (2 * h->aa);
    h->inv_span = 1 / (h->r_out - h->r_in);
    h->inv_lobe = 1 / (R * 0.75f);
    h->s0 = rgba(110, 170, 255, clampf(0.30f * strength, 0, 1));
    h->s1 = rgba(70, 130, 255, clampf(0.10f * strength, 0, 1));
    h->s2 = rgba(40, 90, 255, 0);
    h->la = clampf(0.22f * strength, 0, 1);
    h->lobe = dir != NULL;
    h->lx = h->ly = 0;
    if (dir) {
        float dl = hypotf(dir[0], dir[1]);
        if (dl == 0) dl = 1;
        h->lx = cx + dir[0] / dl * R * 0.9f;
        h->ly = cy + dir[1] / dl * R * 0.9f;
    }
    const int ext = (int)ceilf(h->r_out + h->aa) + 1;
    h->xa = (int)floorf(cx) - ext;
    h->xb = (int)floorf(cx) + ext;
    h->ya = (int)floorf(cy) - ext;
    h->yb = (int)floorf(cy) + ext;
    if (h->xa < 0) h->xa = 0;
    if (h->ya < 0) h->ya = 0;
    if (h->xb > size - 1) h->xb = size - 1;
    if (h->yb > size - 1) h->yb = size - 1;
}

/* The halo over src (NULL: black), written to box, for the pixels of [xa, xb] x [ya, yb] (within the extent). A pixel
 * that neither ring nor lobe reaches is not written. Only the annulus is visited: per row the columns within
 * r_out + aa (plus a 1 px margin), skipping those certainly inside r_in (less a 1 px margin); the per-pixel tests
 * still decide. mode LOBE_DRAW visits only the lobe's candidates (lobe_candidate) and LOBE_RESTORE copies those from
 * ring instead of computing them. */
enum { HALO_ALL, LOBE_DRAW, LOBE_RESTORE };

/* A pixel the lobe may reach: d >= r_lobe and within 0.75 R of the lobe centre, each with a 1 px margin. Elsewhere
 * the lobe term is exactly zero (skipped below r_lobe; alpha la * (1 - 1) beyond 0.75 R) and over() with alpha 0
 * leaves the colour as is, so the halo with the lobe equals the halo without it, bit for bit. */
static bool lobe_candidate(const halo_t *h, float dx, float dy, float px, float py)
{
    const float rl = h->r_lobe - 1, lr = h->R * 0.75f + 1, ex = px - h->lx, ey = py - h->ly;
    return dx * dx + dy * dy >= rl * rl && ex * ex + ey * ey <= lr * lr;
}

static void halo_rect(const halo_t *h, uint16_t *box, const uint16_t *src, uint16_t size, int xa, int ya, int xb, int yb,
                      int mode, const uint16_t *ring)
{
    const float cx = h->cx, cy = h->cy;
    const float ro = h->r_out + h->aa + 1, ri = h->r_in - 1;
    if (xa < h->xa) xa = h->xa;
    if (ya < h->ya) ya = h->ya;
    if (xb > h->xb) xb = h->xb;
    if (yb > h->yb) yb = h->yb;
    for (int y = ya; y <= yb; y++) {
        const float dy = y + 0.5f - cy;
        if (fabsf(dy) >= ro) continue;
        const float wo = sqrtf(ro * ro - dy * dy);
        int xl = (int)floorf(cx - wo) - 1, xr = (int)ceilf(cx + wo) + 1, sl = xr + 1, sr = xr;   /* skip [sl, sr] */
        if (xl < xa) xl = xa;
        if (xr > xb) xr = xb;
        if (ri > 0 && fabsf(dy) < ri) {
            const float wi = sqrtf(ri * ri - dy * dy);
            sl = (int)ceilf(cx - wi + 0.5f);
            sr = (int)floorf(cx + wi - 1.5f);
        }
        if (mode != HALO_ALL) {   /* only the lobe circle's columns can hold candidates (1 px margin past its test's) */
            const float lr = h->R * 0.75f + 2, ey = y + 0.5f - h->ly;
            if (fabsf(ey) >= lr) continue;
            const float we = sqrtf(lr * lr - ey * ey);
            const int la = (int)floorf(h->lx - we) - 1, lb = (int)ceilf(h->lx + we) + 1;
            if (xl < la) xl = la;
            if (xr > lb) xr = lb;
        }
        for (int x = xl; x <= xr; x++) {
            if (x >= sl && x <= sr) { x = sr; continue; }
            if (mode != HALO_ALL) {
                if (!lobe_candidate(h, x + 0.5f - cx, dy, x + 0.5f, y + 0.5f)) continue;
                if (mode == LOBE_RESTORE) {
                    box[(size_t)y * size + x] = ring[(size_t)y * size + x];
                    continue;
                }
            }
            const float dx = x + 0.5f - cx, d = sqrtf(dx * dx + dy * dy);
            const float cov = clampf((h->r_out - d) * h->inv_2aa + 0.5f, 0, 1);
            if (d < h->r_in || cov <= 0) continue;
            const float t = clampf((d - h->r_in) * h->inv_span, 0, 1);
            const rgba_t ring = t < 0.35f ? lerp4(h->s0, h->s1, t * (1 / 0.35f))
                                          : lerp4(h->s1, h->s2, (t - 0.35f) * (1 / 0.65f));
            rgba_t lobe = { 0, 0, 0, 0 };
            if (h->lobe && d >= h->r_lobe) {
                const float ex = x + 0.5f - h->lx, ey = y + 0.5f - h->ly;
                lobe = rgba(150, 200, 255, h->la * (1 - clampf(sqrtf(ex * ex + ey * ey) * h->inv_lobe, 0, 1)));
            }
            if (ring.a <= 0 && lobe.a <= 0) continue;
            const size_t k = (size_t)y * size + x;
            const uint16_t b = src ? src[k] : 0;
            float c[3] = { (float)omni_ex_r(b), (float)omni_ex_g(b), (float)omni_ex_b(b) };
            over(c, ring, cov);
            over(c, lobe, cov);
            box[k] = omni_pack565((int)clampf(c[0] + 0.5f, 0, 255), (int)clampf(c[1] + 0.5f, 0, 255), (int)clampf(c[2] + 0.5f, 0, 255));
        }
    }
}

void omni_globe_halo(uint16_t *box, uint16_t size, float cx, float cy, float R, const float *dir, float strength)
{
    halo_t h;
    halo_setup(&h, size, cx, cy, R, dir, strength);
    halo_rect(&h, box, box, size, 0, 0, size - 1, size - 1, HALO_ALL, NULL);
}

void omni_globe_halo_lobe_rect(uint16_t size, float cx, float cy, float R, const float *dir, float strength, int rect[4])
{
    halo_t h;
    halo_setup(&h, size, cx, cy, R, dir, strength);
    if (!dir) {
        rect[0] = rect[1] = 0;
        rect[2] = rect[3] = -1;
        return;
    }
    const float lr = R * 0.75f + 1;   /* the lobe's alpha is 0 beyond 0.75 R of its centre */
    rect[0] = (int)floorf(h.lx - lr) - 1;
    rect[1] = (int)floorf(h.ly - lr) - 1;
    rect[2] = (int)ceilf(h.lx + lr) + 1;
    rect[3] = (int)ceilf(h.ly + lr) + 1;
    if (rect[0] < h.xa) rect[0] = h.xa;
    if (rect[1] < h.ya) rect[1] = h.ya;
    if (rect[2] > h.xb) rect[2] = h.xb;
    if (rect[3] > h.yb) rect[3] = h.yb;
}

static void lobe_pass(uint16_t *box, const uint16_t *src, const uint16_t *ring, uint16_t size, float cx, float cy,
                      float R, const float *dir, float strength, int y0, int y1, int mode)
{
    if (!dir) return;
    halo_t h;
    halo_setup(&h, size, cx, cy, R, dir, strength);
    int r[4];
    omni_globe_halo_lobe_rect(size, cx, cy, R, dir, strength, r);
    if (r[1] < y0) r[1] = y0;
    if (r[3] > y1 - 1) r[3] = y1 - 1;
    halo_rect(&h, box, src, size, r[0], r[1], r[2], r[3], mode, ring);
}

void omni_globe_halo_lobe(uint16_t *box, const uint16_t *src, uint16_t size, float cx, float cy, float R,
                          const float *dir, float strength, int y0, int y1)
{
    lobe_pass(box, src, NULL, size, cx, cy, R, dir, strength, y0, y1, LOBE_DRAW);
}

void omni_globe_halo_unlobe(uint16_t *box, const uint16_t *ring, uint16_t size, float cx, float cy, float R,
                            const float *dir, float strength, int y0, int y1)
{
    lobe_pass(box, NULL, ring, size, cx, cy, R, dir, strength, y0, y1, LOBE_RESTORE);
}

/* ---- Frame from the compact LUT (omni/globe_compact.h) */

void omni_globe_render_c(const omni_globe_frame_t *f, uint16_t *out, uint16_t y0, uint16_t y1, omni_globe_cspan_fn fn,
                         bool keep, const int dirty[4])
{
    const omni_globe_clut_t *c = f->clut;
    const uint16_t S = c->cfg.size;
    if (!fn) fn = omni_globe_cspan_ref;
    if (y1 > S) y1 = S;
    if (f->fade != 255) keep = false;   /* the fade changes every pixel */
    omni_globe_cterms_t t;
    omni_globe_cterms(&t, c, &f->sun, f->lon0_deg);
    const int fa = omni_amul(f->fade);
    /* the first span and overlay span at or below y0 (binary searches: a frame is rendered in many row chunks) */
    unsigned si = 0, hi = c->nspans;
    while (si < hi) {
        const unsigned m = (si + hi) / 2;
        if (c->span[m].y < y0) si = m + 1;
        else hi = m;
    }
    uint32_t oi = 0, ohi = f->over ? f->over->nspans : 0;
    while (oi < ohi) {
        const uint32_t m = (oi + ohi) / 2;
        if (f->over->span[m].y < y0) oi = m + 1;
        else ohi = m;
    }
    for (uint16_t y = y0; y < y1; y++) {
        uint16_t *row = out + (size_t)y * S;
        const uint16_t *under = f->under ? f->under + (size_t)y * S : NULL;
        const omni_globe_cspan_t *sp = si < c->nspans && c->span[si].y == y ? &c->span[si++] : NULL;
        int da = 0, db = 0;   /* keep: the columns of this row whose base changed */
        if (keep && dirty && y >= dirty[1] && y <= dirty[3] && dirty[2] >= dirty[0]) {
            da = dirty[0] < 0 ? 0 : dirty[0];
            db = dirty[2] + 1 > S ? S : dirty[2] + 1;
            /* the base, except under the span's interior run (the kernel writes it without reading) */
            const int ia = sp && sp->ib > sp->ia ? sp->x0 + sp->ia : db, ib = sp && sp->ib > sp->ia ? sp->x0 + sp->ib : db;
            const int part[2][2] = { { da, ia < db ? ia : db }, { ib > da ? ib : da, db } };
            for (int k = 0; k < 2; k++) {
                const int a = part[k][0], n = part[k][1] - part[k][0];
                if (n <= 0 || (k == 1 && ia >= db)) continue;
                if (under) memcpy(row + a, under + a, (size_t)n * sizeof *row);
                else memset(row + a, 0, (size_t)n * sizeof *row);
            }
        }
        if (!keep) {
            if (under) memcpy(row, under, S * sizeof *row);
            else memset(row, 0, S * sizeof *row);
        } else if (sp) {   /* only the span's pixels outside its interior run read the base */
            uint16_t *d = row + sp->x0;
            const int runs[2][2] = { { 0, sp->ia }, { sp->ib, sp->n } };
            for (int k = 0; k < 2; k++) {
                const int a = runs[k][0], n = runs[k][1] - runs[k][0];
                if (n <= 0) continue;
                if (under) memcpy(d + a, under + sp->x0 + a, (size_t)n * sizeof *row);
                else memset(d + a, 0, (size_t)n * sizeof *row);
            }
        }
        if (sp) fn(f, sp, &t, row + sp->x0);
        if (f->over) {
            if (!keep) {
                omni_overlay_row(f->over, &oi, y, row, 0, S, f->over_run);
            } else {   /* once over every rewritten pixel: the span and the dirty columns, merged if they touch */
                int a0 = sp ? sp->x0 : 0, b0 = sp ? sp->x0 + sp->n : 0, a1 = da, b1 = db;
                if (b0 > a0 && b1 > a1 && a1 <= b0 && a0 <= b1) {
                    a0 = a0 < a1 ? a0 : a1;
                    b0 = b0 > b1 ? b0 : b1;
                    a1 = b1 = 0;
                }
                if (b0 > a0 || b1 > a1) overlay_row_clip2(f->over, &oi, y, row, a0, b0, a1, b1, f->over_run);
            }
        }
        if (fa != 256)
            for (int x = 0; x < S; x++)
                row[x] = omni_pack565((omni_ex_r(row[x]) * fa) >> 8, (omni_ex_g(row[x]) * fa) >> 8, (omni_ex_b(row[x]) * fa) >> 8);
    }
}
