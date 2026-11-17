#include "fk/cols.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "fk/ease.h"
#include "omni/blend565.h"
#include "omni/raster.h"

/* The column renderer (cols.h): the column maps over the lid's hinge engine and the band compositor, for two flat
 * leaves hinged at the screen's edges. Single precision in a fixed order (-ffp-contract=off, face_kit's CMakeLists):
 * float per column, per band and per row only; every per-pixel step is integer. The look follows drawLeaf, glowColumn
 * and drawDoorsPhase in Designer A's faces-reveal.js and notes-reveal.md's "Light spill". */

#define N 466                   /* the screen, and the door image, is N x N */
#define C 233                   /* the eye's line: cx = cy */
#define F 560.0f
#define D2R 0.017453292519943295f

const omni_hinge_t FK_COLS_HINGE = { OMNI_HINGE_VERTICAL, 0.0f, F, N, 0.0f, 0.0f };

/* The look. The glow's tint is glowColumn's gradient colour weighted by its alpha (an A8 sprite carries one colour):
 * (255, 200..246, 130..225) -> (255, 220, 170). The rim light and the door thickness are drawLeaf's. */
#define GLOW_R 255
#define GLOW_G 220
#define GLOW_B 170

/* the first theme's look (drawLeaf's constants; rd wave 2 Task 4 made them a kind's look) */
const fk_cols_look_t FK_COLS_LOOK_DW = { 0.72f, 0.1f, 0.8f, 0.55f, 7, { 255, 170, 90 }, 9.0f, { 0x12, 0x0c, 0x0a },
                                         { 255, 186, 108 }, 0.75f };

/* a kind's look: its own for an outward kind, else (NULL, or any inward kind) the first theme's */
static const fk_cols_look_t *look_of(const fk_cols_kind_t *k)
{
    return k && k->look && !k->inward ? k->look : &FK_COLS_LOOK_DW;
}

/* ---- the column maps ---- */

static float leaf_light(const fk_cols_kind_t *k, float th);

/* A kept leaf's angle, without libm: sinf and cosf are newlib's on the device and glibc's on the host, which may differ
 * by an ulp, and a keep kind's still frames show its kept leaves at its open_deg. So an inward leaf at exactly the
 * kind's kept angle (the float k->open_deg x D2R, as the timeline's skip and the swing's end give it) takes its sine
 * and cosine from the kind's kept_sin and kept_cos: the correctly rounded float values of that float angle (worked out in
 * long double). The host tests pin the kept leaves' digest, and with sinf and cosf perturbed show that no libm call
 * reaches them. Every other angle, and every outward leaf, goes through sinf and cosf. */
static void inward_sincos(const fk_cols_kind_t *k, float th, float *s, float *c)
{
    if (th == k->open_deg * D2R) {
        *s = k->kept_sin;
        *c = k->kept_cos;
        return;
    }
    *s = sinf(th);
    *c = cosf(th);
}

/* One leaf's columns: distance m = 0 .. 232 from its hinge (column m, or N - 1 - m for the right leaf), each the line
 * omni_hinge_lines_at gives at the column's centre: its v and inv_s by the same single-precision expressions in the
 * same order (test_cols_hinge_engine pins them equal), without the lid's strip shading (dark, spec: two cosf and two
 * expf a column, which the doors never read; Task 12), and with floorf of a value >= 0 as its truncation. The warm rim
 * (drawLeaf's light x f^7 x .55, f = (d + 1) / 233; skipped at .01 or below) is the column's too, so a band reads it
 * instead of working it out again per band. Inward (wave 2 Task 3): the hinge engine's line at -th (the leaf turned
 * away from the eye: inv_s > 1), and drawLeaf's inward shade and warm (doors.h); outward, every step is wave 1's. */
static void leaf_cols(const fk_cols_kind_t *k, float th, fk_col_t *cols, bool right, bool inward)
{
    const omni_hinge_t *h = &FK_COLS_HINGE;
    const float ths = inward ? -th : th;
    omni_hinge_map_t mp;
    float sn, cmc, sp;
    if (!inward) {   /* wave 1's operations, unchanged */
        omni_hinge_map(h, ths, &mp);
        sn = sinf(ths), cmc = 1.0f - cosf(ths), sp = 0.0f;
    } else {   /* omni_hinge_map's expressions at -th, from inward_sincos (sin(-th) = -sin th, cos(-th) = cos th) */
        float s, c;
        inward_sincos(k, th, &s, &c);
        sn = -s, cmc = 1.0f - c, sp = s;   /* sp: drawLeaf's sin th for the inward shading */
        const float Ch = (float)h->n * 0.5f;
        mp.a = h->focal;
        mp.b = -h->focal * h->pos;
        mp.c = sn;
        mp.d = h->focal * c - Ch * sn;
    }
    const float n = (float)h->n, light = leaf_light(k, th);
    const fk_cols_look_t *L = look_of(k);
    for (int m = 0; m < FK_LEAF_W; m++) {
        fk_col_t *c = &cols[right ? N - 1 - m : m];
        const float Y = (float)m + 0.5f, den = mp.c * Y + mp.d;
        const float v = den > 0 ? (mp.a * Y + mp.b) / den + h->pos : -1.0f;
        if (!(den > 0) || !(v >= 0) || !(v < n)) {   /* past the leaf's vanishing line, or off the door image */
            *c = (fk_col_t){ -1, 0, 0, 0 };
            continue;
        }
        const int lv = (int)v;   /* floorf(v), v >= 0 */
        if (lv >= FK_LEAF_W) {   /* past the free edge */
            *c = (fk_col_t){ -1, 0, 0, 0 };
            continue;
        }
        const float inv = 1.0f - (v - h->pos) * sn / h->focal;
        /* drawLeaf's outward shade at the strip's far end f = (d + 1) / 233; skipped at .01 or below, as there */
        const float f = (float)(lv + 1) / (float)FK_LEAF_W;
        float dark, w;
        if (!inward) {
            dark = fk_ease_clampf(L->dark_a * cmc + L->dark_b * sn * (1.0f - f), 0, L->dark_max);
            /* f^pow as ((f2 f2) f2) f for 7: 1 x f2 is f2 exactly, so the first theme's bits are unchanged */
            const float f2 = f * f;
            float p = 1.0f;
            for (unsigned i = L->warm_pow / 2u; i; i--) p = p * f2;
            if (L->warm_pow & 1u) p = p * f;
            w = light * p * L->warm_k;
        } else {   /* drawLeaf :175: the face turns toward the room's light */
            dark = fk_ease_clampf(0.35f * sp * (1.0f - f) + 0.15f * sp, 0, 0.6f);
            w = light * sp * (0.08f + 0.3f * (f * f));
        }
        c->u = (int16_t)(right ? N - 1 - lv : lv);
        c->inv_s_q16 = (uint32_t)(inv * 65536.0f + 0.5f);   /* floorf of a value > 0 */
        c->shade = dark > 0.01f ? fk_ease_to8(dark) : 0;
        c->warm = w > 0.01f ? fk_ease_to8(w) : 0;
    }
}

void fk_cols_k(const fk_cols_kind_t *k, float th_l, float th_r, fk_col_t cols[466])
{
    leaf_cols(k, th_l, cols, false, k->inward);
    leaf_cols(k, th_r, cols, true, k->inward);
}

/* ---- the leaf's light and its free edge (the timeline and the compositor) ---- */

/* The leaf's light (drawLeaf's `light`): up with the pop (the kind's pop_deg), so a closed leaf has none. */
static float leaf_light(const fk_cols_kind_t *k, float th) { return fk_ease_clampf(th / (k->pop_deg * D2R), 0, 1); }

/* drawLeaf's free-edge thickness: a quad from the free edge's face line to its back line (the left leaf's; the right
 * one's mirrored about the centre). It outlives the leaf's face: its back line leaves the screen at about 69.6 deg,
 * past omni_hinge_gone_rad. Returns whether any of it is on screen. Inward (drawLeaf's st = -sin th): the back line
 * lies behind the leaf's face, which covers the quad (it is drawn all the same, as drawLeaf draws it). */
static bool edge_pts(const fk_cols_kind_t *k, float th, bool right, bool inward, omni_pt_t p[4])
{
    float sn, ct;
    if (inward) inward_sincos(k, th, &sn, &ct);
    else sn = sinf(th), ct = cosf(th);
    const float st = inward ? -sn : sn, et = look_of(k)->edge_t;
    const float fx = (float)FK_LEAF_W * ct, fz = (float)FK_LEAF_W * st;
    const float bx = fx + et * st, bz = fz - et * ct;
    const float sa = F / (F - fz), sb = F / (F - bz);
    const float xa = (float)C + (fx - (float)C) * sa, xb = (float)C + (bx - (float)C) * sb;
    p[0] = (omni_pt_t){ right ? (float)N - xa : xa, (float)C - (float)C * sa };
    p[1] = (omni_pt_t){ right ? (float)N - xb : xb, (float)C - (float)C * sb };
    p[2] = (omni_pt_t){ p[1].x, (float)C + (float)C * sb };
    p[3] = (omni_pt_t){ p[0].x, (float)C + (float)C * sa };
    return th > 0 && (xa > 0 || xb > 0);
}

bool fk_cols_leaf_on_screen(float th, float gone, bool inward)
{
    omni_pt_t p[4];
    /* an inward leaf never leaves the screen; an outward one's edge reads no kept angle (any kind would do) */
    return inward || th < gone || edge_pts(NULL, th, false, false, p);
}

bool fk_cols_leaf_on_screen_k(const fk_cols_kind_t *k, float th)
{
    omni_pt_t p[4];
    const bool in = k && k->inward;
    return in || th < omni_hinge_gone_rad(&FK_COLS_HINGE) || edge_pts(in ? NULL : k, th, false, false, p);
}

/* The thickness's colour: the look's edge_rgb under its edge_lit_rgb at edge_lit_k x light (the first theme's: #120c0a
 * under (255, 186, 108) at .75 x light). */
static uint16_t edge_rgb(const fk_cols_kind_t *k, float th)
{
    const fk_cols_look_t *L = look_of(k);
    const uint16_t base = omni_pack565(L->edge_rgb[0], L->edge_rgb[1], L->edge_rgb[2]);
    const uint16_t lit = omni_pack565(L->edge_lit_rgb[0], L->edge_lit_rgb[1], L->edge_lit_rgb[2]);
    return omni_blend565(base, lit, fk_ease_to8(L->edge_lit_k * leaf_light(k, th)));
}

/* The thickness drawn as one omni_fill_poly. The leaf drawn after it covers what it should hide. */
static bool edge_quad(const fk_cols_kind_t *k, float th, bool right, bool inward, uint16_t *out,
                      const omni_rect_t *band)
{
    omni_pt_t p[4];
    if (!edge_pts(k, th, right, inward, p)) return false;
    omni_fill_poly(out + band->x0, N, band, p, 4, edge_rgb(k, th), 255);
    return true;
}

/* The left leaf's free edge on screen (drawLeaf's proj(233)); the right one's is N minus it. */
float fk_cols_free_edge(const fk_cols_kind_t *k, float th, bool inward)
{
    float sn, ct;
    if (inward) inward_sincos(k, th, &sn, &ct);
    else sn = sinf(th), ct = cosf(th);
    const float st = inward ? -sn : sn;
    const float s = F / (F - (float)FK_LEAF_W * st);
    return (float)C + ((float)FK_LEAF_W * ct - (float)C) * s;
}

/* ---- the round panel ---- */

void fk_panel_row(int y, int *x0, int *x1)
{
    *x0 = *x1 = C;
    if (y < 0 || y >= N) return;
    const int32_t t = 2 * y + 1 - N, d = N * N - t * t;   /* (2x + 1 - N)^2 < d: the pixel's centre is on the panel */
    int32_t q = 0;                                         /* floor(sqrt(d - 1)), bit by bit */
    for (int32_t bit = 1 << 9; bit; bit >>= 1)   /* d < 2^18 */
        if ((q + bit) * (q + bit) <= d - 1) q += bit;
    if ((q & 1) == 0) q--;   /* the largest odd |2x + 1 - N| inside */
    if (q < 0) return;
    *x0 = (N - 1 - q) / 2;
    *x1 = (N + 1 + q) / 2;
}

/* ---- the compositor ---- */

/* row y's panel columns within band b: [*p0, *p1), p0 <= p1 */
static void band_panel(int y, const omni_rect_t *b, int *p0, int *p1)
{
    fk_panel_row(y, p0, p1);
    if (*p0 < b->x0) *p0 = b->x0;
    if (*p0 > b->x1) *p0 = b->x1;
    if (*p1 > b->x1) *p1 = b->x1;
    if (*p1 < *p0) *p1 = *p0;
}

/* off the panel: black */
static void band_unpanel(uint16_t *o, int y, const omni_rect_t *b)
{
    int p0, p1;
    band_panel(y, b, &p0, &p1);
    memset(o + b->x0, 0, (size_t)(p0 - b->x0) * 2);
    memset(o + p1, 0, (size_t)(b->x1 - p1) * 2);
}

static inline uint16_t add565(uint16_t d, int r, int g, int b)   /* 'lighter' */
{
    const int R = omni_ex_r(d) + r, G = omni_ex_g(d) + g, B = omni_ex_b(d) + b;
    return omni_pack565(R < 255 ? R : 255, G < 255 ? G : 255, B < 255 ? B : 255);
}

/* One stretched draw of the glow sprite: dest rect (dx, dy, dw, dh) at alpha a, sampled nearest at pixel centres.
 * Columns in Q16 from the absolute x (sx = (q0 + x step) >> 16), so every band samples the same texel; [xa, xb) are
 * the columns that land on the sprite (0 <= sx < 64), found once per band. ap = omni_amul(a). */
typedef struct { int32_t q0, step; float dy, ky; int ap, xa, xb; } glow_pass_t;

static int32_t ceil_div(int64_t n, int64_t d)   /* d > 0 */
{
    return (int32_t)(n >= 0 ? (n + d - 1) / d : -((-n) / d));
}

static void glow_pass(glow_pass_t *g, float dx, float dy, float dw, float dh, uint8_t a, const omni_rect_t *b)
{
    const float kx = 64.0f / dw;
    g->q0 = (int32_t)floorf((0.5f - dx) * kx * 65536.0f);
    g->step = (int32_t)floorf(kx * 65536.0f + 0.5f);
    g->dy = dy;
    g->ky = 256.0f / dh;
    g->ap = omni_amul(a);
    const int32_t xa = ceil_div(-(int64_t)g->q0, g->step), xb = ceil_div((64 << 16) - (int64_t)g->q0, g->step);
    g->xa = xa > b->x0 ? xa : b->x0;
    g->xb = xb < b->x1 ? xb : b->x1;
}

/* A leaf's pixel at row y (dy2 = 2y + 1 - N as a wrapping unsigned) of column c (u on the door): the door's pixel,
 * shaded, plus drawLeaf's warm rim, wr (the look's warm_rgb) at the column's warm (fk_cols_k). false: no leaf there. */
static inline bool leaf_px(const fk_col_t *c, uint32_t dy2, const uint16_t *door, const uint8_t *wr, uint16_t *out)
{
    /* v = floor(cy + (y + .5 - cy) inv_s): Q17 in unsigned arithmetic (defined for any input), then range-checked.
     * Exact for any inv_s below 140: the sum is never negative for an outward inv_s <= 1, and an inward leaf's rows
     * past its ends (inv_s up to 1.42) give a negative sum, which wraps to a v >= N */
    const uint32_t v = (((uint32_t)C << 17) + dy2 * c->inv_s_q16) >> 17;
    if (v >= N) return false;
    uint16_t L = door[v * N + (uint32_t)c->u];
    if (c->shade) {   /* omni_blend565(L, 0, shade): the black source's terms are 0 */
        const uint32_t sq = 256u - (uint32_t)omni_amul(c->shade);
        const uint32_t rb = ((uint32_t)omni_ex_r(L) << 16 | (uint32_t)omni_ex_b(L)) * sq;
        const uint32_t g = (uint32_t)omni_ex_g(L) * sq;
        L = (uint16_t)((rb >> 27) << 11 | (g >> 10) << 5 | ((rb >> 11) & 31u));
    }
    if (c->warm) {
        const int ap = omni_amul(c->warm);
        L = add565(L, (wr[0] * ap) >> 8, (wr[1] * ap) >> 8, (wr[2] * ap) >> 8);
    }
    *out = L;
    return true;
}

void fk_cols_band(const fk_cols_kind_t *k, const fk_cols_state_t *st, const fk_col_t cols[466], const uint16_t *door,
                  const uint16_t *interior, const uint8_t *glow, omni_rect_t band, uint16_t *out)
{
    /* Done with keep (wave 2 Task 3): the interior plus the kept leaves at full weight, with the light out and the
     * fade up: the swing's own steps for that state (so the hold before the end and the end are the same frame) */
    fk_cols_state_t kept;
    if (st->done && st->keep) {
        kept = *st;
        kept.done = false;
        kept.fade = kept.xfade = 255;
        kept.seam = kept.glow = 0;
        st = &kept;
    }
    omni_rect_t b = band;   /* clipped to the screen; out keeps band's layout (row 0 = band.y0) */
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > N) b.x1 = N;
    if (b.y1 > N) b.y1 = N;
    if (b.x0 >= b.x1 || b.y0 >= b.y1) return;
    out += (ptrdiff_t)(b.y0 - band.y0) * N;
    /* Off the round panel nothing shows: those pixels are black, and every pass below works on each row's panel
     * columns only (Task 12: a fifth of the square). */
    if (!st->done && st->fade == 0) {   /* the fade's first frame: black whatever is under it (alpha 0 is the
                                         * destination, black, exactly: blend565.h) */
        for (int y = b.y0; y < b.y1; y++) memset(out + (size_t)(y - b.y0) * N + b.x0, 0, (size_t)(b.x1 - b.x0) * 2);
        return;
    }
    /* The interior under the band, except in the columns a leaf covers whole at full weight (u on the door, inv_s <= 1:
     * every row is on the leaf, which then writes every pixel there from the door alone; the free-edge quad drawn
     * before it is covered too): covered columns run from each hinge (test_cols_monotonic), so [cx0, cx1) is the
     * rest. Task 12: the interior's PSRAM reads were a fixed cost per band. */
    int cx0 = b.x0, cx1 = b.x1;
    if (!st->done && st->xfade == 255) {
        while (cx0 < cx1 && cols[cx0].u >= 0 && cols[cx0].u < N && cols[cx0].inv_s_q16 <= 65536u) cx0++;
        while (cx1 > cx0 && cols[cx1 - 1].u >= 0 && cols[cx1 - 1].u < N && cols[cx1 - 1].inv_s_q16 <= 65536u) cx1--;
    }
    for (int y = b.y0; y < b.y1; y++) {
        uint16_t *o = out + (size_t)(y - b.y0) * N;
        int p0, p1;
        band_panel(y, &b, &p0, &p1);
        const int a = cx0 > p0 ? cx0 : p0, z = cx1 < p1 ? cx1 : p1;
        if (a < z) memcpy(o + a, interior + (size_t)y * N + a, (size_t)(z - a) * 2);
        band_unpanel(o, y, &b);
    }
    if (st->done) return;

    /* the free edges' thickness, under the leaves (while a leaf has moved and its edge is on screen) */
    if (edge_quad(k, st->th_l, false, st->inward, out, &b) | edge_quad(k, st->th_r, true, st->inward, out, &b))
        for (int y = b.y0; y < b.y1; y++) band_unpanel(out + (size_t)(y - b.y0) * N, y, &b);   /* what fell off it */

    /* the band's leaf columns [lx0, lx1); drawLeaf's warm rim, (255, 170, 90) at the column's warm (fk_cols_k) */
    int lx0 = b.x1, lx1 = b.x0;
    for (int x = b.x0; x < b.x1; x++) {
        const int u = cols[x].u;
        if (u < 0 || u >= N) continue;
        if (x < lx0) lx0 = x;
        lx1 = x + 1;
    }

    /* the light through the seam (two draws) and the gap's glow column: drawDoorsPhase's rects */
    glow_pass_t gp[3];
    int ng = 0;
    if (st->seam > 0) {
        glow_pass(&gp[ng++], (float)C - 14.0f, 20.0f, 28.0f, (float)N - 40.0f, st->seam, &b);
        glow_pass(&gp[ng++], (float)C - 48.0f, -20.0f, 96.0f, (float)N + 40.0f, (uint8_t)((st->seam + 1) / 2), &b);
    }
    if (st->glow > 0) {
        const float xl = fk_cols_free_edge(k, st->th_l, st->inward);
        const float xr = (float)N - fk_cols_free_edge(k, st->th_r, st->inward);
        const float gap = xr - xl > 0 ? xr - xl : 0;
        glow_pass(&gp[ng++], xl - 50.0f, -40.0f, gap + 100.0f, (float)N + 80.0f, st->glow, &b);
    }

    /* nothing over the interior (and the edges): the leaves gone and the light out, as from 830 to 1800 ms */
    const uint8_t xfade = st->xfade, fade = st->fade;
    if (lx0 >= lx1 && ng == 0 && fade == 255) return;
    const uint8_t *wr = look_of(k)->warm_rgb;

    /* the leaves, row by row (each pixel's steps in order: the leaf, then the light, then the fade) */
    for (int y = b.y0; y < b.y1; y++) {
        uint16_t *o = out + (size_t)(y - b.y0) * N;
        const uint32_t dy2 = (uint32_t)(2 * y + 1 - N);   /* twice (y + .5 - cy), as a wrapping unsigned */
        int p0, p1;
        band_panel(y, &b, &p0, &p1);
        const int xa = lx0 > p0 ? lx0 : p0, xz = lx1 < p1 ? lx1 : p1;
        for (int x = xa; x < xz; x++) {
            const fk_col_t *c = &cols[x];
            if (c->u < 0 || c->u >= N) continue;
            uint16_t L;
            if (!leaf_px(c, dy2, door, wr, &L)) continue;
            o[x] = xfade == 255 ? L : omni_blend565(o[x], L, xfade);
        }
    }
    const uint32_t fp = (uint32_t)omni_amul(fade);   /* omni_blend565(0, o, fade): the black destination's terms are 0 */
    for (int y = b.y0; y < b.y1; y++) {
        uint16_t *o = out + (size_t)(y - b.y0) * N;
        int p0, p1;
        band_panel(y, &b, &p0, &p1);
        /* the light ('lighter'), each pass over its own columns: a = sprite x pass alpha, both as 0..256 */
        for (int i = 0; i < ng; i++) {
            const glow_pass_t *g = &gp[i];
            const float sy = floorf(((float)y + 0.5f - g->dy) * g->ky);
            if (!(sy >= 0 && sy < 256)) continue;
            const uint8_t *grow = glow + (int)sy * 64;
            const int xa = g->xa > p0 ? g->xa : p0, xz = g->xb < p1 ? g->xb : p1;
            for (int x = xa; x < xz; x++) {
                const int ap = omni_amul(grow[(g->q0 + (int32_t)x * g->step) >> 16]) * g->ap >> 8;
                if (ap) o[x] = add565(o[x], (GLOW_R * ap) >> 8, (GLOW_G * ap) >> 8, (GLOW_B * ap) >> 8);
            }
        }
        /* the fade from black */
        if (fade != 255)
            for (int x = p0; x < p1; x++) {
                const uint16_t q = o[x];
                const uint32_t rb = ((uint32_t)omni_ex_r(q) << 16 | (uint32_t)omni_ex_b(q)) * fp;
                const uint32_t g = (uint32_t)omni_ex_g(q) * fp;
                o[x] = (uint16_t)((rb >> 27) << 11 | (g >> 10) << 5 | ((rb >> 11) & 31u));
            }
    }
}

/* ---- the kept leaves (#3's own draw; wave 2 Task 3) ---- */

void fk_cols_leaves(const fk_cols_kind_t *k, const fk_col_t cols[466], const uint16_t *door, omni_rect_t clip,
                     uint16_t *fb, int stride)
{
    omni_rect_t b = clip;   /* clipped to the screen; fb stays clip's (x0, y0) */
    if (b.x0 < 0) b.x0 = 0;
    if (b.y0 < 0) b.y0 = 0;
    if (b.x1 > N) b.x1 = N;
    if (b.y1 > N) b.y1 = N;
    if (b.x0 >= b.x1 || b.y0 >= b.y1) return;
    const float th = k->open_deg * D2R;
    /* the free edges' thickness, as the band's done state draws it: one quad each, on the panel's pixels only (the
     * band fills it whole, then blacks what fell off the panel; here fb's pixels off the panel stay fb's), one panel
     * row at a time over the quad's rows (omni_fill_poly's pixels do not depend on the clip) */
    for (int side = 0; side < 2; side++) {
        omni_pt_t p[4];
        if (!edge_pts(k, th, side != 0, k->inward, p)) continue;
        float py0 = p[0].y, py1 = p[0].y, px0 = p[0].x, px1 = p[0].x;
        for (int i = 1; i < 4; i++) {
            py0 = p[i].y < py0 ? p[i].y : py0;
            py1 = p[i].y > py1 ? p[i].y : py1;
            px0 = p[i].x < px0 ? p[i].x : px0;
            px1 = p[i].x > px1 ? p[i].x : px1;
        }
        const int ya = py0 > (float)b.y0 ? (int)py0 : b.y0, yz = py1 + 1.0f < (float)b.y1 ? (int)py1 + 1 : b.y1;
        const int qa = px0 > 0 ? (int)px0 : 0, qz = px1 + 1.0f < (float)N ? (int)px1 + 1 : N;
        const uint16_t rgb = edge_rgb(k, th);
        for (int y = ya; y < yz; y++) {
            int p0, p1;
            band_panel(y, &b, &p0, &p1);
            if (p0 < qa) p0 = qa;
            if (p1 > qz) p1 = qz;
            if (p0 >= p1) continue;
            const omni_rect_t r = { (int16_t)p0, (int16_t)y, (int16_t)p1, (int16_t)(y + 1) };
            omni_fill_poly(fb + (ptrdiff_t)(y - clip.y0) * stride + (p0 - clip.x0), stride, &r, p, 4, rgb, 255);
        }
    }
    /* the leaves at full weight, as the band draws them */
    const uint8_t *wr = look_of(k)->warm_rgb;
    for (int y = b.y0; y < b.y1; y++) {
        uint16_t *o = fb + (ptrdiff_t)(y - clip.y0) * stride;   /* o[x - clip.x0]: pixel (x, y) */
        const uint32_t dy2 = (uint32_t)(2 * y + 1 - N);
        int p0, p1;
        band_panel(y, &b, &p0, &p1);
        for (int x = p0; x < p1; x++) {
            const fk_col_t *c = &cols[x];
            if (c->u < 0 || c->u >= N) continue;
            uint16_t L;
            if (leaf_px(c, dy2, door, wr, &L)) o[x - clip.x0] = L;
        }
    }
}
