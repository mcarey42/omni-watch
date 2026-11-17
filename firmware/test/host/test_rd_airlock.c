/* The airlock (rd/airlock.h; rd wave 2 Task 4, spec section 10.2): the timeline (rd_air_at), the dust, the lamp, and
 * RD_REVEAL_AIRLOCK driven as fk_view drives it (frame, the interior drawn into the frame's need at its wake_ms, then
 * every band), over Bay Mouth and the committed rd assets. The view's own parts (the jobs, the cores, the canvas
 * fallback) are face_kit's and the device's. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "rd_assets_host.h"
#include "fk/clock.h"
#include "fk/compose.h"
#include "fk/shapes.h"
#include "omni/blend565.h"
#include "rd/airlock.h"
#include "rd/assets_meta.h"
#include "rd/faces.h"
#include "rd/soft_lut.h"

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define D2R 0.017453292519943295f
#define POISON 0xF81Fu
#define FILL 0xA5A5u

static void *s_face, *s_rctx;
static uint16_t s_interior[W * W], s_a[W * W], s_b[W * W], s_c[W * W], s_leaf[W * W];

static void bind_all(void)
{
    TEST_ASSERT_TRUE(rd_assets_host_open(RD_ASSETS_DIR));
    s_face = calloc(1, RD_FACE_BAY.ctx_bytes);
    TEST_ASSERT_NOT_NULL(s_face);
    TEST_ASSERT_TRUE(RD_FACE_BAY.bind(s_face, rd_assets_host_get));
    s_rctx = calloc(1, RD_REVEAL_AIRLOCK.rctx_bytes);   /* fk_view's: rctx_bytes of zeroed PSRAM */
    TEST_ASSERT_NOT_NULL(s_rctx);
    TEST_ASSERT_TRUE(RD_REVEAL_AIRLOCK.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    /* the doors as the bind composes them, from the assets and the ramp (an independent copy) */
    size_t n = 0;
    fk_img_t d, k;
    const void *b = rd_assets_host_get(ASSET_RD_BY_DOORS, &n);
    TEST_ASSERT_TRUE(b && fk_img_from_bin(&d, b, n) && d.a && d.w == W && d.h == W);
    b = rd_assets_host_get(ASSET_RD_BY_KICK, &n);
    TEST_ASSERT_TRUE(b && fk_img_from_bin(&k, b, n) && k.rgb && k.w == W && k.h == 50);
    uint16_t ramp[256];
    rd_ramp565(RD_BY_DOOR_RAMP, RD_BY_DOOR_RAMP_STOPS, ramp);
    for (int i = 0; i < W * W; i++) s_leaf[i] = ramp[d.a[i]];
    memcpy(&s_leaf[RD_BY_KICK_Y * W], k.rgb, (size_t)W * 50 * 2);
}

static fk_face_in_t base_in(unsigned still)
{
    rd_still_t st;
    rd_still(still, &st);
    return st.in;
}

static bool empty(omni_rect_t r) { return r.x0 >= r.x1 || r.y0 >= r.y1; }

static bool on_panel(int x, int y)
{
    const int dx = 2 * x + 1 - W, dy = 2 * y + 1 - W;
    return dx * dx + dy * dy <= W * W;
}

static unsigned diff_count(const uint16_t *a, const uint16_t *b)
{
    unsigned n = 0;
    for (int i = 0; i < W * W; i++) n += a[i] != b[i];
    return n;
}

/* a still's settled frame on the panel, black off it (the reveal's band shows the panel only) */
static unsigned panel_diff(const uint16_t *got, const uint16_t *face)
{
    unsigned bad = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) bad += on_panel(x, y) ? got[y * W + x] != face[y * W + x] : got[y * W + x] != 0;
    return bad;
}

/* the input t ms into the reveal: a still's clock as it is (unix0 < 0), or the clock at unix0_ms + t (fk_view's
 * advanced(): the wall clock read then) */
static int64_t s_unix0 = -1;
static fk_face_in_t at_time(const fk_face_in_t *in0, uint32_t t)
{
    fk_face_in_t in = *in0;
    if (s_unix0 >= 0 && in.clk.set) {
        const int64_t ms = s_unix0 + t;
        fk_clock_from(ms / 1000, (uint16_t)(ms % 1000), in.clk.stale, &in.clk);
    }
    return in;
}

static fk_face_in_t s_last_in;
static bool s_last_reduced, s_rendered;
static int64_t s_last_unix0;

/* One frame at t as fk_view makes it: the plan (frame), the interior drawn into its need at the plan's wake_ms and
 * reveal_live (poison: the interior first filled with a colour no frame shows, so a band that reads outside the need
 * shows it; not on a frame that reuses the settled face the interior holds), then every band of `rows` rows over an
 * out pre-filled with 0xA5 (a pixel no band writes shows). Another input or setting than the last render's is a new
 * reveal (a fresh bind). Returns the plan. */
static fk_rframe_t render(uint32_t t, bool skip, bool reduced, const fk_face_in_t *in0, int rows, bool poison,
                          uint16_t *out)
{
    if (!s_rendered || memcmp(in0, &s_last_in, sizeof s_last_in) != 0 || reduced != s_last_reduced ||
        s_unix0 != s_last_unix0)
        TEST_ASSERT_TRUE(RD_REVEAL_AIRLOCK.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    memcpy(&s_last_in, in0, sizeof s_last_in);
    s_last_reduced = reduced, s_rendered = true, s_last_unix0 = s_unix0;
    fk_rframe_t fr;
    memset(&fr, 0, sizeof fr);
    for (int i = 0; i < W * W; i++) out[i] = FILL;
    RD_REVEAL_AIRLOCK.frame(NULL, s_rctx, t, skip, reduced, &fr);
    const bool reuse = empty(fr.need) && fr.wake_ms == UINT32_MAX;
    if (poison && !reuse)
        for (int i = 0; i < W * W; i++) s_interior[i] = POISON;
    const omni_rect_t need = poison ? fr.need : (omni_rect_t){0, 0, W, W};
    if (!empty(need)) {
        fk_face_in_t in = at_time(in0, t);
        in.wake_ms = fr.wake_ms;
        in.reveal_live = fr.live;
        in.reduced = reduced;
        if (!poison && empty(fr.need) && fr.wake_ms != UINT32_MAX) in.wake_ms = t;   /* the shut doors cover all */
        RD_FACE_BAY.draw(s_face, &in, need, s_interior + (size_t)need.y0 * W + need.x0, W);
    }
    for (int y0 = 0; y0 < W; y0 += rows) {
        const omni_rect_t b = {0, (int16_t)y0, W, (int16_t)(y0 + rows < W ? y0 + rows : W)};
        RD_REVEAL_AIRLOCK.band(NULL, s_rctx, s_interior, b, out + (size_t)y0 * W);
    }
    return fr;
}

/* a fresh reveal whose first frame (the whole interior) is behind it: the frames after it plan their own need */
static void warm(const fk_face_in_t *in, bool reduced)
{
    s_rendered = false;
    render(0, false, reduced, in, W, true, s_c);
}

static rd_air_state_t at(uint32_t t)
{
    rd_air_state_t st;
    rd_air_at(t, false, &st);
    return st;
}

/* ---- the timeline */
static void test_rd_air_timeline(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.f, at(449).cols.th_l);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 7.f, at(600).cols.th_l / D2R);
    for (uint32_t t = 601; t < 1080; t++) {
        const rd_air_state_t st = at(t);
        TEST_ASSERT_FLOAT_WITHIN(1.6001f, 7.f, st.cols.th_l / D2R);
        TEST_ASSERT_EQUAL_FLOAT(st.cols.th_l, st.cols.th_r);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 7.f, at(1080).cols.th_l / D2R);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 84.f, at(1680).cols.th_l / D2R);
    TEST_ASSERT_FALSE(at(1680).cols.done);
    TEST_ASSERT_TRUE(at(1681).cols.done);
    TEST_ASSERT_FALSE(at(1681).settled);   /* I1: the leaves' end is not the reveal's */
    /* I8: off screen from the first t at which the kind's leaf is off screen, past the gone angle and within 72 deg */
    uint32_t off = 0;
    for (uint32_t t = 1080; t <= 1680 && !off; t++)
        if (!fk_cols_leaf_on_screen_k(&RD_AIR_KIND, at(t).cols.th_l)) off = t;
    TEST_ASSERT_TRUE(off > 0);
    const float th_off = at(off).cols.th_l / D2R;
    printf("airlock: the leaves leave the screen at %u ms, theta %.3f deg\n", (unsigned)off, (double)th_off);
    TEST_ASSERT_TRUE(th_off > 67.4f && th_off < 72.f);
    for (uint32_t t = 0; t <= 2000; t++) TEST_ASSERT_EQUAL(t < off, at(t).cols.leaves);
    /* the shake */
    static const uint32_t STILL[] = {599, 901, 1079, 1401};
    for (unsigned i = 0; i < 4; i++) TEST_ASSERT_TRUE(at(STILL[i]).sx == 0 && at(STILL[i]).sy == 0);
    int jam = 0, kick = 0;
    for (uint32_t t = 0; t <= 2000; t++) {
        const rd_air_state_t st = at(t);
        const int ax = abs(st.sx), ay = abs(st.sy);
        if (t > 600 && t < 900) {
            TEST_ASSERT_TRUE(ax <= 3 && ay <= 3);
            jam = ax > jam ? ax : jam;
        } else if (t > 1080 && t < 1400) {
            TEST_ASSERT_TRUE(ax <= 5 && ay <= 5);
            kick = ax > kick ? ax : kick;
        } else {
            TEST_ASSERT_TRUE(ax == 0 && ay == 0);
        }
    }
    TEST_ASSERT_TRUE(jam >= 2 && kick >= 4);
    /* the lamp: 0 off, 1 red, 2 amber */
    static const struct { uint32_t t; uint8_t lamp; } L[] = {
        {100, 1}, {250, 0}, {360, 2}, {640, 0}, {720, 1}, {1100, 2}};
    for (unsigned i = 0; i < 6; i++) TEST_ASSERT_EQUAL_UINT8_MESSAGE(L[i].lamp, at(L[i].t).lamp, "the lamp");
    TEST_ASSERT_EQUAL_UINT8(0, at(150).beacon_a8);
    for (uint32_t t = 300; t <= 1300; t += 10) TEST_ASSERT_EQUAL_UINT8(255, at(t).beacon_a8);
    TEST_ASSERT_EQUAL_UINT8(0, at(1600).beacon_a8);
    TEST_ASSERT_EQUAL_UINT8(0, at(0).fade8);
    TEST_ASSERT_EQUAL_UINT8(255, at(160).fade8);
    for (uint32_t t = 0; t <= 2100; t += 10) {
        const rd_air_state_t st = at(t);
        TEST_ASSERT_EQUAL_UINT8(255, st.cols.fade);   /* I7: rd fades once, itself */
        TEST_ASSERT_EQUAL_UINT32(t, st.wake_ms);
        TEST_ASSERT_EQUAL_UINT8(255, st.cols.xfade);
        TEST_ASSERT_TRUE(st.cols.seam == 0 && st.cols.glow == 0 && !st.cols.keep && !st.cols.inward);
    }
    /* the starlight: through the gap while it is narrow, out by 45 deg */
    TEST_ASSERT_EQUAL_UINT8(0, at(449).gap_a8);
    TEST_ASSERT_TRUE(at(1000).gap_a8 > 0);
    TEST_ASSERT_EQUAL_UINT8(0, at(1600).gap_a8);
    /* the ops and the kind */
    TEST_ASSERT_EQUAL_UINT16(RD_AIR_MS, RD_REVEAL_AIRLOCK.len_ms);
    TEST_ASSERT_EQUAL_UINT16(RD_AIR_FADE_MS, RD_REVEAL_AIRLOCK.reduced_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_REVEAL_AIRLOCK.pop_ms);
    TEST_ASSERT_NULL(RD_REVEAL_AIRLOCK.pop_asset);
    TEST_ASSERT_TRUE(!RD_AIR_KIND.inward && !RD_AIR_KIND.keep);
    TEST_ASSERT_EQUAL_FLOAT(84.f, RD_AIR_KIND.open_deg);
    TEST_ASSERT_EQUAL_FLOAT(7.f, RD_AIR_KIND.pop_deg);
    TEST_ASSERT_EQUAL_PTR(&RD_AIR_LOOK, RD_AIR_KIND.look);
    TEST_ASSERT_EQUAL_FLOAT(12.f, RD_AIR_LOOK.edge_t);
    TEST_ASSERT_EQUAL_UINT8(6, RD_AIR_LOOK.warm_pow);
    /* a skip: the end frame */
    rd_air_state_t st;
    rd_air_skip(&st);
    TEST_ASSERT_TRUE(st.settled && st.cols.done && !st.cols.leaves && st.fade8 == 255 && st.sx == 0 && st.sy == 0);
    /* reduced: the cross-fade alone */
    rd_air_at(0, true, &st);
    TEST_ASSERT_TRUE(st.cols.xfade == 255 && st.cols.th_l == 0 && st.fade8 == 255 && st.lamp == 0 && st.beacon_a8 == 0);
    rd_air_at(RD_AIR_FADE_MS, true, &st);
    TEST_ASSERT_TRUE(st.cols.xfade == 0 && st.settled);
}

/* I1: the reveal's settled end is Bay's settle's (1950 ms), or a skip at once */
static void test_rd_air_settled_time(void)
{
    fk_rframe_t fr;
    TEST_ASSERT_TRUE(RD_REVEAL_AIRLOCK.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    s_rendered = false;
    RD_REVEAL_AIRLOCK.frame(NULL, s_rctx, 1681, false, false, &fr);
    TEST_ASSERT_FALSE(fr.settled);
    RD_REVEAL_AIRLOCK.frame(NULL, s_rctx, 1949, false, false, &fr);
    TEST_ASSERT_FALSE(fr.settled);
    TEST_ASSERT_EQUAL_UINT32(1949, fr.wake_ms);
    RD_REVEAL_AIRLOCK.frame(NULL, s_rctx, 1950, false, false, &fr);
    TEST_ASSERT_TRUE(fr.settled);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fr.wake_ms);
    TEST_ASSERT_TRUE(RD_REVEAL_AIRLOCK.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    RD_REVEAL_AIRLOCK.frame(NULL, s_rctx, 300, true, false, &fr);   /* a tap */
    TEST_ASSERT_TRUE(fr.settled);
    RD_REVEAL_AIRLOCK.frame(NULL, s_rctx, 499, false, true, &fr);   /* reduced: its cross-fade's end */
    TEST_ASSERT_FALSE(fr.settled);
    RD_REVEAL_AIRLOCK.frame(NULL, s_rctx, 500, false, true, &fr);
    TEST_ASSERT_TRUE(fr.settled);
}

/* M7: on a 40 ms grid through a whole reveal, across a second, a minute and a 2-minute boundary (the ring's step, the
 * time's digits and the star's shift), the frame drawn over an interior drawn only inside each plan's need equals the
 * frame drawn over the whole interior */
static void test_rd_air_need_covers(void)
{
    /* 10:09:59.300 local: the second rolls at t 700, 1700; the minute (and a 2-minute boundary: 10:10) at t 700 */
    static const int64_t START[] = {1793628599300LL, 1793628570500LL, 1793628419990LL};
    fk_face_in_t in0 = base_in(0);
    unsigned frames = 0, partial = 0;
    for (unsigned k = 0; k < 3; k++) {
        s_unix0 = START[k];
        const fk_face_in_t a = at_time(&in0, 0), b = at_time(&in0, 1950);
        if (k == 0) TEST_ASSERT_TRUE(a.clk.m != b.clk.m && rd_by_star_dy(&a.clk) != rd_by_star_dy(&b.clk));
        TEST_ASSERT_TRUE(a.clk.s != b.clk.s);
        s_rendered = false;
        for (uint32_t t = 0; t <= 1990; t += 40) {
            const uint32_t tt = t == 1960 ? 1950 : t;
            const fk_rframe_t fr = render(tt, false, false, &in0, 8, true, s_a);
            render(tt, false, false, &in0, 8, false, s_b);
            char what[64];
            snprintf(what, sizeof what, "start %u, t %u", k, (unsigned)tt);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_b), what);
            frames++;
            partial += !empty(fr.need) && (fr.need.x1 - fr.need.x0) < W;
        }
    }
    s_unix0 = -1;
    printf("airlock need: %u frames, %u of them a part of the interior\n", frames, partial);
    TEST_ASSERT_TRUE(partial > 10);
}

/* M6: every puff's radius is a soft table's */
static void test_rd_air_dust_radii(void)
{
    static const int SR[] = RD_SOFT_LUT_RADII;
    rd_dust_t d[RD_AIR_DUST_MAX];
    int most = 0;
    for (uint32_t t = 0; t <= 2000; t++) {
        const int n = rd_air_dust(t, d);
        TEST_ASSERT_TRUE(n >= 0 && n <= RD_AIR_DUST_MAX);
        most = n > most ? n : most;
        for (int i = 0; i < n; i++) {
            bool in = false;
            for (unsigned j = 0; j < sizeof SR / sizeof SR[0]; j++) in |= SR[j] == d[i].r;
            TEST_ASSERT_TRUE_MESSAGE(in, "a dust radius outside RD_SOFT_LUT_RADII");
            TEST_ASSERT_NOT_NULL(rd_soft_lut(d[i].r));
            TEST_ASSERT_TRUE(d[i].r >= 16 && d[i].r % 8 == 0 && d[i].a8 > 0);
        }
    }
    TEST_ASSERT_EQUAL_INT(9, most);
    TEST_ASSERT_EQUAL_INT(0, rd_air_dust(599, d));
    TEST_ASSERT_EQUAL_INT(4, rd_air_dust(900, d));
    TEST_ASSERT_EQUAL_INT(9, rd_air_dust(1100, d));
    TEST_ASSERT_EQUAL_INT(0, rd_air_dust(1700, d));
    /* the kick's middle puff stays at the centre and grows: 44 (.4 + e) to 8 px steps */
    TEST_ASSERT_EQUAL_INT(5, rd_air_dust(1600, d));   /* the jam's are gone: the kick's alone */
    TEST_ASSERT_TRUE(d[2].x == 233 && d[2].y == 230 && d[2].r >= 56);
}

/* the shake's peaks: the t of the largest |sx| and |sy| in each burst */
static void peaks(uint32_t out[4])
{
    int best[4] = {0, 0, 0, 0};
    out[0] = out[1] = out[2] = out[3] = 0;
    for (uint32_t t = 0; t <= 2000; t++) {
        const rd_air_state_t st = at(t);
        const int k = t < 1000 ? 0 : 2;
        if (abs(st.sx) > best[k]) best[k] = abs(st.sx), out[k] = t;
        if (abs(st.sy) > best[k + 1]) best[k + 1] = abs(st.sy), out[k + 1] = t;
    }
}

static void test_rd_air_band_exact(void)
{
    static const uint32_t T0[] = {0, 40, 120, 300, 650, 700, 950, 1100, 1150, 1300, 1500, 1690, 1900, 1950, 2000};
    uint32_t T[sizeof T0 / sizeof T0[0] + 4];
    memcpy(T, T0, sizeof T0);
    peaks(T + sizeof T0 / sizeof T0[0]);
    static const unsigned STILL[] = {0, 7};
    char what[64];
    for (unsigned k = 0; k < 2; k++) {
        const fk_face_in_t in = base_in(STILL[k]);
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            warm(&in, false);
            render(T[i], false, false, &in, W, true, s_a);
            warm(&in, false);
            render(T[i], false, false, &in, 8, true, s_b);
            warm(&in, false);
            render(T[i], false, false, &in, 8, false, s_c);   /* the whole interior: the need covers what bands read */
            snprintf(what, sizeof what, "still %u, t %u", STILL[k], (unsigned)T[i]);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_b), what);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_c), what);
            for (int j = 0; j < W * W; j++) TEST_ASSERT_TRUE_MESSAGE(s_b[j] != POISON && s_b[j] != FILL, what);
        }
    }
    /* out of order (a skip back, a replay): every frame is its time's alone */
    const fk_face_in_t in = base_in(0);
    s_rendered = false;
    render(950, false, false, &in, 8, true, s_a);
    render(300, false, false, &in, 8, true, s_b);
    render(1800, false, false, &in, 8, true, s_c);
    render(950, false, false, &in, 8, true, s_c);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_c));
    render(300, false, false, &in, 8, true, s_c);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_b, s_c));
}

/* I7: at each burst's largest offsets every column or row the shake uncovers is black, and no pre-fill survives */
static void test_rd_air_shake_edges(void)
{
    uint32_t T[4];
    peaks(T);
    const fk_face_in_t in = base_in(0);
    for (unsigned i = 0; i < 4; i++) {
        const rd_air_state_t st = at(T[i]);
        printf("airlock shake peak at %u ms: (%d, %d)\n", (unsigned)T[i], st.sx, st.sy);
        TEST_ASSERT_TRUE(st.sx != 0 || st.sy != 0);
        for (int rows = 8; rows <= 16; rows += 8) {
            warm(&in, false);
            render(T[i], false, false, &in, rows, true, s_a);   /* render pre-fills 0xA5 */
            unsigned stale = 0, lit = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++) {
                    const uint16_t p = s_a[y * W + x];
                    stale += p == FILL || p == POISON;
                    const bool col = st.sx > 0 ? x < st.sx : x >= W + st.sx;
                    const bool row = st.sy > 0 ? y < st.sy : y >= W + st.sy;
                    lit += (col || row) && p != 0;
                }
            TEST_ASSERT_EQUAL_UINT(0, stale);
            TEST_ASSERT_EQUAL_UINT(0, lit);
        }
    }
}

/* the end: the frame at 2000, and a tap's, are Bay's settled frame; after the leaves are done no leaf pixel shows (the
 * frame is the interior at wake_ms and the dust alone) */
static void test_rd_air_end_seam(void)
{
    static const unsigned STILL[] = {0, 1, 3, 7};
    for (unsigned k = 0; k < 4; k++) {
        const fk_face_in_t in = base_in(STILL[k]);
        rd_still_frame(&RD_FACE_BAY, s_face, STILL[k], s_b);
        s_rendered = false;
        fk_rframe_t fr = render(RD_AIR_MS, false, false, &in, 8, true, s_a);
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, panel_diff(s_a, s_b));
        s_rendered = false;
        fr = render(400, true, false, &in, 8, true, s_a);   /* a tap: the end frame */
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, panel_diff(s_a, s_b));
        s_rendered = false;
        fr = render(RD_AIR_SETTLED_MS, false, false, &in, 8, true, s_a);
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, panel_diff(s_a, s_b));
    }
    const fk_face_in_t in = base_in(0);
    static const uint32_t T[] = {1681, 1690, 1699, 1720, 1800, 1949};
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        s_rendered = false;
        render(T[i], false, false, &in, 8, true, s_a);
        fk_face_in_t f = in;
        f.wake_ms = T[i];
        f.reveal_live = true;
        RD_FACE_BAY.draw(s_face, &f, (omni_rect_t){0, 0, W, W}, s_b, W);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
                if (!on_panel(x, y)) s_b[y * W + x] = 0;
        rd_dust_t d[RD_AIR_DUST_MAX];
        const int n = rd_air_dust(T[i], d);
        const omni_rect_t all = {0, 0, W, W};
        for (int j = 0; j < n; j++)
            fk_glow_add_lut(s_b, W, &all, d[j].x, d[j].y, d[j].r, rd_soft_lut(d[j].r), RD_RGB(150, 140, 128), d[j].a8);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
                if (!on_panel(x, y)) s_b[y * W + x] = 0;
        char what[48];
        snprintf(what, sizeof what, "t %u (%d puffs)", (unsigned)T[i], n);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_b), what);
    }
}

/* reduced (and Fade): t 0 the shut doors at full weight, t 500 the settled face, t 250 a pure blend of the two */
static void test_rd_air_reduced(void)
{
    const fk_face_in_t in = base_in(0);
    s_rendered = false;
    render(0, false, true, &in, 8, true, s_a);
    TEST_ASSERT_EQUAL_UINT(0, panel_diff(s_a, s_leaf));
    fk_face_in_t rin = in;
    rin.reduced = true;
    RD_FACE_BAY.draw(s_face, &rin, (omni_rect_t){0, 0, W, W}, s_b, W);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (!on_panel(x, y)) s_b[y * W + x] = 0;
    fk_rframe_t fr = render(RD_AIR_FADE_MS, false, true, &in, 8, true, s_c);
    TEST_ASSERT_TRUE(fr.settled);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_b, s_c));
    rd_air_state_t st;
    rd_air_at(250, true, &st);
    TEST_ASSERT_TRUE(st.cols.xfade > 0 && st.cols.xfade < 255);
    render(250, false, true, &in, 8, true, s_c);
    unsigned bad = 0;
    for (int i = 0; i < W * W; i++) bad += s_c[i] != omni_blend565(s_b[i], s_a[i], st.cols.xfade);
    TEST_ASSERT_EQUAL_UINT(0, bad);   /* no shake, dust, lamp or beacon */
    s_rendered = false;
    render(0, false, false, &in, 8, true, s_c);   /* the full reveal's t 0: the shut doors at fade 0 are black */
    for (int i = 0; i < W * W; i++) TEST_ASSERT_EQUAL_HEX16(0, s_c[i]);
}

/* Stasis Release's measure (PR11): a disc pixel is static-bright at t when a channel is over 40 and it equals itself
 * 40 ms earlier; the area above half the disc for at most 600 ms in all */
static void test_rd_air_burn_in(void)
{
    const fk_face_in_t in = base_in(0);
    long disc = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) disc += on_panel(x, y);
    s_rendered = false;
    render(0, false, false, &in, 8, true, s_a);
    uint32_t over_ms = 0, worst = 0;
    for (uint32_t t = 40; t <= RD_AIR_MS; t += 40) {
        render(t, false, false, &in, 8, true, s_b);
        long n = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const int i = y * W + x;
                if (!on_panel(x, y) || s_a[i] != s_b[i]) continue;
                const uint16_t p = s_b[i];
                n += omni_ex_r(p) > 40 || omni_ex_g(p) > 40 || omni_ex_b(p) > 40;
            }
        if ((uint32_t)n > worst) worst = (uint32_t)n;
        if (2 * n > disc) over_ms += 40;
        memcpy(s_a, s_b, sizeof s_a);
    }
    printf("airlock burn-in: static bright area above half the disc for %u ms (worst frame %u px of %ld)\n",
           (unsigned)over_ms, (unsigned)worst, disc);
    TEST_ASSERT_TRUE(worst > 1000);   /* the measure sees something */
    TEST_ASSERT_TRUE(over_ms <= 600u);
}

static void test_rd_air_unset_interior(void)
{
    const fk_face_in_t in = base_in(7);
    TEST_ASSERT_FALSE(in.clk.set);
    rd_still_frame(&RD_FACE_BAY, s_face, 7, s_b);
    s_rendered = false;
    render(RD_AIR_MS, false, false, &in, 8, true, s_a);
    TEST_ASSERT_EQUAL_UINT(0, panel_diff(s_a, s_b));
    /* the doors run as usual over the unset interior: the same leaves mid-kick as a set clock's */
    s_rendered = false;
    render(1200, false, false, &in, 8, true, s_a);
    const fk_face_in_t set = base_in(0);
    s_rendered = false;
    render(1200, false, false, &set, 8, true, s_c);
    const rd_air_state_t st = at(1200);
    fk_col_t cols[W];
    fk_cols_k(&RD_AIR_KIND, st.cols.th_l, st.cols.th_r, cols);
    unsigned leaf = 0, same = 0;
    for (int x = 0; x < W; x++) {
        if (cols[x].u < 0 || x + st.sx < 0 || x + st.sx >= W) continue;
        const int y = 233 + st.sy, xs = x + st.sx;
        leaf++, same += s_a[y * W + xs] == s_c[y * W + xs];
    }
    TEST_ASSERT_TRUE(leaf > 20 && same == leaf);
}

/* the lamp sits on the left leaf: its disc's centre is rd_air_lamp_x(theta) (leafProj(0, theta)(203)), and it goes
 * from 12 deg */
static void test_rd_air_lamp_on_leaf(void)
{
    static const float TH[] = {0.f, 3.f, 7.f, 11.f};
    for (unsigned i = 0; i < 4; i++) {
        const float th = TH[i] * D2R;
        /* the projection in double, independently */
        const double s = 560.0 / (560.0 - 203.0 * sin((double)th)), want = 233.0 + (203.0 * cos((double)th) - 233.0) * s;
        TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)want, rd_air_lamp_x(th));
        for (uint8_t lamp = 1; lamp <= 2; lamp++) {
            memset(s_a, 0, sizeof s_a);
            rd_air_lamp_draw(s_a, (omni_rect_t){0, 0, W, W}, th, lamp, 0, 0);
            double sx = 0, sy = 0, n = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++)
                    if (omni_ex_r(s_a[y * W + x]) >= 250) sx += x + 0.5, sy += y + 0.5, n++;
            TEST_ASSERT_TRUE(n > 40 && n < 140);   /* the disc r 5 */
            TEST_ASSERT_FLOAT_WITHIN(0.5f, (float)want, (float)(sx / n));
            TEST_ASSERT_FLOAT_WITHIN(0.5f, (float)RD_AIR_LAMP_Y, (float)(sy / n));
        }
    }
    memset(s_a, 0, sizeof s_a);
    rd_air_lamp_draw(s_a, (omni_rect_t){0, 0, W, W}, 12.f * D2R, 1, 0, 0);
    rd_air_lamp_draw(s_a, (omni_rect_t){0, 0, W, W}, 3.f * D2R, 0, 0, 0);
    for (int i = 0; i < W * W; i++) TEST_ASSERT_EQUAL_HEX16(0, s_a[i]);
    /* in a frame: lit before 12 deg (t 180, red, the fade up), gone after (t 1100: theta > 12 deg, the lamp amber) */
    const fk_face_in_t in = base_in(0);
    warm(&in, false);
    render(180, false, false, &in, 8, true, s_a);
    const int lx = (int)(rd_air_lamp_x(0) + 0.5f);
    const uint16_t lp = s_a[RD_AIR_LAMP_Y * W + lx];   /* the red disc and its glow added */
    TEST_ASSERT_TRUE(omni_ex_r(lp) >= 250 && omni_ex_g(lp) < 140 && omni_ex_b(lp) < 100);
    const rd_air_state_t st = at(1100);
    TEST_ASSERT_TRUE(st.cols.th_l > 12.f * D2R && st.lamp == 2);
    warm(&in, false);
    render(1100, false, false, &in, 8, true, s_a);
    /* where the lamp would be: no amber disc (red 255, green 168 and more with its glow; the leaf's warm rim is
     * (255, 110, 60) at most .5, the starlight's green under 40) */
    const int ax = (int)(rd_air_lamp_x(st.cols.th_l) + 0.5f) + st.sx, ay = RD_AIR_LAMP_Y + st.sy;
    unsigned hot = 0, seen = 0;
    for (int y = ay - 20; y <= ay + 20; y++)
        for (int x = ax - 20; x <= ax + 20; x++) {
            const uint16_t p = s_a[y * W + x];
            seen++, hot += omni_ex_r(p) > 240 && omni_ex_g(p) > 140 && omni_ex_b(p) < 120;
        }
    TEST_ASSERT_EQUAL_UINT(0, hot);
    TEST_ASSERT_TRUE(seen > 1000);
    /* the positive control (review M2): t 1079, theta under 12 deg, the red lamp on and no shake: the same box finds
     * the red disc (red 255 with its glow, green and blue low) */
    const rd_air_state_t s2 = at(1079);
    TEST_ASSERT_TRUE(s2.cols.th_l < 12.f * D2R && s2.lamp == 1 && s2.sx == 0 && s2.sy == 0);
    warm(&in, false);
    render(1079, false, false, &in, 8, true, s_a);
    const int bx = (int)(rd_air_lamp_x(s2.cols.th_l) + 0.5f);
    unsigned red = 0;
    for (int y = RD_AIR_LAMP_Y - 20; y <= RD_AIR_LAMP_Y + 20; y++)
        for (int x = bx - 20; x <= bx + 20; x++) {
            const uint16_t p = s_a[y * W + x];
            red += omni_ex_r(p) > 240 && omni_ex_g(p) < 140 && omni_ex_b(p) < 100;
        }
    TEST_ASSERT_TRUE(red > 40);
}

/* review I1: the rim light is airR's outCubic(ph(t, 450, 700)), carried by the frame's kind (rd_air_kind_at): the
 * free-edge column's warm (drawLeaf's light x f^6 x .5 at f = 1) follows it, not theta / 7 deg, and the reveal's band
 * draws its leaves with that kind */
static void test_rd_air_rim_light(void)
{
    static const uint32_t T[] = {500, 513, 550, 600, 650, 690, 720, 900};
    fk_col_t cols[W];
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        const rd_air_state_t st = at(T[i]);
        const float th = st.cols.th_l;
        TEST_ASSERT_TRUE(th > 0);
        fk_cols_kind_t k;
        rd_air_kind_at(T[i], th, &k);
        TEST_ASSERT_TRUE(k.open_deg == RD_AIR_KIND.open_deg && k.look == RD_AIR_KIND.look && !k.inward);
        const float u = 1.f - ((float)T[i] - 450.f) / 250.f, L = T[i] >= 700 ? 1.f : 1.f - u * u * u;
        fk_cols_k(&k, th, th, cols);
        int best = -1;
        for (int x = 0; x < 233; x++)
            if (cols[x].u >= 0 && (best < 0 || cols[x].u > cols[best].u)) best = x;
        TEST_ASSERT_TRUE(best >= 0 && cols[best].u == 232);   /* f = 1 */
        char what[48];
        snprintf(what, sizeof what, "t %u: light %.3f", (unsigned)T[i], (double)L);
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, (int)(0.5f * L * 255.f + 0.5f), cols[best].warm, what);
    }
    /* the reveal's band draws with that kind: at t 500 (no shake, no dust, the beacon left of x 222, the starlight
     * within 18 px of the seam) the right leaf, x 256 on, is fk_cols_band's with the frame's kind, and not with
     * RD_AIR_KIND's own pop (theta / 7 deg: .70 against airR's .49 there) */
    {
        const fk_face_in_t in = base_in(0);
        warm(&in, false);
        render(500, false, false, &in, 8, true, s_a);
        const rd_air_state_t st = at(500);
        fk_cols_kind_t kf;
        rd_air_kind_at(500, st.cols.th_l, &kf);
        const fk_cols_kind_t *K[2] = {&kf, &RD_AIR_KIND};
        unsigned diff[2] = {0, 0}, n = 0;
        for (int v = 0; v < 2; v++) {
            fk_cols_k(K[v], st.cols.th_l, st.cols.th_r, cols);
            for (int y0 = 0; y0 < W; y0 += 8) {
                const omni_rect_t b = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
                fk_cols_band(K[v], &st.cols, cols, s_leaf, s_interior, NULL, b, s_b + (size_t)y0 * W);
            }
            for (int y = 0; y < W; y++)
                for (int x = 256; x < W; x++) {
                    if (!on_panel(x, y)) continue;
                    n += v == 0, diff[v] += s_a[y * W + x] != s_b[y * W + x];
                }
        }
        TEST_ASSERT_TRUE(n > 30000);
        TEST_ASSERT_EQUAL_UINT(0, diff[0]);
        TEST_ASSERT_TRUE(diff[1] > 1000);
    }
    /* shut: the kind's own pop (no light); the jam's dips no longer dim it (theta / 7 deg gave .89 at 690 ms) */
    fk_cols_kind_t k;
    rd_air_kind_at(300, 0.f, &k);
    TEST_ASSERT_EQUAL_FLOAT(RD_AIR_KIND.pop_deg, k.pop_deg);
    TEST_ASSERT_TRUE(at(690).cols.th_l < 7.f * D2R);
}

/* review M3: the opening fade's value: before the crack, the frame is the shut doors (with the red lamp) at fade8,
 * omni_blend565(black, frame, fade8) on every pixel */
static void test_rd_air_fade_value(void)
{
    const fk_face_in_t in = base_in(0);
    static const uint32_t T[] = {40, 120};
    for (unsigned i = 0; i < 2; i++) {
        const rd_air_state_t st = at(T[i]);
        TEST_ASSERT_TRUE(st.fade8 > 0 && st.fade8 < 255 && st.lamp == 1 && st.beacon_a8 == 0);
        warm(&in, false);
        render(T[i], false, false, &in, 8, true, s_a);
        memcpy(s_b, s_leaf, sizeof s_b);
        rd_air_lamp_draw(s_b, (omni_rect_t){0, 0, W, W}, 0.f, 1, 0, 0);
        unsigned bad = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const uint16_t want = on_panel(x, y) ? omni_blend565(0, s_b[y * W + x], st.fade8) : 0;
                bad += s_a[y * W + x] != want;
            }
        TEST_ASSERT_EQUAL_UINT(0, bad);
    }
}

static unsigned s_gets;
static const void *counting_get(const char *name, size_t *size)
{
    s_gets++;
    return rd_assets_host_get(name, size);
}

/* M18: the bind's reads fit the view's copy, and rctx_bytes covers the whole rctx the bands read */
static void test_rd_air_bind_count(void)
{
    void *rctx = calloc(1, RD_REVEAL_AIRLOCK.rctx_bytes);
    TEST_ASSERT_NOT_NULL(rctx);
    s_gets = 0;
    TEST_ASSERT_TRUE(RD_REVEAL_AIRLOCK.bind(NULL, NULL, counting_get, rctx));
    TEST_ASSERT_EQUAL_UINT(2, s_gets);   /* the doors and the kick plate */
    TEST_ASSERT_TRUE(s_gets <= FK_REVEAL_BIND_ASSETS_MAX);
    free(rctx);
    const size_t parts = (size_t)W * W * 2 + W * sizeof(fk_col_t) + sizeof(rd_air_state_t) +
                         RD_AIR_DUST_MAX * sizeof(rd_dust_t);
    printf("airlock rctx: %u B (the leaf, columns, state and dust: %u B)\n", (unsigned)RD_REVEAL_AIRLOCK.rctx_bytes,
           (unsigned)parts);
    TEST_ASSERT_TRUE(RD_REVEAL_AIRLOCK.rctx_bytes >= parts);
    TEST_ASSERT_TRUE(RD_REVEAL_AIRLOCK.rctx_bytes <= 445000u);
}

/* the wiring, the bind's leaf, the ramp against the table's row, and the soft tables (const, softSprite's profile) */
static bool read_only_mapping(const void *p)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[512];
    bool ro = false, found = false;
    while (fgets(line, sizeof line, f)) {
        unsigned long a, b;
        char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &a, &b, perm) != 3) continue;
        if ((unsigned long)p >= a && (unsigned long)p < b) {
            found = true;
            ro = perm[1] != 'w';
            break;
        }
    }
    fclose(f);
    return found && ro;
}

static void test_rd_air_face(void)
{
    TEST_ASSERT_TRUE(RD_FACE_BAY.reveal);
    TEST_ASSERT_EQUAL_PTR(&RD_REVEAL_AIRLOCK, RD_FACE_BAY.reveal_ops);
    TEST_ASSERT_NULL(RD_FACE_BAY.reveal_asset);
    /* the bind's leaf: the shut doors at t 160 (the fade up, before the crack) are the composed leaf wherever no lamp
     * and no beacon falls (below the lamp's glow; right of the beacon's wash, centred at 233 + 300 sin(160 / 260 2 pi)
     * = 32 and 120 px wide) */
    const fk_face_in_t in = base_in(0);
    warm(&in, false);
    render(160, false, false, &in, 8, true, s_a);
    unsigned bad = 0, n = 0;
    for (int y = 110; y < W; y++)
        for (int x = 160; x < W; x++) {
            if (!on_panel(x, y)) continue;
            n++, bad += s_a[y * W + x] != s_leaf[y * W + x];
        }
    TEST_ASSERT_TRUE(n > 50000);
    TEST_ASSERT_EQUAL_UINT(0, bad);
    /* the ramp's stops are the table row's (tools/export/rd_layers.json; the export indexes against them) */
    FILE *f = fopen(RD_LAYERS_JSON, "r");
    TEST_ASSERT_NOT_NULL(f);
    static char js[1 << 17];
    const size_t len = fread(js, 1, sizeof js - 1, f);
    fclose(f);
    js[len] = 0;
    const char *row = strstr(js, "\"name\": \"rd_by_doors\"");
    TEST_ASSERT_NOT_NULL(row);
    const char *rp = strstr(row, "\"ramp\": [");
    TEST_ASSERT_NOT_NULL(rp);
    rp += strlen("\"ramp\": [");
    for (int k = 0; k < RD_BY_DOOR_RAMP_STOPS; k++) {
        unsigned i = 0, c = 0;
        TEST_ASSERT_EQUAL_INT(2, sscanf(rp, " [%u, \"#%6x\"]", &i, &c));
        TEST_ASSERT_EQUAL_UINT(RD_BY_DOOR_RAMP[k].i, i);
        TEST_ASSERT_EQUAL_HEX32((unsigned)RD_BY_DOOR_RAMP[k].r << 16 | (unsigned)RD_BY_DOOR_RAMP[k].g << 8 |
                                    RD_BY_DOOR_RAMP[k].b,
                                c);
        rp = strchr(rp, ']') + 1;
        if (*rp == ',') rp++;
    }
    TEST_ASSERT_EQUAL_CHAR(']', *rp);
    TEST_ASSERT_NOT_NULL(strstr(js, "\"name\": \"rd_by_kick\""));
    /* the soft tables the airlock draws: const, softSprite's profile; rd_glow_lut keeps the other lists' 16 and 26 */
    static const int R[] = {16, 24, 26, 32, 40, 48, 56, 64};
    static uint8_t lut[FK_GLOW_LUT_MAX];
    for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
        const uint8_t *t = rd_soft_lut(R[i]);
        TEST_ASSERT_NOT_NULL(t);
        TEST_ASSERT_TRUE(read_only_mapping(t));
        rd_soft_lut_build(lut, R[i]);
        TEST_ASSERT_EQUAL_MEMORY(lut, t, FK_GLOW_LUT_N(R[i]));
    }
    fk_glow_lut_build(lut, 16);
    TEST_ASSERT_EQUAL_MEMORY(lut, rd_glow_lut(16), FK_GLOW_LUT_N(16));
    rd_qtr_lut_build(lut, 26);
    TEST_ASSERT_EQUAL_MEMORY(lut, rd_glow_lut(26), FK_GLOW_LUT_N(26));
    TEST_ASSERT_TRUE(rd_soft_lut(16) != rd_glow_lut(16) && rd_soft_lut(26) != rd_glow_lut(26));
    TEST_ASSERT_NULL(rd_soft_lut(14));
    TEST_ASSERT_NULL(rd_soft_lut(46));
}

/* `rd reveal bench`'s windows: each frame's wall time by the airlock's window, in rd_rel_timing's accumulator */
static void test_rd_air_timing(void)
{
    TEST_ASSERT_TRUE(RD_REVEAL_AIRLOCK.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    s_rendered = false;
    rd_rel_timing_reset();
    fk_rframe_t fr;
    static const uint32_t T[] = {0, 60, 440, 500, 1070, 1100, 1670, 1700, 1950};
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) RD_REVEAL_AIRLOCK.frame(NULL, s_rctx, T[i], false, false, &fr);
    rd_rel_timing_t tm;
    rd_rel_timing_get(&tm);
    TEST_ASSERT_EQUAL_UINT32(3, tm.frames[0]);   /* 0, 60, 440 */
    TEST_ASSERT_EQUAL_UINT32(2, tm.frames[1]);   /* 500, 1070 */
    TEST_ASSERT_EQUAL_UINT32(2, tm.frames[2]);   /* 1100, 1670 */
    TEST_ASSERT_EQUAL_UINT32(1, tm.frames[3]);   /* 1700 (to 1950) */
    TEST_ASSERT_EQUAL_UINT32(60 + 380 + 60, tm.ms[0]);
    TEST_ASSERT_EQUAL_UINT32(570, tm.max_ms[1]);
    TEST_ASSERT_EQUAL_UINT32(250, tm.ms[3]);
    rd_rel_timing_reset();
}

int main(void)
{
    UNITY_BEGIN();
    bind_all();
    RUN_TEST(test_rd_air_timeline);
    RUN_TEST(test_rd_air_settled_time);
    RUN_TEST(test_rd_air_need_covers);
    RUN_TEST(test_rd_air_dust_radii);
    RUN_TEST(test_rd_air_band_exact);
    RUN_TEST(test_rd_air_shake_edges);
    RUN_TEST(test_rd_air_end_seam);
    RUN_TEST(test_rd_air_reduced);
    RUN_TEST(test_rd_air_burn_in);
    RUN_TEST(test_rd_air_unset_interior);
    RUN_TEST(test_rd_air_lamp_on_leaf);
    RUN_TEST(test_rd_air_bind_count);
    RUN_TEST(test_rd_air_face);
    RUN_TEST(test_rd_air_timing);
    RUN_TEST(test_rd_air_rim_light);
    RUN_TEST(test_rd_air_fade_value);
    free(s_rctx);
    free(s_face);
    rd_assets_host_close();
    return UNITY_END();
}
