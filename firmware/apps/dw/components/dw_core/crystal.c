/* Face 1, Crystal Chamber (dw/faces.h; dw wave 1 Task 11, spec sections 10 and 11), from faces-reveal.js
 * buildCrystalBase, buildLances, buildScriptRing, buildCore, crystalInterior, monitor and crystalSleep.
 *
 * Layers (tools/export/dw_layers.json): dw_c1_base (the floor, pillars, minute track and numerals, RGB565), dw_c1_lance
 * (the lances' alpha, added in a fixed amber), dw_c1_script (the script ring, ORS1 r 214-231, turned by the annulus
 * map), dw_c1_core (the 140 px crystal) and the atlases dw_c1_mono (the monitor's date) and dw_dymo (the stale strip).
 * Drawn here: the shard hands (hands.c), the seconds pip, the crystal's glows and glint, the monitor's frame, the
 * stale marker, and the glance.
 *
 * Motion at rest stays in regions (spec section 11): the script steps 6 degrees each second, eased over 280 ms (D3),
 * redrawing only the annulus (8 rects); the glint runs once round the crystal's facets in [400, 900) ms of each
 * second, redrawing only its box; the hands jump each minute. The windows are disjoint (100 ms or more apart), so a
 * frame at the cadence redraws one region or the other. The limit that forces this is dirty()'s 8 rects, which the
 * annulus's cover fills (its 8 boxes are about 94 600 px; with the crystal's 64 px box the union would still be under
 * 100 000 px, but there is no 9th rect for it). The chime's breath (Task 16, spec section 13) swells the lances with
 * its envelope, redrawing their box (DW_C1_LANCES, about 132 000 px; the whole frame when the script steps in the same
 * frame), not under reduced motion. Under reduced motion (the final review, M1) the script steps at each second with
 * no easing, onto the place the eased step comes to rest, and there is no glint: nothing moves within a second. In the glance nothing moves within a minute: dirty() reports nothing. The mockup's
 * continuous facet shimmer, its breathing core glow and its lance shimmer at rest are left out for this (the glance's
 * power and the dirty budget, spec section 11).
 * The wake (wake_ms < UINT32_MAX) is the doors' tail on the device timeline: the lances flare and settle, the script
 * winds in, the hands grow; it ends in the settled frame exactly.
 * Pure: no libm trig (fk_sincos), no allocation. */
#include <math.h>
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/doors.h"
#include "dw/faces.h"
#include "omni/blend565.h"
#include "omni/raster.h"
#include "omni/ring.h"

#define W 466
#define C 233.f
#define PI_F 3.14159265358979f
#define D2R (PI_F / 180.f)
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define ANN_MAX 98304   /* >= fk_annulus_bytes(214, 231, 2048) (test_c1_annulus_fits) */

const omni_rect_t DW_C1_MONITOR = {188, 340, 278, 376};
const omni_rect_t DW_C1_STALE = {164, 100, 300, 130};
const omni_rect_t DW_C1_BOX = {201, 201, 265, 265};
const omni_rect_t DW_C1_LANCES = {49, 25, 405, 396};

typedef struct {
    bool base_ok, lance_ok, core_ok, script_ok, mono_ok, dymo_ok;
    fk_img_t base, lance, core, mono_img, dymo_img;
    fk_atlas_t mono, dymo;
    omni_strip_t script;
    fk_annulus_t *ann;
    omni_rect_t ring[8];   /* the annulus's cover, 8 sector boxes */
    /* the glows about whole-pixel centres as tables (Task 12), the crystal's 120 and 22 px: const data (dw_glow_lut,
     * Task 13), none built at bind (the stale lamp's 12 px is dw_stale_mark's) */
    const uint8_t *g120, *g22;
    uint64_t ann_mem[ANN_MAX / 8];
} c1_t;

/* ---- the face's state for an input */
typedef struct {
    bool set, stale;
    float ha, ma;        /* hand angles: they jump each minute */
    float grow;          /* the hands' growth (the wake's tail; 1 settled) */
    float lance;         /* the lances' gain (0.42 settled) */
    float bloom;         /* the core glow's boost in the tail */
    uint16_t off;        /* the script's strip offset */
    int glint_k;         /* the lit facet, -1 none */
    uint8_t glint_a;
} c1_state_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float ph(float t, float a, float b) { return clampf((t - a) / (b - a), 0, 1); }
static float out_cubic(float s) { const float u = 1 - s; return 1 - u * u * u; }
static float in_out_cubic(float s) { const float u = -2 * s + 2; return s < 0.5f ? 4 * s * s * s : 1 - u * u * u / 2; }
static float out_back(float s) { const float u = s - 1; return 1 + 2.4f * u * u * u + 1.4f * u * u; }
static float in_out_sine(float s)
{
    float sn, cs;
    fk_sincos(PI_F * s, &sn, &cs);
    return -(cs - 1) / 2;
}

static uint16_t script_off(const fk_face_in_t *in, float settle)
{
    if (!in->clk.set) return 0;
    /* reduced motion (the final review, M1): no easing; the step lands at the second, on the place it eases to */
    const float ease = in->reduced ? 1.f : out_cubic(clampf((float)in->clk.ms / (float)DW_C1_EASE_MS, 0, 1));
    const float e = ((float)in->clk.s + ease) * ((float)DW_C1_SCRIPT_NT / 60.f) -
                    (1 - settle) * (75.f / 360.f) * (float)DW_C1_SCRIPT_NT;
    int m = (int)floorf(e + 0.5f) % DW_C1_SCRIPT_NT;
    if (m < 0) m += DW_C1_SCRIPT_NT;
    return (uint16_t)((DW_C1_SCRIPT_NT - m) % DW_C1_SCRIPT_NT);   /* a larger offset turns it counter-clockwise */
}

static void state_of(const fk_face_in_t *in, c1_state_t *st)
{
    memset(st, 0, sizeof *st);
    st->set = in->clk.set;
    st->stale = in->clk.set && in->clk.stale;
    float lance = 1, settle = 1, hands = 1;
    if (in->wake_ms != UINT32_MAX) {   /* revealState's lance, settle, hands and bloom */
        const float te = dw_mockup_te(in->wake_ms);
        const float flare = out_cubic(ph(te, 520, 980)) * (1 - in_out_sine(ph(te, 1000, 2250)));
        lance = 1 + 1.6f * flare - (1 - ph(te, 400, 900));
        settle = out_cubic(ph(te, 650, 2200));
        hands = out_back(ph(te, 980, 1560));
        st->bloom = 0.5f * out_cubic(ph(te, 430, 720)) * (1 - in_out_cubic(ph(te, 720, 1450)));
    }
    st->lance = lance * 0.42f;
    /* the chime's breath (spec section 13): the lances swell with the envelope, not under reduced motion or unset */
    if (in->clk.set && !in->reduced && in->chime > 0) st->lance += 0.5f * clampf(in->chime, 0, 1);
    st->grow = hands;
    st->off = script_off(in, settle);
    st->glint_k = -1;
    if (!st->set) return;
    st->ma = ((float)in->clk.m / 60.f) * 2 * PI_F;
    st->ha = (((float)(in->clk.h % 12) + (float)in->clk.m / 60.f) / 12.f) * 2 * PI_F;
    if (!in->reduced && in->clk.ms >= DW_C1_GLINT_MS0 && in->clk.ms < DW_C1_GLINT_MS1) {   /* no glint if reduced */
        const float x = (float)(in->clk.ms - DW_C1_GLINT_MS0) * (6.f / (float)(DW_C1_GLINT_MS1 - DW_C1_GLINT_MS0));
        const int k = (int)floorf(x);
        float sn, cs;
        fk_sincos((x - (float)k) * PI_F, &sn, &cs);
        const int a = (int)(0.35f * sn * 255.f + 0.5f);
        if (a > 0) st->glint_k = k, st->glint_a = (uint8_t)a;
    }
}

uint16_t dw_c1_script_off(const fk_face_in_t *in)
{
    c1_state_t st;
    state_of(in, &st);
    return st.off;
}

void dw_c1_monitor_text(const fk_face_in_t *in, char out[12])
{
    static const char DOW[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    if (!in->clk.set) {
        memcpy(out, "--:--", 6);
        return;
    }
    const char *d = in->clk.wday < 7 ? DOW[in->clk.wday] : "---";
    int n = 0;
    for (int i = 0; i < 3; i++) out[n++] = d[i];
    out[n++] = ' ';
    const unsigned md = in->clk.mday > 99 ? 99 : in->clk.mday;
    if (md >= 10) out[n++] = (char)('0' + md / 10);
    out[n++] = (char)('0' + md % 10);
    out[n] = '\0';
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

/* the box of the annulus [214, 231] (pixel centres) between angles a0 and a1, clockwise from 12 o'clock */
static void sector_box(float a0, float a1, omni_rect_t *r)
{
    /* zeroed (wave 2 Task 14): host gcc -O2 cannot see that na >= 2 fills xs[0] (-Wmaybe-uninitialized); every value
     * read is written first, so the box (and every CRC) is unchanged */
    float xs[12] = {0}, ys[12] = {0};
    int n = 0;
    float ang[6];
    int na = 0;
    ang[na++] = a0;
    ang[na++] = a1;
    for (int k = -4; k <= 8; k++) {
        const float q = (float)k * (PI_F / 2);
        if (q > a0 && q < a1 && na < 6) ang[na++] = q;
    }
    for (int i = 0; i < na; i++) {
        float s, c;
        fk_sincos(ang[i], &s, &c);
        for (int j = 0; j < 2; j++) {
            const float rr = j ? (float)DW_C1_SCRIPT_R1 : (float)DW_C1_SCRIPT_R0;
            xs[n] = C + rr * s, ys[n] = C - rr * c, n++;
        }
    }
    float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
    for (int i = 1; i < n; i++) {
        if (xs[i] < x0) x0 = xs[i];
        if (xs[i] > x1) x1 = xs[i];
        if (ys[i] < y0) y0 = ys[i];
        if (ys[i] > y1) y1 = ys[i];
    }
    /* a pixel whose centre (x + .5) lies in the region has floor(x0) <= x < ceil(x1): no margin is needed */
    r->x0 = (int16_t)floorf(x0), r->y0 = (int16_t)floorf(y0);
    r->x1 = (int16_t)ceilf(x1), r->y1 = (int16_t)ceilf(y1);
}

static bool c1_bind(void *ctx, fk_asset_fn get)
{
    c1_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->base_ok = img(get, ASSET_DW_C1_BASE, &k->base, FK_CF_RGB565, W, W);
    k->lance_ok = img(get, ASSET_DW_C1_LANCE, &k->lance, FK_CF_A8, W, W);
    k->core_ok = img(get, ASSET_DW_C1_CORE, &k->core, FK_CF_RGB565A8, ASSET_DW_C1_CORE_W, ASSET_DW_C1_CORE_H);
    k->mono_ok = img(get, ASSET_DW_C1_MONO, &k->mono_img, FK_CF_RGB565A8, ASSET_DW_C1_MONO_W, ASSET_DW_C1_MONO_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->mono = (fk_atlas_t){&k->mono_img, DW_CELLS_DW_C1_MONO, DW_GLYPHS_DW_C1_MONO, ASSET_DW_C1_MONO_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    size_t n = 0;
    const void *s = get(ASSET_DW_C1_SCRIPT, &n);
    k->script_ok = s && omni_strip_map(&k->script, s, n) && k->script.n_theta == DW_C1_SCRIPT_NT &&
                   k->script.r_min == DW_C1_SCRIPT_R0 && k->script.n_r == DW_C1_SCRIPT_R1 - DW_C1_SCRIPT_R0;
    k->ann = fk_annulus_build(k->ann_mem, sizeof k->ann_mem, C, C, DW_C1_SCRIPT_R0, DW_C1_SCRIPT_R1,
                              DW_C1_SCRIPT_NT);
    for (int i = 0; i < 8; i++)   /* offset 21 degrees: the smallest union of 8 boxes (measured) */
        sector_box((21.f + 45.f * (float)i) * D2R, (21.f + 45.f * (float)(i + 1)) * D2R, &k->ring[i]);
    k->g120 = dw_glow_lut(120);
    k->g22 = dw_glow_lut(22);
    return k->g120 && k->g22 && k->base_ok && k->lance_ok && k->core_ok && k->mono_ok && k->dymo_ok && k->script_ok &&
           k->ann;
}

/* ---- drawing */
static void fill(uint16_t *fb, int stride, omni_rect_t clip, uint16_t v)
{
    for (int y = clip.y0; y < clip.y1; y++) {
        uint16_t *d = fb + (y - clip.y0) * stride;
        for (int x = 0; x < clip.x1 - clip.x0; x++) d[x] = v;
    }
}

static bool meets(omni_rect_t clip, omni_rect_t r)
{
    return r.x0 < clip.x1 && clip.x0 < r.x1 && r.y0 < clip.y1 && clip.y0 < r.y1;
}

static void hexagon(float cx, float cy, float w, float h, omni_pt_t p[6])
{
    p[0] = (omni_pt_t){cx - w / 2 + 9, cy - h / 2};
    p[1] = (omni_pt_t){cx + w / 2 - 9, cy - h / 2};
    p[2] = (omni_pt_t){cx + w / 2, cy};
    p[3] = (omni_pt_t){cx + w / 2 - 9, cy + h / 2};
    p[4] = (omni_pt_t){cx - w / 2 + 9, cy + h / 2};
    p[5] = (omni_pt_t){cx - w / 2, cy};
}

/* monitor(): the blue hexagonal holo monitor at 6 (drawn there at C - 48, C + 104, 96 x 40: centre 233, 357) */
static void monitor(const c1_t *k, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!meets(clip, DW_C1_MONITOR)) return;
    omni_pt_t p[6];
    hexagon(C, 357.f, 80, 26, p);
    omni_fill_poly(fb, stride, &clip, p, 6, RGB(6, 14, 34), 235);
    fk_poly_stroke(fb, stride, &clip, p, 6, 1.1f, RGB(80, 150, 255), 204);
    hexagon(C, 357.f, 74, 20, p);
    fk_poly_stroke(fb, stride, &clip, p, 6, 0.8f, RGB(80, 150, 255), 64);
    if (!k->mono_ok) return;
    char txt[12];
    dw_c1_monitor_text(in, txt);
    const int w = fk_atlas_width(&k->mono, txt, 0);
    /* textAlign center at 233; textBaseline middle at 357.5: the baseline 4.9 px lower (IBM Plex Mono's em box) */
    fk_atlas_text(fb, stride, clip, &k->mono, txt, (W - w) / 2, 362, 0, 255);
}

/* staleMark() (dw_stale_mark), here above the crystal at (233, 115), drawn under the hands so it never hides them */
static void stale_marker(const c1_t *k, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!meets(clip, DW_C1_STALE) || !k->dymo_ok) return;
    dw_stale_mark(fb, stride, &clip, &k->dymo, 233.f, 115.f);
}

static void c1_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const c1_t *k = ctx;
    c1_state_t st;
    state_of(in, &st);
    /* the floor, pillars, minute track and numerals */
    if (k->base_ok) fk_blit(fb, stride, clip, &k->base, 0, 0, 255);
    else fill(fb, stride, clip, 0);
    /* the script ring, and its blue eye riding it: the seconds pip */
    if (k->script_ok && k->ann) fk_annulus_draw(k->ann, &k->script, st.off, 255, fb, stride, clip);
    if (st.set) {
        float s, c;
        fk_sincos(-(float)st.off * (2 * PI_F / (float)DW_C1_SCRIPT_NT), &s, &c);
        const float x = C + 223.f * s, y = C - 223.f * c;
        fk_glow_add(fb, stride, &clip, x, y, 9.f, RGB(61, 139, 255), 204, DW_C1_SCRIPT_R0, DW_C1_SCRIPT_R1);
        fk_disc(fb, stride, &clip, x, y, 2.2f, RGB(0xd6, 0xe6, 0xff), 255);
    }
    /* the lances of crystalline light, additive at L (and again above 1) */
    if (k->lance_ok && st.lance > 0) {
        const float l1 = st.lance < 1 ? st.lance : 1;
        if (st.lance > 1)   /* the two passes in one (fk_add_a8_2: the same bytes) */
            fk_add_a8_2(fb, stride, clip, &k->lance, 0, 0, W, W, RGB(240, 160, 90), (uint8_t)(l1 * 255.f + 0.5f),
                        (uint8_t)(clampf(st.lance - 1, 0, 1) * 255.f + 0.5f));
        else
            fk_add_a8(fb, stride, clip, &k->lance, 0, 0, W, W, RGB(240, 160, 90), (uint8_t)(l1 * 255.f + 0.5f));
    }
    /* the stale marker, under the hands: it never covers them */
    if (st.stale) stale_marker(k, clip, fb, stride);
    /* the hands: crystal shards growing from the crystal */
    if (st.set) {
        dw_hand_shard(fb, stride, &clip, st.ha, true, st.grow, 255);
        dw_hand_shard(fb, stride, &clip, st.ma, false, st.grow, 255);
    }
    monitor(k, in, clip, fb, stride);
    /* the crystal: its glow (dimmed when the time is unset), the crystal, the glint, the hot centre */
    const float glow = clampf((st.set ? 0.5f : 0.25f) * 0.75f + st.bloom * 0.3f, 0, 0.95f);
    fk_glow_add_lut(fb, stride, &clip, 233, 233, 120, k->g120, RGB(255, 170, 80), (uint8_t)(glow * 255.f + 0.5f));
    if (k->core_ok) {
        const int cx0 = 233 - k->core.w / 2, cy0 = 233 - k->core.h / 2;
        fk_blit(fb, stride, clip, &k->core, cx0, cy0, 255);
        if (!st.set) {   /* dimmed by colour: the crystal's pixels toward black, by its own alpha */
            const omni_rect_t b = {(int16_t)cx0, (int16_t)cy0, (int16_t)(cx0 + k->core.w), (int16_t)(cy0 + k->core.h)};
            for (int y = b.y0 > clip.y0 ? b.y0 : clip.y0; y < b.y1 && y < clip.y1; y++)
                for (int x = b.x0 > clip.x0 ? b.x0 : clip.x0; x < b.x1 && x < clip.x1; x++) {
                    const uint8_t a = k->core.a[(y - cy0) * k->core.w + (x - cx0)];
                    uint16_t *d = fb + (y - clip.y0) * stride + (x - clip.x0);
                    if (a) *d = omni_blend565(*d, 0, (a * 128) / 255);
                }
        }
    }
    if (st.glint_k >= 0) {
        float s, c;
        fk_sincos((float)st.glint_k * (PI_F / 3), &s, &c);
        const omni_pt_t t[3] = {{C, C}, {C + 30.f * s, C - 30.f * c}, {C + 26.f * c + 15.f * s, C + 26.f * s - 15.f * c}};
        fk_poly_add(fb, stride, &clip, t, 3, RGB(255, 240, 210), st.glint_a);
    }
    fk_glow_add_lut(fb, stride, &clip, 233, 233, 22, k->g22, RGB(255, 250, 235), 89);
}

/* crystalSleep(): twelve dim dots, a small crystal outline and the outline hands, on the glance orbit. Colours kept
 * under 110 per channel (the hands' #7a4a1c at 0.9). */
static void c1_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    (void)ctx;
    fill(fb, stride, clip, 0);
    const float ox = C + (float)in->odx, oy = C + (float)in->ody;
    for (int h = 1; h <= 12; h++) {
        float s, c;
        fk_sincos((float)h * 30.f * D2R, &s, &c);
        fk_disc(fb, stride, &clip, ox + 200.f * s, oy - 200.f * c, h % 3 == 0 ? 2.6f : 1.8f,
                h == 12 ? RGB(0x6a, 0x40, 0x18) : RGB(0x4a, 0x2c, 0x10), 255);
    }
    omni_pt_t hx[6];
    for (int i = 0; i < 6; i++) {
        float s, c;
        fk_sincos((float)i * (PI_F / 3), &s, &c);
        hx[i] = (omni_pt_t){ox + 9.f * s, oy - 9.f * c};
    }
    fk_poly_stroke(fb, stride, &clip, hx, 6, 1.2f, RGB(0x4e, 0x2e, 0x10), 255);
    if (!in->clk.set) {   /* "--:--" at (C, C + 40), 13 px mono cells of 7.8 px, in #4a3a24 */
        const uint16_t col = RGB(0x4a, 0x3a, 0x24);
        const float x0 = ox - 19.5f, ym = oy + 40.5f;
        for (int i = 0; i < 5; i++) {
            const float cx = x0 + 7.8f * (float)i;
            if (i == 2) {
                const omni_pt_t d1[4] = {{cx + 3.1f, ym - 3.6f}, {cx + 4.7f, ym - 3.6f}, {cx + 4.7f, ym - 2.0f},
                                         {cx + 3.1f, ym - 2.0f}};
                const omni_pt_t d2[4] = {{cx + 3.1f, ym + 2.0f}, {cx + 4.7f, ym + 2.0f}, {cx + 4.7f, ym + 3.6f},
                                         {cx + 3.1f, ym + 3.6f}};
                omni_fill_poly(fb, stride, &clip, d1, 4, col, 255);
                omni_fill_poly(fb, stride, &clip, d2, 4, col, 255);
            } else {
                const omni_pt_t d[4] = {{cx + 1.6f, ym - 0.65f}, {cx + 6.2f, ym - 0.65f}, {cx + 6.2f, ym + 0.65f},
                                        {cx + 1.6f, ym + 0.65f}};
                omni_fill_poly(fb, stride, &clip, d, 4, col, 255);
            }
        }
        return;
    }
    c1_state_t st;
    state_of(in, &st);
    const uint16_t hand = RGB(110, 66, 25);
    dw_hand_outline(fb, stride, &clip, st.ha, true, ox, oy, hand, 255);
    dw_hand_outline(fb, stride, &clip, st.ma, false, ox, oy, hand, 255);
    if (st.stale)   /* the stale lamp, dim: inside the marker's rect on every orbit offset */
        fk_disc(fb, stride, &clip, 181.f + (float)in->odx, 115.f + (float)in->ody, 2.2f, RGB(96, 64, 22), 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t c1_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    const c1_t *k = ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->wake_ms != b->wake_ms || a->reduced != b->reduced)
        return full(out);   /* the mode, the state, the orbit, the wake's tail, reduced motion */
    if (!b->clk.set) return 0;   /* nothing moves on an unset clock */
    if (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday || a->clk.mon != b->clk.mon ||
        a->clk.wday != b->clk.wday)
        return full(out);   /* the minute: the hands jump (and the date at midnight) */
    if (b->glance) return 0;   /* the glance draws neither the script nor the glint: nothing within a minute */
    c1_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    const bool ring = sa.off != sb.off;
    const bool glint = sa.glint_k != sb.glint_k || (sa.glint_k >= 0 && sa.glint_a != sb.glint_a);
    const bool breath = sa.lance != sb.lance;   /* the chime's: the lances' box (it holds the crystal's) */
    if (ring && (glint || breath)) return full(out);   /* frames further apart than the windows' gap, or the breath */
    if (breath) {
        out[0] = DW_C1_LANCES;
        return 1;
    }
    if (ring) {
        memcpy(out, k->ring, sizeof k->ring);
        return 8;
    }
    if (glint) {
        out[0] = DW_C1_BOX;
        return 1;
    }
    return 0;
}

const fk_face_ops_t DW_FACE_CRYSTAL = {
    .key = "crystal",
    .ctx_bytes = sizeof(c1_t),
    .cadence_ms = 40,
    .bind = c1_bind,
    .draw = c1_draw,
    .glance = c1_glance,
    .dirty = c1_dirty,
    .tap = NULL,
    .reveal = true,
    .reveal_ops = &DW_REVEAL_DOORS,   /* rd wave 1 Task 2: the doors as face_kit's reveal */
};
