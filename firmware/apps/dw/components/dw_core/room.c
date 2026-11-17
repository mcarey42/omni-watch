/* Face 2, Roundel Room (dw/faces.h; dw wave 2 Task 5, spec sections 10 and 11), Classic's Flip-open home behind the
 * Classic doors (ASSET_DW_DOORS_C, outward), from faces-reveal.js buildClassicBase, buildLances, classicHands,
 * classicInterior and classicSleep.
 *
 * Layers (tools/export/dw_layers.json): dw_r2_base (the room, both roundel rows, the engraved numerals, the minute
 * track, the six console panels and the collar, RGB565), dw_r2_beam (the six beams from the column, alpha only, added
 * in blue-white; only their lit box, 360 x 340 at (38, 63), wave 2 Task 6), dw_r2_num (the lit hour's numeral, alpha
 * only, tinted #1a1712), and the shared atlases dw_dymo (the date tape, TIME UNSET, the stale strip) and dw_c1_mono
 * (the glance's --:--, tinted). Drawn here: the lit hour (its r 40 glow, disc, ring and numeral), the baton hands
 * (hands.c dw_hand_room), the rotor seen from above, the stale marker and the glance.
 *
 * Motion at rest stays in one region (spec section 11): the rotor bobs on a 2 s sine of the time of day (dw_r2_lift),
 * its inner ring and chevrons growing with the lift and its r 70 glow breathing, redrawing only DW_R2_ROTOR (144 x
 * 144; WD41). cadence_ms 40. The hands step once a minute. Under reduced motion the rotor stands at mid-lift. In the
 * glance nothing moves within a minute.
 * The chime (spec section 13; not under reduced motion, in the glance or unset): the rotor rides the envelope, the
 * beams swell (0.55 x the envelope) and the rotor's glow with them (0.5 x), and the hour roundel's glow flares (0.5 x;
 * the firmware's envelope carries the pings' glints, so it stands for the mockup's ping). There is no full-screen
 * tint and no outer-wall pulse (WD33), and the mockup's ping ripple ring (faces-reveal.js:754) is left out (WD43).
 * The wake (wake_ms < UINT32_MAX) is the doors' tail on the device timeline (dw_mockup_te, #1's map): the beams flare
 * (revealState's lance) and the hands grow (outBack over te 980..1560); it ends in the settled frame exactly.
 * Unset (WD42): no hands, no lit roundel (the unlit roundels are the empty rings), TIME UNSET on the tape, and the
 * rotor at rest (lift 0) with its glow at half. Stale: the amber NOT SYNCED strip centred (233, 329), above the date tape,
 * under the hands.
 * Pure: no libm trig (fk_sincos), no allocation. */
#include <math.h>
#include <string.h>
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/doors.h"
#include "dw/faces.h"
#include "omni/raster.h"

#define W 466
#define C 233.f
#define PI_F 3.14159265358979f
#define RGB(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define BEAM_RGB RGB(200, 215, 240)        /* the beams' blue-white (cols[1]; the white of their root is left out) */
#define HOUR_R 207.f                       /* the outer roundel row */
#define TAPE_Y (C + 126.f)                 /* dymo(.., C + 126) */
#define TAPE_INK RGB(240, 240, 240)        /* the tape's #f0f0f0 letters */
#define STALE_Y 329.f

const omni_rect_t DW_R2_ROTOR = {161, 161, 305, 305};
const omni_rect_t DW_R2_STALE = {164, 314, 302, 344};
/* the box of dw_r2_beam's lit pixels, which is all the layer holds (wave 2 Task 6, M4: cropped to it, drawn at its
 * origin; test_room_chime_regions) */
const omni_rect_t DW_R2_BEAMS = {ASSET_DW_R2_BEAM_X, ASSET_DW_R2_BEAM_Y, ASSET_DW_R2_BEAM_X + ASSET_DW_R2_BEAM_W,
                                 ASSET_DW_R2_BEAM_Y + ASSET_DW_R2_BEAM_H};

typedef struct {
    bool base_ok, beam_ok, num_ok, dymo_ok, mono_ok;
    fk_img_t base, beam, num_img, dymo_img, mono_img;
    fk_atlas_t num, dymo, mono;
    const uint8_t *g70, *g40;   /* the rotor's and the lit hour's glow tables (dw_glow_lut: const data) */
} r2_t;

/* ---- the face's state for an input */
typedef struct {
    bool set, stale;
    float ha, ma;        /* the hands' angles: they step each minute */
    float grow;          /* the hands' growth (the doors' tail; 1 settled) */
    float lift;          /* dw_r2_lift */
    uint8_t beam;        /* the beams' gain */
    uint8_t glow;        /* the rotor glow's gain */
    uint8_t hour;        /* the lit hour glow's gain (0: no lit hour) */
} r2_state_t;

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float ph(float t, float a, float b) { return clampf((t - a) / (b - a), 0, 1); }
static float out_cubic(float s) { const float u = 1 - s; return 1 - u * u * u; }
static float out_back(float s) { const float u = s - 1; return 1 + 2.4f * u * u * u + 1.4f * u * u; }
static float in_out_sine(float s)
{
    float sn, cs;
    fk_sincos(PI_F * s, &sn, &cs);
    return -(cs - 1) / 2;
}
static uint8_t a8_of(float x) { return (uint8_t)(clampf(x, 0, 1) * 255.f + 0.5f); }

float dw_r2_lift(const fk_face_in_t *in)
{
    if (!in->clk.set) return 0.f;
    if (in->reduced) return 0.5f;
    if (in->chime > 0) return clampf(in->chime, 0, 1);
    /* the ms of the day mod 2000: a minute is a whole number of 2 s periods */
    const uint32_t t = (uint32_t)(in->clk.s % 2u) * 1000u + in->clk.ms % 1000u;
    float s, c;
    fk_sincos((float)t / 2000.f * 2 * PI_F, &s, &c);
    return (s + 1) / 2;
}

static void hour_xy(int h, float *x, float *y)
{
    float s, c;
    fk_sincos((float)h * (PI_F / 6.f), &s, &c);
    *x = C + HOUR_R * s, *y = C - HOUR_R * c;
}

/* the lit hour's glow is a table about a whole pixel: the roundel's centre rounded */
static int whole(float v) { return (int)floorf(v + 0.5f); }

void dw_r2_hour_rect(int h, omni_rect_t *out)
{
    float x, y;
    hour_xy(h, &x, &y);
    const int cx = whole(x), cy = whole(y);
    const int x0 = cx - 42, y0 = cy - 42, x1 = cx + 42, y1 = cy + 42;
    *out = (omni_rect_t){(int16_t)(x0 < 0 ? 0 : x0), (int16_t)(y0 < 0 ? 0 : y0), (int16_t)(x1 > W ? W : x1),
                         (int16_t)(y1 > W ? W : y1)};
}

static void state_of(const fk_face_in_t *in, r2_state_t *st)
{
    memset(st, 0, sizeof *st);
    st->set = in->clk.set;
    st->stale = in->clk.set && in->clk.stale;
    float lance = 1, hands = 1;
    if (in->wake_ms != UINT32_MAX) {   /* revealState's lance and hands */
        const float te = dw_mockup_te(in->wake_ms);
        const float flare = out_cubic(ph(te, 520, 980)) * (1 - in_out_sine(ph(te, 1000, 2250)));
        lance = 1 + 1.6f * flare - (1 - ph(te, 400, 900));
        hands = out_back(ph(te, 980, 1560));
    }
    const float w = in->clk.set && !in->reduced && in->chime > 0 ? clampf(in->chime, 0, 1) : 0.f;
    st->grow = hands;
    st->lift = dw_r2_lift(in);
    st->beam = a8_of(lance * 0.3f + w * 0.55f);
    st->glow = a8_of(in->clk.set ? 0.18f + st->lift * 0.22f + w * 0.5f : 0.09f);
    if (!st->set) return;
    st->hour = a8_of(0.55f + w * 0.5f);
    st->ma = ((float)in->clk.m / 60.f) * 2 * PI_F;
    st->ha = (((float)(in->clk.h % 12) + (float)in->clk.m / 60.f) / 12.f) * 2 * PI_F;
}

/* ---- binding */
static bool img(fk_asset_fn get, const char *name, fk_img_t *im, uint8_t cf, int w, int h)
{
    size_t n = 0;
    const void *p = get(name, &n);
    return p && fk_img_from_bin(im, p, n) && im->cf == cf && im->w == w && im->h == h;
}

static bool r2_bind(void *ctx, fk_asset_fn get)
{
    r2_t *k = ctx;
    memset(k, 0, sizeof *k);
    k->base_ok = img(get, ASSET_DW_R2_BASE, &k->base, FK_CF_RGB565, W, W);
    k->beam_ok = img(get, ASSET_DW_R2_BEAM, &k->beam, FK_CF_A8, ASSET_DW_R2_BEAM_W, ASSET_DW_R2_BEAM_H);
    k->num_ok = img(get, ASSET_DW_R2_NUM, &k->num_img, FK_CF_A8, ASSET_DW_R2_NUM_W, ASSET_DW_R2_NUM_H);
    k->dymo_ok = img(get, ASSET_DW_DYMO, &k->dymo_img, FK_CF_RGB565A8, ASSET_DW_DYMO_W, ASSET_DW_DYMO_H);
    k->mono_ok = img(get, ASSET_DW_C1_MONO, &k->mono_img, FK_CF_RGB565A8, ASSET_DW_C1_MONO_W, ASSET_DW_C1_MONO_H);
    k->num = (fk_atlas_t){&k->num_img, DW_CELLS_DW_R2_NUM, DW_GLYPHS_DW_R2_NUM, ASSET_DW_R2_NUM_LINE_H};
    k->dymo = (fk_atlas_t){&k->dymo_img, DW_CELLS_DW_DYMO, DW_GLYPHS_DW_DYMO, ASSET_DW_DYMO_LINE_H};
    k->mono = (fk_atlas_t){&k->mono_img, DW_CELLS_DW_C1_MONO, DW_GLYPHS_DW_C1_MONO, ASSET_DW_C1_MONO_LINE_H};
    k->g70 = dw_glow_lut(70);
    k->g40 = dw_glow_lut(40);
    return k->g70 && k->g40 && k->base_ok && k->beam_ok && k->num_ok && k->dymo_ok && k->mono_ok;
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

/* local (lx, ly) about the centre, turned by (s, c) as canvas rotate() turns it */
static omni_pt_t rot(float lx, float ly, float s, float c)
{
    return (omni_pt_t){C + lx * c - ly * s, C + lx * s + ly * c};
}

/* the current hour's roundel fully lit (classicInterior 719-726): a glow of r 40 in (255, 236, 200), the disc
 * (17 x .82) #fff6e4, the ring (17 x .62, 0.9 px, black .35) and the numeral, 600 13px, #1a1712 */
static void lit_hour(const r2_t *k, int h, uint8_t glow, omni_rect_t clip, uint16_t *fb, int stride)
{
    omni_rect_t box;
    dw_r2_hour_rect(h, &box);
    if (!meets(clip, box)) return;
    float x, y;
    hour_xy(h, &x, &y);
    fk_glow_add_lut(fb, stride, &clip, whole(x), whole(y), 40, k->g40, RGB(255, 236, 200), glow);
    fk_disc(fb, stride, &clip, x, y, 17.f * 0.82f, RGB(0xff, 0xf6, 0xe4), 255);
    fk_ring(fb, stride, &clip, x, y, 17.f * 0.62f, 0.9f, RGB(0, 0, 0), 89);
    if (!k->num_ok) return;
    char t[3] = {0};
    if (h >= 10) t[0] = '1', t[1] = (char)('0' + h - 10);
    else t[0] = (char)('0' + h);
    const int w = fk_atlas_width(&k->num, t, 0);
    /* textAlign center at x; textBaseline middle at y + .5: the baseline 4.875 px lower (IBM Plex's em box) */
    fk_atlas_text_rgb(fb, stride, clip, &k->num, t, whole(x - (float)w / 2), whole(y + 0.5f + 4.875f), 0,
                      RGB(0x1a, 0x17, 0x12), 255, 0);
}

/* the rotor seen from above (classicInterior 738-753): its glow, the glass, three spheres, the inner ring and
 * the three chevrons, growing with the lift */
static void rotor(const r2_t *k, const r2_state_t *st, omni_rect_t clip, uint16_t *fb, int stride)
{
    if (!meets(clip, DW_R2_ROTOR)) return;
    const float lift = st->lift;
    fk_glow_add_lut(fb, stride, &clip, 233, 233, 70, k->g70, RGB(230, 240, 255), st->glow);
    fk_disc(fb, stride, &clip, C, C, 34.f, RGB(190, 210, 235), 20);
    fk_ring(fb, stride, &clip, C, C, 34.f, 2.f, RGB(235, 242, 255), 140);
    fk_ring(fb, stride, &clip, C, C, 30.f, 1.f, RGB(235, 242, 255), 64);
    for (int i = 0; i < 3; i++) {   /* at k turns / 3 + .5 rad, from 3 o'clock as canvas measures */
        float s, c;
        fk_sincos((float)i * (2 * PI_F / 3.f) + 0.5f, &s, &c);
        const float x = C + 25.f * c, y = C + 25.f * s;
        fk_disc(fb, stride, &clip, x, y, 3.4f, RGB(0x9d, 0xa3, 0xab), 255);
        fk_disc(fb, stride, &clip, x - 1.f, y - 1.f, 1.1f, RGB(0xee, 0xf2, 0xf7), 255);
    }
    fk_ring(fb, stride, &clip, C, C, 14.f + lift * 6.f, 1.4f, RGB(240, 246, 255), a8_of(0.45f + lift * 0.4f));
    const float sc = 0.75f + lift * 0.4f;
    for (int i = 0; i < 3; i++) {   /* rotate(turn / 6) before each: at 60, 120 and 180 degrees */
        float s, c;
        fk_sincos((float)(i + 1) * (PI_F / 3.f), &s, &c);
        const omni_pt_t t[3] = {rot(0, -11.f * sc, s, c), rot(9.f * sc, 6.f * sc, s, c),
                                rot(-9.f * sc, 6.f * sc, s, c)};
        if (i % 2) omni_fill_poly(fb, stride, &clip, t, 3, RGB(25, 25, 28), 230);
        else omni_fill_poly(fb, stride, &clip, t, 3, RGB(235, 240, 248), a8_of(0.6f + lift * 0.35f));
    }
}

/* v as decimal digits */
static void put_u(char *p, unsigned v)
{
    char t[6];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 5);
    while (n) *p++ = t[--n];
    *p = 0;
}

static const char MON[12][4] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const char DOW[7][4] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

static void r2_draw(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const r2_t *k = ctx;
    r2_state_t st;
    state_of(in, &st);
    if (k->base_ok) fk_blit(fb, stride, clip, &k->base, 0, 0, 255);
    else fill(fb, stride, clip, 0);
    if (st.set) lit_hour(k, (int)(in->clk.h % 12u) ? (int)(in->clk.h % 12u) : 12, st.hour, clip, fb, stride);
    /* the soft white beams from the column, additive */
    if (k->beam_ok && st.beam && meets(clip, DW_R2_BEAMS))   /* the layer is its lit box alone, at its origin */
        fk_add_a8(fb, stride, clip, &k->beam, ASSET_DW_R2_BEAM_X, ASSET_DW_R2_BEAM_Y, ASSET_DW_R2_BEAM_W,
                  ASSET_DW_R2_BEAM_H, BEAM_RGB, st.beam);
    if (k->dymo_ok) {   /* the date tape: "SAT 24 OCT", or TIME UNSET */
        char t[16] = "TIME UNSET";
        if (st.set) {
            memcpy(t, in->clk.wday < 7 ? DOW[in->clk.wday] : "---", 3);
            t[3] = ' ';
            put_u(t + 4, in->clk.mday > 99 ? 99u : in->clk.mday);
            const size_t n = strlen(t);
            t[n] = ' ';
            memcpy(t + n + 1, in->clk.mon < 12 ? MON[in->clk.mon] : "---", 4);
        }
        dw_tape(fb, stride, &clip, &k->dymo, t, C, TAPE_Y, true, TAPE_INK);
        /* the stale marker, under the hands: it never covers them */
        if (st.stale && meets(clip, DW_R2_STALE)) dw_stale_mark(fb, stride, &clip, &k->dymo, C, STALE_Y);
    }
    if (st.set) {
        dw_hand_room(fb, stride, &clip, st.ha, true, st.grow, 255);
        dw_hand_room(fb, stride, &clip, st.ma, false, st.grow, 255);
    }
    rotor(k, &st, clip, fb, stride);
}

/* classicSleep (757-772): the twelve hour rings, the centre ring and the round-capped hands, on the glance orbit;
 * --:-- unset. Colours at most 110 per channel (the hands' #6b6556). */
static void r2_glance(const void *ctx, const fk_face_in_t *in, omni_rect_t clip, uint16_t *fb, int stride)
{
    const r2_t *k = ctx;
    fill(fb, stride, clip, 0);
    const float dx = (float)in->odx, dy = (float)in->ody;
    for (int h = 1; h <= 12; h++) {
        float x, y;
        hour_xy(h, &x, &y);
        const uint16_t ink = h == 12 ? RGB(0x5e, 0x5a, 0x4e) : RGB(0x3c, 0x3a, 0x33);
        fk_ring_max(fb, stride, &clip, x + dx, y + dy, 9.f, 1.f, ink, 255);
    }
    fk_ring_max(fb, stride, &clip, C + dx, C + dy, 11.f, 1.f, RGB(0x3a, 0x38, 0x32), 255);
    if (!in->clk.set) {   /* 500 13px mono, #4a4740, middle at C + 40 */
        if (!k->mono_ok) return;
        const int w = fk_atlas_width(&k->mono, "--:--", 0);
        fk_atlas_text_rgb(fb, stride, clip, &k->mono, "--:--", (W - w) / 2 + in->odx, 278 + in->ody, 0,
                          RGB(0x4a, 0x47, 0x40), 255, 0);
        return;
    }
    r2_state_t st;
    state_of(in, &st);
    const uint16_t ink = RGB(0x6b, 0x65, 0x56);
    for (int hand = 0; hand < 2; hand++) {   /* lineCap round: the line and a disc at each end */
        const float a = hand ? st.ma : st.ha, r1 = hand ? 160.f : 104.f, w = hand ? 2.f : 3.f;
        float s, c;
        fk_sincos(a, &s, &c);
        const float x0 = C + dx + 14.f * s, y0 = C + dy - 14.f * c, x1 = C + dx + r1 * s, y1 = C + dy - r1 * c;
        fk_line_max(fb, stride, &clip, x0, y0, x1, y1, w, ink, 255);
        fk_disc(fb, stride, &clip, x0, y0, w / 2, ink, 255);
        fk_disc(fb, stride, &clip, x1, y1, w / 2, ink, 255);
    }
    if (st.stale)   /* the stale lamp, dim: inside the marker's rect on every orbit offset */
        fk_disc(fb, stride, &clip, 181.f + dx, STALE_Y + dy, 2.2f, RGB(96, 64, 22), 255);
}

static uint8_t full(omni_rect_t out[8])
{
    out[0] = (omni_rect_t){0, 0, W, W};
    return 1;
}

static uint8_t r2_dirty(const void *ctx, const fk_face_in_t *a, const fk_face_in_t *b, omni_rect_t out[8])
{
    (void)ctx;
    if (a->glance != b->glance || a->clk.set != b->clk.set || a->clk.stale != b->clk.stale || a->odx != b->odx ||
        a->ody != b->ody || a->wake_ms != b->wake_ms || a->reduced != b->reduced)
        return full(out);   /* the mode, the state, the orbit, the doors' tail, reduced motion */
    if (!b->clk.set) return 0;   /* nothing moves on an unset clock (the rotor at rest, no breath) */
    if (a->clk.h != b->clk.h || a->clk.m != b->clk.m || a->clk.mday != b->clk.mday || a->clk.mon != b->clk.mon ||
        a->clk.wday != b->clk.wday)
        return full(out);   /* the minute: the hands step (the hour lights, the date turns) */
    if (b->glance) return 0;   /* nothing moves in the glance within a minute */
    r2_state_t sa, sb;
    state_of(a, &sa);
    state_of(b, &sb);
    uint8_t n = 0;
    if (sa.beam != sb.beam) out[n++] = DW_R2_BEAMS;   /* the chime's breath: the beams' box holds the rotor's */
    else if (sa.lift != sb.lift || sa.glow != sb.glow) out[n++] = DW_R2_ROTOR;
    if (sa.hour != sb.hour) dw_r2_hour_rect((int)(b->clk.h % 12u) ? (int)(b->clk.h % 12u) : 12, &out[n++]);
    return n;
}

const fk_face_ops_t DW_FACE_ROOM = {
    .key = "room",
    .ctx_bytes = sizeof(r2_t),
    .cadence_ms = 40,
    .bind = r2_bind,
    .draw = r2_draw,
    .glance = r2_glance,
    .dirty = r2_dirty,
    .tap = NULL,
    .reveal = true,
    .reveal_ops = &DW_REVEAL_DOORS,   /* rd wave 1 Task 2: the doors as face_kit's reveal */
    .reveal_asset = ASSET_DW_DOORS_C,
    .reveal_arg = &DW_DOORS_OUT,
};
