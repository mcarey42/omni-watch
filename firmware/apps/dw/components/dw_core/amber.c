/* Face 10, Crystal (dw/faces.h; dw wave 2 Task 6, spec sections 10 and 11), the Modern analog from faces-modern.js
 * shardHand, the wand hand, caBuild, caHands and modern-analog.
 *
 * Layers (tools/export/dw_layers.json): dw_c10_dial (caBuild's dial over black: the crystal room's floor spokes and
 * rings, the minute track, the four blue eye rings at the quarters and the Cormorant SC 34 numerals with their glow,
 * RGB565), dw_c10_script (scriptRing(W, 202, 228, 2018, ..) unwrapped to an A8 strip over r 198-232, tinted (202, 130,
 * 60); its alpha carries the .30), dw_c10_mono (the unset --:--, 22 px Plex Mono 500, A8) and the shared dw_dymo (the
 * stale strip). Drawn here: the core glow (r 40, (255, 180, 90): glowSprite's .5 at globalAlpha .5), the amber shard
 * hands and the wand (hands.c dw_hand_amber, dw_hand_wand), the hex nut with its blue eye, the stale marker and the
 * glance.
 *
 * Motion at rest (spec section 11): the wand sweeps (s + ms / 1000) / 60 of a turn with its trail and its tip's
 * pulse (.75 + .25 sin(2 pi 1.2 t)), redrawing its old and new boxes (dw_hand_wand_rects, 4 each) at cadence_ms 50;
 * the hour and minute hands step once a minute (lesson 8); the script ring steps 1 degree each 10 s, once round an
 * hour (WD30), and that frame redraws the annulus as 7 sector boxes and the wand's two places as one box. Under
 * reduced motion the wand steps once a second with no pulse (1), the script is still and there is no wake. In the
 * glance nothing moves within a minute.
 * The wake (wake_len_ms 900, the face's own: no doors; :561-574): the dial (with the script and the core glow) fades
 * up over 700 ms while the hands sweep from 12 by easeOut3 over 900 ms. The mockup draws the bare hand sprites in its
 * wake; here the wand keeps its trail and the hex nut stays, so neither pops in when the wake ends. Its end is the
 * settled frame exactly.
 * Unset: no hands and no nut; --:-- at (233, 293) in rgba(242, 194, 126, .6), dimmed by colour. Stale: the amber lamp
 * and NOT SYNCED centred (233, 317), under the hands. The chime does not breathe here.
 * Pure: no libm trig (fk_sincos), no allocation. */
#include <math.h>
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/faces.h"
#include "omni/raster.h"

#define W 466
#define C 233.f
#define PI_F 3.14159265358979f
#define D2R (PI_F / 180.f)
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define ANN_MAX 147456   /* >= fk_annulus_bytes(198, 232, 2048), 143 487 (test_amber_annulus_fits) */
#define SCRIPT_RGB RGB(202, 130, 60)
#define CORE_RGB RGB(255, 180, 90)
#define CORE_A 0.25f     /* glowSprite('255,180,90', 40, .5) drawn at globalAlpha .5 */
#define UNSET_RGB RGB(145, 116, 76)   /* rgba(242, 194, 126, .6) over black, by colour */
#define UNSET_Y 293      /* fillText('--:--', cx, cy + 60): the alphabetic baseline */
#define STALE_Y 317.f
#define RING_N 7         /* the annulus's cover at a script step: 7 sector boxes (the 8th holds the wand) */

_Static_assert(ASSET_DW_C10_SCRIPT_NT == DW_C10_SCRIPT_NT && ASSET_DW_C10_SCRIPT_R0 == DW_C10_SCRIPT_R0 &&
                   ASSET_DW_C10_SCRIPT_NR == DW_C10_SCRIPT_R1 - DW_C10_SCRIPT_R0,
               "dw_c10_script is the strip faces.h names");

const omni_rect_t DW_C10_STALE = {164, 302, 302, 332};

typedef struct {
    bool dial_ok, script_ok, mono_ok, dymo_ok;
    fk_img_t dial, script, mono_img, dymo_img;
    fk_atlas_t mono, dymo;
    fk_annulus_t *ann;
    omni_rect_t ring[RING_N];   /* the annulus's cover, RING_N sector boxes */
    const uint8_t *g40;         /* the core glow's table (dw_glow_lut: const data) */
    uint64_t ann_mem[ANN_MAX / 8];
} c10_t;

/* ---- the face's state for an input */
typedef struct {
    bool set, stale;
    float ha, ma, sa;   /* the hands' angles (the wake's sweep from 12 applied) */
    float pulse;
    uint8_t fade;       /* the dial's level: the wake's fade */
    uint16_t off;       /* the script's strip offset */
} c10_state_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float out_cubic(float s) { const float u = 1 - clampf(s, 0, 1); return 1 - u * u * u; }
static bool settled(const fk_face_in_t *in) { return in->reduced || in->wake_ms >= DW_C10_WAKE_MS; }

uint16_t dw_c10_script_off(const fk_face_in_t *in)
{
    if (!in->clk.set || in->reduced) return 0;
    const unsigned q = ((in->clk.m % 60u) * 60u + in->clk.s % 60u) / 10u;   /* the 10 s steps into the hour */
    const unsigned k = (q * DW_C10_SCRIPT_NT + 180u) / 360u % DW_C10_SCRIPT_NT;
    return (uint16_t)((DW_C10_SCRIPT_NT - k) % DW_C10_SCRIPT_NT);   /* clockwise: a smaller offset */
}

float dw_c10_wand_angle(const fk_face_in_t *in)
{
    if (!in->clk.set) return 0;
    const float s = (float)(in->clk.s % 60u) + (in->reduced ? 0.f : (float)(in->clk.ms % 1000u) / 1000.f);
    return s * (2 * PI_F / 60.f);
}

float dw_c10_pulse(const fk_face_in_t *in)
{
    if (!in->clk.set || in->reduced) return 1.f;
    /* the seconds of the day mod 5 (5 s is six periods of 1 / 1.2 s), so the phase stays exact in single precision */
    const unsigned sod = (in->clk.h % 24u) * 3600u + (in->clk.m % 60u) * 60u + in->clk.s % 60u;
    const float t = (float)(sod % 5u) + (float)(in->clk.ms % 1000u) / 1000.f;
    float s, c;
    fk_sincos(t * (2 * PI_F * 1.2f), &s, &c);
    return 0.75f + 0.25f * s;
}

static void state_of(const fk_face_in_t *in, c10_state_t *st)
{
    memset(st, 0, sizeof *st);
    st->set = in->clk.set;
    st->stale = in->clk.set && in->clk.stale;
    const bool done = settled(in);
    const float e = done ? 1.f : out_cubic((float)in->wake_ms / (float)DW_C10_WAKE_MS);
    st->fade = done ? 255 : (uint8_t)(clampf((float)in->wake_ms / (float)DW_C10_FADE_MS, 0, 1) * 255.f + 0.5f);
    st->off = dw_c10_script_off(in);
    st->pulse = dw_c10_pulse(in);
    if (!st->set) return;
    st->ma = ((float)in->clk.m / 60.f) * 2 * PI_F * e;
    st->ha = (((float)(in->clk.h % 12) + (float)in->clk.m / 60.f) / 12.f) * 2 * PI_F * e;
    st->sa = dw_c10_wand_angle(in) * e;
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

/* the box of the annulus [R0, R1] (pixel centres) between angles a0 and a1 clockwise from 12 o'clock (crystal.c's
 * sector_box over this face's radii), kept to the screen */
static void sector_box(float a0, float a1, omni_rect_t *r)
{
    /* zeroed (wave 2 Task 14): host gcc -O2 cannot see that na >= 2 fills xs[0] (-Wmaybe-uninitialized); every value
     * read is written first, so the box (and every CRC) is unchanged */
    float xs[12] = {0}, ys[12] = {0}, ang[6];
    int n = 0, na = 0;
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
            const float rr = j ? (float)DW_C10_SCRIPT_R1 : (float)DW_C10_SCRIPT_R0;
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
    r->x0 = (int16_t)(x0 < 0 ? 0 : floorf(x0)), r->y0 = (int16_t)(y0 < 0 ? 0 : floorf(y0));
    r->x1 = (int16_t)(x1 > W ? W : ceilf(x1)), r->y1 = (int16_t)(y1 > W ? W : ceilf(y1));
}

static bool c10_bind(void *ctx, fk_asset_fn get)
{
    c10_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->dial_ok = img(get, ASSET_DW_C10_DIAL, &k->dial, FK_CF_RGB565, W, W);
    k->script_ok = img(get, ASSET_DW_C10_SCRIPT, &k->script, FK_CF_A8, DW_C10_SCRIPT_NT,
                       DW_C10_SCRIPT_R1 - DW_C10_SCRIPT_R0);
    k->mono_ok = img(get, ASSET_DW_C10_MONO, &k->mono_img, FK_CF_A8, ASSET_DW_C10_MONO_W, ASSET_DW_C10_MONO_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->mono = (fk_atlas_t){&k->mono_img, DW_CELLS_DW_C10_MONO, DW_GLYPHS_DW_C10_MONO, ASSET_DW_C10_MONO_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    k->ann = fk_annulus_build(k->ann_mem, sizeof k->ann_mem, C, C, (float)DW_C10_SCRIPT_R0, (float)DW_C10_SCRIPT_R1,
                              DW_C10_SCRIPT_NT);
    for (int i = 0; i < RING_N; i++)   /* offset 24 degrees: the smallest union of 7 boxes (measured) */
        sector_box((24.f + 360.f / RING_N * (float)i) * D2R, (24.f + 360.f / RING_N * (float)(i + 1)) * D2R,
                   &k->ring[i]);
    k->g40 = dw_glow_lut(40);
    return k->g40 && dw_glow_lut(26) && k->dial_ok && k->script_ok && k->mono_ok && k->dymo_ok && k->ann;
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

/* local (lx, ly) about (cx, cy), turned by (s, c) as canvas rotate() turns it */
static omni_pt_t rot(float lx, float ly, float s, float c, float cx, float cy)
{
    return (omni_pt_t){cx + lx * c - ly * s, cy + lx * s + ly * c};
}

/* the centre (caHands :485-487): a hex nut r 9 (#1a1210, stroked AMBER.mid 1.5 px) and its blue eye (AMBER.blueHi,
 * r 3.6, 1.6 px) */
static void nut(omni_rect_t clip, uint16_t *fb, int stride)
{
    const omni_rect_t box = {220, 220, 247, 247};
    if (!meets(clip, box)) return;
    omni_pt_t p[6];
    for (int i = 0; i < 6; i++) {   /* at i / 6 of a turn from 3 o'clock, as canvas measures */
        float s, c;
        fk_sincos((float)i * (PI_F / 3.f), &s, &c);
        p[i] = (omni_pt_t){C + 9.f * c, C + 9.f * s};
    }
    omni_fill_poly(fb, stride, &clip, p, 6, RGB(0x1a, 0x12, 0x10), 255);
    fk_poly_stroke(fb, stride, &clip, p, 6, 1.5f, RGB(0xca, 0x74, 0x2b), 255);
    fk_ring(fb, stride, &clip, C, C, 3.6f, 1.6f, RGB(0x9c, 0xc6, 0xff), 255);
}

static void c10_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const c10_t *k = ctx;
    c10_state_t st;
    state_of(in, &st);
    /* the dial, the script ring and the core glow, at the wake's fade (from black) */
    if (k->dial_ok && st.fade == 255) {
        fk_blit(fb, stride, clip, &k->dial, 0, 0, 255);
    } else {
        fill(fb, stride, clip, 0);
        if (k->dial_ok && st.fade) fk_blit(fb, stride, clip, &k->dial, 0, 0, st.fade);
    }
    if (k->script_ok && k->ann && st.fade)
        fk_annulus_draw_a8(k->ann, &k->script, st.off, SCRIPT_RGB, st.fade, fb, stride, clip);
    fk_glow_add_lut(fb, stride, &clip, 233, 233, 40, k->g40, CORE_RGB,
                    (uint8_t)(CORE_A * (float)st.fade + 0.5f));
    if (!st.set) {   /* 500 22px Plex Mono, centred on 233 */
        if (!k->mono_ok) return;
        const int w = fk_atlas_width(&k->mono, "--:--", 0);
        fk_atlas_text_rgb(fb, stride, clip, &k->mono, "--:--", (W - w) / 2, UNSET_Y, 0, UNSET_RGB, 255, 0);
        return;
    }
    /* the stale marker, under the hands: it never covers them */
    if (st.stale && k->dymo_ok && meets(clip, DW_C10_STALE)) dw_stale_mark(fb, stride, &clip, &k->dymo, C, STALE_Y);
    dw_hand_amber(fb, stride, &clip, st.ha, true, 255);
    dw_hand_amber(fb, stride, &clip, st.ma, false, 255);
    dw_hand_wand(fb, stride, &clip, st.sa, st.pulse);
    nut(clip, fb, stride);
}

/* modern-analog sleepFrame (:576-590): twelve dots at r 188 (#4A3018; the quarters 3.2 px, the others 2), the outline
 * shard hands (#6A4420, 2 px, round joins) and the centre ring (#21405F, r 4, 1.6 px), on the glance orbit; unset, the
 * dots alone. Colours at most 106 per channel. */
static void c10_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    (void)ctx;
    fill(fb, stride, clip, 0);
    const float ox = C + (float)in->odx, oy = C + (float)in->ody;
    for (int h = 0; h < 12; h++) {
        float s, c;
        fk_sincos((float)h * (PI_F / 6.f), &s, &c);
        fk_disc(fb, stride, &clip, ox + 188.f * s, oy - 188.f * c, h % 3 ? 2.f : 3.2f, RGB(0x4a, 0x30, 0x18), 255);
    }
    if (!in->clk.set) return;
    c10_state_t st;
    state_of(in, &st);
    const uint16_t ink = RGB(0x6a, 0x44, 0x20);
    for (int hand = 0; hand < 2; hand++) {   /* (0, 14), (-w, -.62 len), (0, -len), (w, -.62 len), closed */
        const float a = hand ? st.ma : st.ha, len = hand ? 150.f : 100.f, w = hand ? 5.f : 7.f;
        float s, c;
        fk_sincos(a, &s, &c);
        const omni_pt_t p[4] = {rot(0, 14.f, s, c, ox, oy), rot(-w, -0.62f * len, s, c, ox, oy),
                                rot(0, -len, s, c, ox, oy), rot(w, -0.62f * len, s, c, ox, oy)};
        for (int i = 0; i < 4; i++) {   /* lineJoin round: each side, and a disc at each corner */
            const omni_pt_t q = p[(i + 1) % 4];
            fk_line_max(fb, stride, &clip, p[i].x, p[i].y, q.x, q.y, 2.f, ink, 255);
        }
        for (int i = 0; i < 4; i++) fk_disc(fb, stride, &clip, p[i].x, p[i].y, 1.f, ink, 255);
    }
    fk_ring_max(fb, stride, &clip, ox, oy, 4.f, 1.6f, RGB(0x21, 0x40, 0x5f), 255);
    if (st.stale)   /* the stale lamp, dim: inside the marker's rect on every orbit offset */
        fk_disc(fb, stride, &clip, 181.f + (float)in->odx, STALE_Y + (float)in->ody, 2.2f, RGB(96, 64, 22), 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

/* the box holding every one of n boxes */
static omni_rect_t hull(const omni_rect_t *r, int n)
{
    omni_rect_t h = r[0];
    for (int i = 1; i < n; i++) {
        if (r[i].x0 < h.x0) h.x0 = r[i].x0;
        if (r[i].y0 < h.y0) h.y0 = r[i].y0;
        if (r[i].x1 > h.x1) h.x1 = r[i].x1;
        if (r[i].y1 > h.y1) h.y1 = r[i].y1;
    }
    return h;
}

static uint8_t c10_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    const c10_t *k = ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->reduced != b->reduced)
        return full(out);   /* the mode, the clock's state, the orbit, reduced motion */
    if (a->wake_ms != b->wake_ms && !(settled(a) && settled(b))) return full(out);   /* the wake: fade and sweep */
    if (!b->clk.set) return 0;   /* nothing moves on an unset clock */
    if (a->clk.h != b->clk.h || a->clk.m != b->clk.m) return full(out);   /* the minute: the hands step */
    if (b->glance) return 0;   /* nothing moves in the glance within a minute */
    c10_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    const bool wand = sa.sa != sb.sa || sa.pulse != sb.pulse;
    if (sa.off != sb.off) {   /* the script's 10 s step (or any jump): the annulus, and the wand's places in one box */
        memcpy(out, k->ring, sizeof k->ring);
        omni_rect_t r[8];
        dw_hand_wand_rects(sa.sa, r);
        dw_hand_wand_rects(sb.sa, r + 4);
        out[RING_N] = hull(r, 8);
        return RING_N + 1;
    }
    if (!wand) return 0;
    dw_hand_wand_rects(sa.sa, out);
    dw_hand_wand_rects(sb.sa, out + 4);
    return 8;
}

const fk_face_ops_t DW_FACE_AMBER = {
    .key = "amber",
    .ctx_bytes = sizeof(c10_t),
    .cadence_ms = 50,
    .bind = c10_bind,
    .draw = c10_draw,
    .glance = c10_glance,
    .dirty = c10_dirty,
    .tap = NULL,
    .reveal = false,
    .wake_len_ms = DW_C10_WAKE_MS,
};
