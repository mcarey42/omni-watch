/* The hull pass (rd/pass.h; rd wave 2 Task 5, spec section 10.3 as ruling R166 has it): the timeline (rd_pass_at) and
 * RD_REVEAL_PASS driven as fk_view drives it (frame, the interior drawn into the frame's need at its wake_ms, then every
 * band), over Drift Plate (which draws the pass itself) and the committed rd assets. The view's own parts (the jobs, the
 * cores, the claim and its generation, the whole-frame fallback) are face_kit's and the device's, unchanged here. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "rd_assets_host.h"
#include "fk/clock.h"
#include "fk/compose.h"
#include "fk/ease.h"
#include "omni/blend565.h"
#include "rd/assets_meta.h"
#include "rd/faces.h"
#include "rd/pass.h"
#include "rd/release.h"

static int64_t s_unix0 = -1;
void setUp(void) { s_unix0 = -1; }   /* each case starts on the stills' own clocks */
void tearDown(void) {}

#define W 466
#define POISON 0xF81Fu
#define FILL 0xA5A5u

static void *s_face, *s_rctx;
static uint16_t s_interior[W * W], s_ref[W * W], s_a[W * W], s_b[W * W], s_c[W * W];

static void bind_all(void)
{
    TEST_ASSERT_TRUE(rd_assets_host_open(RD_ASSETS_DIR));
    s_face = calloc(1, RD_FACE_DRIFT.ctx_bytes);
    TEST_ASSERT_NOT_NULL(s_face);
    TEST_ASSERT_TRUE(RD_FACE_DRIFT.bind(s_face, rd_assets_host_get));
    s_rctx = calloc(1, RD_REVEAL_PASS.rctx_bytes);   /* fk_view's: rctx_bytes of zeroed PSRAM */
    TEST_ASSERT_NOT_NULL(s_rctx);
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
}

static fk_face_in_t base_in(unsigned still)
{
    rd_still_t st;
    rd_still(still, &st);
    return st.in;
}

static bool empty(omni_rect_t r) { return r.x0 >= r.x1 || r.y0 >= r.y1; }

static unsigned diff_count(const uint16_t *a, const uint16_t *b)
{
    unsigned n = 0;
    for (int i = 0; i < W * W; i++) n += a[i] != b[i];
    return n;
}

static bool inside(const omni_rect_t *r, int x, int y) { return x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1; }

/* the input t ms into the reveal: a still's clock as it is (unix0 < 0), or the clock at unix0_ms + t (fk_view's
 * advanced(): the wall clock read then) */
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
 * reveal_live, then every band of `rows` rows over an out pre-filled with 0xA5 (a pixel no band writes shows). With
 * poison the interior is fk_view's own buffer: it keeps what earlier frames drew (the pass reuses it outside each
 * frame's need) and is filled at each new reveal with a colour no frame shows, so a pixel the plans never drew shows
 * it. Without poison a separate reference interior is drawn whole at the plan's wake_ms. Another input or setting than
 * the last render's is a new reveal (a fresh bind). */
static fk_rframe_t render(uint32_t t, bool skip, bool reduced, const fk_face_in_t *in0, int rows, bool poison,
                          uint16_t *out)
{
    if (!s_rendered || memcmp(in0, &s_last_in, sizeof s_last_in) != 0 || reduced != s_last_reduced ||
        s_unix0 != s_last_unix0) {
        TEST_ASSERT_TRUE(RD_REVEAL_PASS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
        for (int i = 0; i < W * W; i++) s_interior[i] = POISON;
    }
    memcpy(&s_last_in, in0, sizeof s_last_in);
    s_last_reduced = reduced, s_rendered = true, s_last_unix0 = s_unix0;
    fk_rframe_t fr;
    memset(&fr, 0, sizeof fr);
    for (int i = 0; i < W * W; i++) out[i] = FILL;
    RD_REVEAL_PASS.frame(NULL, s_rctx, t, skip, reduced, &fr);
    uint16_t *interior = poison ? s_interior : s_ref;
    const omni_rect_t need = poison ? fr.need : (omni_rect_t){0, 0, W, W};
    if (!empty(need)) {
        fk_face_in_t in = at_time(in0, t);
        in.wake_ms = fr.wake_ms;
        in.reveal_live = fr.live;
        in.reduced = reduced;
        RD_FACE_DRIFT.draw(s_face, &in, need, interior + (size_t)need.y0 * W + need.x0, W);
    }
    for (int y0 = 0; y0 < W; y0 += rows) {
        const omni_rect_t b = {0, (int16_t)y0, W, (int16_t)(y0 + rows < W ? y0 + rows : W)};
        RD_REVEAL_PASS.band(NULL, s_rctx, interior, b, out + (size_t)y0 * W);
    }
    return fr;
}

static rd_pass_state_t at(uint32_t t)
{
    rd_pass_state_t st;
    rd_pass_at(t, false, &st);
    return st;
}

/* ---- the timeline (hullR) */
static void test_rd_pass_timeline(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2700.f, at(0).off);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.f, at(1500).off);
    float lo = 1e9f;
    uint32_t lo_t = 0;
    for (uint32_t t = 1500; t <= 1800; t++) {
        const float o = at(t).off;
        if (o < lo) lo = o, lo_t = t;
        if (t > 1500 && t < 1800) TEST_ASSERT_TRUE_MESSAGE(o < 0.f, "the overshoot runs past the plate");
    }
    printf("pass: the overshoot's least offset %.3f px at %u ms\n", (double)lo, (unsigned)lo_t);
    TEST_ASSERT_TRUE(lo > -7.f && lo < -4.f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, at(1800).off);
    for (uint32_t t = 1801; t <= RD_PASS_MS; t += 7) TEST_ASSERT_EQUAL_FLOAT(0.f, at(t).off);
    for (uint32_t t = 0; t < 1500; t += 10) TEST_ASSERT_TRUE(at(t).off > at(t + 10).off);   /* it slows, never stops */
    TEST_ASSERT_EQUAL_UINT8(0, at(1450).lights8);
    TEST_ASSERT_EQUAL_UINT8(128, at(1700).lights8);
    TEST_ASSERT_EQUAL_UINT8(255, at(1950).lights8);
    TEST_ASSERT_EQUAL_UINT8(0, at(1250).settle8);
    TEST_ASSERT_EQUAL_UINT8(255, at(2000).settle8);
    TEST_ASSERT_TRUE(at(1999).settle8 >= 254);
    TEST_ASSERT_EQUAL_UINT8(0, at(0).fade8);
    TEST_ASSERT_EQUAL_UINT8(128, at(100).fade8);
    TEST_ASSERT_EQUAL_UINT8(255, at(200).fade8);
    for (uint32_t t = 0; t < 200; t++) TEST_ASSERT_TRUE(at(t).fade8 <= at(t + 1).fade8);
    TEST_ASSERT_FALSE(at(1800).done);
    for (uint32_t t = 1801; t <= RD_PASS_MS; t++) TEST_ASSERT_TRUE(at(t).done);
    TEST_ASSERT_FALSE(at(1999).settled);
    TEST_ASSERT_TRUE(at(2000).settled);
    TEST_ASSERT_TRUE(at(RD_PASS_MS).settled);
    /* reduced: off 0, lights and settle full, fade outCubic(ph(t, 0, 500)), settled at 500 */
    rd_pass_state_t r;
    rd_pass_at(0, true, &r);
    TEST_ASSERT_TRUE(r.off == 0.f && r.lights8 == 255 && r.settle8 == 255 && r.fade8 == 0 && !r.settled);
    rd_pass_at(250, true, &r);
    TEST_ASSERT_EQUAL_UINT8(fk_ease_to8(fk_ease_out_cubic(0.5f)), r.fade8);
    TEST_ASSERT_FALSE(r.settled);
    rd_pass_at(RD_PASS_FADE_MS, true, &r);
    TEST_ASSERT_TRUE(r.fade8 == 255 && r.settled);
    rd_pass_skip(&r);
    TEST_ASSERT_TRUE(r.off == 0.f && r.lights8 == 255 && r.settle8 == 255 && r.fade8 == 255 && r.done && r.settled);
}

/* I1: frame() reports settled from 2000 ms, and at once on a skip */
static void test_rd_pass_settled_time(void)
{
    fk_rframe_t fr;
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    RD_REVEAL_PASS.frame(NULL, s_rctx, 0, false, false, &fr);
    TEST_ASSERT_FALSE(fr.settled);
    RD_REVEAL_PASS.frame(NULL, s_rctx, 1999, false, false, &fr);
    TEST_ASSERT_FALSE(fr.settled);
    TEST_ASSERT_EQUAL_UINT32(1999, fr.wake_ms);
    RD_REVEAL_PASS.frame(NULL, s_rctx, 2000, false, false, &fr);
    TEST_ASSERT_TRUE(fr.settled);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fr.wake_ms);
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    RD_REVEAL_PASS.frame(NULL, s_rctx, 120, true, false, &fr);   /* a tap */
    TEST_ASSERT_TRUE(fr.settled);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fr.wake_ms);
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    RD_REVEAL_PASS.frame(NULL, s_rctx, 499, false, true, &fr);
    TEST_ASSERT_FALSE(fr.settled);
    RD_REVEAL_PASS.frame(NULL, s_rctx, 500, false, true, &fr);
    TEST_ASSERT_TRUE(fr.settled);
}

/* the frame built in 8-row bands (and 6-row ones) equals the frame built whole */
static void test_rd_pass_band_exact(void)
{
    static const uint32_t T[] = {100, 600, 1200, 1600, 1900};
    static const unsigned STILLS[] = {0, 3, 7, 8};
    for (unsigned s = 0; s < sizeof STILLS / sizeof STILLS[0]; s++)
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            const fk_face_in_t in = base_in(STILLS[s]);
            s_rendered = false;
            render(T[i], false, false, &in, W, false, s_a);
            s_rendered = false;
            render(T[i], false, false, &in, 8, false, s_b);
            char what[48];
            snprintf(what, sizeof what, "still %u, t %u", STILLS[s], (unsigned)T[i]);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_b), what);
            s_rendered = false;
            render(T[i], false, false, &in, 6, false, s_b);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_b), what);
        }
}

/* M7: on a 40 ms grid through a whole pass, across a second, a minute, a 10 s drift boundary and midnight (the date),
 * the frame drawn over an interior drawn only inside each plan's need equals the frame drawn over the whole interior */
static void test_rd_pass_need_covers(void)
{
    /* local times on 2 Nov 2026 (mktime: the host's zone): 10:09:59.300, the second, the minute and the drift's step
     * (10:10:00) at t 700, the second again at 1700; 10:09:35.500, the second at 500 and 1500 (the windows lit from
     * 1450); 10:09:48.200, the drift's step at 1800; 23:59:59.300, the date at 700 */
    static const int HMS[4][4] = {{10, 9, 59, 300}, {10, 9, 35, 500}, {10, 9, 48, 200}, {23, 59, 59, 300}};
    int64_t START[4];
    for (int k = 0; k < 4; k++) {
        struct tm tm = {0};
        tm.tm_year = 126, tm.tm_mon = 10, tm.tm_mday = 2, tm.tm_hour = HMS[k][0], tm.tm_min = HMS[k][1];
        tm.tm_sec = HMS[k][2], tm.tm_isdst = -1;
        START[k] = (int64_t)mktime(&tm) * 1000 + HMS[k][3];
    }
    unsigned frames = 0, partial = 0;
    for (unsigned k = 0; k < sizeof START / sizeof START[0]; k++)
        for (int st = 0; st < 3; st++) {
            fk_face_in_t in0 = base_in(st == 2 ? 3 : 0);   /* 24 h; stale; 12 h */
            in0.clk.stale = st == 1;
            s_unix0 = START[k];
            const fk_face_in_t a = at_time(&in0, 0), b = at_time(&in0, 1990);
            TEST_ASSERT_TRUE(a.clk.s != b.clk.s);
            if (k == 0) TEST_ASSERT_TRUE(a.clk.m != b.clk.m && rd_dp_drift(&a.clk) != rd_dp_drift(&b.clk));
            if (k == 2) TEST_ASSERT_TRUE(rd_dp_drift(&a.clk) != rd_dp_drift(&b.clk));
            if (k == 3) TEST_ASSERT_TRUE(a.clk.mday != b.clk.mday);
            s_rendered = false;
            for (uint32_t t = 0; t <= RD_PASS_MS + 40; t += 40) {
                const uint32_t tt = t > RD_PASS_MS ? RD_PASS_MS : t;
                const fk_rframe_t fr = render(tt, false, false, &in0, 8, true, s_a);
                render(tt, false, false, &in0, 8, false, s_b);
                char what[64];
                snprintf(what, sizeof what, "start %u state %d, t %u", k, st, (unsigned)tt);
                TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_b), what);
                frames++;
                partial += !empty(fr.need) && (fr.need.y1 - fr.need.y0) < W;
            }
        }
    s_unix0 = -1;
    printf("pass need: %u frames, %u of them a part of the interior\n", frames, partial);
    TEST_ASSERT_TRUE(partial > 400);
}

/* C13: the stars stay still. From 200 ms (the fade done) no pixel outside the band, the windows, the label rects and
 * the time changes between one frame and the next */
static void test_rd_pass_stars_still(void)
{
    const omni_rect_t ok[] = {RD_DP_BAND, RD_DP_WINDOWS, RD_DP_STALE, RD_DP_DATE, RD_DP_CAPTION, RD_DP_TIME};
    const fk_face_in_t in = base_in(0);
    s_rendered = false;
    render(160, false, false, &in, 8, true, s_c);
    render(200, false, false, &in, 8, true, s_a);
    long moved = 0;
    for (uint32_t t = 240; t <= RD_PASS_MS; t += 40) {
        render(t, false, false, &in, 8, true, s_b);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const int i = y * W + x;
                if (s_a[i] == s_b[i]) continue;
                bool in_one = false;
                for (unsigned q = 0; q < sizeof ok / sizeof ok[0] && !in_one; q++) in_one = inside(&ok[q], x, y);
                if (!in_one) {
                    char m[96];
                    snprintf(m, sizeof m, "t %u: pixel (%d, %d) changed outside the band, windows and labels",
                             (unsigned)t, x, y);
                    TEST_FAIL_MESSAGE(m);
                }
                moved++;
            }
        memcpy(s_a, s_b, sizeof s_a);
    }
    TEST_ASSERT_TRUE(moved > 100000);   /* the band does move */
    long stars = 0;   /* and there are stars outside it */
    for (int y = 0; y < RD_DP_BY; y++)
        for (int x = 0; x < W; x++) stars += s_b[y * W + x] != 0 && !inside(&RD_DP_STALE, x, y) &&
                                             !inside(&RD_DP_DATE, x, y);
    TEST_ASSERT_TRUE(stars > 100);
}

/* the frame at 2050, at 2000, and after a skip equals the settled frame byte for byte (the band copies the whole frame:
 * no panel mask), on stills 0, 3, 7 and 8 */
static void test_rd_pass_end_seam(void)
{
    static const unsigned STILL[] = {0, 3, 7, 8};
    for (unsigned k = 0; k < sizeof STILL / sizeof STILL[0]; k++) {
        const fk_face_in_t in = base_in(STILL[k]);
        rd_still_frame(&RD_FACE_DRIFT, s_face, STILL[k], s_b);
        s_rendered = false;
        fk_rframe_t fr = render(RD_PASS_MS, false, false, &in, 8, true, s_a);
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
        s_rendered = false;
        fr = render(300, true, false, &in, 8, true, s_a);   /* a tap: the end frame */
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
        s_rendered = false;
        fr = render(RD_PASS_SETTLED_MS, false, false, &in, 8, true, s_a);
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
        /* the settled end after a run of frames: the frame that lands on 2000 after 1960 */
        s_rendered = false;
        render(1960, false, false, &in, 8, true, s_c);
        fr = render(2000, false, false, &in, 8, true, s_a);
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
        fr = render(2040, false, false, &in, 8, true, s_a);   /* then reused: no redraw */
        TEST_ASSERT_TRUE(empty(fr.need));
        TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
    }
}

/* reduced (and Fade): t 0 black, t 500 the settled face, t 250 a pure fade of it */
static void test_rd_pass_reduced(void)
{
    const fk_face_in_t in = base_in(0);
    s_rendered = false;
    render(0, false, true, &in, 8, true, s_a);
    for (int i = 0; i < W * W; i++) TEST_ASSERT_EQUAL_HEX16(0, s_a[i]);
    fk_face_in_t rin = in;
    rin.reduced = true;
    RD_FACE_DRIFT.draw(s_face, &rin, (omni_rect_t){0, 0, W, W}, s_b, W);
    fk_rframe_t fr = render(RD_PASS_FADE_MS, false, true, &in, 8, true, s_c);
    TEST_ASSERT_TRUE(fr.settled);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_b, s_c));
    rd_pass_state_t st;
    rd_pass_at(250, true, &st);
    TEST_ASSERT_TRUE(st.fade8 > 0 && st.fade8 < 255);
    s_rendered = false;
    render(250, false, true, &in, 8, true, s_c);
    unsigned bad = 0;
    for (int i = 0; i < W * W; i++) bad += s_c[i] != omni_blend565(0, s_b[i], st.fade8);
    TEST_ASSERT_EQUAL_UINT(0, bad);   /* no pass, no offset: the settled face, faded */
    /* the full reveal's t 0: the pass at fade 0 is black */
    s_rendered = false;
    render(0, false, false, &in, 8, true, s_c);
    for (int i = 0; i < W * W; i++) TEST_ASSERT_EQUAL_HEX16(0, s_c[i]);
}

/* Stasis Release's measure (PR11): a disc pixel is static-bright at t when a channel is over 40 and it equals itself
 * 40 ms earlier; the area above half the disc for at most 200 ms in all */
static void test_rd_pass_burn_in(void)
{
    const fk_face_in_t in = base_in(0);
    long disc = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const int dx = 2 * x + 1 - W, dy = 2 * y + 1 - W;
            disc += dx * dx + dy * dy <= W * W;
        }
    s_rendered = false;
    render(0, false, false, &in, 8, true, s_a);
    uint32_t over_ms = 0, worst = 0;
    for (uint32_t t = 40; t <= RD_PASS_MS; t += 40) {
        render(t, false, false, &in, 8, true, s_b);
        long n = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const int i = y * W + x, dx = 2 * x + 1 - W, dy = 2 * y + 1 - W;
                if (dx * dx + dy * dy > W * W || s_a[i] != s_b[i]) continue;
                const uint16_t p = s_b[i];
                n += omni_ex_r(p) > 40 || omni_ex_g(p) > 40 || omni_ex_b(p) > 40;
            }
        if ((uint32_t)n > worst) worst = (uint32_t)n;
        if (2 * n > disc) over_ms += 40;
        memcpy(s_a, s_b, sizeof s_a);
    }
    printf("pass burn-in: static bright area above half the disc for %u ms (worst frame %u px of %ld)\n",
           (unsigned)over_ms, (unsigned)worst, disc);
    TEST_ASSERT_TRUE(worst > 1000);   /* the measure sees something */
    TEST_ASSERT_TRUE(over_ms <= 200u);
}

/* the pass itself: mid-pass the band is the settled band moved by the rounded offset (the content to the right), and
 * the time rides it; the face reads its own tile (the bind reads nothing) */
static void test_rd_pass_moves(void)
{
    const fk_face_in_t in = base_in(0);
    rd_still_frame(&RD_FACE_DRIFT, s_face, 0, s_c);
    static const uint32_t T[] = {1200, 1350, 1600};
    for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++) {
        const int off = (int)lroundf(at(T[k]).off);
        s_rendered = false;
        render(T[k], false, false, &in, 8, true, s_a);
        /* the tile's own rows under its top lip, above the frame numbers (y BY + 6 to BY + 26: nothing else is drawn
         * there): frame(x) = settled(x - off), mod 466 */
        long same = 0, cmp = 0;
        for (int y = RD_DP_BY + 6; y < RD_DP_BY + 26; y++)
            for (int x = 0; x < W; x++) {
                const int sx = ((x - off) % W + W) % W;
                cmp++, same += s_a[y * W + x] == s_c[y * W + sx];
            }
        char what[64];
        snprintf(what, sizeof what, "t %u (offset %d): %ld of %ld band px", (unsigned)T[k], off, same, cmp);
        TEST_ASSERT_TRUE_MESSAGE(off != 0, what);
        TEST_ASSERT_TRUE_MESSAGE(cmp > 2000 && same == cmp, what);
    }
}

static unsigned s_gets;
static const void *counting_get(const char *name, size_t *size)
{
    s_gets++;
    return rd_assets_host_get(name, size);
}

/* the wiring: the brief's numbers; the bind reads no asset; the rctx is the last plan only */
static void test_rd_pass_face(void)
{
    TEST_ASSERT_TRUE(RD_FACE_DRIFT.reveal);
    TEST_ASSERT_EQUAL_PTR(&RD_REVEAL_PASS, RD_FACE_DRIFT.reveal_ops);
    TEST_ASSERT_NULL(RD_FACE_DRIFT.reveal_asset);
    TEST_ASSERT_NULL(RD_FACE_DRIFT.reveal_arg);
    TEST_ASSERT_EQUAL_UINT16(2050, RD_REVEAL_PASS.len_ms);
    TEST_ASSERT_EQUAL_UINT16(500, RD_REVEAL_PASS.reduced_ms);
    TEST_ASSERT_EQUAL_UINT16(0, RD_REVEAL_PASS.pop_ms);
    TEST_ASSERT_NULL(RD_REVEAL_PASS.pop_asset);
    TEST_ASSERT_EQUAL_UINT16(RD_PASS_MS, RD_REVEAL_PASS.len_ms);
    void *rctx = calloc(1, RD_REVEAL_PASS.rctx_bytes);
    TEST_ASSERT_NOT_NULL(rctx);
    s_gets = 0;
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.bind(NULL, NULL, counting_get, rctx));
    TEST_ASSERT_EQUAL_UINT(0, s_gets);
    free(rctx);
    printf("pass rctx: %u B\n", (unsigned)RD_REVEAL_PASS.rctx_bytes);
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.rctx_bytes >= sizeof(rd_pass_state_t));
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.rctx_bytes <= 64u);
    /* the need: the whole interior on the first frame, then the band each frame, the windows from 1450 and the labels
     * (the label, the date, the caption) from 1250 */
    fk_rframe_t fr;
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    RD_REVEAL_PASS.frame(NULL, s_rctx, 0, false, false, &fr);
    TEST_ASSERT_TRUE(fr.need.x0 == 0 && fr.need.y0 == 0 && fr.need.x1 == W && fr.need.y1 == W && fr.live);
    RD_REVEAL_PASS.frame(NULL, s_rctx, 40, false, false, &fr);
    TEST_ASSERT_TRUE(fr.need.x0 <= 0 && fr.need.x1 >= W && fr.need.y0 <= RD_DP_BAND.y0 &&
                     fr.need.y1 >= RD_DP_BAND.y1 && fr.need.y0 > RD_DP_DATE.y1 && fr.need.y1 < RD_DP_CAPTION.y0);
    RD_REVEAL_PASS.frame(NULL, s_rctx, 1250, false, false, &fr);
    TEST_ASSERT_TRUE(fr.need.y0 <= RD_DP_STALE.y0 && fr.need.y1 >= RD_DP_CAPTION.y1);
    RD_REVEAL_PASS.frame(NULL, s_rctx, 1500, false, false, &fr);
    TEST_ASSERT_TRUE(fr.need.y0 <= RD_DP_STALE.y0 && fr.need.y1 >= RD_DP_CAPTION.y1 &&
                     fr.need.y1 >= RD_DP_WINDOWS.y1 && fr.need.y1 < W);
}

/* `rd reveal bench`: the frames' wall time by the pass's windows (rush, slow, park, settle) */
static void test_rd_pass_timing(void)
{
    TEST_ASSERT_TRUE(RD_REVEAL_PASS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    rd_rel_timing_reset();
    fk_rframe_t fr;
    static const uint32_t T[] = {0, 60, 990, 1000, 1490, 1500, 1790, 1800, 2050};
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) RD_REVEAL_PASS.frame(NULL, s_rctx, T[i], false, false, &fr);
    rd_rel_timing_t tm;
    rd_rel_timing_get(&tm);
    TEST_ASSERT_EQUAL_UINT32(3, tm.frames[0]);   /* 0, 60, 990 */
    TEST_ASSERT_EQUAL_UINT32(2, tm.frames[1]);   /* 1000, 1490 */
    TEST_ASSERT_EQUAL_UINT32(2, tm.frames[2]);   /* 1500, 1790 */
    TEST_ASSERT_EQUAL_UINT32(1, tm.frames[3]);   /* 1800 (to 2050) */
    TEST_ASSERT_EQUAL_UINT32(990 + 10, tm.ms[0]);
    TEST_ASSERT_EQUAL_UINT32(490, tm.max_ms[1]);
    TEST_ASSERT_EQUAL_UINT32(250, tm.ms[3]);
    rd_rel_timing_reset();
}

int main(void)
{
    UNITY_BEGIN();
    bind_all();
    RUN_TEST(test_rd_pass_timeline);
    RUN_TEST(test_rd_pass_settled_time);
    RUN_TEST(test_rd_pass_band_exact);
    RUN_TEST(test_rd_pass_need_covers);
    RUN_TEST(test_rd_pass_stars_still);
    RUN_TEST(test_rd_pass_end_seam);
    RUN_TEST(test_rd_pass_reduced);
    RUN_TEST(test_rd_pass_burn_in);
    RUN_TEST(test_rd_pass_moves);
    RUN_TEST(test_rd_pass_face);
    RUN_TEST(test_rd_pass_timing);
    return UNITY_END();
}
