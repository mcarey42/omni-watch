/* The dw faces (dw/faces.h; dw wave 1 Task 11, spec sections 9 and 11): one table row per registered
 * fk_face_ops_t (Tasks 13-15 add rows), every row checked for band exactness, dirty coverage, the glance, the unset
 * and stale clock, its working set and its still-frame CRCs; the hands; the clock helper. Wave 2 Task 2 (R147 PR10)
 * adds wave 1's lessons on every row: dirty() over skips and back-steps, reduced motion held still, the full-day glance
 * sweep, taps probed at their edges and no numeral on a wave-2 face; the frame, instant, lit scan, orbit and tap-edge
 * helpers live in fk_test_util.{c,h}.
 *
 * Built twice (CMakeLists.txt): test_dw_faces over the generic assets (firmware/apps/dw/assets) against
 * dw/verify_crc.h, and test_dw_faces_overlay over the staged private set (the overlay's files over the generic ones)
 * against the overlay's verify_crc_overlay.h first, then dw/verify_crc.h (R147 PR3: the overlay's table supplements the
 * generic one with rows for the faces whose bind reads a swap asset, and only those). Without
 * -DDW_ART_OVERLAY=<clone> at configure, the overlay build reports skipped (77), never passed. Built a third time (wave 2
 * Task 11): test_dw_faces_mockup, with its own doors.c and doorway.c on Designer A's timeline (DW_DOORS_MOCKUP), runs
 * #3's doors seams there. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "dw_assets_host.h"
#include "fk_test_util.h"
#include "dw/assets_meta.h"
#include "fk/compose.h"
#include "dw/doors.h"
#include "fk/reveal_sm.h"
#include "dw/faces.h"
#include "dw/vortex.h"
#include "dw/prefs.h"
#include "dw/circles.h"
#include "omni/blend565.h"
#include "omni/raster.h"
/* R147 PR3 (wave 2 Task 4): the overlay's table supplements the generic one. Both variants have the generic table; the
 * overlay variant also has the overlay's (rows only for the faces whose bind reads a swap asset), looked up first. */
#include "dw/verify_crc.h"
#if defined(DW_FACES_OVERLAY) && defined(DW_ART_DIR) && !defined(DW_NO_OVERLAY_CRC)
#include "verify_crc_overlay.h"
#define DW_TEST_OVERLAY_TABLE 1
#endif

#if (defined(DW_FACES_OVERLAY) && !defined(DW_ART_DIR)) || defined(DW_FACES_MOCKUP)
#pragma GCC diagnostic ignored "-Wunused-function"   /* this build only reports skipped (77), or runs #3's seams */
#endif

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define PI_F 3.14159265358979f
#define RGB565(r, g, b) (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

static uint16_t s_a[W * W], s_b[W * W], s_c[W * W];

/* ---- the table: every registered face, with its own extra checks */
/* Task 15: one row per page of a paged screen (the key's three), sharing the face's bound context; every input a row
 * draws or compares carries its page (whole, in_clips, dirty_pair). */
typedef struct {
    const fk_face_ops_t *ops;
    void *ctx;
    uint8_t page;
} row_t;
static row_t s_rows[24];   /* 13 faces and the key's 3 pages at wave 2 Task 10 (15 rows); 17 at Task 12, 18 at Task 14 */
static unsigned s_nrows;
static uint8_t s_page;   /* the page row_of picks (the paged rows' wrappers set it, then put it back to 0) */

static void bind_all(void)
{
    s_nrows = 0;
    for (unsigned i = 0; i < DW_FACE_COUNT; i++) {
        void *ctx = calloc(1, DW_FACES[i]->ctx_bytes ? DW_FACES[i]->ctx_bytes : 1);
        TEST_ASSERT_NOT_NULL(ctx);
        TEST_ASSERT_TRUE_MESSAGE(DW_FACES[i]->bind(ctx, dw_assets_host_get), DW_FACES[i]->key);
        const unsigned pages = dw_still_count(DW_FACES[i]) / DW_STILL_N;
        for (unsigned p = 0; p < pages; p++) {
            TEST_ASSERT_TRUE(s_nrows < sizeof s_rows / sizeof s_rows[0]);
            s_rows[s_nrows++] = (row_t){DW_FACES[i], ctx, (uint8_t)p};
        }
    }
}

static const row_t *row_of(const char *key)
{
    for (unsigned i = 0; i < s_nrows; i++)
        if (strcmp(s_rows[i].ops->key, key) == 0 && s_rows[i].page == s_page) return &s_rows[i];
    TEST_FAIL_MESSAGE("no such face row");
    return NULL;
}

/* in on r's page */
static fk_face_in_t paged(const row_t *r, const fk_face_in_t *in)
{
    fk_face_in_t p = *in;
    p.page = r->page;
    return p;
}

/* ---- inputs */
/* fk_test_in_at as a value (most cases build their input inline): set, settled, not reduced, not the glance */
static fk_face_in_t in_at(int h, int m, int s, int ms)
{
    fk_face_in_t in;
    fk_test_in_at(&in, h, m, s, ms, false, false);
    return in;
}

/* in advanced by ms (the clock within one day) */
static fk_face_in_t later(fk_face_in_t in, int ms)
{
    long t = ((in.clk.h * 60L + in.clk.m) * 60L + in.clk.s) * 1000L + in.clk.ms + ms;
    in.clk.ms = (uint16_t)(t % 1000), t /= 1000;
    in.clk.s = (uint8_t)(t % 60), t /= 60;
    in.clk.m = (uint8_t)(t % 60), t /= 60;
    in.clk.h = (uint8_t)(t % 24);
    return in;
}

typedef void (*draw_fn)(const void *, const fk_face_in_t *, omni_rect_t, uint16_t *, int);

/* fk_test_frame for row r: fn (r's draw or glance) picks the frame, on r's page */
static void row_frame(const row_t *r, draw_fn fn, const fk_face_in_t *in, uint16_t *fb)
{
    fk_face_in_t p = paged(r, in);
    p.glance = fn == r->ops->glance;
    fk_test_frame(r->ops, r->ctx, &p, fb);
}

static void in_clips(const row_t *r, draw_fn fn, const fk_face_in_t *in_, int rows, int cols, uint16_t *fb)
{
    const fk_face_in_t pin = paged(r, in_), *in = &pin;
    memset(fb, 0xC3, sizeof s_a);
    for (int y0 = 0; y0 < W; y0 += rows)
        for (int x0 = 0; x0 < W; x0 += cols) {
            const omni_rect_t c = {(int16_t)x0, (int16_t)y0, (int16_t)(x0 + cols < W ? x0 + cols : W),
                                   (int16_t)(y0 + rows < W ? y0 + rows : W)};
            fn(r->ctx, in, c, fb + (size_t)y0 * W + x0, W);
        }
}

static void assert_same(const uint16_t *want, const uint16_t *got, const char *what)
{
    for (int i = 0; i < W * W; i++)
        if (want[i] != got[i]) {
            char m[256];
            snprintf(m, sizeof m, "%s: pixel (%d, %d) want %04x got %04x", what, i % W, i / W, want[i], got[i]);
            TEST_FAIL_MESSAGE(m);
        }
}

static bool inside(const omni_rect_t *r, int x, int y) { return x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1; }

static long rects_union(const omni_rect_t *r, int n)
{
    long px = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            for (int k = 0; k < n; k++)
                if (inside(&r[k], x, y)) {
                    px++;
                    break;
                }
    return px;
}

/* ---- per-face checks */
static void band_exact(const char *key)
{
    const row_t *r = row_of(key);
    for (unsigned i = 0; i < DW_STILL_N; i++) {
        dw_still_t st;
        dw_still(i, &st);
        const draw_fn fn = st.glance ? r->ops->glance : r->ops->draw;
        row_frame(r, fn, &st.in, s_a);
        in_clips(r, fn, &st.in, 8, W, s_b);
        assert_same(s_a, s_b, st.what);
        in_clips(r, fn, &st.in, 6, 37, s_b);   /* odd blocks too */
        assert_same(s_a, s_b, st.what);
    }
    /* the wake's tail (the interior under the doors) is band-exact too */
    fk_face_in_t in = in_at(10, 9, 30, 500);
    for (uint32_t w = 0; w <= 2000; w += 250) {
        in.wake_ms = w;
        row_frame(r, r->ops->draw, &in, s_a);
        in_clips(r, r->ops->draw, &in, 8, W, s_b);
        assert_same(s_a, s_b, "the wake's tail");
    }
}

/* every pixel that differs between the frames for a and b lies inside dirty(a, b); returns the rects' union */
static long dirty_pair(const row_t *r, draw_fn fn, const fk_face_in_t *a_, const fk_face_in_t *b_, const char *what)
{
    omni_rect_t rects[8];
    const fk_face_in_t pa = paged(r, a_), pb = paged(r, b_), *a = &pa, *b = &pb;
    const uint8_t n = r->ops->dirty(r->ctx, a, b, rects);
    TEST_ASSERT_TRUE(n <= 8);
    row_frame(r, fn, a, s_a);
    row_frame(r, fn, b, s_b);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (s_a[y * W + x] == s_b[y * W + x]) continue;
            bool in = false;
            for (int k = 0; k < n && !in; k++) in = inside(&rects[k], x, y);
            if (!in) {
                char m[160];
                snprintf(m, sizeof m, "%s: pixel (%d, %d) differs outside the %u dirty rects", what, x, y, n);
                TEST_FAIL_MESSAGE(m);
            }
        }
    return rects_union(rects, n);
}

/* The dirty area each face may redraw (R147 PR10): one row per face that needs more than the default, by key. sec: a
 * (t, t + 1 s) pair awake; frame: a (t, t + 40 ms) pair; step: the (t, t + 1 s) pair onto a :x0 second (#10's script
 * steps each 10 s). AREA_TBD: the face's task sets it from its geometry (a row of a face that exists with a TBD fails). */
#define AREA_DEF 100000L
#define AREA_TBD (-1L)
typedef struct {
    const char *key;
    long sec, frame, step;
} area_t;
static const area_t AREA[] = {
    /* #14: the script band (r 162-234, about 90 000 px) turns each second: its 8 sector boxes are about 158 000 px */
    {"circles", 160000, AREA_DEF, 160000},
    /* #8 (WD34): the ring reaches the rim, so each second's step redraws the whole frame */
    {"broadcast", (long)W * W, AREA_DEF, (long)W * W},
    /* #12: the tunnel's box on every frame (Task 13) */
    {"vortex", AREA_DEF, 196420, AREA_DEF},   /* Task 14: DW_V12_TUNNEL, the square less the calm centre (test_vortex_dirty_tunnel
                                               * moves mono_ms, which this table's clock pairs do not) */
    /* #10 (Task 6): at each 10 s script step the script ring's annulus (r 198-232, about 46 000 px) as 7 sector boxes
     * (124 855 px) and the wand's old and new places in one box (under 30 000 px) */
    {"amber", AREA_DEF, AREA_DEF, 155000},
};

static area_t area_of(const char *key)
{
    for (unsigned i = 0; i < sizeof AREA / sizeof AREA[0]; i++)
        if (strcmp(AREA[i].key, key) == 0) {
            char m[96];
            snprintf(m, sizeof m, "%s's dirty area allowance is still to be set (the AREA table)", key);
            TEST_ASSERT_TRUE_MESSAGE(AREA[i].sec >= 0 && AREA[i].frame >= 0 && AREA[i].step >= 0, m);
            return AREA[i];
        }
    return (area_t){key, AREA_DEF, AREA_DEF, AREA_DEF};
}

static void dirty_covers(const char *key)
{
    const row_t *r = row_of(key);
    const area_t cap = area_of(key);
    static const int phase[] = {0, 120, 250, 300, 380, 500, 880, 950};
    for (int g = 0; g < 2; g++) {
        const draw_fn fn = g ? r->ops->glance : r->ops->draw;
        for (unsigned k = 0; k < sizeof phase / sizeof phase[0]; k++) {
            fk_face_in_t t = in_at(10, 9, 21, phase[k]);
            t.glance = g != 0;
            const fk_face_in_t t1 = later(t, 1000), t40 = later(t, 40);
            char what[64];
            snprintf(what, sizeof what, "%s (t, t + 1 s) at %d ms", g ? "glance" : "draw", phase[k]);
            long u = dirty_pair(r, fn, &t, &t1, what);
            TEST_ASSERT_TRUE_MESSAGE(u <= (g ? AREA_DEF : cap.sec), what);
            snprintf(what, sizeof what, "%s (t, t + 40 ms) at %d ms", g ? "glance" : "draw", phase[k]);
            u = dirty_pair(r, fn, &t, &t40, what);
            TEST_ASSERT_TRUE_MESSAGE(u <= (g ? AREA_DEF : cap.frame), what);
            fk_face_in_t p = in_at(10, 9, 29, phase[k]);   /* onto :30, a 10 s step */
            p.glance = g != 0;
            const fk_face_in_t p1 = later(p, 1000);
            snprintf(what, sizeof what, "%s (:29, :30) at %d ms", g ? "glance" : "draw", phase[k]);
            u = dirty_pair(r, fn, &p, &p1, what);
            TEST_ASSERT_TRUE_MESSAGE(u <= (g ? AREA_DEF : cap.step), what);
        }
        /* at the minute and the hour: anything goes (the hands jump), but still covered */
        fk_face_in_t h = in_at(10, 59, 59, 0);
        h.glance = g != 0;
        const fk_face_in_t h1 = later(h, 1000);
        dirty_pair(r, fn, &h, &h1, "(hh:59:59, hh+1:00:00)");
        fk_face_in_t d = in_at(23, 59, 59, 600);
        d.glance = g != 0;
        fk_face_in_t d1 = later(d, 1000);
        d1.clk.mday = 2, d1.clk.wday = 1;
        dirty_pair(r, fn, &d, &d1, "midnight");
    }
    /* the wake's tail, the clock's state, the orbit */
    fk_face_in_t a = in_at(10, 9, 30, 500), b = a;
    a.wake_ms = 600, b.wake_ms = 640;
    dirty_pair(r, r->ops->draw, &a, &b, "the wake's tail");
    a.wake_ms = 1700, b.wake_ms = UINT32_MAX;
    dirty_pair(r, r->ops->draw, &a, &b, "the wake's end");
    a = in_at(10, 9, 30, 500), b = a;
    b.clk.stale = true;
    dirty_pair(r, r->ops->draw, &a, &b, "stale");
    a.glance = b.glance = true;
    dirty_pair(r, r->ops->glance, &a, &b, "stale glance");
    a.glance = b.glance = false;
    b = a;
    b.clk.set = false;
    dirty_pair(r, r->ops->draw, &a, &b, "unset");
    b = a;
    a.glance = b.glance = true;
    b.odx = 4, b.ody = -4;
    dirty_pair(r, r->ops->glance, &a, &b, "the orbit");
    b = a;
    b.glance = false;   /* the glance ending: the whole frame */
    TEST_ASSERT_TRUE(dirty_pair(r, r->ops->draw, &a, &b, "the glance's end") == (long)W * W);
}

/* I1: in the glance nothing moves within a minute, so dirty() reports nothing (no flush of unchanged black) */
static void glance_quiet(const char *key)
{
    const row_t *r = row_of(key);
    for (int st = 0; st < 3; st++)
        for (int ms = 0; ms < 1000; ms += 40) {
            fk_face_in_t t = in_at(10, 9, 21, ms);
            t.glance = true;
            t.clk.stale = st == 1;
            t.clk.set = st != 2;
            t.page = r->page;
            const fk_face_in_t t1 = later(t, 1000), t40 = later(t, 40);
            omni_rect_t rects[8];
            TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &t, &t1, rects));
            TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &t, &t40, rects));
        }
}

static void glance_dim(const char *key)
{
    const row_t *r = row_of(key);
    static const int hm[3][2] = {{10, 9}, {3, 47}, {18, 30}};
    static const int8_t orb[5][2] = {{0, 0}, {4, 4}, {-4, 4}, {4, -4}, {-4, -4}};
    for (int k = 0; k < 3; k++)
        for (int o = 0; o < 5; o++)
            for (int st = 0; st < 3; st++) {   /* synced, stale, unset */
                fk_face_in_t in = in_at(hm[k][0], hm[k][1], 12, 300);
                in.odx = orb[o][0], in.ody = orb[o][1];
                in.clk.stale = st == 1;
                in.clk.set = st != 2;
                row_frame(r, r->ops->glance, &in, s_a);
                float pct;
                uint8_t maxc;
                fk_test_lit(s_a, &pct, &maxc);
                TEST_ASSERT_TRUE_MESSAGE(pct < 5.f, "the glance lights 5 % or more of the disc");
                TEST_ASSERT_TRUE_MESSAGE(maxc <= 110, "a glance channel above 110");
            }
}

static void stale_marker_only(const char *key, const omni_rect_t *rect)
{
    const row_t *r = row_of(key);
    for (int g = 0; g < 2; g++) {
        const draw_fn fn = g ? r->ops->glance : r->ops->draw;
        static const int8_t orb[3][2] = {{0, 0}, {4, -4}, {-4, 4}};
        for (int o = 0; o < (g ? 3 : 1); o++) {
            fk_face_in_t in = in_at(10, 9, 30, 500);
            in.odx = orb[o][0], in.ody = orb[o][1];
            row_frame(r, fn, &in, s_a);
            in.clk.stale = true;
            row_frame(r, fn, &in, s_b);
            long inner = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++) {
                    if (s_a[y * W + x] == s_b[y * W + x]) continue;
                    TEST_ASSERT_TRUE_MESSAGE(inside(rect, x, y), "stale differs from synced outside the marker's rect");
                    inner++;
                }
            TEST_ASSERT_TRUE_MESSAGE(inner > 20, "no stale marker drawn");
        }
    }
}

static void ctx_bytes(const char *key)
{
    const row_t *r = row_of(key);
    TEST_ASSERT_TRUE(r->ops->ctx_bytes <= 1500000u - (size_t)W * W * 2);   /* spec section 8 [call], less the canvas */
}

/* ---- #1: Crystal Chamber */
static void crystal_unset(void)
{
    const row_t *r = row_of("crystal");
    fk_face_in_t ref;
    memset(&ref, 0, sizeof ref);
    ref.wake_ms = UINT32_MAX, ref.battery = -1;
    row_frame(r, r->ops->draw, &ref, s_c);
    char txt[12];
    dw_c1_monitor_text(&ref, txt);
    TEST_ASSERT_EQUAL_STRING("--:--", txt);
    static const int hms[3][3] = {{10, 9, 30}, {3, 47, 12}, {7, 22, 58}};
    for (int k = 0; k < 3; k++) {
        fk_face_in_t in = in_at(hms[k][0], hms[k][1], hms[k][2], 640);
        in.clk.set = false;   /* the other fields left as a set clock's: the face must not read them */
        dw_c1_monitor_text(&in, txt);
        TEST_ASSERT_EQUAL_STRING("--:--", txt);
        TEST_ASSERT_EQUAL_UINT16(0, dw_c1_script_off(&in));
        row_frame(r, r->ops->draw, &in, s_a);
        assert_same(s_c, s_a, "unset at another time");   /* nothing reads the time: the same frame */
        /* the hands' boxes for that time hold the unset reference, and a synced frame differs there */
        const float am = ((float)hms[k][1] / 60.f) * 2 * PI_F;
        const float ah = (((float)(hms[k][0] % 12) + (float)hms[k][1] / 60.f) / 12.f) * 2 * PI_F;
        omni_rect_t bb[2];
        dw_hand_shard_bbox(ah, true, &bb[0]);
        dw_hand_shard_bbox(am, false, &bb[1]);
        in.clk.set = true;
        row_frame(r, r->ops->draw, &in, s_b);
        long differ = 0;
        for (int i = 0; i < 2; i++)
            for (int y = bb[i].y0 < 0 ? 0 : bb[i].y0; y < bb[i].y1 && y < W; y++)
                for (int x = bb[i].x0 < 0 ? 0 : bb[i].x0; x < bb[i].x1 && x < W; x++) {
                    TEST_ASSERT_EQUAL_HEX16(s_c[y * W + x], s_a[y * W + x]);
                    differ += s_b[y * W + x] != s_c[y * W + x];
                }
        TEST_ASSERT_TRUE_MESSAGE(differ > 500, "the synced hands do not show in their boxes");
        /* the monitor: the --:-- cells, not the date */
        long mon = 0;
        for (int y = DW_C1_MONITOR.y0; y < DW_C1_MONITOR.y1; y++)
            for (int x = DW_C1_MONITOR.x0; x < DW_C1_MONITOR.x1; x++) mon += s_b[y * W + x] != s_c[y * W + x];
        TEST_ASSERT_TRUE_MESSAGE(mon > 20, "the monitor shows the same cells set and unset");
        /* the glance: no hands either */
        in.clk.set = false;
        row_frame(r, r->ops->glance, &in, s_a);
        row_frame(r, r->ops->glance, &ref, s_b);
        assert_same(s_b, s_a, "unset glance at another time");
    }
}

/* M3: the NOT SYNCED tape never hides the hands, also when they stand at 12 (the hands' own opaque pixels show) */
static void crystal_stale_under_hands(void)
{
    const row_t *r = row_of("crystal");
    static const int hm[3][2] = {{12, 0}, {11, 59}, {0, 2}};
    for (int k = 0; k < 3; k++) {
        const fk_face_in_t away = in_at(6, 30, 30, 500);   /* both hands down: the marker's area is clear */
        fk_face_in_t in = in_at(hm[k][0], hm[k][1], 30, 500);
        row_frame(r, r->ops->draw, &in, s_a);   /* synced, hands at 12 */
        row_frame(r, r->ops->draw, &away, s_c);
        in.clk.stale = true;
        row_frame(r, r->ops->draw, &in, s_b);   /* stale, hands at 12 */
        long hand = 0, shown = 0;
        for (int y = DW_C1_STALE.y0; y < DW_C1_STALE.y1; y++)
            for (int x = DW_C1_STALE.x0; x < DW_C1_STALE.x1; x++) {
                const int i = y * W + x;
                if (s_a[i] == s_c[i]) continue;   /* not a hand pixel */
                hand++;
                shown += s_b[i] == s_a[i];
            }
        TEST_ASSERT_TRUE_MESSAGE(hand > 300, "the hands do not cross the marker's rect at this time");
        TEST_ASSERT_TRUE_MESSAGE(shown * 10 >= hand * 6, "the stale tape covers the hands");
    }
}

/* M6 (a): the doors' tail at their end time is the settled frame exactly (the seam the last swing frame relies on) */
static void crystal_tail_end_is_settled(void)
{
    const row_t *r = row_of("crystal");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &in, s_a);
    in.wake_ms = DW_DOORS_END_MS;
    row_frame(r, r->ops->draw, &in, s_b);
    assert_same(s_a, s_b, "draw at the doors' end time vs settled");
}

/* M6 (b): the doors' end frame (and a tap's) over the real #1 interior, with the real doors and glow, is the still */
static void crystal_doors_end_is_still(void)
{
    const row_t *r = row_of("crystal");
    size_t n;
    fk_img_t door, glow;
    const void *dp = dw_assets_host_get(ASSET_DW_DOORS_M, &n);
    TEST_ASSERT_TRUE(dp && fk_img_from_bin(&door, dp, n) && door.rgb);
    const void *gp = dw_assets_host_get(ASSET_DW_GLOW, &n);
    TEST_ASSERT_TRUE(gp && fk_img_from_bin(&glow, gp, n) && glow.a);
    dw_still_frame(r->ops, r->ctx, 0, s_c);   /* the interior: the settled still */
    static fk_col_t cols[W];
    for (int how = 0; how < 3; how++) {
        fk_cols_state_t st;
        if (how == 0) dw_doors_at(DW_DOORS_END_MS, false, &st);
        else if (how == 1) dw_doors_at(DW_DOORS_REDUCED_MS, true, &st);
        else dw_doors_skip(&st);
        dw_doors_cols(st.th_l, st.th_r, cols);
        memset(s_a, 0x5A, sizeof s_a);
        for (int y0 = 0; y0 < W; y0 += 8) {
            const omni_rect_t b = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
            fk_cols_band(&DW_DOORS_OUT, &st, cols, door.rgb, s_c, glow.a, b, s_a + (size_t)y0 * W);
        }
        /* the still on the round panel, black off it (doors.h, Task 12) */
        for (int y = 0; y < W; y++) {
            int x0, x1;
            fk_panel_row(y, &x0, &x1);
            for (int x = 0; x < W; x++)
                if (x < x0 || x >= x1) s_b[y * W + x] = 0;
                else s_b[y * W + x] = s_c[y * W + x];
        }
        assert_same(s_b, s_a, how == 0 ? "the doors' end" : how == 1 ? "the cross-fade's end" : "a tap");
    }
}

/* M6 (c): the unset monitor shows exactly the --:-- cells: the atlas's ink for "--:--" where the face puts it, and no
 * text ink anywhere else on the monitor */
static void crystal_unset_monitor_cells(void)
{
    const row_t *r = row_of("crystal");
    fk_face_in_t in;
    memset(&in, 0, sizeof in);
    in.wake_ms = UINT32_MAX, in.battery = -1;
    row_frame(r, r->ops->draw, &in, s_a);
    size_t n;
    fk_img_t im;
    const void *p = dw_assets_host_get(ASSET_DW_C1_MONO, &n);
    TEST_ASSERT_TRUE(p && fk_img_from_bin(&im, p, n));
    const fk_atlas_t at = {&im, DW_CELLS_DW_C1_MONO, DW_GLYPHS_DW_C1_MONO, ASSET_DW_C1_MONO_LINE_H};
    /* the cells' ink alone, drawn as the face draws its text, over black */
    memset(s_c, 0, sizeof s_c);
    const int w = fk_atlas_width(&at, "--:--", 0);
    TEST_ASSERT_TRUE(w > 20);
    fk_atlas_text(s_c, W, (omni_rect_t){0, 0, W, W}, &at, "--:--", (W - w) / 2, 362, 0, 255);
    long ink = 0, lit = 0, stray = 0;
    for (int y = DW_C1_MONITOR.y0 + 6; y < DW_C1_MONITOR.y1 - 6; y++)
        for (int x = DW_C1_MONITOR.x0 + 12; x < DW_C1_MONITOR.x1 - 12; x++) {
            const uint16_t m = s_c[y * W + x], f = s_a[y * W + x];
            /* the text's colour is #b9d6ff: bright in red as well as blue (the frame's strokes are (80,150,255)) */
            const bool text = ((f >> 11) & 31) * 255 / 31 > 120 && (f & 31) * 255 / 31 > 150;
            if (((m & 31) * 255 / 31) > 200) {   /* a solid ink pixel of a cell */
                ink++;
                lit += text;
            } else if (!m) {
                stray += text;   /* text ink where no cell inks */
            }
        }
    TEST_ASSERT_TRUE_MESSAGE(ink > 15, "the --:-- cells hold no solid ink");
    TEST_ASSERT_EQUAL_MESSAGE(ink, lit, "a --:-- cell's ink is not on the monitor");
    TEST_ASSERT_EQUAL_MESSAGE(0, stray, "text ink on the monitor outside the --:-- cells");
}

static void crystal_monitor_text(void)
{
    fk_face_in_t in = in_at(10, 9, 30, 0);
    char txt[12];
    dw_c1_monitor_text(&in, txt);
    TEST_ASSERT_EQUAL_STRING("SUN 1", txt);
    in.clk.wday = 3, in.clk.mday = 28;
    dw_c1_monitor_text(&in, txt);
    TEST_ASSERT_EQUAL_STRING("WED 28", txt);
    in.clk.wday = 9;   /* out of range: no garbage */
    dw_c1_monitor_text(&in, txt);
    TEST_ASSERT_EQUAL_STRING("--- 28", txt);
}

/* the script steps 6 degrees a second, eased over 280 ms, counter-clockwise offsets (compose.h's rule) */
static void crystal_script(void)
{
    for (int s = 0; s < 60; s++) {
        const fk_face_in_t a = in_at(10, 9, s, 0), b = in_at(10, 9, s, DW_C1_EASE_MS), c = in_at(10, 9, s, 999);
        const int want = (int)((DW_C1_SCRIPT_NT - (s * DW_C1_SCRIPT_NT + 30) / 60) % DW_C1_SCRIPT_NT);
        TEST_ASSERT_EQUAL_UINT16(want, dw_c1_script_off(&a));
        TEST_ASSERT_EQUAL_UINT16(dw_c1_script_off(&b), dw_c1_script_off(&c));   /* settled from 280 ms */
        const fk_face_in_t n = in_at(10, 9, (s + 1) % 60, 0);
        TEST_ASSERT_EQUAL_UINT16(dw_c1_script_off(&n), dw_c1_script_off(&b));
    }
    /* the wake winds it in: wound back early, home by the doors' end */
    fk_face_in_t w = in_at(10, 9, 30, 500);
    const uint16_t home = dw_c1_script_off(&w);
    w.wake_ms = 200;
    TEST_ASSERT_TRUE(dw_c1_script_off(&w) != home);
    w.wake_ms = 1800;
    TEST_ASSERT_EQUAL_UINT16(home, dw_c1_script_off(&w));
}

/* ---- every face: an unset clock reads no time (the same frame at any time), awake and in the glance */
static void unset_timeless(const char *key)
{
    const row_t *r = row_of(key);
    for (int g = 0; g < 2; g++) {
        const draw_fn fn = g ? r->ops->glance : r->ops->draw;
        fk_face_in_t ref;
        memset(&ref, 0, sizeof ref);
        ref.wake_ms = UINT32_MAX, ref.battery = -1, ref.glance = g != 0;
        row_frame(r, fn, &ref, s_c);
        static const int hms[3][3] = {{10, 9, 30}, {3, 47, 12}, {12, 0, 1}};
        for (int k = 0; k < 3; k++) {
            fk_face_in_t in = in_at(hms[k][0], hms[k][1], hms[k][2], 640);
            in.clk.set = false;   /* the other fields left as a set clock's: the face must not read them */
            in.glance = g != 0;
            row_frame(r, fn, &in, s_a);
            assert_same(s_c, s_a, g ? "unset glance at another time" : "unset at another time");
            in.clk.set = true;
            row_frame(r, fn, &in, s_b);
            long differ = 0;
            for (int i = 0; i < W * W; i++) differ += s_a[i] != s_b[i];
            TEST_ASSERT_TRUE_MESSAGE(differ > 500, "a set clock draws the same frame as an unset one");
        }
    }
}

/* ---- #4: Roundel analog */
static const fk_img_t *img_of(const char *name, fk_img_t *im)
{
    size_t n;
    const void *p = dw_assets_host_get(name, &n);
    TEST_ASSERT_NOT_NULL_MESSAGE(p, name);
    TEST_ASSERT_TRUE_MESSAGE(fk_img_from_bin(im, p, n), name);
    return im;
}

/* at 10:09 only the 10 o'clock roundel's box differs from the dial (dw_r4_base) among the twelve, outside the hands */
static void roundel_lit_hour(void)
{
    const row_t *r = row_of("roundel");
    fk_img_t base;
    img_of(ASSET_DW_R4_BASE, &base);
    TEST_ASSERT_TRUE(base.rgb && base.w == W && base.h == W);
    for (int g = 0; g < 2; g++) {   /* settled, and at the end of the wake */
        fk_face_in_t in = in_at(10, 9, 30, 500);
        in.wake_ms = g ? DW_R4_WAKE_MS - 1 : UINT32_MAX;
        row_frame(r, r->ops->draw, &in, s_a);
        omni_rect_t hands[7];
        dw_hand_baton_bbox(((10.f + 9.f / 60.f) / 12.f) * 2 * PI_F, true, &hands[0]);
        dw_hand_baton_bbox((9.f / 60.f) * 2 * PI_F, false, &hands[1]);
        dw_hand_second_rects(dw_second_angle(&in.clk), &hands[2]);
        hands[6] = (omni_rect_t){218, 218, 248, 248};   /* the hub */
        const omni_rect_t lit10 = {DW_R4_LIT_AT[9][0], DW_R4_LIT_AT[9][1],
                                   (int16_t)(DW_R4_LIT_AT[9][0] + DW_R4_LIT_CELL),
                                   (int16_t)(DW_R4_LIT_AT[9][1] + DW_R4_LIT_CELL)};
        for (int h = 1; h <= 12; h++) {
            TEST_ASSERT_EQUAL_UINT8(h == 10 ? 255 : 0, dw_r4_lit(&in, h));
            const int x0 = DW_R4_LIT_AT[h - 1][0], y0 = DW_R4_LIT_AT[h - 1][1];
            long differ = 0;
            for (int y = y0; y < y0 + DW_R4_LIT_CELL; y++)
                for (int x = x0; x < x0 + DW_R4_LIT_CELL; x++) {
                    bool hand = false;
                    for (int k = 0; k < 7 && !hand; k++) hand = inside(&hands[k], x, y);
                    if (h != 10 && inside(&lit10, x, y)) continue;   /* the boxes of 10 and 11 share a corner */
                    if (!hand) differ += s_a[y * W + x] != base.rgb[y * W + x];
                }
            if (h == 10) TEST_ASSERT_TRUE_MESSAGE(differ > 1500, "the 10 o'clock roundel is not lit");
            else {
                char m[64];
                snprintf(m, sizeof m, "the %d o'clock roundel's box differs from the dial", h);
                TEST_ASSERT_EQUAL_MESSAGE(0, differ, m);
            }
        }
    }
    /* unset: no roundel lit */
    fk_face_in_t u = in_at(10, 9, 30, 500);
    u.clk.set = false;
    for (int h = 1; h <= 12; h++) TEST_ASSERT_EQUAL_UINT8(0, dw_r4_lit(&u, h));
}

static double overshoot_want(int s, int ms)
{
    const double tick = ms < 160 ? 1 + sin(ms / 160.0 * 3.14159265358979) * 0.18 * (1 - ms / 160.0) : 1;
    return ((s - 1) + tick) / 60.0 * 2 * 3.14159265358979;
}

static void second_overshoot(void)
{
    static const int MS[] = {0, 40, 80, 120, 159, 160, 161, 500, 999};
    for (int s = 0; s < 60; s += 7)
        for (unsigned k = 0; k < sizeof MS / sizeof MS[0]; k++) {
            const fk_face_in_t in = in_at(10, 9, s, MS[k]);
            const float a = dw_second_angle(&in.clk);
            TEST_ASSERT_FLOAT_WITHIN(2e-5f, (float)overshoot_want(s, MS[k]), a);
            if (MS[k] >= DW_SECOND_OVERSHOOT_MS)   /* exact from 160 on: s / 60 turns */
                TEST_ASSERT_EQUAL_FLOAT(((float)s / 60.f) * 2 * PI_F, a);
        }
    /* the overshoot: past s (about 0.1 s of arc at its peak, near 50 ms) and back by 160 ms */
    const fk_clock_t c0 = in_at(10, 9, 30, 0).clk, c40 = in_at(10, 9, 30, 40).clk;
    const fk_clock_t c80 = in_at(10, 9, 30, 80).clk, c120 = in_at(10, 9, 30, 120).clk;
    const float rest = (30.f / 60.f) * 2 * PI_F, sec = 2 * PI_F / 60.f;
    TEST_ASSERT_TRUE(dw_second_angle(&c40) > rest + 0.09f * sec && dw_second_angle(&c80) > rest + 0.08f * sec);
    TEST_ASSERT_TRUE(dw_second_angle(&c120) > rest && dw_second_angle(&c120) < dw_second_angle(&c80));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, rest, dw_second_angle(&c0));
    fk_clock_t u = c80;
    u.set = false;
    TEST_ASSERT_EQUAL_FLOAT(0.f, dw_second_angle(&u));
}

static void test_hand_baton_and_second_band_exact(void)
{
    static const float ang[] = {0.f, 0.3f, 1.5707963f, 2.2f, 3.14159265f, 4.0f, 5.5f, 6.1f};
    for (unsigned k = 0; k < sizeof ang / sizeof ang[0]; k++)
        for (int part = 0; part < 3; part++) {
            memset(s_a, 0, sizeof s_a);
            if (part < 2) dw_hand_baton(s_a, W, NULL, ang[k], part == 0, 255);
            else dw_hand_second(s_a, W, NULL, ang[k]);
            memset(s_b, 0, sizeof s_b);
            for (int y0 = 0; y0 < W; y0 += 8) {
                const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
                if (part < 2) dw_hand_baton(s_b + (size_t)y0 * W, W, &c, ang[k], part == 0, 255);
                else dw_hand_second(s_b + (size_t)y0 * W, W, &c, ang[k]);
            }
            assert_same(s_a, s_b, "hand in 8-row clips");
            omni_rect_t bb[4];
            int nb = 1;
            if (part < 2) dw_hand_baton_bbox(ang[k], part == 0, &bb[0]);
            else dw_hand_second_rects(ang[k], bb), nb = 4;
            long ink = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++)
                    if (s_a[y * W + x]) {
                        bool in = false;
                        for (int i = 0; i < nb && !in; i++) in = inside(&bb[i], x, y);
                        TEST_ASSERT_TRUE_MESSAGE(in, "hand ink outside its boxes");
                        ink++;
                    }
            TEST_ASSERT_TRUE(ink > 100);
        }
}

/* ---- #5: Call box digital */
static void callbox_panes(void)
{
    for (int s = 0; s < 60; s++) {
        const fk_face_in_t in = in_at(10, 9, s, 999);
        TEST_ASSERT_EQUAL_UINT8(s / 5 + 1 > 12 ? 12 : s / 5 + 1, dw_b5_panes_lit(&in));
    }
    const row_t *r = row_of("callbox");
    const fk_face_in_t in = in_at(10, 9, 37, 500);
    TEST_ASSERT_EQUAL_UINT8(8, dw_b5_panes_lit(&in));
    row_frame(r, r->ops->draw, &in, s_a);
    for (int i = 0; i < DW_B5_PANE_N; i++) {
        const int x0 = DW_B5_PANES[i][0], y0 = DW_B5_PANES[i][1], pw = DW_B5_PANES[i][2], ph = DW_B5_PANES[i][3];
        TEST_ASSERT_EQUAL_UINT8(i < 8 ? 255 : 0, dw_b5_pane_level(&in, i));
        long dark = 0;
        for (int y = y0; y < y0 + ph; y++)
            for (int x = x0; x < x0 + pw; x++) dark += s_a[y * W + x] == RGB565(5, 8, 15);
        if (i < 8) TEST_ASSERT_EQUAL_MESSAGE(0, dark, "a lit pane has dark pixels");
        else TEST_ASSERT_EQUAL_MESSAGE((long)pw * ph, dark, "an unlit pane is not dark");
    }
    /* the fade: pane 7 (on at 35 s) half way at 35.2 s; the wake's stagger holds them dark early on */
    fk_face_in_t f = in_at(10, 9, 35, 200);
    const uint8_t half = dw_b5_pane_level(&f, 7);
    TEST_ASSERT_TRUE(half > 100 && half < 155);
    f.clk.s = 37, f.wake_ms = 400;
    for (int i = 0; i < 12; i++) TEST_ASSERT_EQUAL_UINT8(0, dw_b5_pane_level(&f, i));
    f.clk.set = false, f.wake_ms = UINT32_MAX;
    TEST_ASSERT_EQUAL_UINT8(0, dw_b5_panes_lit(&f));
}

/* D6: in this set's dw_b5_box (the generic one; the overlay's in test_dw_faces_overlay), every pane rectangle and the
 * lens from assets_meta.h sits in its well: the rectangle is flat (one colour, where the sprite goes), and the border
 * around it (the window surround, 1-5 px out from its window; the lamp body round the lens) differs from it */
static long ring_differs(const fk_img_t *box, int x0, int y0, int x1, int y1, int k, uint16_t inner, long *n)
{
    long d = 0;
    *n = 0;
    for (int y = y0 - k; y < y1 + k; y++)
        for (int x = x0 - k; x < x1 + k; x++) {
            if (x > x0 - k && x < x1 + k - 1 && y > y0 - k && y < y1 + k - 1) continue;   /* the ring only */
            (*n)++;
            d += box->rgb[y * W + x] != inner;
        }
    return d;
}

static bool well(const fk_img_t *box, int px, int py, int pw, int ph, int wx0, int wy0, int wx1, int wy1)
{
    const uint16_t inner = box->rgb[py * W + px];
    for (int y = py; y < py + ph; y++)
        for (int x = px; x < px + pw; x++)
            if (box->rgb[y * W + x] != inner) return false;   /* not flat */
    for (int k = 1; k <= 5; k++) {
        long n;
        const long d = ring_differs(box, wx0, wy0, wx1, wy1, k, inner, &n);
        if (d * 4 >= n) return true;
    }
    return false;
}

static void callbox_geometry(void)
{
    fk_img_t box;
    img_of(ASSET_DW_B5_BOX, &box);
    TEST_ASSERT_TRUE(box.rgb && box.w == W && box.h == W);
    for (int i = 0; i < DW_B5_PANE_N; i++) {
        const int16_t *p = DW_B5_PANES[i];
        const int16_t *a = DW_B5_PANES[i < 6 ? 0 : 6], *b = DW_B5_PANES[i < 6 ? 5 : 11];   /* its window */
        char m[64];
        snprintf(m, sizeof m, "pane %d is not in its well", i);
        TEST_ASSERT_TRUE_MESSAGE(well(&box, p[0], p[1], p[2], p[3], a[0], a[1], b[0] + b[2], b[1] + b[3]), m);
    }
    TEST_ASSERT_TRUE_MESSAGE(well(&box, DW_B5_LENS_X, DW_B5_LENS_Y, DW_B5_LENS_W, DW_B5_LENS_H, DW_B5_LENS_X,
                                  DW_B5_LENS_Y, DW_B5_LENS_X + DW_B5_LENS_W, DW_B5_LENS_Y + DW_B5_LENS_H),
                             "the lens is not in its well");
#if defined(DW_FACES_OVERLAY)
    TEST_ASSERT_TRUE_MESSAGE(dw_assets_host_from_overlay(ASSET_DW_B5_BOX), "the box is not the overlay's");
#endif
}

/* the face binds ASSET_DW_B5_BOX once, and no other asset it binds is wider than 200 px: the box is one file */
static unsigned s_box_asks, s_wide;
static const void *counting_get(const char *name, size_t *size)
{
    const void *p = dw_assets_host_get(name, size);
    if (strcmp(name, ASSET_DW_B5_BOX) == 0) s_box_asks++;
    else {
        fk_img_t im;
        if (p && fk_img_from_bin(&im, p, *size) && im.w > 200) s_wide++;
    }
    return p;
}

static void callbox_one_box_asset(void)
{
    void *ctx = calloc(1, DW_FACE_CALLBOX.ctx_bytes);
    TEST_ASSERT_NOT_NULL(ctx);
    s_box_asks = s_wide = 0;
    TEST_ASSERT_TRUE(DW_FACE_CALLBOX.bind(ctx, counting_get));
    TEST_ASSERT_EQUAL_UINT(1, s_box_asks);
    TEST_ASSERT_EQUAL_UINT(0, s_wide);
    free(ctx);
}

static void callbox_tap_lantern(void)
{
    const row_t *r = row_of("callbox");
    /* the lantern's target: 30 px each way of the tap centre (233, 55) is in, corners too (a 61 px square, probed at
     * its edges); 31 is out */
    const int lx = DW_B5_LENS_X + DW_B5_LENS_W / 2;
    TEST_ASSERT_TRUE(lx >= 203 && lx < 264);   /* the lens is inside the target */
    fk_test_tap_edges(r->ops, r->ctx, (omni_rect_t){203, 25, 264, 86}, FK_TAP_PULSE);
    TEST_ASSERT_EQUAL_INT(FK_TAP_NONE, r->ops->tap(r->ctx, 233, 400));
    TEST_ASSERT_NULL(row_of("roundel")->ops->tap);
}

/* the lamp: 0.28 at rest, three pulses on the wake and a tap, one on the hour, 0.18 unset */
static void callbox_lamp(void)
{
    fk_face_in_t in = in_at(10, 9, 30, 500);
    const uint8_t rest = dw_b5_lamp_level(&in);
    TEST_ASSERT_UINT8_WITHIN(1, 71, rest);   /* 0.28 */
    in.wake_ms = 450;   /* the first pulse's peak */
    TEST_ASSERT_UINT8_WITHIN(1, 255, dw_b5_lamp_level(&in));
    in.wake_ms = UINT32_MAX, in.pulsing = true, in.pulse_ms = 1350;   /* a tap's second pulse */
    TEST_ASSERT_UINT8_WITHIN(1, 255, dw_b5_lamp_level(&in));
    in.pulsing = false;
    in.clk.m = 0, in.clk.s = 1, in.clk.ms = 0;   /* the hour's pulse */
    TEST_ASSERT_UINT8_WITHIN(1, 255, dw_b5_lamp_level(&in));
    in.clk.set = false;
    TEST_ASSERT_UINT8_WITHIN(1, 46, dw_b5_lamp_level(&in));
}

/* review M-b: #5's region rects (a pane's fade, the lamp's pulses, the plaque) cover every changed pixel, and are
 * regions, not the whole frame */
static void callbox_dirty_regions(void)
{
    const row_t *r = row_of("callbox");
    struct { fk_face_in_t a, b; const char *what; } c[5];
    c[0].a = in_at(10, 9, 35, 100), c[0].b = in_at(10, 9, 35, 140), c[0].what = "pane 7 fading in";
    c[1].a = in_at(10, 9, 30, 999), c[1].b = c[1].a;
    c[1].a.pulsing = c[1].b.pulsing = true, c[1].a.pulse_ms = 400, c[1].b.pulse_ms = 440, c[1].what = "a tap's pulse";
    c[2].a = in_at(10, 0, 0, 500), c[2].b = in_at(10, 0, 0, 540), c[2].what = "the hour's pulse";
    c[3].a = in_at(10, 9, 30, 999), c[3].b = c[3].a;
    c[3].a.battery = 50, c[3].b.battery = 51, c[3].what = "the battery";
    c[4].a = in_at(10, 9, 30, 999), c[4].b = c[4].a;
    c[4].a.sun_kind = c[4].b.sun_kind = 1, c[4].a.sun_h = c[4].b.sun_h = 7, c[4].a.sun_m = 51, c[4].b.sun_m = 52;
    c[4].what = "the sun's minute";
    for (int i = 0; i < 5; i++) {
        omni_rect_t rects[8];
        TEST_ASSERT_TRUE_MESSAGE(r->ops->dirty(r->ctx, &c[i].a, &c[i].b, rects) > 0, c[i].what);
        const long u = dirty_pair(r, r->ops->draw, &c[i].a, &c[i].b, c[i].what);
        TEST_ASSERT_TRUE_MESSAGE(u < 20000, c[i].what);   /* a region (the lamp, a window, the plaque) */
        long differ = 0;
        for (int k = 0; k < W * W; k++) differ += s_a[k] != s_b[k];
        TEST_ASSERT_TRUE_MESSAGE(differ > 0, c[i].what);   /* something really moved */
    }
}

/* review M-c: under reduced motion #4's seconds tick with no overshoot (faces-classic.js: tick = 1), and nothing is
 * redrawn within a second */
static void roundel_reduced_no_overshoot(void)
{
    const row_t *r = row_of("roundel");
    for (int ms = 0; ms < 1000; ms += 20) {
        fk_face_in_t in = in_at(10, 9, 30, ms);
        in.reduced = true;
        TEST_ASSERT_EQUAL_FLOAT((30.f / 60.f) * 2 * PI_F, dw_second_angle_in(&in));
        in.reduced = false;
        TEST_ASSERT_EQUAL_FLOAT(dw_second_angle(&in.clk), dw_second_angle_in(&in));
    }
    fk_face_in_t a = in_at(10, 9, 30, 0), b = in_at(10, 9, 30, 40);
    a.reduced = b.reduced = true;
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    dirty_pair(r, r->ops->draw, &a, &b, "reduced: within the overshoot window");
    a.reduced = false;   /* a change of the setting: the whole frame */
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &a, &b, rects));
}

/* the final review, M1: under reduced motion #1's script steps once a second with no easing and has no glint, and #5's
 * panes step on with no fade and its lamp has no hourly pulse: every frame within a second is the second's first, and
 * dirty() reports nothing; a change of the setting redraws the whole frame */
static void reduced_still_within_second(const char *key, const int (*hms)[3], int n)
{
    const row_t *r = row_of(key);
    for (int k = 0; k < n; k++) {
        fk_face_in_t a = in_at(hms[k][0], hms[k][1], hms[k][2], 0);
        a.reduced = true;
        row_frame(r, r->ops->draw, &a, s_c);
        for (int ms = 20; ms < 1000; ms += 20) {
            fk_face_in_t b = a;
            b.clk.ms = (uint16_t)ms;
            char what[64];
            snprintf(what, sizeof what, "%s reduced at %02d:%02d:%02d.%03d", key, hms[k][0], hms[k][1], hms[k][2], ms);
            omni_rect_t rects[8];
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, r->ops->dirty(r->ctx, &a, &b, rects), what);
            row_frame(r, r->ops->draw, &b, s_a);
            assert_same(s_c, s_a, what);
        }
        fk_face_in_t f = a;   /* the setting changed: the whole frame */
        f.reduced = false;
        f.clk.ms = 500;
        omni_rect_t rects[8];
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, r->ops->dirty(r->ctx, &a, &f, rects), key);
        TEST_ASSERT_TRUE(rects[0].x0 == 0 && rects[0].y0 == 0 && rects[0].x1 == W && rects[0].y1 == W);
    }
}

static void crystal_reduced(void)
{
    static const int hms[3][3] = {{10, 9, 30}, {3, 47, 12}, {12, 0, 59}};
    reduced_still_within_second("crystal", hms, 3);
    /* the script still steps each second (a discrete step: the next second's offset at once) */
    fk_face_in_t a = in_at(10, 9, 30, 999), b = in_at(10, 9, 31, 0);
    a.reduced = b.reduced = true;
    TEST_ASSERT_TRUE(dw_c1_script_off(&a) != dw_c1_script_off(&b));
    /* at the step's settled place: the frame the eased step comes to rest on (no easing, no lag) */
    for (int ms = 0; ms < 1000; ms += 100) {
        fk_face_in_t rr = in_at(10, 9, 30, ms), settled = in_at(10, 9, 30, 500);
        rr.reduced = true;
        TEST_ASSERT_EQUAL_UINT16(dw_c1_script_off(&settled), dw_c1_script_off(&rr));
    }
}

static void callbox_reduced(void)
{
    /* the panes' fades (:00, :05, :25 and :55) and the hour's lamp pulse (:00:00 and :00:01) */
    static const int hms[6][3] = {{10, 9, 0}, {10, 9, 5}, {10, 9, 25}, {10, 9, 55}, {11, 0, 0}, {11, 0, 1}};
    reduced_still_within_second("callbox", hms, 6);
    fk_face_in_t in = in_at(10, 9, 5, 0);
    in.reduced = true;
    TEST_ASSERT_EQUAL_UINT8(255, dw_b5_pane_level(&in, 1));   /* on at once at its second */
    TEST_ASSERT_EQUAL_UINT8(0, dw_b5_pane_level(&in, 2));
    fk_face_in_t h = in_at(11, 0, 1, 0);
    h.reduced = true;
    fk_face_in_t rest = in_at(11, 0, 30, 0);
    rest.reduced = true;
    TEST_ASSERT_EQUAL_UINT8(dw_b5_lamp_level(&rest), dw_b5_lamp_level(&h));   /* no hourly pulse */
}

/* ---- #7: Rotor */
static bool rect_in(const omni_rect_t *r, const omni_rect_t *box)
{
    return r->x0 >= box->x0 && r->y0 >= box->y0 && r->x1 <= box->x1 && r->y1 <= box->y1 && r->x0 < r->x1 &&
           r->y0 < r->y1;
}

static bool same_rect(const omni_rect_t *a, const omni_rect_t *b)
{
    return a->x0 == b->x0 && a->y0 == b->y0 && a->x1 == b->x1 && a->y1 == b->y1;
}

/* across 40 ms steps the stack's frames redraw only the column box, and a second's tick only the old and the new rim
 * pinpoint; the minute redraws the whole frame */
static void rotor_motion_confined(void)
{
    const row_t *r = row_of("rotor");
    fk_face_in_t a = in_at(10, 9, 28, 0);
    int moved = 0;
    for (int step = 0; step < 75; step++) {   /* 3 s */
        const fk_face_in_t b = later(a, 40);
        omni_rect_t rects[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
        omni_rect_t pa, pb;
        dw_t7_pin_rect(a.clk.s, &pa);
        dw_t7_pin_rect(b.clk.s, &pb);
        for (int k = 0; k < n; k++) {
            const bool pin = a.clk.s != b.clk.s && (same_rect(&rects[k], &pa) || same_rect(&rects[k], &pb));
            TEST_ASSERT_TRUE_MESSAGE(pin || rect_in(&rects[k], &DW_T7_COLUMN), "a rect outside the column box");
            moved += !pin;
        }
        char what[48];
        snprintf(what, sizeof what, "rotor at :%02d.%03d + 40 ms", a.clk.s, a.clk.ms);
        dirty_pair(r, r->ops->draw, &a, &b, what);
        a = b;
    }
    TEST_ASSERT_TRUE_MESSAGE(moved > 60, "the stack does not move");
    omni_rect_t pin;
    dw_t7_pin_rect(30, &pin);
    TEST_ASSERT_TRUE(pin.x1 - pin.x0 <= 9 && pin.y1 - pin.y0 <= 9);
    a = in_at(10, 9, 59, 980);   /* the minute: the whole frame */
    const fk_face_in_t b = later(a, 40);
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &a, &b, rects));
    TEST_ASSERT_EQUAL_INT(W, rects[0].x1 - rects[0].x0);
}

static void rotor_lift_period(void)
{
    TEST_ASSERT_EQUAL_FLOAT(dw_rotor_lift(0), dw_rotor_lift(2000));
    TEST_ASSERT_TRUE(dw_rotor_lift(0) < 0.001f);
    TEST_ASSERT_TRUE(dw_rotor_lift(1000) > 0.999f);
    for (uint32_t ms = 0; ms < 2000; ms++) TEST_ASSERT_TRUE(dw_rotor_lift(ms) <= dw_rotor_lift(1000));
    TEST_ASSERT_TRUE(dw_rotor_lift(500) > dw_rotor_lift(250));   /* rising on the even second */
    TEST_ASSERT_TRUE(dw_rotor_lift(1500) < dw_rotor_lift(1250)); /* falling on the odd one */
}

static long luma_in(const uint16_t *fb, const omni_rect_t *box)
{
    long sum = 0;
    for (int y = box->y0; y < box->y1; y++)
        for (int x = box->x0; x < box->x1; x++) {
            const uint16_t p = fb[y * W + x];
            sum += ((p >> 11) & 31) * 2 + ((p >> 5) & 63) + (p & 31) * 2;
        }
    return sum;
}

/* the wake is a take-off: only the column changes, the stack rising as the core brightens; reduced motion holds the
 * stack still at mid-stroke */
static void rotor_takeoff(void)
{
    const row_t *r = row_of("rotor");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &in, s_c);   /* settled */
    long prev = -1;
    for (uint32_t w = 150; w <= 1250; w += 275) {
        in.wake_ms = w;
        row_frame(r, r->ops->draw, &in, s_a);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
                if (s_a[y * W + x] != s_c[y * W + x])
                    TEST_ASSERT_TRUE_MESSAGE(inside(&DW_T7_COLUMN, x, y), "the take-off changes a pixel outside the column");
        const long l = luma_in(s_a, &DW_T7_COLUMN);
        TEST_ASSERT_TRUE_MESSAGE(l > prev, "the core does not brighten through the take-off");
        prev = l;
    }
    in.wake_ms = DW_T7_WAKE_MS;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "the take-off's end is the settled frame");
    /* reduced motion: still within a second and between seconds but for the pinpoints */
    fk_face_in_t a = in_at(10, 9, 30, 0);
    a.reduced = true;
    for (int ms = 40; ms < 1000; ms += 40) {
        const fk_face_in_t b = later(a, ms);
        omni_rect_t rects[8];
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    }
    fk_face_in_t b = later(a, 1000);
    dirty_pair(r, r->ops->draw, &a, &b, "reduced: the second's tick");
    omni_rect_t rects[8];
    const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
    TEST_ASSERT_EQUAL_UINT8(2, n);   /* the two pinpoints, no column */
    b = a;
    b.reduced = false;
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &a, &b, rects));   /* the setting's change: the whole frame */
}

/* unset: the stack at rest (no stroke), the core dark */
static void rotor_unset_at_rest(void)
{
    const row_t *r = row_of("rotor");
    fk_face_in_t a = in_at(10, 9, 30, 0), b = in_at(10, 9, 31, 0);
    a.clk.set = b.clk.set = false;
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    row_frame(r, r->ops->draw, &a, s_a);
    const fk_face_in_t lit = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &lit, s_b);
    TEST_ASSERT_TRUE_MESSAGE(luma_in(s_a, &DW_T7_COLUMN) * 2 < luma_in(s_b, &DW_T7_COLUMN), "the unset core is not dark");
}

/* ---- #11: Coral */
static void coral_cells(void)
{
    const row_t *r = row_of("coral");
    fk_img_t base;
    img_of(ASSET_DW_K11_BASE, &base);
    fk_face_in_t in = in_at(10, 9, 37, 500);
    row_frame(r, r->ops->draw, &in, s_a);
    for (int i = 0; i < 60; i++) {
        TEST_ASSERT_EQUAL_UINT8(i < 37 ? 1 : i == 37 ? 2 : 0, dw_k11_cell_state(&in, i));
        float x, y;
        dw_k11_cell_xy(i, &x, &y);
        const int px = (int)x, py = (int)y;
        const uint16_t p = s_a[py * W + px], b = base.rgb[py * W + px];
        const int R = omni_ex_r(p), G = omni_ex_g(p), B = omni_ex_b(p);
        char m[64];
        snprintf(m, sizeof m, "cell %d at :37", i);
        if (i < 37) TEST_ASSERT_TRUE_MESSAGE(p != b && R > B + 30 && R >= G, m);   /* gold */
        else if (i == 37) TEST_ASSERT_TRUE_MESSAGE(G >= 0xf0 && B >= 0xd8 && R < G, m);   /* teal */
        else TEST_ASSERT_TRUE_MESSAGE(p == b, m);   /* unlit: the base */
    }
    in = in_at(10, 10, 0, 300);   /* :00: every cell reset, the first one current */
    row_frame(r, r->ops->draw, &in, s_a);
    for (int i = 0; i < 60; i++) {
        TEST_ASSERT_EQUAL_UINT8(i == 0 ? 2 : 0, dw_k11_cell_state(&in, i));
        float x, y;
        dw_k11_cell_xy(i, &x, &y);
        const int px = (int)x, py = (int)y;
        if (i) TEST_ASSERT_EQUAL_HEX16(base.rgb[py * W + px], s_a[py * W + px]);
    }
    in.clk.set = false;
    for (int i = 0; i < 60; i++) TEST_ASSERT_EQUAL_UINT8(0, dw_k11_cell_state(&in, i));
}

/* the hard rule (spec section 12): no strut or cell touches an under-line numeral, whatever the digit */
static void coral_underline_no_touch(void)
{
    for (int g = 0; g < 4; g++) {
        float gx, gy;
        dw_k11_underline_xy(g, &gx, &gy);
        for (int d = 0; d <= DW_G_EMPTY; d++) {
            for (int k = 0; k < DW_K11_STRUT_N; k++) {
                const dw_bezier_t *b = &DW_K11_STRUTS[k];
                for (int i = 0; i <= 200; i++) {   /* the stroke as discs along the curve */
                    const float t = (float)i / 200.f, u = 1 - t;
                    const float x = u * u * u * b->x0 + 3 * u * u * t * b->c1x + 3 * u * t * t * b->c2x + t * t * t * b->x1;
                    const float y = u * u * u * b->y0 + 3 * u * u * t * b->c1y + 3 * u * t * t * b->c2y + t * t * t * b->y1;
                    TEST_ASSERT_TRUE_MESSAGE(dw_glyph_clearance(d, gx, gy, DW_K11_G_R, x, y, 0.f, b->w) > 0.f,
                                             "a strut touches an under-line numeral");
                }
            }
            for (int c = 0; c < 60; c++) {
                float x, y;
                dw_k11_cell_xy(c, &x, &y);
                TEST_ASSERT_TRUE_MESSAGE(dw_glyph_clearance(d, gx, gy, DW_K11_G_R, x, y, 0.f, 2 * DW_K11_CELL_R) > 0.f,
                                         "a cell touches an under-line numeral");
            }
            /* the colon's glow box and the AM/PM line stay clear too */
            TEST_ASSERT_TRUE(gy - DW_G_LINE_OUT * DW_K11_G_R - 1.f > (float)DW_K11_COLON.y1);
        }
    }
}

static void coral_underline_follows_h12(void)
{
    fk_face_in_t in = in_at(21, 7, 0, 0);
    int d[4];
    dw_k11_underline(&in, d);
    TEST_ASSERT_EQUAL_INT(0, d[0]), TEST_ASSERT_EQUAL_INT(9, d[1]), TEST_ASSERT_EQUAL_INT(0, d[2]);
    TEST_ASSERT_EQUAL_INT(7, d[3]);
    in.h24 = true;
    dw_k11_underline(&in, d);
    TEST_ASSERT_EQUAL_INT(2, d[0]), TEST_ASSERT_EQUAL_INT(1, d[1]), TEST_ASSERT_EQUAL_INT(0, d[2]);
    TEST_ASSERT_EQUAL_INT(7, d[3]);
    in = in_at(0, 30, 0, 0);   /* 12 h: 12:30 AM */
    dw_k11_underline(&in, d);
    TEST_ASSERT_EQUAL_INT(1, d[0]), TEST_ASSERT_EQUAL_INT(2, d[1]);
    in.clk.set = false;
    dw_k11_underline(&in, d);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT(DW_G_EMPTY, d[i]);
    /* and the frame follows it: 12 h and 24 h differ in the under-line (and the digits) at 21:07 */
    const row_t *r = row_of("coral");
    in = in_at(21, 7, 0, 0);
    row_frame(r, r->ops->draw, &in, s_a);
    in.h24 = true;
    row_frame(r, r->ops->draw, &in, s_b);
    float gx, gy;
    dw_k11_underline_xy(0, &gx, &gy);
    long differ = 0;
    for (int y = (int)gy - 14; y < (int)gy + 14; y++)
        for (int x = (int)gx - 14; x < (int)gx + 14; x++) differ += s_a[y * W + x] != s_b[y * W + x];
    TEST_ASSERT_TRUE_MESSAGE(differ > 10, "the under-line's first numeral does not follow 12/24 h");
}

/* the colon's frames redraw only its box; a second's tick only the old and the new cell; reduced motion holds the slug */
static void coral_motion_confined(void)
{
    const row_t *r = row_of("coral");
    TEST_ASSERT_EQUAL_FLOAT(dw_coral_slug(0), dw_coral_slug(2200));
    TEST_ASSERT_TRUE(dw_coral_slug(550) > 0.999f && dw_coral_slug(1650) < -0.999f);
    fk_face_in_t a = in_at(10, 9, 28, 0);
    int colon = 0;
    for (int step = 0; step < 75; step++) {
        const fk_face_in_t b = later(a, 40);
        omni_rect_t rects[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
        for (int k = 0; k < n; k++) {
            if (rect_in(&rects[k], &DW_K11_COLON)) {
                colon++;
                continue;
            }
            TEST_ASSERT_TRUE_MESSAGE(a.clk.s != b.clk.s, "a rect outside the colon within a second");
            TEST_ASSERT_TRUE_MESSAGE((rects[k].x1 - rects[k].x0) * (rects[k].y1 - rects[k].y0) <= 40 * 40,
                                     "a cell's rect over 40 px");
        }
        char what[48];
        snprintf(what, sizeof what, "coral at :%02d.%03d + 40 ms", a.clk.s, a.clk.ms);
        dirty_pair(r, r->ops->draw, &a, &b, what);
        a = b;
    }
    TEST_ASSERT_TRUE_MESSAGE(colon > 60, "the slug does not move");
    a = in_at(10, 9, 30, 0);
    a.reduced = true;
    for (int ms = 40; ms < 1000; ms += 40) {
        const fk_face_in_t b = later(a, ms);
        omni_rect_t rects[8];
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
        dirty_pair(r, r->ops->draw, &a, &b, "coral reduced: within a second");
    }
    fk_face_in_t w = a;   /* review M-d: reduced motion skips the wake's fade */
    w.wake_ms = 300;
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &w, &a, rects));
    dirty_pair(r, r->ops->draw, &w, &a, "coral reduced: the wake's fade skipped");
}

/* review I1: a skipped or stepped-back second (a sync, a stall) redraws every cell between the two */
static void coral_skipped_seconds(void)
{
    const row_t *r = row_of("coral");
    static const int ss[3][2] = {{10, 12}, {40, 20}, {0, 59}};
    for (int k = 0; k < 3; k++) {
        const fk_face_in_t a = in_at(10, 9, ss[k][0], 500), b = in_at(10, 9, ss[k][1], 500);
        char what[48];
        snprintf(what, sizeof what, "coral :%02d -> :%02d", ss[k][0], ss[k][1]);
        dirty_pair(r, r->ops->draw, &a, &b, what);
    }
}

/* review M-a: the glance's worst cases stay under 5 % lit at every orbit offset, and no minute of the day passes it in
 * 12 h or 24 h */
/* the glance's lit % at in (fk_test_lit) */
static float glance_pct(const row_t *r, const fk_face_in_t *in)
{
    row_frame(r, r->ops->glance, in, s_a);
    float pct;
    uint8_t maxc;
    fk_test_lit(s_a, &pct, &maxc);
    return pct;
}

static void coral_glance_worst_case(void)
{
    const row_t *r = row_of("coral");
    static const int hm[2][2] = {{8, 8}, {0, 8}};
    for (int k = 0; k < 2; k++)
        for (int o = 0; o < 9; o++) {
            fk_face_in_t in = in_at(hm[k][0], hm[k][1], 12, 300);
            in.glance = true, in.h24 = true, in.odx = ORB9[o][0], in.ody = ORB9[o][1];
            TEST_ASSERT_TRUE_MESSAGE(glance_pct(r, &in) < 5.f, "the coral glance lights 5 % or more (24 h)");
        }
    for (int h24 = 0; h24 < 2; h24++)
        for (int t = 0; t < 1440; t++) {
            fk_face_in_t in = in_at(t / 60, t % 60, 12, 300);
            in.glance = true, in.h24 = h24 != 0;
            if (glance_pct(r, &in) >= 5.f) {
                char m[64];
                snprintf(m, sizeof m, "the coral glance at %02d:%02d (%s) lights 5 %% or more", t / 60, t % 60,
                         h24 ? "24 h" : "12 h");
                TEST_FAIL_MESSAGE(m);
            }
        }
}

/* review M-b, M-c: the chime does not move the core under reduced motion; an unset clock has no take-off */
static void rotor_reduced_chime_unset_takeoff(void)
{
    const row_t *r = row_of("rotor");
    omni_rect_t rects[8];
    fk_face_in_t a = in_at(10, 9, 30, 500), b = a;
    b.chime = 0.8f;
    TEST_ASSERT_TRUE(r->ops->dirty(r->ctx, &a, &b, rects) > 0);   /* awake, normal: the core breathes */
    dirty_pair(r, r->ops->draw, &a, &b, "the chime's breath");
    a.reduced = b.reduced = true;
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    row_frame(r, r->ops->draw, &a, s_c);
    row_frame(r, r->ops->draw, &b, s_b);
    assert_same(s_c, s_b, "reduced: the chime leaves the core");
    a = in_at(10, 9, 30, 500), a.clk.set = false;
    b = a, b.wake_ms = 300;
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    row_frame(r, r->ops->draw, &a, s_c);
    row_frame(r, r->ops->draw, &b, s_b);
    assert_same(s_c, s_b, "unset: no take-off");
}

/* ---- #14 Circle Script, #15 the key, #16 Learning (Task 15) */
/* the farthest a digit's ink reaches from its centre (dw_glyph_clearance's extent) */
static float glyph_reach(int d, float r)
{
    return (dw_glyph(d).lines ? DW_G_LINE_OUT * r : r) + 0.5f * dw_glyph_stroke(r);
}

/* the gap between a glyph (every digit) and a box's nearest point */
static float box_gap(const dw_slot_t *s, const omni_rect_t *b)
{
    const float nx = s->cx < b->x0 ? (float)b->x0 : s->cx > b->x1 ? (float)b->x1 : s->cx;
    const float ny = s->cy < b->y0 ? (float)b->y0 : s->cy > b->y1 ? (float)b->y1 : s->cy;
    return sqrtf((nx - s->cx) * (nx - s->cx) + (ny - s->cy) * (ny - s->cy)) - glyph_reach(9, s->r);
}

/* the innermost radius about the centre where an A8 image (466 square, its top-left at 0, 0) has ink */
static float ink_r_min(const fk_img_t *im)
{
    float m = 1e9f;
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++)
            if (im->a[y * im->w + x]) {
                const float dx = x + 0.5f - 233.f, dy = y + 0.5f - 233.f, rr = sqrtf(dx * dx + dy * dy);
                if (rr < m) m = rr;
            }
    return m;
}

/* the hard rule (spec section 12) on each layout: no decorative line, box or ring comes near a numeral */
static void g14_layout_no_touch(void)
{
    for (int i = 0; i < 4; i++) {
        const dw_slot_t *s = &DW_G14_SLOTS[i];
        TEST_ASSERT_EQUAL_FLOAT(40.f, s->r);
        for (int d = 0; d <= DW_G_EMPTY; d++) {
            TEST_ASSERT_TRUE_MESSAGE(dw_glyph_clearance(d, s->cx, s->cy, s->r, 233, 233, DW_G14_CIRCLE_R,
                                                        DW_G14_CIRCLE_W) > 0, "a ring touches the inner circle");
            TEST_ASSERT_TRUE_MESSAGE(dw_glyph_clearance(d, s->cx, s->cy, s->r, 233, 233, DW_G14_DOTS_R, 3.4f) > 0,
                                     "a ring touches the fine dots");
            TEST_ASSERT_TRUE_MESSAGE(dw_glyph_clearance(d, s->cx, s->cy, s->r, 233, 233, DW_G14_BAND_R0, 1.f) > 0,
                                     "a ring touches the band's inner edge");
            TEST_ASSERT_TRUE_MESSAGE(dw_glyph_clearance(d, s->cx, s->cy, s->r, 233, 233, DW_G14_SCRIPT_R0, 0.f) > 0,
                                     "a ring reaches the script strip");
            for (int k = 0; k < 2; k++) {   /* the date's numerals and their knocked-out discs */
                float x, y;
                dw_g14_date_xy(k, &x, &y);
                TEST_ASSERT_TRUE(dw_glyph_clearance(d, s->cx, s->cy, s->r, x, y, 0.f, 2 * DW_G14_KNOCK_R) > 0);
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(box_gap(s, &DW_G14_STALE) > 0, "the stale marker touches a ring");
        TEST_ASSERT_TRUE_MESSAGE(box_gap(s, &DW_G14_HINT) > 0, "the hint touches a ring");
        for (int j = 0; j < 4; j++)   /* nor one ring another */
            if (j != i) {
                const dw_slot_t *t = &DW_G14_SLOTS[j];
                TEST_ASSERT_TRUE(hypotf(s->cx - t->cx, s->cy - t->cy) > 2 * glyph_reach(9, s->r));
            }
    }
    /* the date's numerals and what is left of the circle: its arc outside the knocked-out discs */
    for (int k = 0; k < 2; k++) {
        float gx, gy;
        dw_g14_date_xy(k, &gx, &gy);
        for (int d = 0; d <= DW_G_EMPTY; d++) {
            float near = 1e9f;
            for (int i = 0; i < 3600; i++) {
                float sn, cs;
                fk_sincos((float)i / 3600.f * 2 * PI_F, &sn, &cs);
                const float x = 233.f + DW_G14_CIRCLE_R * sn, y = 233.f - DW_G14_CIRCLE_R * cs;
                bool knocked = false;
                for (int q = 0; q < 2; q++) {
                    float kx, ky;
                    dw_g14_date_xy(q, &kx, &ky);
                    knocked |= hypotf(x - kx, y - ky) < DW_G14_KNOCK_R;
                }
                if (!knocked && hypotf(x - gx, y - gy) < near) near = hypotf(x - gx, y - gy);
            }
            TEST_ASSERT_TRUE_MESSAGE(near - 0.5f * DW_G14_CIRCLE_W - glyph_reach(d, DW_G14_DATE_R) > 0,
                                     "the circle touches a date numeral");
        }
    }
    /* #16: the twins, the HOURS / MINUTES labels' line, the "?" button, the stale marker and the band's ink */
    fk_img_t ring;
    TEST_ASSERT_NOT_NULL(img_of(ASSET_DW_G16_RING, &ring));
    const float ink = ink_r_min(&ring);
    for (int i = 0; i < 4; i++) {
        const dw_slot_t *s = &DW_G16_SLOTS[i];
        TEST_ASSERT_EQUAL_FLOAT(34.f, s->r);
        for (int j = 0; j < 4; j++) {
            omni_rect_t b;
            dw_g16_twin_box(j, &b);
            TEST_ASSERT_TRUE_MESSAGE(box_gap(s, &b) > 0, "a twin touches a ring");
        }
        TEST_ASSERT_TRUE_MESSAGE(box_gap(s, &DW_G16_HELP) > 0, "the ? button touches a ring");
        TEST_ASSERT_TRUE_MESSAGE(box_gap(s, &DW_G16_STALE) > 0, "the stale marker touches a ring");
        for (int d = 0; d <= DW_G_EMPTY; d++)
            TEST_ASSERT_TRUE_MESSAGE(dw_glyph_clearance(d, s->cx, s->cy, s->r, 233, 233, ink, 0.f) > 0,
                                     "a ring reaches the learning band's ink");
    }
    /* #15: on every page, set and unset, no glyph touches another, the pips or the back button */
    for (int p = 0; p < DW_KEY_PAGES; p++)
        for (int set = 0; set < 2; set++) {
            fk_face_in_t in = in_at(21, 7, 30, 0);
            in.page = (uint8_t)p, in.clk.set = set != 0;
            dw_slot_t sl[12];
            const int n = dw_key_slots(&in, sl);
            TEST_ASSERT_TRUE(n > 0 && n <= 12);
            for (int i = 0; i < n; i++) {
                for (int j = i + 1; j < n; j++)
                    TEST_ASSERT_TRUE_MESSAGE(hypotf(sl[i].cx - sl[j].cx, sl[i].cy - sl[j].cy) >
                                                 glyph_reach(9, sl[i].r) + glyph_reach(9, sl[j].r),
                                             "two of the key's glyphs touch");
                TEST_ASSERT_TRUE(box_gap(&sl[i], &DW_KEY_BACK) > 0);
                TEST_ASSERT_TRUE(sl[i].cy + glyph_reach(9, sl[i].r) < 390.f);   /* the pips at 400 */
            }
        }
}

/* the band: 0.6 degrees a second clockwise (n / 600 entries, rounded), once round in ten minutes, held still when unset
 * or under reduced motion */
static void g14_band_step(void)
{
    const int n = DW_G14_SCRIPT_NT;
    const fk_face_in_t z = in_at(10, 0, 0, 0);
    for (int t = 0; t < 1300; t++) {
        const fk_face_in_t a = later(z, t * 1000 + 300), b = later(a, 1000);
        const int step = ((int)dw_g14_script_off(&a) - (int)dw_g14_script_off(&b) + n) % n;   /* clockwise: down */
        TEST_ASSERT_TRUE_MESSAGE(fabsf((float)step - (float)n / 600.f) <= 1.f, "a step is not n / 600 +- 1");
        const fk_face_in_t c = later(a, 600000), e = later(a, 400);   /* ten minutes on; later in the second */
        TEST_ASSERT_EQUAL_UINT16(dw_g14_script_off(&a), dw_g14_script_off(&c));
        TEST_ASSERT_EQUAL_UINT16(dw_g14_script_off(&a), dw_g14_script_off(&e));
    }
    TEST_ASSERT_EQUAL_UINT16(0, dw_g14_script_off(&z));   /* :x0:00 is the start */
    fk_face_in_t u = in_at(10, 3, 17, 0);
    TEST_ASSERT_TRUE(dw_g14_script_off(&u) != 0);
    u.reduced = true;
    TEST_ASSERT_EQUAL_UINT16(0, dw_g14_script_off(&u));
    u.reduced = false, u.clk.set = false;
    TEST_ASSERT_EQUAL_UINT16(0, dw_g14_script_off(&u));
}

/* a slot's box in fb holds exactly digit d drawn alone on black (nothing else is drawn there) */
/* a pixel within a slot's reach (any digit's ink, + 1.5 px): what is drawn there is the glyph alone */
static bool in_reach(const dw_slot_t *s, int x, int y)
{
    return hypotf(x + 0.5f - s->cx, y + 0.5f - s->cy) < glyph_reach(9, s->r) + 1.5f;
}

static void slot_shows(const uint16_t *fb, const dw_slot_t *s, int d, uint16_t ink, const char *what)
{
    omni_rect_t bb;
    dw_glyph_bbox(s->cx, s->cy, s->r, &bb);
    memset(s_c, 0, sizeof s_c);
    const omni_rect_t all = {0, 0, W, W};
    dw_glyph_draw(s_c, W, &all, d, s->cx, s->cy, s->r, ink, 255, 1.f);
    for (int y = bb.y0; y < bb.y1; y++)
        for (int x = bb.x0; x < bb.x1; x++)
            if (in_reach(s, x, y) && fb[y * W + x] != s_c[y * W + x]) {
                char m[128];
                snprintf(m, sizeof m, "%s: digit %d's box differs at (%d, %d)", what, d, x, y);
                TEST_FAIL_MESSAGE(m);
            }
}

#define G_INK RGB565(0xf3, 0xd5, 0x94)

/* D5: 21:07 is 2 1 0 7 on #14 and #16 whatever the shared 12/24 h, and the rings show exactly those digits */
static void g14_24h(void)
{
    fk_face_in_t in = in_at(21, 7, 30, 0);
    in.h24 = false;
    int d[4];
    dw_g14_digits(&in, d);
    TEST_ASSERT_EQUAL_INT(2, d[0]), TEST_ASSERT_EQUAL_INT(1, d[1]), TEST_ASSERT_EQUAL_INT(0, d[2]);
    TEST_ASSERT_EQUAL_INT(7, d[3]);
    static const char *const keys[2] = {"circles", "learn"};
    for (int f = 0; f < 2; f++) {
        const row_t *r = row_of(keys[f]);
        row_frame(r, r->ops->draw, &in, s_a);
        in.h24 = true;
        row_frame(r, r->ops->draw, &in, s_b);
        in.h24 = false;
        assert_same(s_a, s_b, "12 h and 24 h draw the same frame");
        for (int i = 0; i < 4; i++) slot_shows(s_a, f ? &DW_G16_SLOTS[i] : &DW_G14_SLOTS[i], d[i], G_INK, keys[f]);
    }
    in.clk.set = false;
    dw_g14_digits(&in, d);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT(DW_G_EMPTY, d[i]);
}

static void g14_tap_opens_key(void)
{
    const row_t *r = row_of("circles");
    TEST_ASSERT_NOT_NULL(r->ops->tap);
    static const int16_t pts[6][2] = {{233, 233}, {176, 172}, {60, 233}, {233, 420}, {400, 233}, {0, 0}};
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_INT(FK_TAP_SCREEN, r->ops->tap(r->ctx, pts[i][0], pts[i][1]));
}

/* the bead runs and the band steps: each 40 ms step and each skipped or stepped-back second is covered by dirty */
static void g14_motion(void)
{
    const row_t *r = row_of("circles");
    fk_face_in_t a = in_at(10, 9, 28, 600);
    int moved = 0;
    for (int step = 0; step < 40; step++) {
        const fk_face_in_t b = later(a, 40);
        omni_rect_t rects[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
        if (a.clk.s == b.clk.s) {   /* within a second only the bead: two small boxes */
            TEST_ASSERT_TRUE_MESSAGE(n >= 1 && n <= 2, "the bead's step is not its boxes");
            for (int k = 0; k < n; k++)
                TEST_ASSERT_TRUE((rects[k].x1 - rects[k].x0) * (rects[k].y1 - rects[k].y0) <= 34 * 34);
            moved++;
        }
        char what[48];
        snprintf(what, sizeof what, "#14 at :%02d.%03d + 40 ms", a.clk.s, a.clk.ms);
        dirty_pair(r, r->ops->draw, &a, &b, what);
        a = b;
    }
    TEST_ASSERT_TRUE_MESSAGE(moved > 30, "the bead does not move");
    static const int jumps[5][2] = {{21, 23}, {21, 40}, {40, 12}, {59, 0}, {0, 59}};   /* within the minute */
    for (int k = 0; k < 5; k++) {
        const fk_face_in_t p = in_at(10, 9, jumps[k][0], 500), q = in_at(10, 9, jumps[k][1], 100);
        char what[48];
        snprintf(what, sizeof what, "#14 :%02d -> :%02d", jumps[k][0], jumps[k][1]);
        dirty_pair(r, r->ops->draw, &p, &q, what);
    }
    const fk_face_in_t p = in_at(10, 9, 30, 500), q = in_at(10, 4, 30, 500);   /* a sync back five minutes */
    dirty_pair(r, r->ops->draw, &p, &q, "#14 back five minutes");
}

/* the wake: the rings draw on in reading order (about 1.1 s), the band fades in, the hint shows for 2.5 s and fades;
 * at its end the frame is the settled one. Reduced motion: everything at once, nothing within a second */
static void g14_wake_and_reduced(void)
{
    const row_t *r = row_of("circles");
    fk_face_in_t in = in_at(21, 7, 30, 0), set = in;
    row_frame(r, r->ops->draw, &set, s_b);
    int d[4];
    dw_g14_digits(&in, d);
    in.wake_ms = 0;
    row_frame(r, r->ops->draw, &in, s_a);
    for (int i = 0; i < 4; i++) {
        omni_rect_t bb;
        dw_glyph_bbox(DW_G14_SLOTS[i].cx, DW_G14_SLOTS[i].cy, DW_G14_SLOTS[i].r, &bb);
        long ink = 0;
        for (int y = bb.y0; y < bb.y1; y++)
            for (int x = bb.x0; x < bb.x1; x++) ink += in_reach(&DW_G14_SLOTS[i], x, y) && s_a[y * W + x] != 0;
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, ink, "a ring is drawn at the wake's first frame");
    }
    in.wake_ms = 400;   /* the first ring under way, the last not yet begun */
    row_frame(r, r->ops->draw, &in, s_a);
    long first = 0, last = 0;
    {
        omni_rect_t b0, b3;
        dw_glyph_bbox(DW_G14_SLOTS[0].cx, DW_G14_SLOTS[0].cy, 40, &b0);
        dw_glyph_bbox(DW_G14_SLOTS[3].cx, DW_G14_SLOTS[3].cy, 40, &b3);
        for (int y = b0.y0; y < b0.y1; y++)
            for (int x = b0.x0; x < b0.x1; x++) first += in_reach(&DW_G14_SLOTS[0], x, y) && s_a[y * W + x] != 0;
        for (int y = b3.y0; y < b3.y1; y++)
            for (int x = b3.x0; x < b3.x1; x++) last += in_reach(&DW_G14_SLOTS[3], x, y) && s_a[y * W + x] != 0;
    }
    TEST_ASSERT_TRUE_MESSAGE(first > 50 && last == 0, "the rings do not draw on in reading order");
    in.wake_ms = 1300;   /* drawn on: the rings are the still ones */
    row_frame(r, r->ops->draw, &in, s_a);
    for (int i = 0; i < 4; i++) slot_shows(s_a, &DW_G14_SLOTS[i], d[i], G_INK, "drawn on by 1.3 s");
    long hint = 0;
    for (int y = DW_G14_HINT.y0; y < DW_G14_HINT.y1; y++)
        for (int x = DW_G14_HINT.x0; x < DW_G14_HINT.x1; x++) hint += s_a[y * W + x] != s_b[y * W + x];
    TEST_ASSERT_TRUE_MESSAGE(hint > 100, "no hint in the wake");
    in.wake_ms = DW_G14_WAKE_MS;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_b, s_a, "the wake's end is the settled frame");
    fk_face_in_t w0 = set, w1 = set;   /* the wake's frames are covered */
    for (uint32_t t = 0; t < DW_G14_WAKE_MS + 200; t += 100) {
        w0.wake_ms = t, w1.wake_ms = t + 100 >= DW_G14_WAKE_MS ? UINT32_MAX : t + 100;
        dirty_pair(r, r->ops->draw, &w0, &w1, "#14's wake");
    }
    /* reduced motion: the wake shows at once, the band holds, the bead steps once a second */
    fk_face_in_t a = in_at(21, 7, 30, 0);
    a.reduced = true;
    row_frame(r, r->ops->draw, &a, s_b);
    fk_face_in_t w = a;
    w.wake_ms = 300;
    row_frame(r, r->ops->draw, &w, s_a);
    assert_same(s_b, s_a, "reduced: the wake shows at once");
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &w, &a, rects));
    for (int ms = 40; ms < 1000; ms += 40) {
        const fk_face_in_t b = later(a, ms);
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
        dirty_pair(r, r->ops->draw, &a, &b, "#14 reduced: within a second");
    }
    const fk_face_in_t b = later(a, 1000);
    const long u = dirty_pair(r, r->ops->draw, &a, &b, "#14 reduced: the second");
    TEST_ASSERT_TRUE_MESSAGE(u <= 2 * 34 * 34, "reduced: more than the bead's boxes at the second");
}

/* unset: four empty rings, dim, breathing slowly (on mono_ms; still under reduced motion), no bead, no date */
static void g14_unset_breath(void)
{
    const row_t *r = row_of("circles");
    fk_face_in_t a = in_at(10, 9, 30, 0);
    a.clk.set = false;
    long changed = 0;
    for (uint32_t t = 0; t < 4000; t += 40) {
        fk_face_in_t p = a, q = a;
        p.mono_ms = t, q.mono_ms = t + 40;
        dirty_pair(r, r->ops->draw, &p, &q, "#14 unset: the breath");
        row_frame(r, r->ops->draw, &p, s_a);
        row_frame(r, r->ops->draw, &q, s_b);
        for (int i = 0; i < W * W; i++) changed += s_a[i] != s_b[i];
    }
    TEST_ASSERT_TRUE_MESSAGE(changed > 1000, "the unset rings do not breathe");
    fk_face_in_t p = a, q = a;
    p.reduced = q.reduced = true;
    p.mono_ms = 0, q.mono_ms = 1000;
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &p, &q, rects));
    row_frame(r, r->ops->draw, &p, s_a);
    row_frame(r, r->ops->draw, &q, s_b);
    assert_same(s_a, s_b, "reduced: the unset rings hold still");
    p.reduced = false, p.mono_ms = 1000;   /* the breath is dim: never the set ink */
    row_frame(r, r->ops->draw, &p, s_a);
    for (int i = 0; i < W * W; i++) TEST_ASSERT_TRUE(s_a[i] != G_INK);
}

/* the glance's worst cases stay under 5 % lit (every minute of the day, the worst at each orbit offset) */
/* the key: every glyph on every page is at least 38 px (r >= 19), set, unset and while a page builds */
static void key_glyph_size(void)
{
    static const uint32_t wake[4] = {0, 500, 5000, UINT32_MAX};
    for (int p = 0; p < DW_KEY_PAGES; p++)
        for (int set = 0; set < 2; set++)
            for (int w = 0; w < 4; w++) {
                fk_face_in_t in = in_at(21, 7, 30, 0);
                in.page = (uint8_t)p, in.clk.set = set != 0, in.wake_ms = wake[w];
                dw_slot_t sl[12];
                const int n = dw_key_slots(&in, sl);
                TEST_ASSERT_TRUE(n >= 4);
                for (int i = 0; i < n; i++) TEST_ASSERT_TRUE_MESSAGE(sl[i].r >= 19.f, "a key glyph under 38 px");
            }
    fk_face_in_t in = in_at(21, 7, 30, 0);
    dw_slot_t sl[12];
    TEST_ASSERT_EQUAL_INT(10, dw_key_slots(&in, sl));   /* page 0: the ten numerals */
}

/* page 3 (2): the time frozen when the page opened: frames 5 s apart read the same time and draw it the same */
static void key_page3_frozen(void)
{
    s_page = 2;
    const row_t *r = row_of("key");
    s_page = 0;
    fk_face_in_t a = in_at(21, 7, 58, 0), b = in_at(21, 8, 3, 0);
    a.page = b.page = 2;
    a.wake_ms = 1000, b.wake_ms = 6000;
    int da[4], db[4];
    dw_key_time(&a, da);
    dw_key_time(&b, db);
    TEST_ASSERT_EQUAL_INT_ARRAY(da, db, 4);
    TEST_ASSERT_EQUAL_INT(2, da[0]), TEST_ASSERT_EQUAL_INT(1, da[1]), TEST_ASSERT_EQUAL_INT(0, da[2]);
    TEST_ASSERT_EQUAL_INT(7, da[3]);
    /* b's frame reads no clock but its frozen time: the clock 30 s earlier with the same page time draws it too */
    fk_face_in_t c = in_at(21, 7, 33, 0);
    c.page = 2, c.wake_ms = 6000;
    row_frame(r, r->ops->draw, &b, s_a);
    row_frame(r, r->ops->draw, &c, s_b);
    assert_same(s_a, s_b, "page 3 draws the frozen time");
    a.wake_ms = b.wake_ms = UINT32_MAX;   /* settled: the clock itself */
    dw_key_time(&b, db);
    TEST_ASSERT_EQUAL_INT(8, db[3]);
    fk_face_in_t m = in_at(0, 0, 2, 0);   /* opened before midnight: 23:59 */
    m.page = 2, m.wake_ms = 5000;
    dw_key_time(&m, db);
    TEST_ASSERT_EQUAL_INT(2, db[0]), TEST_ASSERT_EQUAL_INT(3, db[1]), TEST_ASSERT_EQUAL_INT(5, db[2]);
    TEST_ASSERT_EQUAL_INT(9, db[3]);
}

/* the key's taps: the back button (>= 44 px, probed at its edges) goes back, anywhere else turns the page; the page 1
 * sum cycles 5 + n, still under reduced motion; page 0 builds at once under reduced motion */
static void key_tap_sum_reduced(void)
{
    const row_t *r = row_of("key");
    TEST_ASSERT_NOT_NULL(r->ops->tap);
    TEST_ASSERT_TRUE(DW_KEY_BACK.x1 - DW_KEY_BACK.x0 >= 44 && DW_KEY_BACK.y1 - DW_KEY_BACK.y0 >= 44);
    fk_test_tap_edges(r->ops, r->ctx, DW_KEY_BACK, FK_TAP_BACK);
    TEST_ASSERT_EQUAL_INT(FK_TAP_NEXT, r->ops->tap(r->ctx, 233, 233));
    TEST_ASSERT_EQUAL_INT(FK_TAP_NEXT, r->ops->tap(r->ctx, 233, 300));
    fk_face_in_t in = in_at(10, 9, 30, 0);
    in.page = 1;
    int n;
    uint8_t lvl;
    for (int k = 0; k < 10; k++) {
        in.wake_ms = (uint32_t)(k * DW_KEY_SUM_MS + 1200);
        dw_key_sum(&in, &n, &lvl);
        TEST_ASSERT_EQUAL_INT(k % 5, n);
        TEST_ASSERT_EQUAL_UINT8(255, lvl);
    }
    in.wake_ms = DW_KEY_SUM_MS + 100;
    dw_key_sum(&in, &n, &lvl);
    TEST_ASSERT_TRUE(lvl < 255);   /* the result fades in */
    s_page = 1;
    const row_t *r1 = row_of("key");
    s_page = 0;
    fk_face_in_t p = in, q = in;
    p.reduced = q.reduced = true;
    p.wake_ms = 100, q.wake_ms = 5000;
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r1->ops->dirty(r1->ctx, &p, &q, rects));
    row_frame(r1, r1->ops->draw, &p, s_a);
    row_frame(r1, r1->ops->draw, &q, s_b);
    assert_same(s_a, s_b, "reduced: the sum holds still");
    for (uint32_t t = 0; t < 2 * DW_KEY_SUM_MS; t += 40) {   /* the sum's frames are covered */
        p = q = in;
        p.wake_ms = t, q.wake_ms = t + 40;
        dirty_pair(r1, r1->ops->draw, &p, &q, "the key's sum");
    }
    fk_face_in_t z = in_at(10, 9, 30, 0), s = z;   /* page 0 under reduced motion: built at once */
    z.reduced = s.reduced = true;
    z.wake_ms = 0, s.wake_ms = UINT32_MAX;
    row_frame(r, r->ops->draw, &z, s_a);
    row_frame(r, r->ops->draw, &s, s_b);
    assert_same(s_a, s_b, "reduced: page 0 shows at once");
}

/* #16: the twins' area gives ALT, the "?" centre OPEN_KEY with a box of at least 44 px (probed at its edges); hidden
 * twins change only the twins' boxes */
static void learn_tap(void)
{
    const row_t *r = row_of("learn");
    TEST_ASSERT_NOT_NULL(r->ops->tap);
    TEST_ASSERT_EQUAL_UINT16(DW_G16_ALT_MS, r->ops->alt_len_ms);
    for (int i = 0; i < 4; i++) {
        omni_rect_t b;
        dw_g16_twin_box(i, &b);
        TEST_ASSERT_EQUAL_INT(FK_TAP_ALT, r->ops->tap(r->ctx, (int16_t)((b.x0 + b.x1) / 2), (int16_t)((b.y0 + b.y1) / 2)));
        TEST_ASSERT_EQUAL_INT(FK_TAP_ALT, r->ops->tap(r->ctx, (int16_t)DW_G16_SLOTS[i].cx, (int16_t)DW_G16_SLOTS[i].cy));
    }
    TEST_ASSERT_TRUE(DW_G16_HELP.x1 - DW_G16_HELP.x0 >= 44 && DW_G16_HELP.y1 - DW_G16_HELP.y0 >= 44);
    TEST_ASSERT_TRUE(DW_G16_HELP.x0 <= 233 && DW_G16_HELP.x1 > 233 && DW_G16_HELP.y0 <= 418 && DW_G16_HELP.y1 > 418);
    fk_test_tap_edges(r->ops, r->ctx, DW_G16_HELP, FK_TAP_SCREEN);
    fk_face_in_t a = in_at(21, 7, 30, 0), b = a;
    b.alt = true;
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    long differ = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (s_a[y * W + x] == s_b[y * W + x]) continue;
            bool in = false;
            for (int i = 0; i < 4 && !in; i++) {
                omni_rect_t bx;
                dw_g16_twin_box(i, &bx);
                in = inside(&bx, x, y);
            }
            TEST_ASSERT_TRUE_MESSAGE(in, "hiding the twins changes a pixel outside their boxes");
            differ++;
        }
    TEST_ASSERT_TRUE_MESSAGE(differ > 200, "the twins do not hide");
    dirty_pair(r, r->ops->draw, &a, &b, "#16 hide the twins");
    dirty_pair(r, r->ops->draw, &b, &a, "#16 the twins back");
}

static void learn_still(void)   /* nothing moves at rest within a minute */
{
    const row_t *r = row_of("learn");
    fk_face_in_t a = in_at(10, 9, 3, 0);
    for (int ms = 40; ms < 50000; ms += 1000) {
        const fk_face_in_t b = later(a, ms);
        omni_rect_t rects[8];
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    }
}

static void key_page_rows_differ(void)   /* the three pages are three frames */
{
    const row_t *r = row_of("key");
    fk_face_in_t in = in_at(21, 7, 30, 0);
    for (int p = 0; p < 3; p++) {
        in.page = (uint8_t)p;
        r->ops->draw(r->ctx, &in, (omni_rect_t){0, 0, W, W}, p ? s_b : s_a, W);
        if (p) {
            long differ = 0;
            for (int i = 0; i < W * W; i++) differ += s_a[i] != s_b[i];
            TEST_ASSERT_TRUE(differ > 2000);
        }
    }
}


/* review I1: reduced motion changes motion, not information. #14's hint shows at its level, with no fade, for the
 * first 2.5 s after a revealing wake (in->fresh_ms), and not when stale */
static void g14_reduced_hint(void)
{
    const row_t *r = row_of("circles");
    fk_face_in_t base = in_at(21, 7, 30, 0);
    base.reduced = true;
    row_frame(r, r->ops->draw, &base, s_b);
    fk_face_in_t f = base;
    f.fresh = true, f.fresh_ms = 100;
    row_frame(r, r->ops->draw, &f, s_a);
    long hint = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (s_a[y * W + x] != s_b[y * W + x]) {
                TEST_ASSERT_TRUE_MESSAGE(inside(&DW_G14_HINT, x, y), "the reduced hint changes a pixel outside its rect");
                hint++;
            }
    TEST_ASSERT_TRUE_MESSAGE(hint > 100, "no hint under reduced motion");
    fk_face_in_t g = f;   /* no fade: the same hint all through its 2.5 s */
    g.fresh_ms = DW_G14_HINT_MS - 1;
    row_frame(r, r->ops->draw, &g, s_c);
    assert_same(s_a, s_c, "reduced: the hint does not fade");
    g.fresh_ms = DW_G14_HINT_MS;   /* then gone at once */
    row_frame(r, r->ops->draw, &g, s_c);
    assert_same(s_b, s_c, "reduced: the hint ends at 2.5 s");
    fk_face_in_t h = f;
    h.fresh_ms = DW_G14_HINT_MS - 40;
    dirty_pair(r, r->ops->draw, &h, &g, "#14 reduced: the hint's end");
    dirty_pair(r, r->ops->draw, &base, &f, "#14 reduced: the hint's start");
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &h, &g, rects));
    fk_face_in_t st = f;   /* stale: the marker has the place */
    st.clk.stale = true;
    row_frame(r, r->ops->draw, &st, s_a);
    st.fresh = false;
    row_frame(r, r->ops->draw, &st, s_c);
    assert_same(s_a, s_c, "stale: no hint");
}

/* review I1, I2: the key's page 3 under reduced motion still reads ring by ring, in discrete steps (no fade) */
static void key_page3_reduced(void)
{
    s_page = 2;
    const row_t *r = row_of("key");
    s_page = 0;
    fk_face_in_t a = in_at(21, 7, 30, 0);
    a.reduced = true;
    a.wake_ms = 0;
    row_frame(r, r->ops->draw, &a, s_a);
    fk_face_in_t n = a;
    n.reduced = false, n.wake_ms = 400;   /* the normal page once its fade is done, the same step */
    row_frame(r, r->ops->draw, &n, s_b);
    assert_same(s_b, s_a, "reduced: page 3 at once, as the normal page after its fade");
    fk_face_in_t b = a;
    b.wake_ms = 400;
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    int steps = 0;
    for (uint32_t t = 0; t < DW_KEY_CYCLE_MS + 400; t += 100) {
        fk_face_in_t p = a, q = a;
        p.wake_ms = t, q.wake_ms = t + 100;
        const long u = dirty_pair(r, r->ops->draw, &p, &q, "the key's page 3, reduced");
        steps += u > 0;
    }
    TEST_ASSERT_TRUE_MESSAGE(steps >= 8, "reduced: page 3 does not step ring by ring");
    fk_face_in_t c = a;   /* the second ring's turn: its line is the second's */
    c.wake_ms = DW_KEY_STEP_MS + 100;
    row_frame(r, r->ops->draw, &c, s_b);
    long differ = 0;
    for (int i = 0; i < W * W; i++) differ += s_a[i] != s_b[i];
    TEST_ASSERT_TRUE_MESSAGE(differ > 200, "reduced: the active ring does not move on");
}

/* review I2: #16 and the key's pages: the jump list of g14_motion through dirty_pair; page 3's clock stepped back
 * with the same page time; #16 under reduced motion reads no mono_ms */
static void learn_key_jumps(void)
{
    static const int jumps[4][2] = {{21, 40}, {40, 12}, {0, 59}, {21, 23}};
    for (int f = 0; f < 4; f++) {
        s_page = (uint8_t)(f ? f - 1 : 0);
        const row_t *r = row_of(f ? "key" : "learn");
        s_page = 0;
        for (int w = 0; w < 2; w++) {
            const uint32_t wake = w ? 3000u : UINT32_MAX;
            for (int k = 0; k < 4; k++) {
                fk_face_in_t p = in_at(10, 9, jumps[k][0], 500), q = in_at(10, 9, jumps[k][1], 100);
                p.wake_ms = q.wake_ms = wake;
                char what[64];
                snprintf(what, sizeof what, "%s p%d :%02d -> :%02d", r->ops->key, r->page, jumps[k][0], jumps[k][1]);
                dirty_pair(r, r->ops->draw, &p, &q, what);
            }
            fk_face_in_t p = in_at(10, 9, 30, 500), q = in_at(10, 4, 30, 500);   /* back five minutes */
            p.wake_ms = q.wake_ms = wake;
            dirty_pair(r, r->ops->draw, &p, &q, "back five minutes");
        }
    }
    s_page = 2;
    const row_t *k2 = row_of("key");
    s_page = 0;
    fk_face_in_t p = in_at(21, 8, 3, 0), q = in_at(21, 3, 3, 0);   /* a sync back, the same page time */
    p.wake_ms = q.wake_ms = 6000;
    int dp[4], dq[4];
    dw_key_time(&p, dp);
    dw_key_time(&q, dq);
    TEST_ASSERT_TRUE(memcmp(dp, dq, sizeof dp) != 0);
    dirty_pair(k2, k2->ops->draw, &p, &q, "page 3: the clock back, the same page time");
    const row_t *l = row_of("learn");
    fk_face_in_t a = in_at(10, 9, 30, 0), b = a;
    a.reduced = b.reduced = true;
    a.mono_ms = 0, b.mono_ms = 1000;
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, l->ops->dirty(l->ctx, &a, &b, rects));
    row_frame(l, l->ops->draw, &a, s_a);
    row_frame(l, l->ops->draw, &b, s_b);
    assert_same(s_a, s_b, "#16 reduced: mono_ms draws nothing");
}

/* M3: the glow tables are const data equal to fk_glow_lut_build's */
static void test_glow_lut_tables(void)
{
    static uint8_t lut[FK_GLOW_LUT_MAX];   /* the largest table any radius <= 255 has (a fine one, r 127) */
    static const int radii[] = DW_GLOW_LUT_RADII;
    for (unsigned i = 0; i < sizeof radii / sizeof radii[0]; i++) {
        TEST_ASSERT_TRUE(radii[i] <= 255);
        const uint8_t *t = dw_glow_lut(radii[i]);
        TEST_ASSERT_NOT_NULL(t);
        fk_glow_lut_build(lut, radii[i]);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(lut, t, FK_GLOW_LUT_N(radii[i]));
    }
    TEST_ASSERT_NULL(dw_glow_lut(13));
}

/* ---- Task 16: the chime's breath (spec section 13: the lances, the rotor, the hour roundel). At rest (chime 0) every
 * face draws exactly its pinned frames (verify_crc's rows are drawn with chime 0); with the chime on only the breathing
 * faces change, always inside dirty(), also when the clock moves in the same frame; never under reduced motion, in the
 * glance or on an unset clock. */
static bool breathes(const char *key)
{
    return strcmp(key, "crystal") == 0 || strcmp(key, "roundel") == 0 || strcmp(key, "rotor") == 0 ||
           strcmp(key, "room") == 0 || strcmp(key, "doorway") == 0;
}

static long frames_differ(void)
{
    long n = 0;
    for (int i = 0; i < W * W; i++) n += s_a[i] != s_b[i];
    return n;
}

static void chime_rest_exact(void)
{
    {   /* #1's breath redraws DW_C1_LANCES: the lances' layer lights nothing outside it */
        fk_img_t im;
        const fk_img_t *l = img_of(ASSET_DW_C1_LANCE, &im);
        TEST_ASSERT_NOT_NULL(l);
        long outside = 0;
        for (int y = 0; y < l->h; y++)
            for (int x = 0; x < l->w; x++) outside += l->a[y * l->w + x] && !inside(&DW_C1_LANCES, x, y);
        TEST_ASSERT_EQUAL_INT32(0, outside);
    }
    for (unsigned i = 0; i < DW_STILL_N; i++) {
        dw_still_t st;
        dw_still(i, &st);
        TEST_ASSERT_TRUE(st.in.chime == 0.0f);   /* the CRC rows are the rest frames */
    }
    static const int phase[] = {0, 100, 250, 450, 700, 950};
    for (unsigned i = 0; i < s_nrows; i++) {
        const row_t *r = &s_rows[i];
        const bool br = breathes(r->ops->key);
        long breathed = 0;   /* #7's core is already full at the top of its stroke: some phase, not each */
        char what[96];
        for (unsigned k = 0; k < sizeof phase / sizeof phase[0]; k++) {
            fk_face_in_t a = in_at(10, 9, 21, phase[k]), b = a;
            b.chime = 0.7f;
            snprintf(what, sizeof what, "%s p%u chime 0 -> 0.7 at %d ms", r->ops->key, r->page, phase[k]);
            dirty_pair(r, r->ops->draw, &a, &b, what);
            const long d = frames_differ();
            breathed += d;
            if (!br) TEST_ASSERT_TRUE_MESSAGE(d == 0, what);   /* only the breathing faces change */
            /* the breath and the clock in one frame (the cadence's 40 ms step, the next second) */
            a.chime = 0.3f;
            fk_face_in_t c = later(a, 40);
            c.chime = 0.55f;
            snprintf(what, sizeof what, "%s p%u breath + 40 ms at %d ms", r->ops->key, r->page, phase[k]);
            dirty_pair(r, r->ops->draw, &a, &c, what);
            c = later(a, 1000);
            c.chime = 0.9f;
            snprintf(what, sizeof what, "%s p%u breath + 1 s at %d ms", r->ops->key, r->page, phase[k]);
            dirty_pair(r, r->ops->draw, &a, &c, what);
            c = a, c.chime = 0.0f;   /* the end of the chime */
            snprintf(what, sizeof what, "%s p%u breath -> 0 at %d ms", r->ops->key, r->page, phase[k]);
            dirty_pair(r, r->ops->draw, &a, &c, what);
            /* reduced motion, the glance, an unset clock: no breath */
            a.chime = 0.0f, b = a, b.chime = 0.7f;
            a.reduced = b.reduced = true;
            snprintf(what, sizeof what, "%s p%u reduced at %d ms", r->ops->key, r->page, phase[k]);
            dirty_pair(r, r->ops->draw, &a, &b, what);
            TEST_ASSERT_TRUE_MESSAGE(frames_differ() == 0, what);
            a.reduced = b.reduced = false;
            a.glance = b.glance = true;
            omni_rect_t rects[8];
            const fk_face_in_t pa = paged(r, &a), pb = paged(r, &b);
            snprintf(what, sizeof what, "%s p%u glance at %d ms", r->ops->key, r->page, phase[k]);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, r->ops->dirty(r->ctx, &pa, &pb, rects), what);
            dirty_pair(r, r->ops->glance, &a, &b, what);
            TEST_ASSERT_TRUE_MESSAGE(frames_differ() == 0, what);
            a.glance = b.glance = false;
            a.clk.set = b.clk.set = false;
            snprintf(what, sizeof what, "%s p%u unset at %d ms", r->ops->key, r->page, phase[k]);
            dirty_pair(r, r->ops->draw, &a, &b, what);
            TEST_ASSERT_TRUE_MESSAGE(frames_differ() == 0, what);
        }
        snprintf(what, sizeof what, "%s p%u breathes", r->ops->key, r->page);
        if (br) TEST_ASSERT_TRUE_MESSAGE(breathed > 0, what);
        /* the wake's tail with the breath (a chime during the doors or a face's own wake) */
        fk_face_in_t a = in_at(10, 9, 30, 500), b = a;
        a.wake_ms = 600, b.wake_ms = 640;
        a.chime = 0.2f, b.chime = 0.6f;
        snprintf(what, sizeof what, "%s p%u breath in the wake", r->ops->key, r->page);
        dirty_pair(r, r->ops->draw, &a, &b, what);
    }
}

/* ---- the CRC tables (R147 PR3, wave 2 Task 4)
 * A still's applicable row is the first table's that holds one (fk_verify_lookup): the generic harness has the generic
 * table alone, the overlay variant (and the private `dw verify`) the overlay's, then the generic one. The tables are
 * sound when every still has exactly one applicable row, no table holds two rows for one still, and every row is a
 * registered face's still. */
static bool verify_tables_sound(const fk_verify_tab_t *tabs, unsigned ntabs, const char *const *keys,
                                const unsigned *counts, unsigned nk, char *why, size_t wn)
{
    for (unsigned t = 0; t < ntabs; t++)
        for (unsigned r = 0; r < tabs[t].n; r++) {
            const fk_verify_t *v = &tabs[t].rows[r];
            bool known = false;
            for (unsigned f = 0; f < nk; f++)
                if (strcmp(keys[f], v->face) == 0 && v->still < counts[f]) known = true;
            if (!known) {
                snprintf(why, wn, "table %u row %u (%s %u) is no face's still", t, r, v->face, v->still);
                return false;
            }
            for (unsigned q = r + 1; q < tabs[t].n; q++)
                if (strcmp(tabs[t].rows[q].face, v->face) == 0 && tabs[t].rows[q].still == v->still) {
                    snprintf(why, wn, "table %u has two rows for %s %u", t, v->face, v->still);
                    return false;
                }
        }
    for (unsigned f = 0; f < nk; f++)
        for (unsigned k = 0; k < counts[f]; k++) {
            unsigned dups = 0;
            if (!fk_verify_lookup(tabs, ntabs, keys[f], k, &dups) || dups != 1) {
                snprintf(why, wn, "%s still %u has %u applicable rows", keys[f], k, dups);
                return false;
            }
        }
    return true;
}

/* the registered faces as keys and still counts */
static const char *s_vkeys[32];
static unsigned s_vcounts[32];
static void verify_faces(void)
{
    TEST_ASSERT_TRUE(DW_FACE_COUNT <= sizeof s_vkeys / sizeof s_vkeys[0]);
    for (unsigned i = 0; i < DW_FACE_COUNT; i++) s_vkeys[i] = DW_FACES[i]->key, s_vcounts[i] = dw_still_count(DW_FACES[i]);
}

/* this variant's tables, in lookup order */
static unsigned verify_tabs(fk_verify_tab_t *tabs)
{
    unsigned n = 0;
#if defined(DW_TEST_OVERLAY_TABLE)
    tabs[n++] = (fk_verify_tab_t){DW_VERIFY_OVERLAY, (unsigned)(sizeof DW_VERIFY_OVERLAY / sizeof DW_VERIFY_OVERLAY[0])};
#endif
    tabs[n++] = (fk_verify_tab_t){DW_VERIFY, (unsigned)(sizeof DW_VERIFY / sizeof DW_VERIFY[0])};
    return n;
}

/* does a face's bind read an overlay file (a swap asset of the staged private set)? */
static bool s_read_overlay;
static const void *overlay_watch_get(const char *name, size_t *size)
{
    const void *p = dw_assets_host_get(name, size);
    if (p && dw_assets_host_from_overlay(name)) s_read_overlay = true;
    return p;
}
static bool bind_reads_overlay(const fk_face_ops_t *ops)
{
    void *ctx = calloc(1, ops->ctx_bytes ? ops->ctx_bytes : 1);
    TEST_ASSERT_NOT_NULL(ctx);
    s_read_overlay = false;
    TEST_ASSERT_TRUE_MESSAGE(ops->bind(ctx, overlay_watch_get), ops->key);
    free(ctx);
    return s_read_overlay;
}

static void verify_crc_supplement(void)
{
    /* synthetic tables: faces a (2 stills) and b (3); the generic table has every still, the overlay b's alone */
    static const char *const keys[] = {"a", "b"};
    static const unsigned counts[] = {2, 3};
    static const fk_verify_t gen[] = {{"a", 0, 1}, {"a", 1, 2}, {"b", 0, 3}, {"b", 1, 4}, {"b", 2, 5}};
    static const fk_verify_t ov[] = {{"b", 0, 13}, {"b", 1, 14}, {"b", 2, 15}};
    char why[96];
    fk_verify_tab_t t2[2] = {{ov, 3}, {gen, 5}};
    TEST_ASSERT_TRUE_MESSAGE(verify_tables_sound(t2, 2, keys, counts, 2, why, sizeof why), why);
    TEST_ASSERT_TRUE_MESSAGE(verify_tables_sound(t2 + 1, 1, keys, counts, 2, why, sizeof why), why);
    unsigned dups = 9;
    const fk_verify_t *v = fk_verify_lookup(t2, 2, "b", 1, &dups);   /* the overlay's first */
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT32(14, v->crc);
    TEST_ASSERT_EQUAL_UINT(1, dups);
    v = fk_verify_lookup(t2, 2, "a", 1, &dups);                      /* then the generic one */
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT32(2, v->crc);
    v = fk_verify_lookup(t2 + 1, 1, "b", 1, &dups);                  /* the generic table alone */
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_UINT32(4, v->crc);
    TEST_ASSERT_NULL(fk_verify_lookup(t2, 2, "b", 3, &dups));        /* no row */
    TEST_ASSERT_EQUAL_UINT(0, dups);
    TEST_ASSERT_NULL(fk_verify_lookup(t2, 2, "c", 0, NULL));
    TEST_ASSERT_NULL(fk_verify_lookup(t2, 0, "a", 0, &dups));
    /* a still with no row fails */
    static const fk_verify_t gen_short[] = {{"a", 0, 1}, {"b", 0, 3}, {"b", 1, 4}, {"b", 2, 5}};
    fk_verify_tab_t t3[2] = {{ov, 3}, {gen_short, 4}};
    TEST_ASSERT_FALSE(verify_tables_sound(t3, 2, keys, counts, 2, why, sizeof why));
    TEST_ASSERT_NOT_NULL(strstr(why, "a still 1 has 0 applicable rows"));
    /* a duplicate row within one table fails: in the overlay's, where it applies, and in the generic one's, though the
     * overlay's row shadows it */
    static const fk_verify_t ov_dup[] = {{"b", 0, 13}, {"b", 1, 14}, {"b", 1, 16}, {"b", 2, 15}};
    fk_verify_tab_t t4[2] = {{ov_dup, 4}, {gen, 5}};
    TEST_ASSERT_FALSE(verify_tables_sound(t4, 2, keys, counts, 2, why, sizeof why));
    TEST_ASSERT_NOT_NULL(strstr(why, "table 0 has two rows for b 1"));
    v = fk_verify_lookup(t4, 2, "b", 1, &dups);
    TEST_ASSERT_EQUAL_UINT(2, dups);
    static const fk_verify_t gen_dup[] = {{"a", 0, 1}, {"a", 1, 2}, {"b", 0, 3}, {"b", 0, 3}, {"b", 1, 4}, {"b", 2, 5}};
    fk_verify_tab_t t5[2] = {{ov, 3}, {gen_dup, 6}};
    TEST_ASSERT_FALSE(verify_tables_sound(t5, 2, keys, counts, 2, why, sizeof why));
    TEST_ASSERT_NOT_NULL(strstr(why, "table 1 has two rows for b 0"));
    /* a row that is no face's still fails */
    static const fk_verify_t ov_stray[] = {{"b", 0, 13}, {"b", 1, 14}, {"b", 2, 15}, {"b", 3, 16}};
    fk_verify_tab_t t6[2] = {{ov_stray, 4}, {gen, 5}};
    TEST_ASSERT_FALSE(verify_tables_sound(t6, 2, keys, counts, 2, why, sizeof why));
    TEST_ASSERT_NOT_NULL(strstr(why, "is no face's still"));

    /* this variant's real tables */
    verify_faces();
    fk_verify_tab_t tabs[2];
    const unsigned nt = verify_tabs(tabs);
    TEST_ASSERT_TRUE_MESSAGE(verify_tables_sound(tabs, nt, s_vkeys, s_vcounts, DW_FACE_COUNT, why, sizeof why), why);
#if defined(DW_FACES_OVERLAY) && !defined(DW_TEST_OVERLAY_TABLE)
    (void)nt;
    TEST_FAIL_MESSAGE("the overlay has no verify_crc_overlay.h (dw_shot --assets ... --overlay ... --crc writes it)");
#elif defined(DW_FACES_OVERLAY)
    TEST_ASSERT_EQUAL_UINT(2, nt);
    TEST_ASSERT_EQUAL_STRING("overlay", DW_VERIFY_OVERLAY_SET);
    /* a face has overlay rows exactly when its bind reads a swap asset, and then a row for every still */
    unsigned readers = 0;
    for (unsigned i = 0; i < DW_FACE_COUNT; i++) {
        const bool reads = bind_reads_overlay(DW_FACES[i]);
        unsigned rows = 0;
        for (unsigned r = 0; r < tabs[0].n; r++) rows += strcmp(tabs[0].rows[r].face, DW_FACES[i]->key) == 0;
        char m[96];
        snprintf(m, sizeof m, "%s: bind reads a swap asset %d, overlay rows %u of %u", DW_FACES[i]->key, reads, rows,
                 dw_still_count(DW_FACES[i]));
        TEST_ASSERT_TRUE_MESSAGE(reads ? rows == dw_still_count(DW_FACES[i]) : rows == 0, m);
        readers += reads;
    }
    TEST_ASSERT_TRUE_MESSAGE(readers >= 1, "no face reads a swap asset (#5 binds its box)");
    TEST_ASSERT_TRUE(bind_reads_overlay(&DW_FACE_CALLBOX));
    TEST_ASSERT_FALSE(bind_reads_overlay(&DW_FACE_CRYSTAL));   /* #1's doors are dw_view's, never its bind's */
#else
    TEST_ASSERT_EQUAL_UINT(1, nt);
    TEST_ASSERT_EQUAL_STRING("generic", DW_VERIFY_SET);
    for (unsigned i = 0; i < DW_FACE_COUNT; i++) TEST_ASSERT_FALSE(bind_reads_overlay(DW_FACES[i]));
#endif
}

static void verify_crc_current(void)
{
#if defined(DW_FACES_OVERLAY) && !defined(DW_TEST_OVERLAY_TABLE)
    TEST_FAIL_MESSAGE("the overlay has no verify_crc_overlay.h (dw_shot --assets ... --overlay ... --crc writes it)");
#else
    fk_verify_tab_t tabs[2];
    const unsigned nt = verify_tabs(tabs);
    for (unsigned i = 0; i < s_nrows; i++)
        for (unsigned k = 0; k < dw_still_count(s_rows[i].ops) && s_rows[i].page == 0; k++) {
            dw_still_frame(s_rows[i].ops, s_rows[i].ctx, k, s_a);
            const uint32_t crc = fk_crc32(0, s_a, sizeof s_a);
            unsigned dups = 0;
            const fk_verify_t *v = fk_verify_lookup(tabs, nt, s_rows[i].ops->key, k, &dups);
            char m[112];
            snprintf(m, sizeof m, "%s still %u: %08x, the table (%s) says %08x (dw_shot --crc)", s_rows[i].ops->key, k,
                     (unsigned)crc, v && v >= tabs[0].rows && v < tabs[0].rows + tabs[0].n && nt == 2 ? "overlay"
                                                                                                    : "generic",
                     v ? (unsigned)v->crc : 0u);
            TEST_ASSERT_TRUE_MESSAGE(v != NULL && dups == 1, "a still without exactly one applicable row");
            TEST_ASSERT_TRUE_MESSAGE(crc == v->crc, m);
        }
#endif
}

/* ---- #2: Roundel Room (wave 2 Task 5) */
static float room_ha(int h, int m) { return (((float)(h % 12) + (float)m / 60.f) / 12.f) * 2 * PI_F; }

/* R147 PR18: 10:09 against 11:09. The beams are additive and the same in both, so outside the hour hand's two boxes
 * only the boxes of the 10 and the 11 o'clock roundels differ (not a comparison with the base, which the beams cross
 * at every hour); both roundels change (10 goes out, 11 lights) */
static void room_lit_hour(void)
{
    const row_t *r = row_of("room");
    for (int g = 0; g < 2; g++) {   /* settled, and in the doors' tail */
        fk_face_in_t a = in_at(10, 9, 30, 500), b = in_at(11, 9, 30, 500);
        a.wake_ms = b.wake_ms = g ? 1800 : UINT32_MAX;
        row_frame(r, r->ops->draw, &a, s_a);
        row_frame(r, r->ops->draw, &b, s_b);
        omni_rect_t ok[4];
        dw_r2_hour_rect(10, &ok[0]);
        dw_r2_hour_rect(11, &ok[1]);
        dw_hand_room_bbox(room_ha(10, 9), true, &ok[2]);
        dw_hand_room_bbox(room_ha(11, 9), true, &ok[3]);
        long in10 = 0, in11 = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                if (s_a[y * W + x] == s_b[y * W + x]) continue;
                bool in = false;
                for (int k = 0; k < 4 && !in; k++) in = inside(&ok[k], x, y);
                if (!in) {
                    char m[112];
                    snprintf(m, sizeof m, "10:09 and 11:09 differ at (%d, %d), outside the roundels and hands", x,
                             y);
                    TEST_FAIL_MESSAGE(m);
                }
                if (inside(&ok[2], x, y) || inside(&ok[3], x, y)) continue;
                in10 += inside(&ok[0], x, y), in11 += inside(&ok[1], x, y);
            }
        TEST_ASSERT_TRUE_MESSAGE(in10 > 300, "the 10 o'clock roundel does not go out");
        TEST_ASSERT_TRUE_MESSAGE(in11 > 300, "the 11 o'clock roundel does not light");
    }
    /* the twelve boxes: each holds its roundel's glow (r 40 about the roundel at r 207) and lies on the screen */
    for (int h = 1; h <= 12; h++) {
        omni_rect_t b;
        dw_r2_hour_rect(h, &b);
        float s, c;
        fk_sincos((float)h * PI_F / 6.f, &s, &c);
        const float x = 233.f + 207.f * s, y = 233.f - 207.f * c;
        TEST_ASSERT_TRUE(b.x0 >= 0 && b.y0 >= 0 && b.x1 <= W && b.y1 <= W);
        TEST_ASSERT_TRUE(b.x0 <= fmaxf(x - 41.f, 0.f) && b.x1 >= fminf(x + 41.f, (float)W));
        TEST_ASSERT_TRUE(b.y0 <= fmaxf(y - 41.f, 0.f) && b.y1 >= fminf(y + 41.f, (float)W));
        TEST_ASSERT_TRUE(b.x1 - b.x0 <= 86 && b.y1 - b.y0 <= 86);
    }
}

/* within a minute (40 ms steps) every dirty rect lies inside DW_R2_ROTOR, and the rotor moves (WD41) */
static void room_rotor_confined(void)
{
    const row_t *r = row_of("room");
    TEST_ASSERT_EQUAL_INT(144, DW_R2_ROTOR.x1 - DW_R2_ROTOR.x0);
    TEST_ASSERT_EQUAL_INT(144, DW_R2_ROTOR.y1 - DW_R2_ROTOR.y0);
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_R2_ROTOR.x0 + DW_R2_ROTOR.x1);
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_R2_ROTOR.y0 + DW_R2_ROTOR.y1);
    fk_face_in_t a = in_at(10, 9, 0, 0);
    int moved = 0;
    for (int step = 0; step < 1499; step++) {   /* :00.000 .. :59.960 */
        const fk_face_in_t b = later(a, 40);
        omni_rect_t rects[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
        for (int k = 0; k < n; k++)
            TEST_ASSERT_TRUE_MESSAGE(rect_in(&rects[k], &DW_R2_ROTOR), "a rect outside the rotor");
        moved += n > 0;
        if (step % 50 == 7) {
            char what[48];
            snprintf(what, sizeof what, "room at :%02d.%03d + 40 ms", a.clk.s, a.clk.ms);
            dirty_pair(r, r->ops->draw, &a, &b, what);
        }
        a = b;
    }
    TEST_ASSERT_TRUE_MESSAGE(moved > 1400, "the rotor does not move at the cadence");
    TEST_ASSERT_EQUAL_UINT16(40, DW_FACE_ROOM.cadence_ms);
    a = in_at(10, 9, 59, 980);   /* the minute: the hands step, the whole frame */
    const fk_face_in_t b = later(a, 40);
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &a, &b, rects));
    TEST_ASSERT_EQUAL_INT(W, rects[0].x1 - rects[0].x0);
    /* the lift: (sin(t / 2000 turns) + 1) / 2 for t the ms of the day mod 2000; 0 unset */
    for (int ms = 0; ms < 2000; ms += 125) {
        fk_face_in_t in = in_at(10, 9, 30 + ms / 1000, ms % 1000);   /* 10:09:30.000 is 36 570 000 ms: 0 mod 2000 */
        const float want = (float)((sin(ms / 2000.0 * 2 * 3.14159265358979) + 1) / 2);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, want, dw_r2_lift(&in));
        in.clk.set = false;
        TEST_ASSERT_EQUAL_FLOAT(0.f, dw_r2_lift(&in));
    }
}

/* under reduced motion the rotor stands at 0.5 at any ms, the chime too */
static void room_reduced_rotor_still(void)
{
    for (int ms = 0; ms < 4000; ms += 37) {
        fk_face_in_t in = in_at(10, 9, 30 + ms / 1000, ms % 1000);
        in.reduced = true;
        TEST_ASSERT_EQUAL_FLOAT(0.5f, dw_r2_lift(&in));
        in.chime = 0.7f;
        TEST_ASSERT_EQUAL_FLOAT(0.5f, dw_r2_lift(&in));
        in.reduced = false;   /* the chime rides the envelope */
        TEST_ASSERT_EQUAL_FLOAT(0.7f, dw_r2_lift(&in));
    }
}

/* the Classic doors: #2 opens behind ASSET_DW_DOORS_C (dw_view's), and its bind asks for no doors at all */
static unsigned s_room_doors_asks;
static const void *room_get(const char *name, size_t *size)
{
    if (strcmp(name, ASSET_DW_DOORS_M) == 0 || strcmp(name, ASSET_DW_DOORS_C) == 0) s_room_doors_asks++;
    return dw_assets_host_get(name, size);
}

static void room_doors_classic_asset(void)
{
    TEST_ASSERT_TRUE(DW_FACE_ROOM.reveal);
    TEST_ASSERT_NOT_NULL(DW_FACE_ROOM.reveal_asset);
    TEST_ASSERT_EQUAL_STRING(ASSET_DW_DOORS_C, DW_FACE_ROOM.reveal_asset);
    TEST_ASSERT_EQUAL_PTR(&DW_DOORS_OUT, DW_FACE_ROOM.reveal_arg);
    TEST_ASSERT_EQUAL_STRING("room", DW_FACE_ROOM.key);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_ROOM, dw_face_by_key("room"));
    void *ctx = calloc(1, DW_FACE_ROOM.ctx_bytes);
    TEST_ASSERT_NOT_NULL(ctx);
    s_room_doors_asks = 0;
    TEST_ASSERT_TRUE(DW_FACE_ROOM.bind(ctx, room_get));
    TEST_ASSERT_EQUAL_UINT(0, s_room_doors_asks);
    free(ctx);
}

/* the chime (0.7) changes the frame only inside DW_R2_ROTOR, the beams' box and the hour roundel's box; not under
 * reduced motion. The beams' layer lights nothing outside DW_R2_BEAMS. */
static void room_chime_regions(void)
{
    {
        fk_img_t im;
        const fk_img_t *l = img_of(ASSET_DW_R2_BEAM, &im);
        /* (wave 2 Task 6, M4) the asset is its lit box alone, drawn at its origin: DW_R2_BEAMS, and tight */
        TEST_ASSERT_TRUE(l->a && l->w == DW_R2_BEAMS.x1 - DW_R2_BEAMS.x0 && l->h == DW_R2_BEAMS.y1 - DW_R2_BEAMS.y0);
        TEST_ASSERT_EQUAL_INT(DW_R2_BEAMS.x0, ASSET_DW_R2_BEAM_X);
        TEST_ASSERT_EQUAL_INT(DW_R2_BEAMS.y0, ASSET_DW_R2_BEAM_Y);
        int x0 = l->w, y0 = l->h, x1 = -1, y1 = -1;
        for (int y = 0; y < l->h; y++)
            for (int x = 0; x < l->w; x++)
                if (l->a[y * l->w + x]) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
        TEST_ASSERT_TRUE_MESSAGE(x0 == 0 && y0 == 0 && x1 == l->w - 1 && y1 == l->h - 1, "the beam box is not tight");
    }
    const row_t *r = row_of("room");
    static const int phase[] = {0, 333, 500, 940};
    omni_rect_t ok[3] = {DW_R2_ROTOR, DW_R2_BEAMS};
    dw_r2_hour_rect(10, &ok[2]);
    long hour = 0;
    for (unsigned k = 0; k < sizeof phase / sizeof phase[0]; k++) {
        fk_face_in_t a = in_at(10, 9, 21, phase[k]), b = a;
        b.chime = 0.7f;
        row_frame(r, r->ops->draw, &a, s_a);
        row_frame(r, r->ops->draw, &b, s_b);
        long d = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                if (s_a[y * W + x] == s_b[y * W + x]) continue;
                bool in = false;
                for (int q = 0; q < 3 && !in; q++) in = inside(&ok[q], x, y);
                TEST_ASSERT_TRUE_MESSAGE(in, "the chime changes a pixel outside the rotor, the beams and the hour");
                hour += inside(&ok[2], x, y) && !inside(&DW_R2_BEAMS, x, y);
                d++;
            }
        TEST_ASSERT_TRUE_MESSAGE(d > 1000, "the chime changes nothing");
        a.reduced = b.reduced = true;
        row_frame(r, r->ops->draw, &a, s_a);
        row_frame(r, r->ops->draw, &b, s_b);
        TEST_ASSERT_EQUAL_INT32(0, frames_differ());
    }
    TEST_ASSERT_TRUE_MESSAGE(hour > 50, "the chime does not flare the hour roundel");
}

/* unset: no hands, no lit roundel; the rotor at rest */
static void room_unset_no_hands(void)
{
    const row_t *r = row_of("room");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &in, s_a);
    in.clk.set = false;
    row_frame(r, r->ops->draw, &in, s_b);
    /* each hand between its centre line's end and its shoulder (the hour hand at r 100, outside the rotor's box; the
     * minute hand at r 150): cream when set */
    const float ang[2] = {room_ha(10, 9), (9.f / 60.f) * 2 * PI_F}, rr[2] = {100.f, 150.f};
    for (int k = 0; k < 2; k++) {
        float s, c;
        fk_sincos(ang[k], &s, &c);
        const int x = (int)(233.f + rr[k] * s), y = (int)(233.f - rr[k] * c);
        const uint16_t set = s_a[y * W + x], uns = s_b[y * W + x];
        TEST_ASSERT_TRUE_MESSAGE(omni_ex_r(set) > 200 && omni_ex_g(set) > 200, "no hand where the set clock has one");
        TEST_ASSERT_TRUE_MESSAGE(set != uns, "a hand on the unset clock");
    }
    /* the 10 o'clock roundel's centre: lit when set, not when unset */
    const uint16_t lit = s_a[(int)(233.f - 103.5f) * W + (int)(233.f - 179.3f)];
    const uint16_t dark = s_b[(int)(233.f - 103.5f) * W + (int)(233.f - 179.3f)];
    TEST_ASSERT_TRUE_MESSAGE(omni_ex_r(lit) > omni_ex_r(dark) + 20, "the unset clock lights the hour");
    /* nothing moves on an unset clock */
    fk_face_in_t a = in, b = later(in, 700);
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    row_frame(r, r->ops->draw, &b, s_c);
    assert_same(s_b, s_c, "the unset room at another ms");
}

/* the stale marker: centred (233, 329), between the rotor's box and the Dymo date at 359 */
static void room_stale_rect(void)
{
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_R2_STALE.x0 + DW_R2_STALE.x1);
    TEST_ASSERT_EQUAL_INT(2 * 329, DW_R2_STALE.y0 + DW_R2_STALE.y1);
    TEST_ASSERT_TRUE(DW_R2_STALE.y0 >= DW_R2_ROTOR.y1);
    TEST_ASSERT_TRUE(DW_R2_STALE.y1 <= 359 - 10);   /* the date's 19 px tape starts at 349.5 */
    stale_marker_only("room", &DW_R2_STALE);
}

/* the doors' tail at their end time is the settled frame exactly */
static void room_tail_end_is_settled(void)
{
    const row_t *r = row_of("room");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &in, s_a);
    in.wake_ms = DW_DOORS_END_MS;
    row_frame(r, r->ops->draw, &in, s_b);
    assert_same(s_a, s_b, "#2 at the doors' end time vs settled");
}

/* classicHands' baton: band-exact, inside its box at every grow the tail reaches */
static void hand_room_band_exact(void)
{
    static const float ang[] = {0.f, 0.3f, 1.5707963f, 2.2f, 3.14159265f, 4.0f, 5.5f, 6.1f};
    for (unsigned k = 0; k < sizeof ang / sizeof ang[0]; k++)
        for (int hour = 0; hour < 2; hour++)
            for (int g = 0; g < 3; g++) {
                const float grow = g == 0 ? 1.f : g == 1 ? 0.35f : 1.12f;
                memset(s_a, 0, sizeof s_a);
                dw_hand_room(s_a, W, NULL, ang[k], hour != 0, grow, 255);
                memset(s_b, 0, sizeof s_b);
                for (int y0 = 0; y0 < W; y0 += 8) {
                    const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
                    dw_hand_room(s_b + (size_t)y0 * W, W, &c, ang[k], hour != 0, grow, 255);
                }
                assert_same(s_a, s_b, "room hand in 8-row clips");
                omni_rect_t bb;
                dw_hand_room_bbox(ang[k], hour != 0, &bb);
                long ink = 0;
                for (int y = 0; y < W; y++)
                    for (int x = 0; x < W; x++)
                        if (s_a[y * W + x]) {
                            TEST_ASSERT_TRUE_MESSAGE(inside(&bb, x, y), "room hand ink outside its bbox");
                            ink++;
                        }
                TEST_ASSERT_TRUE(ink > 100);
            }
    memset(s_a, 0x3C, sizeof s_a);
    memcpy(s_b, s_a, sizeof s_a);
    dw_hand_room(s_a, W, NULL, 1.0f, true, 0.f, 255);
    assert_same(s_b, s_a, "room hand at grow 0 drew");
}

/* ---- #10: Crystal, the wand seconds (wave 2 Task 6) */
/* the wand's angle as amber.c sweeps it: (s + ms / 1000) / 60 turns */
static float wand_ang(const fk_face_in_t *in) { return dw_c10_wand_angle(in); }

/* the boxes the wand (its body, its tip glow and trail) can touch at in */
static void wand_boxes(const fk_face_in_t *in, omni_rect_t out[4]) { dw_hand_wand_rects(wand_ang(in), out); }

static bool in_any(const omni_rect_t *r, int n, int x, int y)
{
    for (int k = 0; k < n; k++)
        if (inside(&r[k], x, y)) return true;
    return false;
}

/* the screen annulus of the script ring (pixel centres in [R0, R1)) */
static bool in_script(int x, int y)
{
    const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f, q = dx * dx + dy * dy;
    return q >= (float)(DW_C10_SCRIPT_R0 * DW_C10_SCRIPT_R0) && q < (float)(DW_C10_SCRIPT_R1 * DW_C10_SCRIPT_R1);
}

/* For 40 ms steps through a minute: at most 8 rects; they cover the old and the new wand boxes and reach nowhere
 * else, except at the minute and at each 10 s script step (the annulus, r R0-R1, WD30); every changed pixel inside */
static void amber_sweep_rects(void)
{
    const row_t *r = row_of("amber");
    TEST_ASSERT_EQUAL_UINT16(50, DW_FACE_AMBER.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(900, DW_FACE_AMBER.wake_len_ms);
    fk_face_in_t a = in_at(10, 9, 0, 0);
    int steps = 0, script = 0;
    for (int step = 0; step < 1500; step++) {   /* :00.000 .. :59.960 -> :00.000 of the next minute */
        const fk_face_in_t b = later(a, 40);
        omni_rect_t rects[8], ba[4], bb[4];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
        TEST_ASSERT_TRUE(n <= 8);
        const bool minute = a.clk.m != b.clk.m;
        const bool stepped = dw_c10_script_off(&a) != dw_c10_script_off(&b);
        wand_boxes(&a, ba);
        wand_boxes(&b, bb);
        if (!minute && !stepped) {
            /* nothing outside the boxes */
            for (int k = 0; k < n; k++)
                for (int y = rects[k].y0; y < rects[k].y1; y++)
                    for (int x = rects[k].x0; x < rects[k].x1; x++)
                        if (!in_any(ba, 4, x, y) && !in_any(bb, 4, x, y)) {
                            char m[96];
                            snprintf(m, sizeof m, "wand at :%02d.%03d: rect %d reaches (%d, %d) outside the boxes",
                                     a.clk.s, a.clk.ms, k, x, y);
                            TEST_FAIL_MESSAGE(m);
                        }
            steps++;
        } else if (!minute) {
            TEST_ASSERT_TRUE_MESSAGE(b.clk.s % 10 == 0 && b.clk.ms < 40, "the script stepped off a 10 s mark");
            /* wave 2 Task 7 (the Task 6 review's M1): the AREA step allowance at its true worst case, each stepped
             * frame (:10, :20, :40, :50; the harness's dirty_covers sees only :30) */
            char m[80];
            snprintf(m, sizeof m, "amber's script step onto :%02d redraws more than the AREA step allowance", b.clk.s);
            TEST_ASSERT_TRUE_MESSAGE(rects_union(rects, n) <= area_of("amber").step, m);
            script++;
        }
        /* the old and the new boxes are covered (on screen) */
        for (int q = 0; q < 8; q++) {
            const omni_rect_t *s = q < 4 ? &ba[q] : &bb[q - 4];
            for (int y = s->y0 < 0 ? 0 : s->y0; y < s->y1 && y < W; y++)
                for (int x = s->x0 < 0 ? 0 : s->x0; x < s->x1 && x < W; x++)
                    if (!in_any(rects, n, x, y)) {
                        char m[96];
                        snprintf(m, sizeof m, "wand at :%02d.%03d: box pixel (%d, %d) not covered", a.clk.s, a.clk.ms,
                                 x, y);
                        TEST_FAIL_MESSAGE(m);
                    }
        }
        if (step % 25 == 3 || stepped || minute) {
            char what[48];
            snprintf(what, sizeof what, "wand at :%02d.%03d + 40 ms", a.clk.s, a.clk.ms);
            dirty_pair(r, r->ops->draw, &a, &b, what);
        }
        a = b;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, script, "the script steps 5 times inside a minute (and once at the minute)");
    TEST_ASSERT_TRUE(steps > 1400);
}

/* the offset steps only when floor(sec_of_hour / 10) does: k = round(floor(t / 10) n / 360), (n - k) mod n; back to 0
 * after 3600 s; 0 unset and under reduced motion */
static void amber_script_step(void)
{
    uint16_t prev = 0;
    for (int t = 0; t <= 3600; t++) {
        for (int ms = 0; ms < 1000; ms += 499) {
            fk_face_in_t in = in_at(10 + t / 3600, (t / 60) % 60, t % 60, ms);
            const uint16_t off = dw_c10_script_off(&in);
            const long q = (t % 3600) / 10, k = lround((double)q * DW_C10_SCRIPT_NT / 360.0) % DW_C10_SCRIPT_NT;
            TEST_ASSERT_EQUAL_UINT16((uint16_t)((DW_C10_SCRIPT_NT - k) % DW_C10_SCRIPT_NT), off);
            if (t > 0 && ms == 0) {
                if (t % 10) TEST_ASSERT_EQUAL_UINT16_MESSAGE(prev, off, "the script moved off a 10 s mark");
                else TEST_ASSERT_TRUE_MESSAGE(prev != off, "the script did not step at a 10 s mark");
            }
            if (ms == 0) prev = off;
            in.reduced = true;
            TEST_ASSERT_EQUAL_UINT16(0, dw_c10_script_off(&in));
            in.reduced = false, in.clk.set = false;
            TEST_ASSERT_EQUAL_UINT16(0, dw_c10_script_off(&in));
        }
    }
    fk_face_in_t in = in_at(11, 0, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(0, dw_c10_script_off(&in));
    in = in_at(10, 59, 59, 999);   /* 359 steps of 1 degree */
    const uint16_t want = (uint16_t)(DW_C10_SCRIPT_NT - (359 * DW_C10_SCRIPT_NT + 180) / 360);
    TEST_ASSERT_EQUAL_UINT16(want, dw_c10_script_off(&in));
}

/* reduced motion: the wand steps once a second (no trail pulse), the script is still */
static void amber_reduced_steps(void)
{
    const row_t *r = row_of("amber");
    fk_face_in_t a = in_at(10, 9, 10, 100), b = in_at(10, 9, 10, 900), c = in_at(10, 9, 11, 100);
    a.reduced = b.reduced = c.reduced = true;
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    assert_same(s_a, s_b, "reduced :10.100 vs :10.900");
    row_frame(r, r->ops->draw, &c, s_a);
    TEST_ASSERT_TRUE_MESSAGE(frames_differ() > 50, "reduced :10.900 and :11.100 are the same frame");
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    TEST_ASSERT_EQUAL_FLOAT((10.f / 60.f) * 2 * PI_F, dw_c10_wand_angle(&b));
    /* the wake does not run under reduced motion */
    b.wake_ms = 100;
    row_frame(r, r->ops->draw, &b, s_a);
    b.wake_ms = UINT32_MAX;
    row_frame(r, r->ops->draw, &b, s_b);
    assert_same(s_b, s_a, "reduced: no wake");
}

/* the hour and minute hands step once a minute: inside their boxes (less the wand's at either time) :00 and :59 draw
 * the same pixels, and the boxes hold the hands */
static void amber_hands_per_minute(void)
{
    const row_t *r = row_of("amber");
    static const int hm[3][2] = {{10, 9}, {3, 47}, {6, 30}};
    for (int k = 0; k < 3; k++) {
        const fk_face_in_t a = in_at(hm[k][0], hm[k][1], 0, 0), b = in_at(hm[k][0], hm[k][1], 59, 0);
        row_frame(r, r->ops->draw, &a, s_a);
        row_frame(r, r->ops->draw, &b, s_b);
        fk_face_in_t u = a;
        u.clk.set = false;
        row_frame(r, r->ops->draw, &u, s_c);
        omni_rect_t hb[2], sa[4], sb[4];
        const float am = ((float)hm[k][1] / 60.f) * 2 * PI_F;
        const float ah = (((float)(hm[k][0] % 12) + (float)hm[k][1] / 60.f) / 12.f) * 2 * PI_F;
        dw_hand_amber_bbox(ah, true, &hb[0]);
        dw_hand_amber_bbox(am, false, &hb[1]);
        wand_boxes(&a, sa);
        wand_boxes(&b, sb);
        long hands = 0;
        for (int i = 0; i < 2; i++)
            for (int y = hb[i].y0 < 0 ? 0 : hb[i].y0; y < hb[i].y1 && y < W; y++)
                for (int x = hb[i].x0 < 0 ? 0 : hb[i].x0; x < hb[i].x1 && x < W; x++) {
                    if (in_any(sa, 4, x, y) || in_any(sb, 4, x, y)) continue;
                    TEST_ASSERT_EQUAL_HEX16_MESSAGE(s_a[y * W + x], s_b[y * W + x], "a hand moved within the minute");
                    hands += s_a[y * W + x] != s_c[y * W + x];
                }
        TEST_ASSERT_TRUE_MESSAGE(hands > 800, "the hands do not show in their boxes");
        /* outside the hands' boxes, the wand's and the script ring nothing differs either */
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                if (s_a[y * W + x] == s_b[y * W + x]) continue;
                TEST_ASSERT_TRUE_MESSAGE(in_any(sa, 4, x, y) || in_any(sb, 4, x, y) || in_script(x, y),
                                         ":00 and :59 differ outside the wand and the script");
            }
    }
}

/* the amber hands and the wand: band-exact, inside their boxes */
static void hand_wand_band_exact(void)
{
    static const float ang[] = {0.f, 0.3f, 1.5707963f, 2.2f, 3.14159265f, 4.0f, 5.5f, 6.1f, 0.785398f};
    for (unsigned k = 0; k < sizeof ang / sizeof ang[0]; k++) {
        for (int hour = 0; hour < 3; hour++) {   /* 2: the wand */
            memset(s_a, 0, sizeof s_a);
            if (hour < 2) dw_hand_amber(s_a, W, NULL, ang[k], hour != 0, 255);
            else dw_hand_wand(s_a, W, NULL, ang[k], 1.f);
            memset(s_b, 0, sizeof s_b);
            for (int y0 = 0; y0 < W; y0 += 8) {
                const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
                if (hour < 2) dw_hand_amber(s_b + (size_t)y0 * W, W, &c, ang[k], hour != 0, 255);
                else dw_hand_wand(s_b + (size_t)y0 * W, W, &c, ang[k], 1.f);
            }
            assert_same(s_a, s_b, hour < 2 ? "amber hand in 8-row clips" : "wand in 8-row clips");
            omni_rect_t bb[4];
            int nb = 1;
            if (hour < 2) dw_hand_amber_bbox(ang[k], hour != 0, &bb[0]);
            else dw_hand_wand_rects(ang[k], bb), nb = 4;
            long ink = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++)
                    if (s_a[y * W + x]) {
                        if (!in_any(bb, nb, x, y)) {
                            char m[96];
                            snprintf(m, sizeof m, "%s ink at (%d, %d) outside its boxes (angle %.3f)",
                                     hour < 2 ? "amber" : "wand", x, y, (double)ang[k]);
                            TEST_FAIL_MESSAGE(m);
                        }
                        ink++;
                    }
            TEST_ASSERT_TRUE(ink > 300);
        }
        /* the pulse lights only the tip glow: 0.75 and 1 differ, inside the rects */
        memset(s_a, 0, sizeof s_a);
        dw_hand_wand(s_a, W, NULL, ang[k], 0.75f);
        memset(s_b, 0, sizeof s_b);
        dw_hand_wand(s_b, W, NULL, ang[k], 1.f);
        TEST_ASSERT_TRUE(frames_differ() > 100);
    }
    /* the wand's boxes stay small: their union under 12 000 px */
    omni_rect_t sb[4];
    dw_hand_wand_rects(0.785398f, sb);
    TEST_ASSERT_TRUE(rects_union(sb, 4) < 12000);
}

/* the sweep: (s + ms / 1000) / 60 turns; the pulse .75 + .25 sin(2 pi 1.2 t) */
static void amber_sweep_and_pulse(void)
{
    for (int ms = 0; ms < 60000; ms += 137) {
        const fk_face_in_t in = in_at(10, 9, ms / 1000, ms % 1000);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, (float)(ms / 60000.0 * 2 * 3.14159265358979), dw_c10_wand_angle(&in));
        const double t = 36540.0 + ms / 1000.0;   /* 10:09:00 */
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)(0.75 + 0.25 * sin(2 * 3.14159265358979 * 1.2 * t)), dw_c10_pulse(&in));
        fk_face_in_t rd = in;
        rd.reduced = true;
        TEST_ASSERT_EQUAL_FLOAT(1.f, dw_c10_pulse(&rd));
    }
}

/* the wake: the dial fades up over 700 ms, the hands sweep from 12 (easeOut3 over 900 ms); its end is settled */
static void amber_wake(void)
{
    const row_t *r = row_of("amber");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &in, s_c);
    in.wake_ms = DW_C10_WAKE_MS;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "#10 at the wake's end vs settled");
    in.wake_ms = 0;
    row_frame(r, r->ops->draw, &in, s_a);
    /* the dial is black at 0: away from the hands (at 12) the frame is black */
    long lit = 0;
    for (int y = 233; y < W; y++)
        for (int x = 0; x < W; x++) lit += s_a[y * W + x] != 0 && (x < 213 || x > 253);
    TEST_ASSERT_EQUAL_INT32(0, lit);
    in.wake_ms = 350;   /* half faded, the hands on their way */
    row_frame(r, r->ops->draw, &in, s_a);
    long dimmer = 0;
    for (int i = 0; i < W * W; i++) dimmer += s_a[i] != s_c[i];
    TEST_ASSERT_TRUE(dimmer > 20000);
}

/* unset: no hands, --:-- below the centre; the stale marker centred (233, 317) */
static void amber_unset_text(void)
{
    const row_t *r = row_of("amber");
    fk_face_in_t in = in_at(10, 9, 15, 0);
    row_frame(r, r->ops->draw, &in, s_a);
    in.clk.set = false;
    row_frame(r, r->ops->draw, &in, s_b);
    long text = 0;
    for (int y = 270; y < 300; y++)
        for (int x = 200; x < 266; x++) text += s_a[y * W + x] != s_b[y * W + x];
    TEST_ASSERT_TRUE_MESSAGE(text > 40, "no --:-- when unset");
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_C10_STALE.x0 + DW_C10_STALE.x1);
    TEST_ASSERT_EQUAL_INT(2 * 317, DW_C10_STALE.y0 + DW_C10_STALE.y1);
}

static void amber_annulus_fits(void)
{
    const size_t n = fk_annulus_bytes(DW_C10_SCRIPT_R0, DW_C10_SCRIPT_R1, DW_C10_SCRIPT_NT);
    TEST_ASSERT_TRUE(n > 0 && DW_FACE_AMBER.ctx_bytes > n && DW_FACE_AMBER.ctx_bytes < n + 16384u);
    TEST_ASSERT_EQUAL_STRING("amber", DW_FACE_AMBER.key);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_AMBER, dw_face_by_key("amber"));
    TEST_ASSERT_FALSE(DW_FACE_AMBER.reveal);
}

/* ---- #13: the Fob watch (wave 2 Task 7) */
/* the screen disc's pixels (centres inside r 233) */
static long disc_px(void)
{
    long n = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const long dx = 2 * x + 1 - W, dy = 2 * y + 1 - W;
            n += dx * dx + dy * dy < (long)W * W;
        }
    return n;
}

/* the screen disc's pixels the lid covers at theta, from DW_F13_HINGE's lines: row y samples source row v at
 * u = floor(233 + (x + .5 - 233) / s), on the lid where (u, v)'s centre lies inside its r 233 disc */
static long lid_cover(float theta)
{
    static omni_hinge_line_t ln[W];
    omni_hinge_lines(&DW_F13_HINGE, theta, ln);
    long n = 0;
    for (int y = 0; y < W; y++) {
        if (ln[y].v < 0) continue;
        const long dv = 2L * ln[y].v + 1 - W;
        for (int x = 0; x < W; x++) {
            const long dx = 2 * x + 1 - W, dy = 2 * y + 1 - W;
            if (dx * dx + dy * dy >= (long)W * W) continue;
            const long long q = (233LL << 17) + (long long)(2 * x + 1 - W) * ln[y].inv_s_q16;
            if (q < 0) continue;
            const long u = (long)(q >> 17), du = 2 * u + 1 - W;
            n += u < W && du * du + dv * dv < (long)W * W;
        }
    }
    return n;
}

/* a sentinel dial: the bind's getter hands #13 a dial of one colour no lid pixel has (pure green), so a frame's lid
 * pixels are those that differ from the dial frame (R148 (b): the coverage measured on drawn frames) */
#define SENTINEL 0x07E0
static uint16_t s_sentinel[6 + W * W];   /* the 12-byte header, then the RGB565 plane */
static const void *sentinel_get(const char *name, size_t *size)
{
    if (strcmp(name, ASSET_DW_F13_DIAL) != 0) return dw_assets_host_get(name, size);
    uint8_t *h = (uint8_t *)s_sentinel;
    h[0] = 0x19, h[1] = FK_CF_RGB565, h[2] = h[3] = 0;
    h[4] = W & 255, h[5] = W >> 8, h[6] = W & 255, h[7] = W >> 8, h[8] = (2 * W) & 255, h[9] = (2 * W) >> 8;
    h[10] = h[11] = 0;
    for (int i = 0; i < W * W; i++) s_sentinel[6 + i] = SENTINEL;
    *size = sizeof s_sentinel;
    return s_sentinel;
}

/* the drawn lid's pixels inside the screen disc at in (unset: no hands), against the frame with no lid */
static long drawn_lid(const void *ctx, const fk_face_in_t *in)
{
    fk_face_in_t base = *in;
    base.pulsing = false, base.pulse_ms = 0, base.wake_ms = UINT32_MAX;
    fk_test_frame(&DW_FACE_FOB, ctx, &base, s_b);
    fk_test_frame(&DW_FACE_FOB, ctx, in, s_a);
    long n = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const long dx = 2 * x + 1 - W, dy = 2 * y + 1 - W;
            if (dx * dx + dy * dy < (long)W * W) n += s_a[y * W + x] != s_b[y * W + x];
        }
    return n;
}

/* spec section 11: the lid covers more than half the disc for no more than 500 ms in all, on a wake and on a tap's
 * close and reopen (10 ms steps). lid_cover is checked against drawn frames first (R148 (b)), so the count is the
 * face's own: the closed lid, and two angles of a tap's close (no light then) */
static void fob_burn_in(void)
{
    void *ctx = calloc(1, DW_FACE_FOB.ctx_bytes);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_TRUE(DW_FACE_FOB.bind(ctx, sentinel_get));
    fk_face_in_t in = in_at(10, 9, 30, 500);
    in.clk.set = false;   /* no hands; --:-- lies under the lid where the lid is */
    struct { uint32_t wake; uint16_t pulse; float theta; } at[3] = {
        {50, 0, 0.f}, {UINT32_MAX, 250, 0.f}, {UINT32_MAX, 380, 0.f}};
    at[0].theta = dw_f13_theta(50, false), at[1].theta = dw_f13_tap_theta(250), at[2].theta = dw_f13_tap_theta(380);
    for (int k = 0; k < 3; k++) {
        fk_face_in_t f = in;
        f.wake_ms = at[k].wake;
        f.pulsing = at[k].pulse != 0, f.pulse_ms = at[k].pulse;
        const long drawn = drawn_lid(ctx, &f), want = lid_cover(at[k].theta);
        char m[96];
        snprintf(m, sizeof m, "theta %.4f: %ld lid pixels drawn, lid_cover says %ld", (double)at[k].theta, drawn, want);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(want, drawn, m);
        TEST_ASSERT_TRUE(drawn > 1000);
    }
    free(ctx);
    const long half = disc_px() / 2;
    TEST_ASSERT_EQUAL_UINT16(DW_F13_HOLD_MS + DW_F13_OPEN_MS + DW_F13_LIGHT_MS, DW_FACE_FOB.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(2200, DW_FACE_FOB.wake_len_ms);
    int wake = 0, tap = 0;
    for (uint32_t t = 0; t < DW_FACE_FOB.wake_len_ms; t += 10) wake += lid_cover(dw_f13_theta(t, false)) > half ? 10 : 0;
    for (uint32_t t = 0; t < DW_FACE_FOB.pulse_len_ms; t += 10) tap += lid_cover(dw_f13_tap_theta(t)) > half ? 10 : 0;
    printf("fob burn-in: the lid covers > half the disc for %d ms on a wake, %d ms on a tap\n", wake, tap);
    TEST_ASSERT_TRUE_MESSAGE(wake <= 500, "the wake's lid covers more than half the disc for over 500 ms");
    TEST_ASSERT_TRUE_MESSAGE(tap <= 500, "a tap's lid covers more than half the disc for over 500 ms");
    TEST_ASSERT_INT_WITHIN_MESSAGE(10, 300, wake, "the wake's burn-in moved from its pinned 300 ms");
    TEST_ASSERT_INT_WITHIN_MESSAGE(10, 410, tap, "the tap's burn-in moved from its pinned 410 ms");
    TEST_ASSERT_EQUAL_INT32(disc_px(), lid_cover(0.f));
}

/* R148 (a): a re-tap while the lid closes (pulse_ms < CLOSE) is ignored; one after the close restarts the pulse. The
 * rule face_generic applies (fk_pulse_retap), stepped as it runs: taps at 0, 200 (ignored: the close goes on, no
 * jump) and 600 (restarts: open again, closing) */
static void fob_retap_guard(void)
{
    TEST_ASSERT_EQUAL_UINT16(DW_F13_CLOSE_MS, DW_FACE_FOB.pulse_guard_ms);
    TEST_ASSERT_EQUAL_UINT16(0, DW_FACE_CALLBOX.pulse_guard_ms);   /* the lamp: every tap restarts */
    TEST_ASSERT_TRUE(fk_pulse_retap(&DW_FACE_CALLBOX, true, 10));
    TEST_ASSERT_TRUE(fk_pulse_retap(&DW_FACE_FOB, false, 0));
    TEST_ASSERT_FALSE(fk_pulse_retap(&DW_FACE_FOB, true, 0));
    TEST_ASSERT_FALSE(fk_pulse_retap(&DW_FACE_FOB, true, DW_F13_CLOSE_MS - 1));
    TEST_ASSERT_TRUE(fk_pulse_retap(&DW_FACE_FOB, true, DW_F13_CLOSE_MS));
    TEST_ASSERT_TRUE(fk_pulse_retap(&DW_FACE_FOB, true, 2000));
    /* the run: t0 the pulse's start (face_generic's pulse_t0); taps at 0, 200, 600 */
    long t0 = -1;
    float prev = DW_F13_OPEN_RAD;
    for (long t = 0; t <= 1200; t += 10) {
        const bool pulsing = t0 >= 0 && t - t0 < DW_FACE_FOB.pulse_len_ms;
        if ((t == 0 || t == 200 || t == 600) && fk_pulse_retap(&DW_FACE_FOB, pulsing, pulsing ? (uint32_t)(t - t0) : 0))
            t0 = t;
        if (t == 200) TEST_ASSERT_EQUAL_INT_MESSAGE(0, t0, "a re-tap during the close restarted the pulse");
        if (t == 600) TEST_ASSERT_EQUAL_INT_MESSAGE(600, t0, "a re-tap after the close did not restart the pulse");
        const float th = dw_f13_tap_theta((uint32_t)(t - t0));
        if (t > 0 && t < DW_F13_CLOSE_MS)   /* the close runs on through the ignored tap: always lower, never a jump */
            TEST_ASSERT_TRUE_MESSAGE(th < prev && prev - th < 0.2f, "the lid jumped during its close");
        if (t == 600) TEST_ASSERT_EQUAL_FLOAT(DW_F13_OPEN_RAD, th);   /* the restart: the close again from open */
        prev = th;
    }
}

/* a tap anywhere is a pulse; the lid closes over CLOSE, opens again at once, and is gone before CLOSE + OPEN */
static void fob_tap_reopens(void)
{
    const row_t *r = row_of("fob");
    TEST_ASSERT_NOT_NULL(r->ops->tap);
    static const int16_t at[][2] = {{233, 10}, {10, 233}, {233, 233}, {455, 233}, {233, 455}, {0, 0}, {465, 465}};
    for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++)
        TEST_ASSERT_EQUAL_INT(FK_TAP_PULSE, r->ops->tap(r->ctx, at[i][0], at[i][1]));
    fk_test_tap_edges(r->ops, r->ctx, (omni_rect_t){0, 0, W, W}, FK_TAP_PULSE);
    TEST_ASSERT_EQUAL_FLOAT(DW_F13_OPEN_RAD, dw_f13_tap_theta(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.62f * 3.14159265f, dw_f13_tap_theta(0));
    TEST_ASSERT_EQUAL_FLOAT(0.f, dw_f13_tap_theta(DW_F13_CLOSE_MS));
    for (uint32_t p = 10; p < DW_F13_CLOSE_MS; p += 10)   /* closing all the way */
        TEST_ASSERT_TRUE(dw_f13_tap_theta(p) < dw_f13_tap_theta(p - 10));
    TEST_ASSERT_TRUE_MESSAGE(dw_f13_tap_theta(DW_F13_CLOSE_MS + 40) > 0.f, "the lid holds closed after a tap");
    const float gone = omni_hinge_gone_rad(&DW_F13_HINGE);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 76.88f * 3.14159265f / 180.f, gone);
    TEST_ASSERT_TRUE(dw_f13_tap_theta(DW_F13_CLOSE_MS + DW_F13_OPEN_MS - 10) > gone);
    TEST_ASSERT_EQUAL_FLOAT(DW_F13_OPEN_RAD, dw_f13_tap_theta(DW_F13_CLOSE_MS + DW_F13_OPEN_MS));
    TEST_ASSERT_EQUAL_UINT16(2520, DW_FACE_FOB.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT16(DW_F13_CLOSE_MS + DW_F13_OPEN_MS + DW_F13_LIGHT_MS, DW_FACE_FOB.pulse_len_ms);
    /* the wake: closed for the hold, then open by easeOut3 */
    TEST_ASSERT_EQUAL_FLOAT(0.f, dw_f13_theta(0, false));
    TEST_ASSERT_EQUAL_FLOAT(0.f, dw_f13_theta(DW_F13_HOLD_MS, false));
    TEST_ASSERT_TRUE(dw_f13_theta(DW_F13_HOLD_MS + 20, false) > 0.f);
    TEST_ASSERT_EQUAL_FLOAT(DW_F13_OPEN_RAD, dw_f13_theta(DW_F13_HOLD_MS + DW_F13_OPEN_MS, false));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, DW_F13_OPEN_RAD * (1.f - 0.125f), dw_f13_theta(DW_F13_HOLD_MS + 450, false));
}

/* past omni_hinge_gone_rad (76.9 degrees) no lid row: every line is off the leaf, and the face's frame is the frame
 * with no lid */
static void fob_lid_gone(void)
{
    const float gone = omni_hinge_gone_rad(&DW_F13_HINGE);
    static omni_hinge_line_t ln[W];
    for (float th = gone + 0.0005f; th <= DW_F13_OPEN_RAD + 1e-6f; th += 0.01f) {
        omni_hinge_lines(&DW_F13_HINGE, th, ln);
        for (int y = 0; y < W; y++)
            if (ln[y].v >= 0) {
                char m[96];
                snprintf(m, sizeof m, "theta %.4f (past %.4f): row %d shows lid row %d", (double)th, (double)gone, y,
                         ln[y].v);
                TEST_FAIL_MESSAGE(m);
            }
    }
    omni_hinge_lines(&DW_F13_HINGE, gone - 0.02f, ln);   /* just before it, the leaf still shows */
    int rows = 0;
    for (int y = 0; y < W; y++) rows += ln[y].v >= 0;
    TEST_ASSERT_TRUE(rows > 0);
    /* the face: a tap's frame and a wake's frame, both past it with their light at the same time since it began, are
     * equal (the tap's lid and the wake's lid draw no row) */
    const row_t *r = row_of("fob");
    fk_face_in_t a = in_at(10, 9, 30, 500), b = a;
    a.pulsing = true, a.pulse_ms = DW_F13_CLOSE_MS + 700;   /* well past gone */
    float th;
    uint8_t a8;
    dw_f13_lid(&a, &th, &a8);
    TEST_ASSERT_TRUE(th > gone && a8 == 255);
    b.wake_ms = DW_F13_HOLD_MS + 700;   /* the wake's light 700 ms in, the same as the tap's */
    dw_f13_lid(&b, &th, &a8);
    TEST_ASSERT_TRUE(th > gone);
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    assert_same(s_b, s_a, "a tap's and a wake's frames past the gone angle");
}

/* the sub-seconds tick four times a second: .000, .260, .510 and .760 are pairwise different, and only inside
 * DW_F13_SUB */
static void fob_sub_ticks(void)
{
    const row_t *r = row_of("fob");
    static const int ms[4] = {0, 260, 510, 760};
    static uint16_t fr[4][W * W];
    for (int i = 0; i < 4; i++) {
        const fk_face_in_t in = in_at(10, 9, 30, ms[i]);
        row_frame(r, r->ops->draw, &in, fr[i]);
    }
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) {
            long differ = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++) {
                    if (fr[i][y * W + x] == fr[j][y * W + x]) continue;
                    differ++;
                    if (!inside(&DW_F13_SUB, x, y)) {
                        char m[96];
                        snprintf(m, sizeof m, ".%03d and .%03d differ at (%d, %d), outside DW_F13_SUB", ms[i], ms[j],
                                 x, y);
                        TEST_FAIL_MESSAGE(m);
                    }
                }
            TEST_ASSERT_TRUE_MESSAGE(differ > 20, "two quarters of a second draw the same sub-seconds");
        }
    TEST_ASSERT_EQUAL_INT(96, DW_F13_SUB.x1 - DW_F13_SUB.x0);
    TEST_ASSERT_EQUAL_INT(96, DW_F13_SUB.y1 - DW_F13_SUB.y0);
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_F13_SUB.x0 + DW_F13_SUB.x1);
    TEST_ASSERT_EQUAL_INT(2 * 337, DW_F13_SUB.y0 + DW_F13_SUB.y1);
}

/* at rest dirty() is exactly {DW_F13_SUB} when floor(s x 4) changes and empty when it does not (mid-minute) */
static void fob_rest_dirty(void)
{
    const row_t *r = row_of("fob");
    TEST_ASSERT_EQUAL_UINT16(40, DW_FACE_FOB.cadence_ms);
    for (int red = 0; red < 2; red++) {
        fk_face_in_t a = in_at(10, 9, 0, 0);
        a.reduced = red != 0;
        int subs = 0;
        for (int step = 0; step < 1499; step++) {   /* :00.000 .. :59.920, 40 ms apart, inside the minute */
            const fk_face_in_t b = later(a, 40);
            omni_rect_t rects[8];
            const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
            const unsigned qa = a.clk.s * 4u + (red ? 0u : a.clk.ms / 250u), qb = b.clk.s * 4u + (red ? 0u : b.clk.ms / 250u);
            if (qa == qb) {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, n, "the 40 ms timer redraws within a quarter");
            } else {
                TEST_ASSERT_EQUAL_UINT8(1, n);
                TEST_ASSERT_EQUAL_MEMORY(&DW_F13_SUB, &rects[0], sizeof rects[0]);
                subs++;
            }
            if (step % 97 == 5 || (qa != qb && step % 13 == 0)) {
                char what[48];
                snprintf(what, sizeof what, "fob at :%02d.%03d + 40 ms", a.clk.s, a.clk.ms);
                dirty_pair(r, r->ops->draw, &a, &b, what);
            }
            a = b;
        }
        TEST_ASSERT_EQUAL_INT(red ? 59 : 239, subs);
    }
    /* the minute: the hands' boxes and the sub-dial, and they hold every change */
    fk_face_in_t a = in_at(10, 9, 59, 990), b = later(a, 40);
    omni_rect_t rects[8];
    const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
    TEST_ASSERT_TRUE(n >= 2 && n <= 8);
    TEST_ASSERT_TRUE_MESSAGE(dirty_pair(r, r->ops->draw, &a, &b, "fob at the minute") < 100000, "the minute's boxes");
}

/* reduced motion (R147 PR7): a wake shows the dial at once (no lid); a tap's flat lid fades 255 -> 0 over 500 ms; the
 * sub-seconds hold within a second */
static void fob_reduced_fade(void)
{
    const row_t *r = row_of("fob");
    fk_face_in_t in = in_at(10, 9, 30, 100);
    in.reduced = true;
    row_frame(r, r->ops->draw, &in, s_c);   /* settled, reduced */
    float th;
    uint8_t a8;
    for (uint32_t w = 0; w <= 2200; w += 100) {
        in.wake_ms = w;
        dw_f13_lid(&in, &th, &a8);
        TEST_ASSERT_EQUAL_FLOAT(0.f, th);
        TEST_ASSERT_EQUAL_UINT8(0, a8);
        TEST_ASSERT_EQUAL_FLOAT(0.f, dw_f13_theta(w, true));
    }
    in.wake_ms = 0;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "reduced: a wake's first frame is the dial");
    in.wake_ms = UINT32_MAX;
    in.pulsing = true;
    uint8_t prev = 255;
    for (uint32_t p = 0; p <= 600; p += 20) {
        in.pulse_ms = (uint16_t)p;
        dw_f13_lid(&in, &th, &a8);
        TEST_ASSERT_EQUAL_FLOAT(0.f, th);
        if (p == 0) TEST_ASSERT_EQUAL_UINT8(255, a8);
        if (p >= DW_F13_FADE_MS) TEST_ASSERT_EQUAL_UINT8(0, a8);
        TEST_ASSERT_TRUE(a8 <= prev);
        if (p > 0 && p < DW_F13_FADE_MS) TEST_ASSERT_TRUE(a8 < prev);
        prev = a8;
    }
    in.pulse_ms = 250;
    dw_f13_lid(&in, &th, &a8);
    TEST_ASSERT_UINT8_WITHIN(1, 127, a8);
    row_frame(r, r->ops->draw, &in, s_a);
    long differ = 0;
    for (int i = 0; i < W * W; i++) differ += s_a[i] != s_c[i];
    TEST_ASSERT_TRUE_MESSAGE(differ > 100000, "a reduced tap shows no fading lid");
    in.pulse_ms = DW_F13_FADE_MS;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "reduced: the tap's fade has ended at 500 ms");
    /* the sub-seconds frozen within a second */
    in.pulsing = false;
    in.clk.ms = 900;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "reduced: .100 and .900 draw the same sub-seconds");
}

/* a lid frame built in 8-row bands equals the frame built whole (the wake at 400 ms, as the brief names it, and at
 * 250 ms, where the lid shows, and a tap's close) */
static void fob_wake_band_exact(void)
{
    const row_t *r = row_of("fob");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    for (int k = 0; k < 4; k++) {
        in.wake_ms = k == 0 ? 400u : k == 1 ? 250u : k == 2 ? 0u : UINT32_MAX;
        in.pulsing = k == 3, in.pulse_ms = 300;
        row_frame(r, r->ops->draw, &in, s_a);
        in_clips(r, r->ops->draw, &in, 8, W, s_b);
        assert_same(s_a, s_b, "a lid frame in 8-row bands");
        in_clips(r, r->ops->draw, &in, 5, 31, s_b);
        assert_same(s_a, s_b, "a lid frame in 5 x 31 blocks");
    }
    in.pulsing = false;
    in.wake_ms = 250;   /* the lid shows there */
    float th;
    uint8_t a8;
    dw_f13_lid(&in, &th, &a8);
    TEST_ASSERT_TRUE(a8 == 255 && th > 0.f && th < omni_hinge_gone_rad(&DW_F13_HINGE));
}

/* R147 PR20: with a tap's pulse and the lid's wake both running, the lid's angle is the tap's */
static void fob_pulse_over_wake(void)
{
    const row_t *r = row_of("fob");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    static const uint32_t W_MS[3] = {50, 300, 1500};
    static const uint16_t P_MS[3] = {100, 400, 700};
    for (int i = 0; i < 3; i++) {
        in.wake_ms = W_MS[i];
        in.pulsing = true, in.pulse_ms = P_MS[i];
        float th;
        uint8_t a8;
        dw_f13_lid(&in, &th, &a8);
        TEST_ASSERT_EQUAL_FLOAT(dw_f13_tap_theta(P_MS[i]), th);
        TEST_ASSERT_TRUE(th != dw_f13_theta(W_MS[i], false));
        TEST_ASSERT_EQUAL_UINT8(255, a8);
        /* and the frame is the tap's alone: the same pulse with the wake settled draws it */
        row_frame(r, r->ops->draw, &in, s_a);
        fk_face_in_t p = in;
        p.wake_ms = UINT32_MAX;
        row_frame(r, r->ops->draw, &p, s_b);
        assert_same(s_b, s_a, "a pulse over the wake draws the pulse's frame");
    }
}

/* the wake: the closed lid at 0 covers the dial; the wake's end is the settled frame exactly */
static void fob_wake(void)
{
    const row_t *r = row_of("fob");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &in, s_c);
    in.wake_ms = DW_FACE_FOB.wake_len_ms - 1;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "#13 a ms before the wake's end vs settled");
    in.wake_ms = 0;
    row_frame(r, r->ops->draw, &in, s_a);
    long differ = 0;
    for (int i = 0; i < W * W; i++) differ += s_a[i] != s_c[i];
    TEST_ASSERT_TRUE_MESSAGE(differ > 150000, "the closed lid does not cover the dial");
    in.wake_ms = DW_F13_HOLD_MS + 600;   /* the lid gone, the light up */
    row_frame(r, r->ops->draw, &in, s_a);
    long brighter = 0;
    for (int i = 0; i < W * W; i++) brighter += s_a[i] != s_c[i];
    TEST_ASSERT_TRUE_MESSAGE(brighter > 50000, "no escaping light");
    TEST_ASSERT_EQUAL_STRING("fob", DW_FACE_FOB.key);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_FOB, dw_face_by_key("fob"));
    TEST_ASSERT_FALSE(DW_FACE_FOB.reveal);
    TEST_ASSERT_TRUE(DW_FACE_FOB.ctx_bytes < 4096u);
    TEST_ASSERT_EQUAL_INT(OMNI_HINGE_HORIZONTAL, DW_F13_HINGE.axis);
    TEST_ASSERT_EQUAL_FLOAT(0.f, DW_F13_HINGE.pos);
    TEST_ASSERT_EQUAL_FLOAT(1000.f, DW_F13_HINGE.focal);
    TEST_ASSERT_EQUAL_UINT16(466, DW_F13_HINGE.n);
    TEST_ASSERT_EQUAL_FLOAT(0.f, DW_F13_HINGE.dome_r);
    TEST_ASSERT_NOT_NULL(dw_glow_lut(233));
}

/* unset: no hands, --:-- about (233, 173); the stale marker centred (233, 277) */
static void fob_unset_text(void)
{
    const row_t *r = row_of("fob");
    fk_face_in_t in = in_at(10, 9, 15, 0);
    row_frame(r, r->ops->draw, &in, s_a);
    in.clk.set = false;
    row_frame(r, r->ops->draw, &in, s_b);
    long text = 0;
    for (int y = 160; y < 186; y++)
        for (int x = 200; x < 266; x++) text += s_a[y * W + x] != s_b[y * W + x];
    TEST_ASSERT_TRUE_MESSAGE(text > 40, "no --:-- when unset");
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_F13_STALE.x0 + DW_F13_STALE.x1);
    TEST_ASSERT_EQUAL_INT(2 * 277, DW_F13_STALE.y0 + DW_F13_STALE.y1);
}

/* the Breguet hands: band-exact, inside their boxes */
static void hand_breguet_band_exact(void)
{
    static const float ang[] = {0.f, 0.3f, 1.5707963f, 2.2f, 3.14159265f, 4.0f, 5.5f, 6.1f, 0.785398f};
    for (unsigned k = 0; k < sizeof ang / sizeof ang[0]; k++)
        for (int hour = 0; hour < 2; hour++) {
            memset(s_a, 0, sizeof s_a);
            dw_hand_breguet(s_a, W, NULL, ang[k], hour != 0, RGB565(0xe8, 0xec, 0xf0), 255);
            memset(s_b, 0, sizeof s_b);
            for (int y0 = 0; y0 < W; y0 += 8) {
                const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
                dw_hand_breguet(s_b + (size_t)y0 * W, W, &c, ang[k], hour != 0, RGB565(0xe8, 0xec, 0xf0), 255);
            }
            assert_same(s_a, s_b, "Breguet hand in 8-row clips");
            omni_rect_t bb;
            dw_hand_breguet_bbox(ang[k], hour != 0, &bb);
            long ink = 0;
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++)
                    if (s_a[y * W + x]) {
                        if (!inside(&bb, x, y)) {
                            char m[96];
                            snprintf(m, sizeof m, "Breguet ink at (%d, %d) outside its box (angle %.3f)", x, y,
                                     (double)ang[k]);
                            TEST_FAIL_MESSAGE(m);
                        }
                        ink++;
                    }
            TEST_ASSERT_TRUE(ink > 300);
        }
}

/* ---- #9: Console panel (wave 2 Task 9) */
/* a console input: set, battery 76, the sun from 07:00 to 19:00 */
static fk_face_in_t con_at(int h, int m, int s, int ms)
{
    fk_face_in_t in = in_at(h, m, s, ms);
    in.battery = 76;
    in.sun_rise_m = 420, in.sun_set_m = 1140;
    return in;
}

/* every differing pixel of a and b lies inside one of rects[0 .. n); returns how many differ */
static long differ_within(const uint16_t *a, const uint16_t *b, const omni_rect_t *rects, int n, const char *what)
{
    long d = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            if (a[y * W + x] == b[y * W + x]) continue;
            bool in = false;
            for (int k = 0; k < n && !in; k++) in = inside(&rects[k], x, y);
            if (!in) {
                char m[160];
                snprintf(m, sizeof m, "%s: pixel (%d, %d) differs outside its rects", what, x, y);
                TEST_FAIL_MESSAGE(m);
            }
            d++;
        }
    return d;
}

static long differ_in(const uint16_t *a, const uint16_t *b, const omni_rect_t *r)
{
    long d = 0;
    for (int y = r->y0 < 0 ? 0 : r->y0; y < r->y1 && y < W; y++)
        for (int x = r->x0 < 0 ? 0 : r->x0; x < r->x1 && x < W; x++) d += a[y * W + x] != b[y * W + x];
    return d;
}

/* the geometry: each meter's window is its r 36 circle's box at instAt(p, 156) (p.a -90, -30, 30, 90, 150, 210), the
 * lamps' plate inside the signal panel's window, the stale mark centred (233, 301) inside the collar, the centre inside
 * the collar; no two of them meet */
static void console_geometry(void)
{
    static const float A[6] = {-90.f, -30.f, 30.f, 90.f, 150.f, 210.f};
    omni_rect_t all[9];
    for (int i = 0; i < 6; i++) {
        const double a = A[i] * 3.14159265358979 / 180.0, x = 233.0 + cos(a) * 156.0, y = 233.0 + sin(a) * 156.0;
        const omni_rect_t *r = &DW_P9_METER[i];
        TEST_ASSERT_EQUAL_INT((int)floor(x - 36.0 + 1e-6), r->x0);   /* 1e-6: the double pi's own error */
        TEST_ASSERT_EQUAL_INT((int)floor(y - 36.0 + 1e-6), r->y0);
        TEST_ASSERT_EQUAL_INT((int)ceil(x + 36.0 - 1e-6), r->x1);
        TEST_ASSERT_EQUAL_INT((int)ceil(y + 36.0 - 1e-6), r->y1);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)x, DW_P9_XY[i][0]);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)y, DW_P9_XY[i][1]);
        all[i] = *r;
    }
    TEST_ASSERT_TRUE(rect_in(&DW_P9_LAMPS, &DW_P9_METER[4]));
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_P9_STALE.x0 + DW_P9_STALE.x1);
    TEST_ASSERT_EQUAL_INT(2 * 301, DW_P9_STALE.y0 + DW_P9_STALE.y1);
    /* the collar's hexagon (r 100, a vertex at 3 o'clock): its flat sides at 233 -+ 86.6 */
    TEST_ASSERT_TRUE(DW_P9_STALE.y1 <= 320 && DW_P9_CENTRE.y0 >= 146 && DW_P9_CENTRE.y1 <= 320);
    TEST_ASSERT_TRUE(DW_P9_CENTRE.x0 >= 133 && DW_P9_CENTRE.x1 <= 333);
    all[6] = DW_P9_LAMPS, all[7] = DW_P9_CENTRE, all[8] = DW_P9_STALE;
    for (int i = 0; i < 9; i++)
        for (int j = i + 1; j < 9; j++) {
            if (i == 4 && j == 6) continue;   /* the lamps lie in the signal window */
            const bool meet = all[i].x0 < all[j].x1 && all[j].x0 < all[i].x1 && all[i].y0 < all[j].y1 &&
                              all[j].y0 < all[i].y1;
            TEST_ASSERT_FALSE_MESSAGE(meet, "two console rects meet");
        }
}

/* within a minute, at 40 ms steps, every dirty rect lies inside the minutes meter, the seconds dial or the lamps (and
 * the frames agree: dirty_pair, every 25th step) */
static void console_regions(void)
{
    const row_t *r = row_of("console");
    const omni_rect_t ok[3] = {DW_P9_METER[1], DW_P9_METER[3], DW_P9_LAMPS};
    for (int pass = 0; pass < 2; pass++) {
        const fk_face_in_t base = con_at(10, 9, 0, 0);
        for (int t = 0; t + 40 < 60000; t += 40) {
            fk_face_in_t a = later(base, t), b = later(base, t + 40);
            a.reduced = b.reduced = pass != 0;
            omni_rect_t rects[8];
            const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
            TEST_ASSERT_TRUE(n <= 8);
            for (int k = 0; k < n; k++) {
                bool in = false;
                for (int j = 0; j < 3 && !in; j++) in = rect_in(&rects[k], &ok[j]);
                if (!in) {
                    char m[160];
                    snprintf(m, sizeof m, "console at 10:09:%02d.%03d%s: dirty rect {%d, %d, %d, %d} outside the three",
                             a.clk.s, a.clk.ms, pass ? " (reduced)" : "", rects[k].x0, rects[k].y0, rects[k].x1,
                             rects[k].y1);
                    TEST_FAIL_MESSAGE(m);
                }
            }
            if ((t / 40) % 25 == 0) {
                char what[64];
                snprintf(what, sizeof what, "console 10:09:%02d.%03d + 40 ms", a.clk.s, a.clk.ms);
                dirty_pair(r, r->ops->draw, &a, &b, what);
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT16(40, DW_FACE_CONSOLE.cadence_ms);
}

/* unset: the hour and minute needles at 0 (and the seconds hand at 12), only the red lamp lit, the in-flight lamp off */
static void console_unset(void)
{
    const row_t *r = row_of("console");
    static const int ms_of_2s[] = {0, 100, 650, 699, 700, 1000, 1100, 1999};
    for (unsigned k = 0; k < sizeof ms_of_2s / sizeof ms_of_2s[0]; k++)
        for (int red = 0; red < 2; red++) {
            fk_face_in_t in = con_at(10, 9, 30 + ms_of_2s[k] / 1000, ms_of_2s[k] % 1000);
            in.clk.set = false;
            in.reduced = red != 0;
            float v[6];
            dw_p9_values(&in, v);
            TEST_ASSERT_EQUAL_FLOAT(0.f, v[0]);
            TEST_ASSERT_EQUAL_FLOAT(0.f, v[1]);
            TEST_ASSERT_EQUAL_FLOAT(0.f, v[3]);
            TEST_ASSERT_EQUAL_FLOAT(0.f, v[5]);   /* no clock, no sun */
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.76f, v[2]);   /* the battery is no clock reading */
            uint16_t rgb[3];
            float on[3];
            dw_p9_lamps(&in, rgb, on);
            TEST_ASSERT_EQUAL_FLOAT(0.f, on[0]);
            TEST_ASSERT_EQUAL_FLOAT(1.f, on[1]);
            TEST_ASSERT_EQUAL_HEX16(RGB565(0xE0, 0x40, 0x2A), rgb[1]);
            TEST_ASSERT_EQUAL_FLOAT(0.f, on[2]);
        }
    /* the needles' windows equal a set clock's at 00:00:00.000 (every value 0 there, the tick too) */
    fk_face_in_t in = con_at(10, 9, 30, 500), z = con_at(0, 0, 0, 0);
    in.clk.set = false;
    z.sun_rise_m = z.sun_set_m = -1;
    row_frame(r, r->ops->draw, &in, s_a);
    row_frame(r, r->ops->draw, &z, s_b);
    static const int same[3] = {0, 1, 3};
    for (int k = 0; k < 3; k++) TEST_ASSERT_EQUAL_INT(0, differ_in(s_a, s_b, &DW_P9_METER[same[k]]));
    /* a synced clock lights the green lamp instead */
    fk_face_in_t ok = con_at(10, 9, 30, 900);
    uint16_t rgb[3];
    float on[3];
    dw_p9_lamps(&ok, rgb, on);
    TEST_ASSERT_EQUAL_FLOAT(1.f, on[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.f, on[1]);
    TEST_ASSERT_EQUAL_HEX16(RGB565(0x5F, 0xCF, 0x6A), rgb[0]);
    ok.clk.stale = true;
    dw_p9_lamps(&ok, rgb, on);
    TEST_ASSERT_EQUAL_FLOAT(0.f, on[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.f, on[1]);
    TEST_ASSERT_EQUAL_HEX16(RGB565(0xF2, 0xA3, 0x3A), rgb[1]);
    row_frame(r, r->ops->draw, &ok, s_a);
    ok.clk.stale = false;
    row_frame(r, r->ops->draw, &ok, s_b);
    TEST_ASSERT_TRUE_MESSAGE(differ_in(s_a, s_b, &DW_P9_LAMPS) > 20, "the amber lamp does not show when stale");
}

/* solar: clamp((now - rise) / (set - rise)) in whole minutes: rise 420, set 1140 gives 0.4167 at 12:00 and 0.5 at 13:00,
 * the same at :00 and :59 of a minute; 0 with no sun */
static void console_solar(void)
{
    const row_t *r = row_of("console");
    float v[6];
    fk_face_in_t in = con_at(12, 0, 0, 0);
    dw_p9_values(&in, v);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.4167f, v[5]);
    in.clk.h = 13;
    dw_p9_values(&in, v);
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.5f, v[5]);
    const float at0 = v[5];
    in.clk.s = 59, in.clk.ms = 999;
    dw_p9_values(&in, v);
    TEST_ASSERT_EQUAL_FLOAT(at0, v[5]);
    fk_face_in_t a = con_at(13, 0, 0, 0), b = con_at(13, 0, 59, 999);
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    TEST_ASSERT_EQUAL_INT(0, differ_in(s_a, s_b, &DW_P9_METER[5]));
    in = con_at(5, 30, 0, 0);   /* before the sunrise: parked at the start; after the sunset: at the end */
    dw_p9_values(&in, v);
    TEST_ASSERT_EQUAL_FLOAT(0.f, v[5]);
    in.clk.h = 21;
    dw_p9_values(&in, v);
    TEST_ASSERT_EQUAL_FLOAT(1.f, v[5]);
    in = con_at(12, 0, 0, 0);
    in.sun_rise_m = in.sun_set_m = -1;   /* no sun (polar day or night, or no city) */
    dw_p9_values(&in, v);
    TEST_ASSERT_EQUAL_FLOAT(0.f, v[5]);
    in.sun_rise_m = 420, in.sun_set_m = -1;
    dw_p9_values(&in, v);
    TEST_ASSERT_EQUAL_FLOAT(0.f, v[5]);
    /* the sun's minutes moving redraw the solar meter alone */
    a = con_at(13, 0, 10, 0), b = a;
    b.sun_set_m = 1150;
    TEST_ASSERT_TRUE(dirty_pair(r, r->ops->draw, &a, &b, "the sunset moves") <= 80L * 80L);
}

/* the seconds dial's red hand: (s + tick) / 60 turns, tick = sin(ms / 140 pi) .25 (1 - ms / 140) for ms < 140, exact
 * from 140; reduced: s / 60 */
static void console_seconds_tick(void)
{
    const row_t *r = row_of("console");
    static const int ms[4] = {0, 70, 140, 500};
    for (int k = 0; k < 4; k++) {
        fk_face_in_t in = con_at(10, 9, 21, ms[k]);
        float v[6];
        dw_p9_values(&in, v);
        const double tick = ms[k] < 140 ? sin(ms[k] / 140.0 * 3.14159265358979) * 0.25 * (1.0 - ms[k] / 140.0) : 0.0;
        TEST_ASSERT_FLOAT_WITHIN(2e-6f, (float)((21.0 + tick) / 60.0), v[3]);
        if (ms[k] >= 140 || ms[k] == 0) TEST_ASSERT_EQUAL_FLOAT(21.f / 60.f, v[3]);
        in.reduced = true;
        dw_p9_values(&in, v);
        TEST_ASSERT_EQUAL_FLOAT(21.f / 60.f, v[3]);
    }
    /* the frames: :21.140 and :21.500 equal; :21.070 differs from them only in the seconds dial */
    fk_face_in_t a = con_at(10, 9, 21, 140), b = con_at(10, 9, 21, 500), c = con_at(10, 9, 21, 70);
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    assert_same(s_a, s_b, "the seconds hand after its tick");
    row_frame(r, r->ops->draw, &c, s_c);
    TEST_ASSERT_TRUE(differ_within(s_a, s_c, &DW_P9_METER[3], 1, "the tick") > 5);
}

/* reduced: the in-flight lamp steady at .6 (off unset), the lamps' pixels the same all through the 2 s blink */
static void console_reduced_lamp_steady(void)
{
    const row_t *r = row_of("console");
    fk_face_in_t ref = con_at(10, 9, 20, 0);
    ref.reduced = true;
    row_frame(r, r->ops->draw, &ref, s_c);
    int lit = 0, dark = 0;
    for (int t = 0; t < 2000; t += 40) {
        fk_face_in_t in = later(ref, t);
        uint16_t rgb[3];
        float on[3];
        dw_p9_lamps(&in, rgb, on);
        TEST_ASSERT_EQUAL_FLOAT(0.6f, on[2]);
        TEST_ASSERT_EQUAL_HEX16(RGB565(0xFF, 0xF4, 0xD6), rgb[2]);
        if (t % 400 == 0) {
            row_frame(r, r->ops->draw, &in, s_a);
            TEST_ASSERT_EQUAL_INT(0, differ_in(s_c, s_a, &DW_P9_LAMPS));
        }
        in.reduced = false;   /* not reduced: it blinks, on for 700 ms of each 2 s */
        dw_p9_lamps(&in, rgb, on);
        if (on[2] == 1.f) lit++;
        else if (on[2] == 0.f) dark++;
        else TEST_FAIL_MESSAGE("the in-flight lamp is neither on nor off");
    }
    TEST_ASSERT_EQUAL_INT(18, lit);   /* 700 ms in 40 ms steps from :20.000 */
    TEST_ASSERT_EQUAL_INT(32, dark);
    ref.clk.set = false;
    uint16_t rgb[3];
    float on[3];
    dw_p9_lamps(&ref, rgb, on);
    TEST_ASSERT_EQUAL_FLOAT(0.f, on[2]);
    /* reduced: no wake (the needles at their values from t = 0) */
    fk_face_in_t w = con_at(10, 9, 20, 0), s = w;
    w.reduced = s.reduced = true;
    w.wake_ms = 0;
    row_frame(r, r->ops->draw, &w, s_a);
    row_frame(r, r->ops->draw, &s, s_b);
    assert_same(s_b, s_a, "reduced: the wake's first frame is the settled one");
}

/* the wake: needle i springs to its value by springTo(t - 120 - 90 i, 1000); the frames differ from the settled one only in
 * the meters, dirty() names the meters alone, and the wake's end is the settled frame */
static void console_wake(void)
{
    const row_t *r = row_of("console");
    TEST_ASSERT_EQUAL_STRING("console", DW_FACE_CONSOLE.key);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_CONSOLE, dw_face_by_key("console"));
    TEST_ASSERT_EQUAL_UINT16(1570, DW_FACE_CONSOLE.wake_len_ms);
    TEST_ASSERT_FALSE(DW_FACE_CONSOLE.reveal);
    TEST_ASSERT_TRUE(DW_FACE_CONSOLE.ctx_bytes < 4096u);
    fk_face_in_t in = con_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &in, s_c);
    in.wake_ms = DW_FACE_CONSOLE.wake_len_ms - 1;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "#9 a ms before the wake's end vs settled");
    const omni_rect_t meters[6] = {DW_P9_METER[0], DW_P9_METER[1], DW_P9_METER[2],
                                   DW_P9_METER[3], DW_P9_METER[4], DW_P9_METER[5]};
    in.wake_ms = 0;   /* every needle on its stop */
    row_frame(r, r->ops->draw, &in, s_a);
    TEST_ASSERT_TRUE(differ_within(s_c, s_a, meters, 6, "the wake's start") > 100);
    float v[6];
    dw_p9_values(&in, v);
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_FLOAT(0.f, v[i]);
    /* needle i starts moving at 120 + 90 i ms */
    for (int i = 0; i < 6; i++) {
        if (i == 4) continue;   /* the lamps have no needle */
        in.wake_ms = (uint32_t)(120 + 90 * i);
        dw_p9_values(&in, v);
        TEST_ASSERT_EQUAL_FLOAT(0.f, v[i]);
        in.wake_ms += 60;
        dw_p9_values(&in, v);
        TEST_ASSERT_TRUE(v[i] > 0.f);
    }
    for (uint32_t t = 0; t < 1570; t += 40) {
        fk_face_in_t a = con_at(10, 9, 30, 500), b = a;
        a.wake_ms = t, b.wake_ms = t + 40 < 1570 ? t + 40 : UINT32_MAX;
        omni_rect_t rects[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
        for (int k = 0; k < n; k++) {
            bool ok = false;
            for (int j = 0; j < 6 && !ok; j++) ok = rect_in(&rects[k], &meters[j]);
            TEST_ASSERT_TRUE_MESSAGE(ok, "a wake rect outside the meters");
        }
        if (t % 200 == 0) dirty_pair(r, r->ops->draw, &a, &b, "the wake");
    }
}

/* the centre: the day, the time and the date line change only inside DW_P9_CENTRE (and the minute's meters); 12 / 24 h;
 * unset: --:-- and NO TIME SIGNAL */
static void console_centre(void)
{
    const row_t *r = row_of("console");
    fk_face_in_t a = con_at(10, 9, 30, 500), b = con_at(10, 10, 30, 500);
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    const omni_rect_t minute[4] = {DW_P9_CENTRE, DW_P9_METER[0], DW_P9_METER[1], DW_P9_METER[5]};
    TEST_ASSERT_TRUE(differ_within(s_a, s_b, minute, 4, "the minute") > 0);
    TEST_ASSERT_TRUE_MESSAGE(differ_in(s_a, s_b, &DW_P9_CENTRE) > 100, "the centre's time does not change");
    b = a, b.h24 = true, b.clk.h = 22;   /* 10:09 PM vs 22:09: the time and the date line's PM */
    a.clk.h = 22;
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    TEST_ASSERT_TRUE(differ_within(s_a, s_b, &DW_P9_CENTRE, 1, "12 / 24 h") > 100);
    b = a, b.clk.wday = 3, b.clk.mday = 4;   /* another day and date */
    row_frame(r, r->ops->draw, &b, s_b);
    TEST_ASSERT_TRUE(differ_within(s_a, s_b, &DW_P9_CENTRE, 1, "the day and date") > 100);
    b = a, b.clk.set = false;
    row_frame(r, r->ops->draw, &b, s_b);
    TEST_ASSERT_TRUE_MESSAGE(differ_in(s_a, s_b, &DW_P9_CENTRE) > 300, "unset: the centre is the same");
    /* every ink of the centre lies inside it: the frame against the face without the clock's text */
    omni_rect_t rr[1] = {DW_P9_CENTRE};
    a = con_at(23, 58, 30, 500), a.clk.wday = 3, a.clk.mday = 28, a.clk.mon = 8;   /* WEDNESDAY, the widest */
    b = a, b.clk.m = 59;
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    const omni_rect_t wide[4] = {rr[0], DW_P9_METER[0], DW_P9_METER[1], DW_P9_METER[5]};
    differ_within(s_a, s_b, wide, 4, "Wednesday 11:58 PM vs 11:59 PM");
}

/* stale: the stale mark (DW_P9_STALE) and the amber lamp (DW_P9_LAMPS) only; in the glance, the mark's dim lamp only */
static void console_stale(void)
{
    const row_t *r = row_of("console");
    for (int g = 0; g < 2; g++) {
        const draw_fn fn = g ? r->ops->glance : r->ops->draw;
        static const int8_t orb[3][2] = {{0, 0}, {4, -4}, {-4, 4}};
        for (int o = 0; o < (g ? 3 : 1); o++) {
            fk_face_in_t in = con_at(10, 9, 30, 900);
            in.odx = orb[o][0], in.ody = orb[o][1];
            row_frame(r, fn, &in, s_a);
            in.clk.stale = true;
            row_frame(r, fn, &in, s_b);
            const omni_rect_t rr[2] = {DW_P9_STALE, DW_P9_LAMPS};
            differ_within(s_a, s_b, rr, g ? 1 : 2, g ? "stale glance" : "stale");
            TEST_ASSERT_TRUE_MESSAGE(differ_in(s_a, s_b, &DW_P9_STALE) > 20, "no stale mark drawn");
        }
    }
}

/* dw_p9_needle and dw_dome_lamp: band-exact, inside their boxes */
static void p9_shapes_band_exact(void)
{
    static const float val[] = {-0.1f, 0.f, 0.3f, 0.5f, 1.f, 1.1f};
    static const int meter[4] = {0, 1, 2, 5};
    for (int i = 0; i < 4; i++)
        for (unsigned k = 0; k < sizeof val / sizeof val[0]; k++) {
            for (int p = 0; p < W * W; p++) s_a[p] = s_b[p] = RGB565(40, 44, 36);
            dw_p9_needle(s_a, W, NULL, meter[i], val[k], RGB565(0xF2, 0xEA, 0xD8));
            for (int y0 = 0; y0 < W; y0 += 8) {
                const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
                dw_p9_needle(s_b + (size_t)y0 * W, W, &c, meter[i], val[k], RGB565(0xF2, 0xEA, 0xD8));
            }
            assert_same(s_a, s_b, "a meter needle in 8-row clips");
            for (int p = 0; p < W * W; p++) s_c[p] = RGB565(40, 44, 36);
            TEST_ASSERT_TRUE(differ_within(s_c, s_a, &DW_P9_METER[meter[i]], 1, "a needle") > 100);
        }
    for (int on = 0; on < 2; on++) {
        memset(s_a, 0, sizeof s_a);
        memset(s_b, 0, sizeof s_b);
        dw_dome_lamp(s_a, W, NULL, 100.3f, 200.7f, 5.f, RGB565(0x5F, 0xCF, 0x6A), on ? 1.f : 0.f);
        for (int y0 = 0; y0 < W; y0 += 8) {
            const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
            dw_dome_lamp(s_b + (size_t)y0 * W, W, &c, 100.3f, 200.7f, 5.f, RGB565(0x5F, 0xCF, 0x6A), on ? 1.f : 0.f);
        }
        assert_same(s_a, s_b, "a dome lamp in 8-row clips");
        memset(s_c, 0, sizeof s_c);
        const omni_rect_t box = {84, 184, 117, 217};   /* 3 r about the lamp: its spill */
        TEST_ASSERT_TRUE(differ_within(s_c, s_a, &box, 1, "a dome lamp") > 60);
    }
}

/* dw_sun_today (Task 9 review I1): the solar day picked by the local date, the minutes as wall-clock time */
static int64_t local_unix(const char *tz, int y, int mo, int d, int h, int mi)
{
    setenv("TZ", tz, 1);
    tzset();
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900, tm.tm_mon = mo - 1, tm.tm_mday = d, tm.tm_hour = h, tm.tm_min = mi, tm.tm_isdst = -1;
    return (int64_t)mktime(&tm);
}

static void sun_case(const char *what, const char *tz, float lat, float lon, int y, int mo, int d, int h, int r0,
                     int r1, int s0, int s1)
{
    const int64_t now = local_unix(tz, y, mo, d, h, 0);
    int16_t r = 0, st = 0;
    const bool ok = dw_sun_today(now, lat, lon, &r, &st);
    char m[160];
    snprintf(m, sizeof m, "%s %04d-%02d-%02d %02d:00: rise %d set %d (want %d-%d, %d-%d)", what, y, mo, d, h, r, st, r0,
             r1, s0, s1);
    if (r0 < 0) {
        TEST_ASSERT_FALSE_MESSAGE(ok, m);
        TEST_ASSERT_TRUE_MESSAGE(r == -1 && st == -1, m);
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(ok && r >= r0 && r <= r1 && st >= s0 && st <= s1, m);
}

static void sun_today_local_date(void)
{
    const char *was = getenv("TZ");
    char keep[64] = "";
    if (was) snprintf(keep, sizeof keep, "%s", was);
    /* +13 zones more than 12 h ahead of their solar time: the solar day is the next UTC date's (cities.c) */
    sun_case("Apia", "<+13>-13", -13.83f, -171.73f, 2026, 11, 2, 9, 330, 390, 1090, 1150);
    sun_case("Tongatapu", "<+13>-13", -21.13f, -175.20f, 2026, 11, 2, 9, 330, 390, 1110, 1170);
    sun_case("Apia", "<+13>-13", -13.83f, -171.73f, 2026, 6, 21, 23, 380, 440, 1040, 1100);
    /* New York on both 2026 DST days, before and after the change: the same wall-clock minutes */
    for (int h = 1; h <= 4; h += 3) {
        sun_case("New York (spring)", "EST5EDT,M3.2.0,M11.1.0", 40.71f, -74.01f, 2026, 3, 8, h, 420, 445, 1130, 1150);
        sun_case("New York (fall)", "EST5EDT,M3.2.0,M11.1.0", 40.71f, -74.01f, 2026, 11, 1, h, 380, 400, 1000, 1020);
    }
    sun_case("London", "GMT0BST,M3.5.0/1,M10.5.0", 51.51f, -0.13f, 2026, 6, 21, 12, 270, 295, 1270, 1290);
    sun_case("London", "GMT0BST,M3.5.0/1,M10.5.0", 51.51f, -0.13f, 2026, 12, 21, 12, 475, 490, 950, 965);
    /* polar day and night */
    sun_case("Longyearbyen", "CET-1CEST,M3.5.0,M10.5.0/3", 78.22f, 15.65f, 2026, 6, 21, 12, -1, -1, -1, -1);
    sun_case("Longyearbyen", "CET-1CEST,M3.5.0,M10.5.0/3", 78.22f, 15.65f, 2026, 12, 21, 12, -1, -1, -1, -1);
    if (was) setenv("TZ", keep, 1);
    else unsetenv("TZ");
    tzset();
}

static void test_crc32_is_zlibs(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, fk_crc32(0, "123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, fk_crc32(fk_crc32(0, "1234", 4), "56789", 5));
    TEST_ASSERT_EQUAL_HEX32(0, fk_crc32(0, "", 0));
}

/* ---- the hands */
static void test_hand_shard_band_exact(void)
{
    static const float ang[] = {0.f, 0.3f, 1.5707963f, 2.2f, 3.14159265f, 4.0f, 5.5f, 6.1f};
    for (unsigned k = 0; k < sizeof ang / sizeof ang[0]; k++)
        for (int hour = 0; hour < 2; hour++)
            for (int g = 0; g < 3; g++) {
                const float grow = g == 0 ? 1.f : g == 1 ? 0.35f : 1.12f;
                memset(s_a, 0, sizeof s_a);
                dw_hand_shard(s_a, W, NULL, ang[k], hour != 0, grow, 255);
                memset(s_b, 0, sizeof s_b);
                for (int y0 = 0; y0 < W; y0 += 8) {
                    const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
                    dw_hand_shard(s_b + (size_t)y0 * W, W, &c, ang[k], hour != 0, grow, 255);
                }
                assert_same(s_a, s_b, "hand in 8-row clips");
                /* inside its box, at every grow it can have */
                omni_rect_t bb;
                dw_hand_shard_bbox(ang[k], hour != 0, &bb);
                long ink = 0;
                for (int y = 0; y < W; y++)
                    for (int x = 0; x < W; x++)
                        if (s_a[y * W + x]) {
                            TEST_ASSERT_TRUE_MESSAGE(inside(&bb, x, y), "hand ink outside its bbox");
                            ink++;
                        }
                TEST_ASSERT_TRUE(ink > 100);
            }
    /* the outline hand: band-exact and inside its box */
    for (unsigned k = 0; k < sizeof ang / sizeof ang[0]; k++) {
        memset(s_a, 0, sizeof s_a);
        dw_hand_outline(s_a, W, NULL, ang[k], false, 237.f, 229.f, 0x6a43, 255);
        memset(s_b, 0, sizeof s_b);
        for (int y0 = 0; y0 < W; y0 += 8) {
            const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
            dw_hand_outline(s_b + (size_t)y0 * W, W, &c, ang[k], false, 237.f, 229.f, 0x6a43, 255);
        }
        assert_same(s_a, s_b, "outline hand in 8-row clips");
        omni_rect_t bb;
        dw_hand_outline_bbox(ang[k], false, 237.f, 229.f, &bb);
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
                if (s_a[y * W + x]) TEST_ASSERT_TRUE_MESSAGE(inside(&bb, x, y), "outline ink outside its bbox");
    }
}

/* Task 12: fk_glow_add's whole-pixel-centre path (mirrored pixels share one sqrtf) and the table path
 * (fk_glow_add_lut) against the glow's per-pixel formula as it was written (here, by hand), on noise, for several
 * radii, centres (on and off the screen), clips and gains. */
static void glow_ref(uint16_t *fb, int stride, omni_rect_t clip, float cx, float cy, float r, uint16_t rgb, uint8_t a8)
{
    const int x0 = (int)floorf(cx - r) - 1, y0 = (int)floorf(cy - r) - 1;
    const int x1 = (int)ceilf(cx + r) + 2, y1 = (int)ceilf(cy + r) + 2;
    for (int y = y0 > clip.y0 ? y0 : clip.y0; y < y1 && y < clip.y1; y++)
        for (int x = x0 > clip.x0 ? x0 : clip.x0; x < x1 && x < clip.x1; x++) {
            const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            const float f = sqrtf(dx * dx + dy * dy) / r;
            if (f >= 1.f) continue;
            const float al = f < 0.25f ? 1.f - 0.55f * (f / 0.25f)
                           : f < 0.6f  ? 0.45f - 0.33f * ((f - 0.25f) / 0.35f)
                                       : 0.12f * ((1.f - f) / 0.4f);
            const int ap = (omni_amul((int)(al * 255.f + 0.5f)) * omni_amul(a8)) >> 8;
            uint16_t *d = &fb[(y - clip.y0) * stride + (x - clip.x0)];
            if (ap == 0) continue;
            const int R = omni_ex_r(*d) + ((omni_ex_r(rgb) * ap) >> 8), G = omni_ex_g(*d) + ((omni_ex_g(rgb) * ap) >> 8),
                      B = omni_ex_b(*d) + ((omni_ex_b(rgb) * ap) >> 8);
            *d = omni_pack565(R > 255 ? 255 : R, G > 255 ? 255 : G, B > 255 ? 255 : B);
        }
}

static void test_glow_sym_and_lut(void)
{
    static uint8_t lut[FK_GLOW_LUT_N(120)];
    static const int radii[] = { 120, 22, 12, 9, 1, 37 };
    static const int ctr[][2] = { { 233, 233 }, { 181, 115 }, { 3, 460 }, { -10, 240 }, { 470, -2 } };
    static const uint8_t gains[] = { 255, 89, 1, 0 };
    const omni_rect_t clips[3] = { { 0, 0, W, W }, { 100, 37, 333, 53 }, { 220, 0, 246, W } };
    const uint16_t rgb = 0xFD4A;
    int checked = 0;
    for (unsigned ri = 0; ri < sizeof radii / sizeof radii[0]; ri++) {
        const int r = radii[ri];
        fk_glow_lut_build(lut, r);
        for (unsigned c = 0; c < sizeof ctr / sizeof ctr[0]; c++)
            for (unsigned gi = 0; gi < sizeof gains / sizeof gains[0]; gi++)
                for (unsigned k = 0; k < 3; k++) {
                    const omni_rect_t cl = clips[k];
                    const int cw = cl.x1 - cl.x0, n = cw * (cl.y1 - cl.y0);
                    for (int i = 0; i < n; i++) s_a[i] = s_b[i] = s_c[i] = (uint16_t)(i * 2654435761u >> 7);
                    glow_ref(s_a, cw, cl, (float)ctr[c][0], (float)ctr[c][1], (float)r, rgb, gains[gi]);
                    fk_glow_add(s_b, cw, &cl, (float)ctr[c][0], (float)ctr[c][1], (float)r, rgb, gains[gi], 0, 0);
                    fk_glow_add_lut(s_c, cw, &cl, ctr[c][0], ctr[c][1], r, lut, rgb, gains[gi]);
                    TEST_ASSERT_EQUAL_HEX16_ARRAY(s_a, s_b, n);
                    TEST_ASSERT_EQUAL_HEX16_ARRAY(s_a, s_c, n);
                    checked++;
                }
    }
    TEST_ASSERT_EQUAL_INT(6 * 5 * 4 * 3, checked);
}

/* R148 (c), wave 2 Task 7 fix round: a radius >= FK_GLOW_COARSE_R has a coarse table (entry k at N = 64 k, a pixel
 * interpolating between N >> 6 and N >> 6 + 1): every N's alpha is within 4 of the exact one (the sqrt's bend
 * near the centre), a glow drawn from it is
 * within one 565 step of the per-pixel formula (two of green's finer steps), and it is band-exact */
static void test_glow_coarse_large_r(void)
{
    static uint8_t lut[FK_GLOW_LUT_MAX];
    static const int radii[] = {128, 233, 255};
    TEST_ASSERT_EQUAL_UINT(3395, FK_GLOW_LUT_N(233));
    TEST_ASSERT_EQUAL_UINT(7200, FK_GLOW_LUT_N(120));   /* the fine sizes are as they were */
    TEST_ASSERT_TRUE(FK_GLOW_LUT_N(255) <= FK_GLOW_LUT_MAX);
    for (unsigned ri = 0; ri < sizeof radii / sizeof radii[0]; ri++) {
        const int r = radii[ri];
        fk_glow_lut_build(lut, r);
        int worst = 0;
        for (int32_t N = 2; N < 4 * r * r; N += 8) {   /* N = 2 mod 8: every pixel's (2 dx)^2 + (2 dy)^2 */
            const float f = sqrtf((float)N * 0.25f) / (float)r;
            const float al = f < 0.25f ? 1.f - 0.55f * (f / 0.25f)
                           : f < 0.6f  ? 0.45f - 0.33f * ((f - 0.25f) / 0.35f)
                                       : 0.12f * ((1.f - f) / 0.4f);
            const int exact = f >= 1.f ? 0 : (int)(al * 255.f + 0.5f);
            const int32_t k = N >> 6, fr = N & 63;
            TEST_ASSERT_TRUE((size_t)k + 1 < FK_GLOW_LUT_N(r));
            const int got = (int)((lut[k] * (64 - fr) + lut[k + 1] * fr + 32) >> 6);
            const int e = got > exact ? got - exact : exact - got;
            if (e > worst) worst = e;
        }
        char m[64];
        snprintf(m, sizeof m, "r %d: the coarse alpha is %d levels off", r, worst);
        printf("glow r %d: the coarse table's alpha is at most %d levels off the exact one\n", r, worst);
        TEST_ASSERT_TRUE_MESSAGE(worst <= 4, m);
        static const int ctr[][2] = {{233, 233}, {100, 400}, {470, -2}};
        static const uint8_t gains[] = {255, 98, 1};
        for (unsigned c = 0; c < 3; c++)
            for (unsigned gi = 0; gi < 3; gi++) {
                for (int i = 0; i < W * W; i++) s_a[i] = s_c[i] = (uint16_t)(i * 2654435761u >> 7);
                const omni_rect_t all = {0, 0, W, W};
                glow_ref(s_a, W, all, (float)ctr[c][0], (float)ctr[c][1], (float)r, 0xFD4A, gains[gi]);
                fk_glow_add_lut(s_c, W, &all, ctr[c][0], ctr[c][1], r, lut, 0xFD4A, gains[gi]);
                for (int i = 0; i < W * W; i++) {
                    const int dr = abs((s_a[i] >> 11) - (s_c[i] >> 11));
                    const int dg = abs(((s_a[i] >> 5) & 63) - ((s_c[i] >> 5) & 63));
                    const int db = abs((s_a[i] & 31) - (s_c[i] & 31));
                    if (dr > 1 || dg > 2 || db > 1) {   /* one step of red and blue, two of green (its finer steps) */
                        snprintf(m, sizeof m, "r %d: pixel %d off the formula by more than a 565 step", r, i);
                        TEST_FAIL_MESSAGE(m);
                    }
                }
                for (int i = 0; i < W * W; i++) s_b[i] = (uint16_t)(i * 2654435761u >> 7);
                for (int y0 = 0; y0 < W; y0 += 8) {   /* band-exact */
                    const omni_rect_t cl = {37, (int16_t)y0, 401, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
                    uint16_t band[8 * 364];
                    for (int y = cl.y0; y < cl.y1; y++)
                        for (int x = cl.x0; x < cl.x1; x++) band[(y - cl.y0) * 364 + (x - cl.x0)] = s_b[y * W + x];
                    fk_glow_add_lut(band, 364, &cl, ctr[c][0], ctr[c][1], r, lut, 0xFD4A, gains[gi]);
                    for (int y = cl.y0; y < cl.y1; y++)
                        for (int x = cl.x0; x < cl.x1; x++)
                            TEST_ASSERT_EQUAL_HEX16(s_c[y * W + x], band[(y - cl.y0) * 364 + (x - cl.x0)]);
                }
            }
    }
}

static void test_hand_grow_zero_draws_nothing(void)
{
    for (int hour = 0; hour < 2; hour++)
        for (int g = 0; g < 2; g++) {
            memset(s_a, 0x3C, sizeof s_a);
            memcpy(s_b, s_a, sizeof s_a);
            dw_hand_shard(s_a, W, NULL, 1.0f, hour != 0, g ? -0.5f : 0.f, 255);
            assert_same(s_b, s_a, "grow <= 0 drew");
        }
}

static void test_sincos_table(void)
{
    float s, c, worst = 0;
    for (int i = 0; i <= 200000; i++) {   /* the bound, measured: printed for faces.h */
        const float a = (float)i * (6.2831853f / 200000.f);
        fk_sincos(a, &s, &c);
        const float e1 = fabsf(s - (float)sin((double)a)), e2 = fabsf(c - (float)cos((double)a));
        if (e1 > worst) worst = e1;
        if (e2 > worst) worst = e2;
    }
    printf("fk_sincos: worst error %.3g over a turn\n", (double)worst);
    TEST_ASSERT_TRUE(worst < 2e-6f);
    for (int i = -2000; i <= 2000; i++) {
        const float a = (float)i * 0.00731f;
        fk_sincos(a, &s, &c);
        TEST_ASSERT_FLOAT_WITHIN(2e-6f, sinf(a), s);   /* faces.h's bound */
        TEST_ASSERT_FLOAT_WITHIN(2e-6f, cosf(a), c);
    }
}

/* ---- the clock */
static void test_clock_from(void)
{
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
    fk_clock_t c;
    fk_clock_from(1793545770, 512, false, &c);   /* 2026-11-01 15:09:30 UTC: 10:09:30 EST, after the change */
    TEST_ASSERT_TRUE(c.set);
    TEST_ASSERT_FALSE(c.stale);
    TEST_ASSERT_EQUAL_UINT8(10, c.h);
    TEST_ASSERT_EQUAL_UINT8(9, c.m);
    TEST_ASSERT_EQUAL_UINT8(30, c.s);
    TEST_ASSERT_EQUAL_UINT16(512, c.ms);
    TEST_ASSERT_EQUAL_UINT8(1, c.mday);
    TEST_ASSERT_EQUAL_UINT8(10, c.mon);
    TEST_ASSERT_EQUAL_UINT8(0, c.wday);
    fk_clock_from(1793545770, 0, true, &c);
    TEST_ASSERT_TRUE(c.stale);
    fk_clock_from(1000, 300, true, &c);   /* 1970: not set; stale dropped; nothing else either */
    fk_clock_t z;
    memset(&z, 0, sizeof z);
    TEST_ASSERT_EQUAL_MEMORY(&z, &c, sizeof c);
}

static void test_stills_pinned(void)
{
    for (unsigned i = 0; i < DW_STILL_N; i++) {
        dw_still_t a, b;
        dw_still(i, &a);
        dw_still(i, &b);
        TEST_ASSERT_EQUAL_MEMORY(&a.in, &b.in, sizeof a.in);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, a.in.wake_ms);   /* settled: never a frame in motion */
        TEST_ASSERT_NOT_NULL(a.what);
    }
    for (unsigned i = 0; i < DW_STILL_N; i++) {
        dw_still_t a;
        dw_still(i, &a);
        TEST_ASSERT_FALSE(a.in.pulsing);   /* no lamp pulse in a still */
    }
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_CRYSTAL, dw_face_by_key("crystal"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_ROUNDEL, dw_face_by_key("roundel"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_CALLBOX, dw_face_by_key("callbox"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_ROTOR, dw_face_by_key("rotor"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_CORAL, dw_face_by_key("coral"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_CIRCLES, dw_face_by_key("circles"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_LEARN, dw_face_by_key("learn"));
    TEST_ASSERT_EQUAL_PTR(&DW_KEY, dw_face_by_key("key"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_ROOM, dw_face_by_key("room"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_AMBER, dw_face_by_key("amber"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_FOB, dw_face_by_key("fob"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_CONSOLE, dw_face_by_key("console"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_WALL, dw_face_by_key("wall"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_DOORWAY, dw_face_by_key("doorway"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_BROADCAST, dw_face_by_key("broadcast"));
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_VORTEX, dw_face_by_key("vortex"));
    for (unsigned i = 0; i < DW_STILL_N; i++) {   /* wave 2 Task 14: palette 0, the tunnel at 0 in every still */
        dw_still_t a;
        dw_still(i, &a);
        TEST_ASSERT_EQUAL_UINT8(0, a.in.variant);
        TEST_ASSERT_EQUAL_UINT32(0, a.in.mono_ms);
    }
    TEST_ASSERT_EQUAL_UINT(DW_STILL_N, dw_still_count(&DW_FACE_CORAL));
    TEST_ASSERT_EQUAL_UINT(3 * DW_STILL_N, dw_still_count(&DW_KEY));
    TEST_ASSERT_NULL(dw_face_by_key("nonesuch"));
}

/* ---- wave 1's lessons on every row (dw wave 2 Task 2, R147 PR10) */
/* in moved by ms, across midnight too: a set clock's date steps a day (a 28-day February: the faces read no year) */
static fk_face_in_t shifted(fk_face_in_t in, long ms)
{
    static const uint8_t DIM[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const long day = 86400000L;
    long t = ((in.clk.h * 60L + in.clk.m) * 60L + in.clk.s) * 1000L + in.clk.ms + ms;
    const int days = t < 0 ? -1 : t >= day ? 1 : 0;
    t -= days * day;
    in.clk.ms = (uint16_t)(t % 1000), t /= 1000;
    in.clk.s = (uint8_t)(t % 60), t /= 60;
    in.clk.m = (uint8_t)(t % 60), t /= 60;
    in.clk.h = (uint8_t)t;
    if (days && in.clk.set && in.clk.mon < 12) {
        in.clk.wday = (uint8_t)((in.clk.wday + 7 + days) % 7);
        if (days > 0 && ++in.clk.mday > DIM[in.clk.mon]) in.clk.mday = 1, in.clk.mon = (uint8_t)((in.clk.mon + 1) % 12);
        if (days < 0 && --in.clk.mday < 1) in.clk.mon = (uint8_t)((in.clk.mon + 11) % 12), in.clk.mday = DIM[in.clk.mon];
    }
    return in;
}

/* Lesson 1: from each still instant and from 00:00:00.000 and 12:00:59.960, every differing pixel lies inside
 * dirty(a, b) for a forward step, skips, back-steps, the hour and midnight; awake and in the glance */
static void dirty_jumps(const char *key)
{
    const row_t *r = row_of(key);
    static const long step[] = {40, 1000, 7000, -1000, -3000, 61000, 3600000};
    for (unsigned b = 0; b < DW_STILL_N + 2; b++) {
        fk_face_in_t base;
        if (b < DW_STILL_N) {
            dw_still_t st;
            dw_still(b, &st);
            base = st.in;
        } else {
            base = b == DW_STILL_N ? in_at(0, 0, 0, 0) : in_at(12, 0, 59, 960);
        }
        for (int g = 0; g < 2; g++) {
            base.glance = g != 0;
            const draw_fn fn = g ? r->ops->glance : r->ops->draw;
            char what[128];
            for (unsigned k = 0; k < sizeof step / sizeof step[0]; k++) {
                const fk_face_in_t to = shifted(base, step[k]);
                snprintf(what, sizeof what, "%s p%u %s from %02u:%02u:%02u.%03u%s by %+ld ms", key, r->page,
                         g ? "glance" : "draw", base.clk.h, base.clk.m, base.clk.s, base.clk.ms,
                         base.clk.set ? "" : " (unset)", step[k]);
                dirty_pair(r, fn, &base, &to, what);
            }
            fk_face_in_t a = base;   /* hh:59:59.900 -> hh+1:00:00.000 */
            a.clk.m = 59, a.clk.s = 59, a.clk.ms = 900;
            fk_face_in_t to = shifted(a, 100);
            snprintf(what, sizeof what, "%s p%u %s %02u:59:59.900 -> the hour", key, r->page, g ? "glance" : "draw",
                     a.clk.h);
            dirty_pair(r, fn, &a, &to, what);
            a.clk.h = 23, a.clk.ms = 0;   /* 23:59:59 -> 00:00:00 on the next date */
            to = shifted(a, 1000);
            snprintf(what, sizeof what, "%s p%u %s 23:59:59 -> 00:00:00 the next day", key, r->page,
                     g ? "glance" : "draw");
            dirty_pair(r, fn, &a, &to, what);
        }
    }
}

/* Lesson 2: under reduced motion nothing ambient moves within a second: at each settled still instant (moved back so
 * that +300 ms stays in its second) the frames at +40 ms and +300 ms are the instant's */
static void reduced_still(const char *key)
{
    const row_t *r = row_of(key);
    for (unsigned i = 0; i < DW_STILL_N; i++) {
        dw_still_t st;
        dw_still(i, &st);
        fk_face_in_t a = st.in;
        a.reduced = true;
        if (a.clk.ms > 699) a.clk.ms = 699;
        const draw_fn fn = st.glance ? r->ops->glance : r->ops->draw;
        row_frame(r, fn, &a, s_c);
        static const int plus[2] = {40, 300};
        for (int k = 0; k < 2; k++) {
            fk_face_in_t b = a;
            b.clk.ms = (uint16_t)(a.clk.ms + plus[k]);
            row_frame(r, fn, &b, s_a);
            char what[128];
            snprintf(what, sizeof what, "%s p%u reduced, %s, +%d ms", key, r->page, st.what, plus[k]);
            assert_same(s_c, s_a, what);
        }
    }
}

/* DW_GLANCE_SWEEP_STEP: the day sweep's step in minutes (1 when unset; the sanitizer configuration sets it) */
static int sweep_step(void)
{
    const char *e = getenv("DW_GLANCE_SWEEP_STEP");
    const int n = e ? atoi(e) : 1;
    return n >= 1 && n <= 60 ? n : 1;
}

/* the glance at in, at each of the 9 orbit positions: lit < 5.00 % and no channel above 110 */
static void glance_orbits(const row_t *r, fk_face_in_t in, const char *what)
{
    for (int o = 0; o < 9; o++) {
        in.odx = ORB9[o][0], in.ody = ORB9[o][1];
        row_frame(r, r->ops->glance, &in, s_a);
        float pct;
        uint8_t maxc;
        fk_test_lit(s_a, &pct, &maxc);
        if (pct >= 5.f || maxc > 110) {
            char m[160];
            snprintf(m, sizeof m, "%s p%u's glance %s, orbit (%d, %d): %.2f %% lit, max channel %u (< 5 %%, <= 110)",
                     r->ops->key, r->page, what, ORB9[o][0], ORB9[o][1], (double)pct, maxc);
            TEST_FAIL_MESSAGE(m);
        }
    }
}

/* Lesson 4: every minute of the day (at :30.5, orbit (0, 0)) finds the glance's worst lit %; at that minute and at
 * 23:59 every orbit position stays under the bars, and so does the unset glance */
static void glance_day(const char *key)
{
    const row_t *r = row_of(key);
    const int step = sweep_step();
    float worst = -1.f;
    int worst_t = 0, n = 0;
    for (int t = 0; t < 1440; t++) {
        if (t % step && t % 60 != 59) continue;
        fk_face_in_t in;
        fk_test_in_at(&in, t / 60, t % 60, 30, 500, false, true);
        const float pct = glance_pct(r, &in);
        if (pct > worst) worst = pct, worst_t = t;
        n++;
    }
    char what[32];
    fk_face_in_t in;
    fk_test_in_at(&in, worst_t / 60, worst_t % 60, 30, 500, false, true);
    snprintf(what, sizeof what, "at %02d:%02d (the worst)", worst_t / 60, worst_t % 60);
    glance_orbits(r, in, what);
    fk_test_in_at(&in, 23, 59, 30, 500, false, true);
    glance_orbits(r, in, "at 23:59");
    in.clk.set = false;
    glance_orbits(r, in, "unset");
    printf("%s p%u: the glance's worst minute %02d:%02d lights %.2f %% (%d minutes, step %d)\n", key, r->page,
           worst_t / 60, worst_t % 60, (double)worst, n, step);
}

/* Lesson 6: no wave-2 face (a DW_FACES entry after DW_KEY) draws a numeral: dw_glyph_draw (and its _w form) is never
 * reached in any of its stills, awake, through a wake or a doors' tail, reduced, at every second of a minute */
static unsigned long glyph_calls_drawing(const row_t *r)
{
    const unsigned long was = dw_test_glyph_calls;
    for (unsigned i = 0; i < DW_STILL_N; i++) {
        dw_still_t st;
        dw_still(i, &st);
        row_frame(r, st.glance ? r->ops->glance : r->ops->draw, &st.in, s_a);
        st.in.reduced = true;
        row_frame(r, st.glance ? r->ops->glance : r->ops->draw, &st.in, s_a);
    }
    fk_face_in_t in = in_at(21, 7, 0, 0);
    for (int s = 0; s < 60; s += 7) {
        in.clk.s = (uint8_t)s;
        row_frame(r, r->ops->draw, &in, s_a);
    }
    for (uint32_t w = 0; w <= 3000; w += 250) {
        in.wake_ms = w;
        row_frame(r, r->ops->draw, &in, s_a);
    }
    return dw_test_glyph_calls - was;
}

static void test_faces_no_numerals_wave2(void)
{
    unsigned key_at = DW_FACE_COUNT;
    for (unsigned i = 0; i < DW_FACE_COUNT; i++)
        if (DW_FACES[i] == &DW_KEY) key_at = i;
    TEST_ASSERT_TRUE_MESSAGE(key_at < DW_FACE_COUNT, "DW_KEY is not in DW_FACES");
    /* the counter counts: a numeral face reaches it */
    TEST_ASSERT_TRUE_MESSAGE(glyph_calls_drawing(row_of("circles")) > 0, "the glyph counter does not count");
    unsigned checked = 0;
    for (unsigned i = 0; i < s_nrows; i++) {
        unsigned at = DW_FACE_COUNT;
        for (unsigned f = 0; f < DW_FACE_COUNT; f++)
            if (DW_FACES[f] == s_rows[i].ops) at = f;
        if (at <= key_at) continue;
        char m[96];
        snprintf(m, sizeof m, "wave-2 face %s (page %u) draws a numeral", s_rows[i].ops->key, s_rows[i].page);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, glyph_calls_drawing(&s_rows[i]), m);
        checked++;
    }
    printf("no numerals: %u wave-2 rows checked (the DW_FACES entries after DW_KEY)\n", checked);
}

/* ---- #6: Roundel wall (wave 2 Task 10) */
/* faces-classic.js:551-560 wallCells, in double: the staggered grid (pitch 46, columns 46 x .866 apart, odd columns
 * down half a pitch) kept where 104 + 17.5 + 2 < d < 233 + 17.5 x .3, sorted by the angle from 12 clockwise, then by
 * the distance */
typedef struct {
    double a, d, x, y;
} wcell_t;

static int wcell_cmp(const void *p, const void *q)
{
    const wcell_t *a = p, *b = q;
    if (fabs(a->a - b->a) > 1e-9) return a->a < b->a ? -1 : 1;
    return a->d < b->d ? -1 : a->d > b->d ? 1 : 0;
}

static int wall_ref(wcell_t out[225])
{
    const double pitch = 46, colw = pitch * 0.866, rr = 17.5, tau = 2 * 3.14159265358979323846;
    int n = 0;
    for (int c = -7; c <= 7; c++)
        for (int r = -7; r <= 7; r++) {
            const double x = 233 + c * colw, y = 233 + r * pitch + (abs(c) % 2 ? pitch / 2 : 0);
            const double d = hypot(x - 233, y - 233);
            if (d > 104 + rr + 2 && d < 233 + rr * 0.3)
                out[n++] = (wcell_t){fmod(atan2(y - 233, x - 233) + tau / 4 + tau, tau), d, x, y};
        }
    qsort(out, (size_t)n, sizeof out[0], wcell_cmp);
    return n;
}

/* a cell's angle clockwise from 12 (double) */
static double wall_angle(float x, float y)
{
    const double tau = 2 * 3.14159265358979323846;
    return fmod(atan2((double)y - 233, (double)x - 233) + tau / 4 + tau, tau);
}

static void wall_sixty_cells(void)
{
    wcell_t ref[225];
    TEST_ASSERT_EQUAL_INT(60, wall_ref(ref));
    TEST_ASSERT_EQUAL_INT(60, DW_W6_CELLS);
    double prev_a = -1, prev_d = 0;
    for (int i = 0; i < DW_W6_CELLS; i++) {
        float x, y;
        dw_w6_cell(i, &x, &y);
        char m[96];
        snprintf(m, sizeof m, "cell %d at (%.3f, %.3f), wallCells (%.3f, %.3f)", i, (double)x, (double)y, ref[i].x,
                 ref[i].y);
        TEST_ASSERT_TRUE_MESSAGE(fabs(x - ref[i].x) < 1e-3 && fabs(y - ref[i].y) < 1e-3, m);
        const double a = wall_angle(x, y), d = hypot((double)x - 233, (double)y - 233);
        TEST_ASSERT_TRUE_MESSAGE(d > 123.5 && d < 238.25, m);   /* the ring test */
        /* clockwise: the angle never falls; on one ray, outwards (the float table's angles agree to 1e-5 on a ray) */
        TEST_ASSERT_TRUE_MESSAGE(a > prev_a + 1e-5 || (fabs(a - prev_a) <= 1e-5 && d > prev_d), m);
        prev_a = a, prev_d = d;
    }
    /* cell 0 is the first clockwise from 12: straight up, the nearest of the cells there */
    float x0, y0;
    dw_w6_cell(0, &x0, &y0);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 233.f, x0);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 95.f, y0);
    TEST_ASSERT_TRUE(wall_angle(x0, y0) < 1e-9);
    /* every box is the cell's 52 px sprite, cut to the screen, and holds its centre */
    for (int i = 0; i < DW_W6_CELLS; i++) {
        float x, y;
        dw_w6_cell(i, &x, &y);
        omni_rect_t b;
        dw_w6_box(i, &b);
        TEST_ASSERT_TRUE(b.x0 >= 0 && b.y0 >= 0 && b.x1 <= W && b.y1 <= W);
        TEST_ASSERT_TRUE(b.x1 - b.x0 <= 52 && b.y1 - b.y0 <= 52);
        TEST_ASSERT_TRUE(inside(&b, (int)x, (int)y));
    }
}

/* the level of cell m t ms into the day (the breath .72 + .28 sin(t / 2400 turns), on clamp(s / .6)) */
static double wall_breath(int h, int m, int s, int ms)
{
    const long t = ((h * 60L + m) * 60L + s) * 1000L + ms;
    const double on = (s + ms / 1000.0) / 0.6;
    return (on > 1 ? 1 : on) * (0.72 + 0.28 * sin((double)(t % 2400) / 2400.0 * 2 * 3.14159265358979323846));
}

static void wall_minute_cells(void)
{
    fk_face_in_t in = in_at(10, 37, 30, 0);
    for (int i = 0; i < DW_W6_CELLS; i++) {
        char m[64];
        snprintf(m, sizeof m, "10:37:30 cell %d", i);
        if (i < 37) TEST_ASSERT_EQUAL_UINT8_MESSAGE(209, dw_w6_level(&in, i), m);   /* .82 */
        else if (i > 37) TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, dw_w6_level(&in, i), m);
    }
    /* cell 37 breathes: the formula at every 100 ms of a 2.4 s breath, and it spans the breath */
    int lo = 255, hi = 0;
    for (int ms = 0; ms < 2400; ms += 100) {
        fk_face_in_t b = later(in, ms);
        const double want = wall_breath(b.clk.h, b.clk.m, b.clk.s, b.clk.ms) * 255;
        const int got = dw_w6_level(&b, 37);
        TEST_ASSERT_TRUE_MESSAGE(fabs(got - want) <= 1.0, "cell 37's breath");
        lo = got < lo ? got : lo, hi = got > hi ? got : hi;
    }
    TEST_ASSERT_TRUE(lo < 120 && hi > 250);
    /* it fades on over the minute's first .6 s */
    in = in_at(10, 37, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(0, dw_w6_level(&in, 37));
    TEST_ASSERT_EQUAL_UINT8(209, dw_w6_level(&in, 36));
    in = in_at(10, 37, 0, 300);
    TEST_ASSERT_TRUE(fabs(dw_w6_level(&in, 37) - wall_breath(10, 37, 0, 300) * 255) <= 1.0);
    TEST_ASSERT_EQUAL_UINT16(DW_W6_COLS[10 % 6], dw_w6_colour(&in));
    /* 11:00:00.000: every cell dark, the colour the next era's */
    in = in_at(11, 0, 0, 0);
    for (int i = 0; i < DW_W6_CELLS; i++) TEST_ASSERT_EQUAL_UINT8(0, dw_w6_level(&in, i));
    TEST_ASSERT_EQUAL_UINT16(DW_W6_COLS[11 % 6], dw_w6_colour(&in));
    /* the six colours, cream to celadon (faces-classic.js:550 WALL_COLS) */
    static const uint16_t want[6] = {RGB565(0xF2, 0xEA, 0xD8), RGB565(0xDC, 0xCB, 0xF2), RGB565(0xF4, 0xD2, 0x7E),
                                     RGB565(0xBF, 0xE2, 0xE6), RGB565(0xF2, 0xC6, 0xBE), RGB565(0xCF, 0xE6, 0xB9)};
    for (int k = 0; k < 6; k++) TEST_ASSERT_EQUAL_HEX16(want[k], DW_W6_COLS[k]);
    /* 23:59: every cell but the last lit; unset: only cell 0, cream */
    in = in_at(23, 59, 30, 0);
    for (int i = 0; i < 59; i++) TEST_ASSERT_EQUAL_UINT8(209, dw_w6_level(&in, i));
    TEST_ASSERT_TRUE(dw_w6_level(&in, 59) > 0);
    in.clk.set = false;
    for (int i = 1; i < DW_W6_CELLS; i++) TEST_ASSERT_EQUAL_UINT8(0, dw_w6_level(&in, i));
    TEST_ASSERT_EQUAL_UINT16(DW_W6_COLS[0], dw_w6_colour(&in));
    lo = 255, hi = 0;
    for (uint32_t t = 0; t < 2400; t += 100) {
        in.mono_ms = t;
        const int l = dw_w6_level(&in, 0);
        lo = l < lo ? l : lo, hi = l > hi ? l : hi;
    }
    TEST_ASSERT_TRUE(lo == 0 && hi >= 126);   /* .25 + .25 sin: dark (<= .01) at its trough, .5 at its peak */
}

/* the cells' boxes, cut to the screen */
static void wall_regions(void)
{
    const row_t *r = row_of("wall");
    TEST_ASSERT_EQUAL_UINT16(40, DW_FACE_WALL.cadence_ms);
    for (int red = 0; red < 2; red++)
        for (int ms = 0; ms < 60000; ms += 40) {   /* a whole minute at 40 ms: only cell m's box */
            fk_face_in_t a = in_at(10, 37, 0, 0);
            a = later(a, ms);
            a.reduced = red != 0;
            const fk_face_in_t b = later(a, 40);
            if (b.clk.m != a.clk.m) break;
            omni_rect_t rects[8], box;
            dw_w6_box(37, &box);
            const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
            TEST_ASSERT_TRUE(red ? n == 0 : n <= 1);
            for (int k = 0; k < n; k++) TEST_ASSERT_TRUE_MESSAGE(same_rect(&rects[k], &box), "not cell 37's box");
            if (ms % 4000 == 0) dirty_pair(r, r->ops->draw, &a, &b, "#6 at rest");
        }
    /* the minute: cells m - 1 and m, and the centre */
    fk_face_in_t a = in_at(10, 37, 59, 960);
    const fk_face_in_t b = later(a, 40);
    omni_rect_t rects[8], want[3];
    dw_w6_box(37, &want[0]);
    dw_w6_box(38, &want[1]);
    want[2] = DW_W6_CENTRE;
    /* review M2: exactly cell 37 (its breath to .82) and the centre; cell 38 is still dark at :00.000 (its fade-on) */
    (void)want[1];
    TEST_ASSERT_EQUAL_UINT8(0, dw_w6_level(&b, 38));
    const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_TRUE_MESSAGE(same_rect(&rects[0], &want[0]) && same_rect(&rects[1], &want[2]),
                             "the minute's rects are not cell 37's box and the centre");
    dirty_pair(r, r->ops->draw, &a, &b, "#6 at the minute");
}

/* the cells whose level differs between a and b */
static int wall_changed(const fk_face_in_t *a, const fk_face_in_t *b)
{
    int c = 0;
    for (int i = 0; i < DW_W6_CELLS; i++) c += dw_w6_level(a, i) != dw_w6_level(b, i);
    return c;
}

/* dirty(a, b) is the whole frame (one rect) when want_full, else exactly the changed cells' boxes (and the centre when
 * text); covered either way */
static void wall_expect(const row_t *r, const fk_face_in_t *a, const fk_face_in_t *b, bool want_full, bool text,
                        const char *what)
{
    omni_rect_t rects[8];
    const uint8_t n = r->ops->dirty(r->ctx, a, b, rects);
    char m[128];
    snprintf(m, sizeof m, "%s: %u rects, %d cells changed", what, n, wall_changed(a, b));
    if (want_full) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, n, m);
        TEST_ASSERT_TRUE_MESSAGE(same_rect(&rects[0], &(omni_rect_t){0, 0, W, W}), m);
    } else {
        TEST_ASSERT_EQUAL_INT_MESSAGE(wall_changed(a, b) + (text ? 1 : 0), n, m);
        int k = 0;
        for (int i = 0; i < DW_W6_CELLS; i++) {
            if (dw_w6_level(a, i) == dw_w6_level(b, i)) continue;
            omni_rect_t box;
            dw_w6_box(i, &box);
            TEST_ASSERT_TRUE_MESSAGE(same_rect(&rects[k++], &box), m);
        }
        if (text) TEST_ASSERT_TRUE_MESSAGE(same_rect(&rects[k], &DW_W6_CENTRE), m);
    }
    dirty_pair(r, r->ops->draw, a, b, what);
}

/* review I1: the fallback to the whole frame when more cells change than the 8 rects hold (7 with the centre) */
static void wall_dirty_fallback(void)
{
    const row_t *r = row_of("wall");
    /* the wake at m 59: each 40 ms step's changed cells, exactly as boxes while they fit (8 without text), else whole */
    static const uint32_t step[4][2] = {{0, 40}, {40, 80}, {80, 120}, {0, 80}};
    for (int k = 0; k < 4; k++) {
        fk_face_in_t a = in_at(10, 59, 30, 0), b = a;
        a.wake_ms = step[k][0], b.wake_ms = step[k][1];
        const int c = wall_changed(&a, &b);
        char what[64];
        snprintf(what, sizeof what, "#6 wake at :59, %u -> %u ms (%d cells)", step[k][0], step[k][1], c);
        TEST_ASSERT_TRUE(c > 0);
        wall_expect(r, &a, &b, c > 8, false, what);
        if (k == 3) TEST_ASSERT_TRUE_MESSAGE(c > 8, "0 -> 80 ms changes more cells than fit");
    }
    /* a jump of 20 minutes within the hour: whole */
    fk_face_in_t a = in_at(10, 10, 30, 0), b = in_at(10, 30, 30, 0);
    TEST_ASSERT_TRUE(wall_changed(&a, &b) > 7);
    wall_expect(r, &a, &b, true, true, "#6 10:10:30 -> 10:30:30");
    /* the room's edge with the text changing: 7 cells (10..16) and the centre fit in 8 rects; 8 cells do not */
    TEST_ASSERT_TRUE(dw_w6_level(&a, 10) != 209);   /* cell 10 breathes off .82 at 10:10:30.000, so it changes */
    b = in_at(10, 17, 0, 0);
    TEST_ASSERT_EQUAL_INT(7, wall_changed(&a, &b));
    wall_expect(r, &a, &b, false, true, "#6 10:10:30 -> 10:17:00 (7 cells, the room)");
    b = in_at(10, 18, 0, 0);
    TEST_ASSERT_EQUAL_INT(8, wall_changed(&a, &b));
    wall_expect(r, &a, &b, true, true, "#6 10:10:30 -> 10:18:00 (8 cells, one past the room)");
    /* without text, 8 cells fit: the wake at :59 from 0 to 40 ms is checked above */
}

/* review M1: the unset breath, cell 0 alone, on mono_ms: its level against the formula, its box as the rect, none under
 * reduced motion */
static void wall_unset_breath(void)
{
    const row_t *r = row_of("wall");
    fk_face_in_t in = in_at(10, 9, 30, 0);
    in.clk.set = false;
    for (uint32_t t = 0; t < 2400; t += 50) {
        in.mono_ms = t;
        const double l = 0.25 + 0.25 * sin((double)t / 2400.0 * 2 * 3.14159265358979323846);
        const double want = l > 0.01 ? l * 255 : 0;
        TEST_ASSERT_TRUE_MESSAGE(fabs(dw_w6_level(&in, 0) - want) <= 1.0, "the unset breath");
    }
    omni_rect_t box;
    dw_w6_box(0, &box);
    static const uint32_t pair[2][2] = {{0, 40}, {600, 640}};
    for (int red = 0; red < 2; red++)
        for (int k = 0; k < 2; k++) {
            fk_face_in_t a = in, b = in;
            a.reduced = b.reduced = red != 0;
            a.mono_ms = pair[k][0], b.mono_ms = pair[k][1];
            omni_rect_t rects[8];
            const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
            if (red) {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, n, "#6 unset reduced: no rects");
            } else {
                TEST_ASSERT_TRUE(dw_w6_level(&a, 0) != dw_w6_level(&b, 0));
                TEST_ASSERT_EQUAL_UINT8(1, n);
                TEST_ASSERT_TRUE_MESSAGE(same_rect(&rects[0], &box), "#6 unset: not cell 0's box");
            }
            dirty_pair(r, r->ops->draw, &a, &b, red ? "#6 unset reduced breath" : "#6 unset breath");
        }
}

static void wall_hour_dirty_whole(void)
{
    const row_t *r = row_of("wall");
    const fk_face_in_t a = in_at(10, 59, 59, 900), b = in_at(11, 0, 0, 0);
    omni_rect_t rects[8];
    const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
    TEST_ASSERT_TRUE(n >= 1);
    TEST_ASSERT_EQUAL_INT32((long)W * W, rects_union(rects, n));
    dirty_pair(r, r->ops->draw, &a, &b, "#6 10:59:59.900 -> 11:00:00.000");
}

/* reduced motion: cell m steady at .86 (the breath's mean) from the minute's first ms, no fade-on, no wake; the frame
 * holds the whole minute; unset, cell 0 steady at .25 */
static void wall_reduced_steady(void)
{
    const row_t *r = row_of("wall");
    static const int at[5][2] = {{0, 0}, {0, 40}, {0, 300}, {29, 300}, {59, 960}};
    for (int k = 0; k < 5; k++) {
        fk_face_in_t in = in_at(10, 37, at[k][0], at[k][1]);
        in.reduced = true;
        TEST_ASSERT_EQUAL_UINT8(219, dw_w6_level(&in, 37));
        TEST_ASSERT_EQUAL_UINT8(209, dw_w6_level(&in, 36));
        row_frame(r, r->ops->draw, &in, k ? s_a : s_c);
        if (k) assert_same(s_c, s_a, "#6 reduced within the minute");
    }
    fk_face_in_t in = in_at(10, 37, 12, 0);
    in.reduced = true;
    in.wake_ms = 0;   /* no wake: its first frame is the settled one */
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "#6 reduced: the wake's first frame");
    in = in_at(10, 37, 12, 0);
    in.clk.set = false, in.reduced = true;
    for (uint32_t t = 0; t < 2400; t += 300) {
        in.mono_ms = t;
        TEST_ASSERT_EQUAL_UINT8(64, dw_w6_level(&in, 0));
    }
}

/* the wake: shown = min(m, floor(easeOut3(t / 800) (m + 1))): none lit at its start, a lit cell never goes out, and a
 * ms before its end the frame is the settled frame */
static void wall_wake(void)
{
    const row_t *r = row_of("wall");
    TEST_ASSERT_EQUAL_STRING("wall", DW_FACE_WALL.key);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_WALL, dw_face_by_key("wall"));
    TEST_ASSERT_EQUAL_UINT16(800, DW_FACE_WALL.wake_len_ms);
    TEST_ASSERT_FALSE(DW_FACE_WALL.reveal);
    TEST_ASSERT_NULL(DW_FACE_WALL.tap);
    TEST_ASSERT_TRUE(DW_FACE_WALL.ctx_bytes < 4096u);
    fk_face_in_t in = in_at(10, 37, 30, 500);
    row_frame(r, r->ops->draw, &in, s_c);
    in.wake_ms = DW_FACE_WALL.wake_len_ms - 1;
    row_frame(r, r->ops->draw, &in, s_a);
    assert_same(s_c, s_a, "#6 a ms before the wake's end vs settled");
    in.wake_ms = 0;
    for (int i = 0; i < DW_W6_CELLS; i++) TEST_ASSERT_EQUAL_UINT8(0, dw_w6_level(&in, i));
    int was = 0;
    for (uint32_t t = 0; t <= 800; t += 40) {
        in.wake_ms = t;
        int lit = 0;
        for (int i = 0; i < DW_W6_CELLS; i++) lit += dw_w6_level(&in, i) > 0;
        const double x = t / 800.0, e = 1 - (1 - x) * (1 - x) * (1 - x);
        int shown = (int)floor(e * 38);
        shown = shown > 37 ? 37 : shown;
        TEST_ASSERT_EQUAL_INT_MESSAGE(shown + (shown >= 37), lit, "the wake's lit cells");
        TEST_ASSERT_TRUE(lit >= was);
        was = lit;
        fk_face_in_t a = in, b = in;
        b.wake_ms = t + 40 < 800 ? t + 40 : UINT32_MAX;
        if (t % 160 == 0) dirty_pair(r, r->ops->draw, &a, &b, "#6's wake");
    }
}

/* the glance's worst minute in the full-day sweep is at the hour's end, where nearly every dot is lit, and under 5 %.
 * The brief asked for a :59; measured, each hour's :58 lights about 0.06 points more than its :59, because the 60 px
 * '8' has about 100 more lit pixels than the '9' and one r 3 dot adds about 38. So: the worst minute is a :55 or
 * later, the worst :59 is within 0.15 points of it, and both are under 5 % (both printed). */
static void wall_glance_worst_59(void)
{
    const row_t *r = row_of("wall");
    const int step = sweep_step();
    float worst = -1.f, worst59 = -1.f;
    int worst_t = 0, worst59_t = 0;
    for (int t = 0; t < 1440; t++) {
        if (t % step && t % 60 < 55) continue;   /* sampled (ASan), but every hour's last five minutes */
        fk_face_in_t in;
        fk_test_in_at(&in, t / 60, t % 60, 30, 500, false, true);
        const float pct = glance_pct(r, &in);
        if (pct > worst) worst = pct, worst_t = t;
        if (t % 60 == 59 && pct > worst59) worst59 = pct, worst59_t = t;
    }
    char m[160];
    snprintf(m, sizeof m, "#6's glance: the worst minute is %02d:%02d (%.2f %%), the worst :59 %02d:%02d (%.2f %%)",
             worst_t / 60, worst_t % 60, (double)worst, worst59_t / 60, worst59_t % 60, (double)worst59);
    TEST_ASSERT_TRUE_MESSAGE(worst_t % 60 >= 55, m);
    TEST_ASSERT_TRUE_MESSAGE(worst - worst59 < 0.15f, m);
    TEST_ASSERT_TRUE_MESSAGE(worst < 5.f, m);
    printf("%s\n", m);
}

/* ---- #3: Through the Doors (wave 2 Task 11) */
static uint16_t s_d[W * W];   /* a fourth frame: the doors' composite */

/* the doors' assets this build stages (the overlay's dw_doors_m in the overlay variant): the closed doors, the glow */
static void d3_doors(fk_img_t *door, fk_img_t *glow)
{
    img_of(ASSET_DW_DOORS_M, door);
    img_of(ASSET_DW_GLOW, glow);
    TEST_ASSERT_TRUE(door->rgb && door->w == W && door->h == W && glow->a);
}

/* in at the doors' input for state st as the device's frame plan gives it (fk_rs_plan, then fk_rs_job_in: fk_view's
 * frame_plan); *settled carries the plan's "the settled frame is drawn" from frame to frame. Returns false when the plan
 * draws nothing (the interior keeps its pixels). reduced is the doors' (the cross-fade); in->reduced stays the caller's,
 * the motion setting, as dw_view keeps it: the Fade style runs the cross-fade with reduced motion off (Task 11 review
 * M3). */
static bool d3_job(const fk_cols_state_t *st, const fk_col_t *cols, uint32_t t, bool reduced, bool *settled,
                   fk_face_in_t *in, fk_rp_t *plan)
{
    fk_rs_plan(st, cols, reduced, settled, plan);
    if (plan->kind == FK_RP_NONE) return false;
    fk_rs_job_in(plan, t, reduced, &in->wake_ms, &in->reveal_live);
    return true;
}

/* the leaves' free edges for cols: the left leaf's last column + 1 and the right leaf's first column (0 and 466 none) */
static void d3_free_edges(const fk_col_t *cols, int *xl, int *xr)
{
    *xl = 0, *xr = W;
    for (int x = 0; x < W / 2; x++)
        if (cols[x].u >= 0) *xl = x + 1;
    for (int x = W - 1; x >= W / 2; x--)
        if (cols[x].u >= 0) *xr = x;
}

/* the composite: the band over interior, in the device's 8-row bands, into out */
static void d3_band(const fk_cols_state_t *st, const fk_col_t *cols, const fk_img_t *door, const fk_img_t *glow,
                    const uint16_t *interior, uint16_t *out)
{
    memset(out, 0x5A, sizeof s_a);
    for (int y0 = 0; y0 < W; y0 += 8) {
        const omni_rect_t b = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
        fk_cols_band(&DW_DOORS_IN_KEEP, st, cols, door->rgb, interior, glow->a, b, out + (size_t)y0 * W);
    }
}

/* want on the round panel, black off it (the doors' frames) */
static void assert_panel_is(const uint16_t *want, const uint16_t *got, const char *what)
{
    for (int y = 0; y < W; y++) {
        int x0, x1;
        fk_panel_row(y, &x0, &x1);
        for (int x = 0; x < W; x++) {
            const uint16_t w = x >= x0 && x < x1 ? want[y * W + x] : 0;
            if (w != got[y * W + x]) {
                char m[160];
                snprintf(m, sizeof m, "%s: pixel (%d, %d) want %04x got %04x", what, x, y, w, got[y * W + x]);
                TEST_FAIL_MESSAGE(m);
            }
        }
    }
}

/* where fk_cols_leaves writes (the kept leaves and their edges): a mask over two sentinels */
static long d3_leaf_mask(const row_t *r, uint8_t *mask)
{
    static fk_col_t cols[W];
    fk_cols_state_t ds;
    dw_doors_skip_k(&DW_DOORS_IN_KEEP, &ds);
    fk_cols_k(&DW_DOORS_IN_KEEP, ds.th_l, ds.th_r, cols);
    fk_img_t door;
    img_of(ASSET_DW_DOORS_M, &door);
    (void)r;
    long n = 0;
    for (int pass = 0; pass < 2; pass++) {
        const uint16_t sent = pass ? 0x0000 : 0xFFFF;
        for (int i = 0; i < W * W; i++) s_c[i] = sent;
        fk_cols_leaves(&DW_DOORS_IN_KEEP, cols, door.rgb, (omni_rect_t){0, 0, W, W}, s_c, W);
        for (int i = 0; i < W * W; i++) {
            if (!pass) mask[i] = s_c[i] != sent;
            else mask[i] |= s_c[i] != sent;
        }
    }
    for (int i = 0; i < W * W; i++) n += mask[i];
    return n;
}

static void doorway_registered(void)
{
    TEST_ASSERT_EQUAL_STRING("doorway", DW_FACE_DOORWAY.key);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_DOORWAY, dw_face_by_key("doorway"));
    TEST_ASSERT_EQUAL_UINT16(40, DW_FACE_DOORWAY.cadence_ms);
    TEST_ASSERT_TRUE(DW_FACE_DOORWAY.reveal);
    TEST_ASSERT_NULL(DW_FACE_DOORWAY.reveal_asset);   /* the Modern doors: the build's art set */
    TEST_ASSERT_EQUAL_PTR(&DW_DOORS_IN_KEEP, DW_FACE_DOORWAY.reveal_arg);
    TEST_ASSERT_NULL(DW_FACE_DOORWAY.tap);
    TEST_ASSERT_EQUAL_UINT16(0, DW_FACE_DOORWAY.wake_len_ms);
    /* the column glow's r 120 box at (233, 250); the stale marker centred (233, 372) */
    TEST_ASSERT_TRUE(DW_D3_COLUMN.x0 == 113 && DW_D3_COLUMN.y0 == 130 && DW_D3_COLUMN.x1 == 353 &&
                     DW_D3_COLUMN.y1 == 370);
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_D3_STALE.x0 + DW_D3_STALE.x1);
    TEST_ASSERT_EQUAL_INT(2 * 372, DW_D3_STALE.y0 + DW_D3_STALE.y1);
    /* the face is the build's: its id appended (Task 12 appends #8 after it) */
    TEST_ASSERT_EQUAL_INT(13, DW_F_DOORWAY);
    TEST_ASSERT_TRUE(DW_F_DOORWAY < DW_F__COUNT);
    TEST_ASSERT_EQUAL_UINT8(DW_F_DOORWAY, DW_DEF_CAROUSEL[12]);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_DOORWAY, DW_FACES[13]);
}

/* doors_live: the leaves' columns (x < 80, x >= 386) of the frame show no leaf (equal the frame without the leaves:
 * the only pixels that differ from the frame without doors_live are fk_cols_leaves' own, and nearly all of them do) */
static void doorway_doors_live_omits_leaves(void)
{
    const row_t *r = row_of("doorway");
    static uint8_t mask[W * W];
    const long nmask = d3_leaf_mask(r, mask);
    printf("#3's kept leaves: %ld px\n", nmask);
    TEST_ASSERT_TRUE_MESSAGE(nmask > 35000, "the kept leaves cover too little");
    for (int i = 0; i < W * W; i++)
        if (mask[i]) TEST_ASSERT_TRUE_MESSAGE(i % W < 80 || i % W >= 386, "a leaf pixel between x 80 and 386");
    static const int hms[3][3] = {{10, 9, 30}, {3, 47, 12}, {23, 59, 59}};
    for (int k = 0; k < 4; k++) {
        fk_face_in_t in = k < 3 ? in_at(hms[k][0], hms[k][1], hms[k][2], 500) : in_at(10, 9, 30, 500);
        if (k == 3) in.wake_ms = 900;   /* in the doors' tail too */
        in.reveal_live = true;
        row_frame(r, r->ops->draw, &in, s_a);
        in.reveal_live = false;
        row_frame(r, r->ops->draw, &in, s_b);
        long differ = 0;
        for (int i = 0; i < W * W; i++) {
            if (!mask[i]) {
                if (s_a[i] != s_b[i]) {
                    char m[96];
                    snprintf(m, sizeof m, "doors_live changes (%d, %d), no leaf pixel", i % W, i / W);
                    TEST_FAIL_MESSAGE(m);
                }
            } else {
                differ += s_a[i] != s_b[i];
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(differ * 10 > nmask * 9, "with doors_live the leaves still show");
        /* a doors_live frame's leaf columns are its interior: the kept leaves over it give the frame without */
        static fk_col_t cols[W];
        fk_cols_state_t ds;
        dw_doors_skip_k(&DW_DOORS_IN_KEEP, &ds);
        fk_cols_k(&DW_DOORS_IN_KEEP, ds.th_l, ds.th_r, cols);
        fk_img_t door;
        img_of(ASSET_DW_DOORS_M, &door);
        fk_cols_leaves(&DW_DOORS_IN_KEEP, cols, door.rgb, (omni_rect_t){0, 0, W, W}, s_a, W);
        assert_same(s_b, s_a, "the leaves over a doors_live frame");
    }
}

/* R147 PR11: the band at the inward done state over the interior the device draws (draw with doors_live) is the face
 * drawn without it, byte for byte, at both ends: the swing's end (dw_doors_at_k at END), a tap (dw_doors_skip_k), and
 * the reduced and Fade cross-fade's end; and every held swing frame between the leaves' stop and END is the face's own
 * tail frame with its leaves. Over this build's doors: the generic dw_doors_m, and in the overlay variant the overlay's
 * (that variant skips, 77, unconfigured); also on the mockup's timeline (test_dw_faces_mockup). */
static void doorway_end_seam_both_sets(void)
{
    const row_t *r = row_of("doorway");
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    fk_img_t door, glow;
    d3_doors(&door, &glow);
    static fk_col_t cols[W];
    static const int hms[2][3] = {{10, 9, 30}, {3, 47, 12}};
    for (int c = 0; c < 2; c++) {
        fk_face_in_t rest = in_at(hms[c][0], hms[c][1], hms[c][2], 500);
        for (int how = 0; how < 3; how++) {
            fk_cols_state_t st;
            const bool reduced = how == 2;
            rest.reduced = reduced;
            row_frame(r, r->ops->draw, &rest, s_b);   /* the face at rest (reduced for the cross-fade): its own leaves */
            if (how == 0) dw_doors_at_k(k, DW_DOORS_END_MS, false, &st);
            else if (how == 1) dw_doors_skip_k(k, &st);
            else dw_doors_at_k(k, DW_DOORS_REDUCED_MS, true, &st);
            TEST_ASSERT_TRUE(st.done && st.keep && st.leaves);
            fk_cols_k(k, st.th_l, st.th_r, cols);
            bool settled = false;
            fk_face_in_t in = rest;   /* d3_job sets its reduced, wake_ms and doors_live as the device does */
            fk_rp_t plan;
            TEST_ASSERT_TRUE(d3_job(&st, cols, how == 0 ? DW_DOORS_END_MS : 0, reduced, &settled, &in, &plan));
            TEST_ASSERT_EQUAL_INT(FK_RP_SETTLED, plan.kind);
            TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, in.wake_ms);
            TEST_ASSERT_EQUAL(!reduced, in.reveal_live);   /* the band draws the leaves, but under the cross-fade */
            row_frame(r, r->ops->draw, &in, s_a);   /* the interior */
            d3_band(&st, cols, &door, &glow, s_a, s_d);
            assert_panel_is(s_b, s_d, how == 0 ? "the swing's end" : how == 1 ? "a tap" : "the cross-fade's end");
            rest.reduced = false;
        }
        /* the hold: both leaves stopped, the light out, the tail still running */
        const uint32_t stop = DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS + DW_DOORS_SWING_MS;
        long held = 0;
        for (uint32_t t = stop; t < DW_DOORS_END_MS; t += 61) {
            fk_cols_state_t st;
            dw_doors_at_k(k, t, false, &st);
            if (st.seam || st.glow) continue;
            fk_cols_k(k, st.th_l, st.th_r, cols);
            bool settled = false;
            fk_face_in_t in = rest;
            fk_rp_t plan;
            TEST_ASSERT_TRUE(d3_job(&st, cols, t, false, &settled, &in, &plan));
            TEST_ASSERT_EQUAL_INT(FK_RP_SWING, plan.kind);
            TEST_ASSERT_TRUE(in.reveal_live && in.wake_ms == t);
            row_frame(r, r->ops->draw, &in, s_a);
            d3_band(&st, cols, &door, &glow, s_a, s_d);
            in.reveal_live = false;
            row_frame(r, r->ops->draw, &in, s_c);
            assert_panel_is(s_c, s_d, "a held frame vs the face's own tail frame");
            held++;
        }
        TEST_ASSERT_TRUE(held > 3);
    }
    printf("#3's end seam: %s doors, %s timeline\n", dw_assets_host_from_overlay(ASSET_DW_DOORS_M) ? "overlay" : "generic",
           DW_DOORS_END_MS == 2300 ? "the mockup's" : "the device's");
}

/* R147 PR11: under reduced motion (and Fade) the closed doors cross-fade over the face with its kept leaves: the
 * device's plan draws the interior once, with doors_live false, and every step's composite shows the leaves fading in
 * (the frame differs from one over the face without its leaves, where the doors are not at full weight) */
static void doorway_reduced_keeps_leaves(void)
{
    const row_t *r = row_of("doorway");
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    fk_img_t door, glow;
    d3_doors(&door, &glow);
    static uint8_t mask[W * W];
    const long nmask = d3_leaf_mask(r, mask);
    static fk_col_t cols[W];
    fk_face_in_t rest = in_at(10, 9, 30, 500);
    rest.reduced = true;
    row_frame(r, r->ops->draw, &rest, s_b);   /* the face at rest under reduced motion */
    fk_face_in_t bare = rest;
    bare.reveal_live = true;
    static uint16_t s_bare[W * W];
    row_frame(r, r->ops->draw, &bare, s_bare);   /* the face without its leaves */
    bool settled = false;
    unsigned drawn = 0, steps = 0;
    for (uint32_t t = 0; t <= DW_DOORS_REDUCED_MS + 40; t += 10) {
        fk_cols_state_t st;
        dw_doors_at_k(k, t, true, &st);
        fk_cols_k(k, st.th_l, st.th_r, cols);
        fk_face_in_t in = rest;
        fk_rp_t plan;
        if (d3_job(&st, cols, t, true, &settled, &in, &plan)) {   /* the device draws the interior once */
            TEST_ASSERT_FALSE_MESSAGE(in.reveal_live, "the cross-fade's interior leaves its kept leaves to the band");
            row_frame(r, r->ops->draw, &in, s_a);
            drawn++;
        }
        assert_same(s_b, s_a, "the cross-fade's interior is the face with its kept leaves");
        if (st.done) continue;
        TEST_ASSERT_TRUE(st.th_l == 0 && st.th_r == 0);   /* the closed doors fade; the face supplies the leaves */
        d3_band(&st, cols, &door, &glow, s_a, s_d);
        if (st.xfade == 255 || st.fade == 0) continue;
        d3_band(&st, cols, &door, &glow, s_bare, s_c);
        long shown = 0;
        for (int i = 0; i < W * W; i++) shown += mask[i] && s_d[i] != s_c[i];
        char m[96];
        snprintf(m, sizeof m, "the kept leaves are not in the cross-fade at %u ms (xfade %u)", (unsigned)t, st.xfade);
        TEST_ASSERT_TRUE_MESSAGE(shown * 2 > nmask, m);
        steps++;
    }
    TEST_ASSERT_EQUAL_UINT(1, drawn);
    TEST_ASSERT_TRUE(steps > 20);
}

/* fk_view's interior choice (fk_rs_job_in, reveal_sm.h), frame by frame through a whole inward reveal on the device's
 * plan (the interior keeps the pixels a frame's plan does not redraw): every swing frame's interior is drawn with
 * doors_live (no face leaf ever under the band's), the tap's and the end's hand-off are the face at rest; and the Fade
 * style with reduced motion off (the doors' cross-fade, in.reduced false: Task 11 review M3) hands off to the face at
 * rest too, its interior drawn once without doors_live. Whenever the time layer shows in a swing frame (the tail's text
 * fade, from the mockup's te 900), the leaves' free edges lie outside its box (x < 97, x >= 369: DW_D3_TIME), since the
 * band draws the leaves over it (Task 11 deviation 7; review M5). */
static void doorway_handoff_frames(void)
{
    const row_t *r = row_of("doorway");
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    fk_img_t door, glow;
    d3_doors(&door, &glow);
    static fk_col_t cols[W];
    static uint16_t s_int[W * W];
    const fk_face_in_t rest = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &rest, s_b);
    TEST_ASSERT_TRUE(DW_D3_TIME.x0 >= 97 && DW_D3_TIME.x1 <= 369);
    for (int mode = 0; mode < 3; mode++) {   /* 0 the swing to its end, 1 a tap mid-swing, 2 Fade with motion on */
        const bool tap = mode == 1, fade = mode == 2;
        bool settled = false;
        memset(s_int, 0x33, sizeof s_int);
        unsigned swing = 0, drawn = 0, texted = 0;
        for (uint32_t t = 0;; t += 40) {
            fk_cols_state_t st;
            if (tap && t >= DW_DOORS_SWING_T0_MS + 250) dw_doors_skip_k(k, &st);   /* a tap mid-swing */
            else dw_doors_at_k(k, t, fade, &st);
            fk_cols_k(k, st.th_l, st.th_r, cols);
            fk_face_in_t in = rest;   /* reduced motion off in every mode */
            fk_rp_t plan;
            if (d3_job(&st, cols, t, fade, &settled, &in, &plan)) {
                TEST_ASSERT_FALSE(in.reduced);
                if (fade) TEST_ASSERT_FALSE_MESSAGE(in.reveal_live, "the Fade's interior leaves its leaves to the band");
                else TEST_ASSERT_TRUE_MESSAGE(in.reveal_live, "a swing frame's interior draws the kept leaves");
                const omni_rect_t c = {plan.xa, 0, plan.xb, W};
                in.page = r->page;
                r->ops->draw(r->ctx, &in, c, s_int + plan.xa, W);
                drawn++;
                swing += plan.kind == FK_RP_SWING;
                if (plan.kind == FK_RP_SWING) TEST_ASSERT_TRUE(plan.xa == 0 && plan.xb == W);   /* inward: every column */
                /* M5: the time layer shows (dw_d3_text_level > 0) only clear of the leaves the band draws over it */
                if (plan.kind == FK_RP_SWING && in.wake_ms != UINT32_MAX && dw_d3_text_level(&in) > 0) {
                    int xl, xr;
                    d3_free_edges(cols, &xl, &xr);
                    char m[96];
                    snprintf(m, sizeof m, "at %u ms the time shows and the leaves reach x %d / %d", (unsigned)t, xl, xr);
                    TEST_ASSERT_TRUE_MESSAGE(xl < 97 && xr >= 369, m);
                    texted++;
                }
            }
            d3_band(&st, cols, &door, &glow, s_int, s_d);
            if (st.done) {
                assert_panel_is(s_b, s_d, tap ? "the tap's hand-off" : fade ? "the Fade's hand-off (motion on)"
                                                                         : "the end's hand-off");
                break;
            }
            TEST_ASSERT_TRUE(t < 4000);
        }
        if (fade) TEST_ASSERT_TRUE(swing == 0 && drawn == 1);   /* the cross-fade draws the interior once */
        else TEST_ASSERT_TRUE(swing > (tap ? 5u : 20u));
        if (mode == 0) TEST_ASSERT_TRUE_MESSAGE(texted > 3, "no swing frame shows the time (M5 is blind)");
    }
    /* the choice itself: the swing's tail and its settled end draw with doors_live; the cross-fade without */
    const fk_rp_t sw = {FK_RP_SWING, 0, W}, se = {FK_RP_SETTLED, 0, W};
    uint32_t wk;
    bool live;
    fk_rs_job_in(&sw, 700, false, &wk, &live);
    TEST_ASSERT_TRUE(wk == 700 && live);
    fk_rs_job_in(&se, 1800, false, &wk, &live);
    TEST_ASSERT_TRUE(wk == UINT32_MAX && live);
    fk_rs_job_in(&se, 0, true, &wk, &live);
    TEST_ASSERT_TRUE(wk == UINT32_MAX && !live);
}

/* Task 12 review M1: TEXT_TE0 (doorway.c) is hand-measured, so sweep its window 1 ms at a time on this build's timeline
 * (the device's 650-760 ms, Designer A's 1000-1100 ms in test_dw_faces_mockup): wherever dw_d3_text_level is positive
 * in a swing frame (wake_ms t, doors_live), both leaves' free edges lie outside the time's box (x < 97, x >= 369); the
 * level first turns positive inside the window, and is positive at its end */
static void doorway_text_threshold_sweep(void)
{
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    static fk_col_t cols[W];
    const bool mock = DW_DOORS_END_MS == 2300;
    const uint32_t t0 = mock ? 1000 : 650, t1 = mock ? 1100 : 760;
    fk_face_in_t in = in_at(10, 9, 30, 500);
    in.reveal_live = true;
    in.wake_ms = t0;
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, dw_d3_text_level(&in), "the time shows before the swept window");
    uint32_t first = 0;
    for (uint32_t t = t0; t <= t1; t++) {
        in.wake_ms = t;
        if (!dw_d3_text_level(&in)) continue;
        if (!first) first = t;
        fk_cols_state_t st;
        dw_doors_at_k(k, t, false, &st);
        fk_cols_k(k, st.th_l, st.th_r, cols);
        int xl, xr;
        d3_free_edges(cols, &xl, &xr);
        char m[96];
        snprintf(m, sizeof m, "at %u ms the time shows (%u) and the leaves reach x %d / %d", (unsigned)t,
                 dw_d3_text_level(&in), xl, xr);
        TEST_ASSERT_TRUE_MESSAGE(xl < 97 && xr >= 369, m);
    }
    TEST_ASSERT_TRUE_MESSAGE(first > t0 && first < t1, "the time's fade does not start inside the swept window");
    printf("#3's time first shows at %u ms (%s timeline)\n", (unsigned)first, mock ? "the mockup's" : "the device's");
}

/* within a second (40 ms steps) every dirty rect lies inside DW_D3_COLUMN, and the column moves at the cadence; at a
 * second's step, the seconds light's two boxes as well; under reduced motion nothing within a second */
static void doorway_column_confined(void)
{
    const row_t *r = row_of("doorway");
    fk_face_in_t a = in_at(10, 9, 0, 0);
    int moved = 0, steps = 0;
    for (int step = 0; step < 1499; step++) {   /* :00.000 .. :59.960 */
        const fk_face_in_t b = later(a, 40);
        omni_rect_t rects[8];
        const uint8_t n = r->ops->dirty(r->ctx, &a, &b, rects);
        omni_rect_t s0, s1;
        dw_d3_sec_rect(a.clk.s, &s0);
        dw_d3_sec_rect(b.clk.s, &s1);
        for (int q = 0; q < n; q++) {
            const bool ok = rect_in(&rects[q], &DW_D3_COLUMN) ||
                            (a.clk.s != b.clk.s && (!memcmp(&rects[q], &s0, sizeof s0) || !memcmp(&rects[q], &s1, sizeof s1)));
            TEST_ASSERT_TRUE_MESSAGE(ok, "a rect outside the column within a second");
        }
        moved += n > 0;
        steps += a.clk.s != b.clk.s;
        if (step % 50 == 7 || a.clk.s != b.clk.s) {
            char what[48];
            snprintf(what, sizeof what, "doorway at :%02d.%03d + 40 ms", a.clk.s, a.clk.ms);
            if (step % 50 == 7 || a.clk.s % 15 == 14) dirty_pair(r, r->ops->draw, &a, &b, what);
        }
        a = b;
    }
    /* the column redraws when its level moves (.08 of a 4.4 s sine: a level about every 40 ms at its steepest) */
    printf("#3's column: %d of 1499 steps redraw\n", moved);
    TEST_ASSERT_TRUE_MESSAGE(moved > 700, "the column does not breathe at the cadence");
    TEST_ASSERT_EQUAL_INT(59, steps);
    /* the breath: .35 + .08 sin(t / 700), t the ms of the day, redraws the column; reduced: steady */
    fk_face_in_t p = in_at(10, 9, 30, 0), q = later(p, 40);
    row_frame(r, r->ops->draw, &p, s_a);
    row_frame(r, r->ops->draw, &q, s_b);
    long in_col = 0;
    for (int i = 0; i < W * W; i++)
        if (s_a[i] != s_b[i]) {
            TEST_ASSERT_TRUE(inside(&DW_D3_COLUMN, i % W, i / W));
            in_col++;
        }
    TEST_ASSERT_TRUE_MESSAGE(in_col > 1000, "the column does not breathe");
    p.reduced = q.reduced = true;
    omni_rect_t rects[8];
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &p, &q, rects));
    /* reduced: the seconds light still steps, exactly its two boxes */
    q = later(p, 1000);
    TEST_ASSERT_EQUAL_UINT8(2, r->ops->dirty(r->ctx, &p, &q, rects));
    omni_rect_t s0, s1;
    dw_d3_sec_rect(p.clk.s, &s0);
    dw_d3_sec_rect(q.clk.s, &s1);
    TEST_ASSERT_EQUAL_MEMORY(&s0, &rects[0], sizeof s0);
    TEST_ASSERT_EQUAL_MEMORY(&s1, &rects[1], sizeof s1);
    TEST_ASSERT_TRUE(dirty_pair(r, r->ops->draw, &p, &q, "reduced: the seconds step") < 2000);
    row_frame(r, r->ops->draw, &p, s_a);
    row_frame(r, r->ops->draw, &q, s_b);
    TEST_ASSERT_TRUE_MESSAGE(frames_differ() > 100, "the seconds light does not step under reduced motion");
    /* the minute and the date: the whole frame */
    fk_face_in_t m = in_at(10, 9, 59, 980);
    const fk_face_in_t m1 = later(m, 40);
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &m, &m1, rects));
    TEST_ASSERT_EQUAL_INT(W, rects[0].x1 - rects[0].x0);
}

/* every text pixel lies within 200 px of the centre: the time's cells and the date line's box, at every minute of a day
 * in both 12 and 24 h and on the widest dates, and unset; the text moves nothing outside those boxes */
static void doorway_text_radius(void)
{
    const row_t *r = row_of("doorway");
    double worst = 0;
    for (int h24 = 0; h24 < 2; h24++)
        for (int t = 0; t <= 1440; t++) {
            fk_face_in_t in = in_at(t / 60 % 24, t % 60, 30, 500);
            in.h24 = h24 != 0;
            if (t == 1440) in.clk.set = false;
            in.clk.wday = (uint8_t)(t % 7), in.clk.mday = (uint8_t)(t % 31 + 1), in.clk.mon = (uint8_t)(t % 12);
            omni_rect_t box[2];
            dw_d3_text_boxes(r->ctx, &in, &box[0], &box[1]);
            for (int b = 0; b < 2; b++) {
                if (box[b].x0 >= box[b].x1) continue;
                TEST_ASSERT_TRUE_MESSAGE(rect_in(&box[b], &DW_D3_TIME), "text outside the time layer's box");
                for (int c = 0; c < 4; c++) {
                    const double x = (c & 1 ? box[b].x1 : box[b].x0) - 233.0, y = (c & 2 ? box[b].y1 : box[b].y0) - 233.0;
                    const double d = sqrt(x * x + y * y);
                    worst = d > worst ? d : worst;
                }
            }
        }
    char m[64];
    snprintf(m, sizeof m, "a text box's corner at %.1f px from the centre", worst);
    TEST_ASSERT_TRUE_MESSAGE(worst <= 200.0, m);
    printf("#3's text: the farthest box corner %.1f px from the centre\n", worst);
    /* the boxes hold the ink: the date alone changed, then the time alone (its minute mark aside); under reduced motion,
     * so the column's breath (the ms of the day) holds still between the two */
    fk_face_in_t a = in_at(10, 9, 30, 500), b = a;
    a.reduced = b.reduced = true;
    b.clk.wday = 3, b.clk.mday = 30, b.clk.mon = 8;   /* WED 30 SEP */
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    omni_rect_t da, db, ta, tb, ma, mb;
    dw_d3_text_boxes(r->ctx, &a, &ta, &da);
    dw_d3_text_boxes(r->ctx, &b, &tb, &db);
    long n = 0;
    for (int i = 0; i < W * W; i++)
        if (s_a[i] != s_b[i]) {
            TEST_ASSERT_TRUE_MESSAGE(inside(&da, i % W, i / W) || inside(&db, i % W, i / W), "date ink outside its box");
            n++;
        }
    TEST_ASSERT_TRUE(n > 50);
    b = in_at(10, 8, 30, 500);   /* 10:08: the minute mark moves (the hour mark stays at 50) */
    b.reduced = true;
    row_frame(r, r->ops->draw, &b, s_b);
    dw_d3_text_boxes(r->ctx, &b, &tb, &db);
    dw_d3_mark_rect(9, false, &ma);
    dw_d3_mark_rect(8, false, &mb);
    n = 0;
    for (int i = 0; i < W * W; i++)
        if (s_a[i] != s_b[i]) {
            const int x = i % W, y = i / W;
            TEST_ASSERT_TRUE_MESSAGE(inside(&ta, x, y) || inside(&tb, x, y) || inside(&ma, x, y) || inside(&mb, x, y),
                                     "the time's ink outside its box");
            n++;
        }
    TEST_ASSERT_TRUE(n > 100);
}

/* #3's thumbnail comes from the generic doors in both builds (WD26): its dw_layers.json row swaps buildDoors' canvas for
 * dw_doors_m's generic drawing, and the committed thumbnail's leaves look like the generic doors' (the mean colour of
 * the left leaf's strip, against the face drawn over the generic doors and scaled to 68 px) */
static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *p = malloc((size_t)len + 1);
    if (p && fread(p, 1, (size_t)len, f) != (size_t)len) {
        free(p);
        p = NULL;
    }
    fclose(f);
    if (p) p[len] = 0, *n = (size_t)len;
    return p;
}

static void d3_strip_mean(const uint16_t *fb, int w, int x0, int x1, int y0, int y1, double rgb[3])
{
    rgb[0] = rgb[1] = rgb[2] = 0;
    long n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const uint16_t p = fb[y * w + x];
            rgb[0] += omni_ex_r(p), rgb[1] += omni_ex_g(p), rgb[2] += omni_ex_b(p);
            n++;
        }
    for (int c = 0; c < 3; c++) rgb[c] /= (double)n;
}

static void doorway_thumb_generic(void)
{
    size_t n = 0;
    char *json = slurp(DW_ASSETS_DIR "/../../../../tools/export/dw_layers.json", &n);
    TEST_ASSERT_NOT_NULL_MESSAGE(json, "tools/export/dw_layers.json");
    const char *row = strstr(json, "{\"asset\": \"dw_th_d3\"");
    TEST_ASSERT_NOT_NULL_MESSAGE(row, "no dw_th_d3 row");
    const char *end = strchr(row, '}');
    TEST_ASSERT_NOT_NULL(end);
    const char *swap = strstr(row, "\"swap\": {\"buildDoors\": \"dw_doors_m\"}");
    TEST_ASSERT_TRUE_MESSAGE(swap && swap < end + 40, "dw_th_d3 does not draw the generic doors");
    TEST_ASSERT_TRUE(strstr(row, "\"face\": \"dw-reveal-doorway\"") < end);
    free(json);
    /* the generic doors, read from the committed set even in the overlay variant */
    char *gd = slurp(DW_ASSETS_DIR "/" ASSET_DW_DOORS_M, &n);
    TEST_ASSERT_NOT_NULL(gd);
    fk_img_t door, th;
    TEST_ASSERT_TRUE(fk_img_from_bin(&door, gd, n) && door.w == W);
    /* the committed thumbnail, read from the committed set even in the overlay variant (public release Task 9: the
     * overlay may carry its own, a swap file kept as it was) */
    char *gth = slurp(DW_ASSETS_DIR "/" ASSET_DW_TH_D3, &n);
    TEST_ASSERT_NOT_NULL(gth);
    TEST_ASSERT_TRUE(fk_img_from_bin(&th, gth, n));
    const row_t *r = row_of("doorway");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    in.reveal_live = true;
    row_frame(r, r->ops->draw, &in, s_a);   /* the face without leaves: each set's leaves go over a copy */
    static fk_col_t cols[W];
    fk_cols_state_t ds;
    dw_doors_skip_k(&DW_DOORS_IN_KEEP, &ds);
    fk_cols_k(&DW_DOORS_IN_KEEP, ds.th_l, ds.th_r, cols);
    fk_img_t staged;
    img_of(ASSET_DW_DOORS_M, &staged);   /* this build's doors: the overlay's in the overlay variant */
    memcpy(s_b, s_a, sizeof s_a);
    fk_cols_leaves(&DW_DOORS_IN_KEEP, cols, door.rgb, (omni_rect_t){0, 0, W, W}, s_a, W);
    fk_cols_leaves(&DW_DOORS_IN_KEEP, cols, staged.rgb, (omni_rect_t){0, 0, W, W}, s_b, W);
    /* the left leaf's strip, x 20..70, y 140..330 on the face, x 3..10, y 20..48 on the 68 px thumbnail, compared by
     * its chromaticity (r, g, b over their sum): the mockup's drawLeaf overlaps its 2 px strips (w + .7 px each), so its
     * leaves are brighter than the doors' engine draws them, but their hue is the art's */
    double gen[3], set[3], got[3];
    d3_strip_mean(s_a, W, 20, 70, 140, 330, gen);
    d3_strip_mean(s_b, W, 20, 70, 140, 330, set);
    d3_strip_mean(th.rgb, th.w, 3, 10, 20, 48, got);
    double dg = 0, ds_ = 0;
    const double sg = gen[0] + gen[1] + gen[2], ss = set[0] + set[1] + set[2], st = got[0] + got[1] + got[2];
    char m[160];
    snprintf(m, sizeof m, "thumbnail leaf (%.0f, %.0f, %.0f) vs the generic doors' (%.0f, %.0f, %.0f), this set's (%.0f, %.0f, %.0f)",
             got[0], got[1], got[2], gen[0], gen[1], gen[2], set[0], set[1], set[2]);
    printf("%s\n", m);
    for (int c = 0; c < 3; c++) {
        const double e = fabs(got[c] / st - gen[c] / sg), f = fabs(got[c] / st - set[c] / ss);
        TEST_ASSERT_TRUE_MESSAGE(e < 0.03, m);
        dg += e, ds_ += f;
    }
    if (dw_assets_host_from_overlay(ASSET_DW_DOORS_M)) TEST_ASSERT_TRUE_MESSAGE(dg < ds_, m);   /* not the overlay's */
    free(gth);
    free(gd);
}

/* the chime (not reduced): the column, the lances and the ripple breathe, inside their boxes; no tint */
static void doorway_chime_regions(void)
{
    const row_t *r = row_of("doorway");
    static const int phase[] = {0, 333, 500, 940};
    for (unsigned k = 0; k < sizeof phase / sizeof phase[0]; k++)
        for (int c = 0; c < 3; c++) {
            fk_face_in_t a = in_at(10, 9, 21, phase[k]), b = a;
            b.chime = c == 0 ? 0.7f : c == 1 ? 1.0f : 0.15f;
            row_frame(r, r->ops->draw, &a, s_a);
            row_frame(r, r->ops->draw, &b, s_b);
            long d = 0, ring = 0;
            for (int i = 0; i < W * W; i++) {
                if (s_a[i] == s_b[i]) continue;
                const int x = i % W, y = i / W;
                TEST_ASSERT_TRUE_MESSAGE(inside(&DW_D3_COLUMN, x, y) || inside(&DW_D3_LANCES, x, y) ||
                                             inside(&DW_D3_RIPPLE, x, y),
                                         "the chime changes a pixel outside the column, the lances and the ripple");
                ring += inside(&DW_D3_RIPPLE, x, y) && !inside(&DW_D3_COLUMN, x, y) && !inside(&DW_D3_LANCES, x, y);
                d++;
            }
            TEST_ASSERT_TRUE_MESSAGE(d > 5000, "the chime changes little");
            TEST_ASSERT_TRUE_MESSAGE(ring > 100, "no ripple on the floor");
            dirty_pair(r, r->ops->draw, &a, &b, "doorway chime");
            a.reduced = b.reduced = true;
            row_frame(r, r->ops->draw, &a, s_a);
            row_frame(r, r->ops->draw, &b, s_b);
            TEST_ASSERT_EQUAL_INT32(0, frames_differ());
        }
    /* the lances' layer lights nothing outside DW_D3_LANCES, and its box is tight */
    fk_img_t im;
    const fk_img_t *l = img_of(ASSET_DW_D3_LANCE, &im);
    TEST_ASSERT_TRUE(l->a && l->w == DW_D3_LANCES.x1 - DW_D3_LANCES.x0 && l->h == DW_D3_LANCES.y1 - DW_D3_LANCES.y0);
    int x1 = -1, y1 = -1;
    for (int y = 0; y < l->h; y++)
        for (int x = 0; x < l->w; x++)
            if (l->a[y * l->w + x]) x1 = x > x1 ? x : x1, y1 = y > y1 ? y : y1;
    TEST_ASSERT_TRUE_MESSAGE(x1 == l->w - 1 && y1 == l->h - 1, "the lance box is not tight");
}

/* the doors' tail at their end time is the settled frame exactly; the time fades in by the tail */
static void doorway_tail(void)
{
    const row_t *r = row_of("doorway");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    row_frame(r, r->ops->draw, &in, s_a);
    in.wake_ms = DW_DOORS_END_MS;
    row_frame(r, r->ops->draw, &in, s_b);
    assert_same(s_a, s_b, "#3 at the doors' end time vs settled");
    in.wake_ms = DW_DOORS_SWING_T0_MS;   /* te 380: no time yet */
    row_frame(r, r->ops->draw, &in, s_b);
    omni_rect_t tb, db;
    dw_d3_text_boxes(r->ctx, &in, &tb, &db);
    long lit = 0;
    for (int y = tb.y0; y < tb.y1; y++)
        for (int x = tb.x0; x < tb.x1; x++) lit += omni_ex_g(s_a[y * W + x]) > 200 && omni_ex_g(s_b[y * W + x]) < 200;
    TEST_ASSERT_TRUE_MESSAGE(lit > 300, "the time is up before the tail brings it");
}

/* the glance: the seam hairline and the time, no window outlines; unset --:-- */
static void doorway_glance_seam(void)
{
    const row_t *r = row_of("doorway");
    fk_face_in_t in = in_at(10, 9, 30, 500);
    in.glance = true;
    row_frame(r, r->ops->glance, &in, s_a);
    TEST_ASSERT_EQUAL_HEX16(RGB565(0x20, 0x22, 0x2c), s_a[200 * W + 232]);
    TEST_ASSERT_EQUAL_HEX16(RGB565(0x20, 0x22, 0x2c), s_a[415 * W + 232]);
    TEST_ASSERT_EQUAL_HEX16(0, s_a[416 * W + 232]);
    TEST_ASSERT_EQUAL_HEX16(0, s_a[195 * W + 232]);
    long lit = 0;   /* nothing where the windows were (y 80..172) */
    for (int y = 80; y < 172; y++)
        for (int x = 70; x < 400; x++) lit += s_a[y * W + x] != 0;
    TEST_ASSERT_EQUAL_INT32(0, lit);
}

/* ---- #8: Broadcast (wave 2 Task 12) */
static void broadcast_registered(void)
{
    TEST_ASSERT_EQUAL_STRING("broadcast", DW_FACE_BROADCAST.key);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_BROADCAST, dw_face_by_key("broadcast"));
    TEST_ASSERT_EQUAL_UINT16(0, DW_FACE_BROADCAST.cadence_ms);   /* 1 Hz at rest: the shell's update each second */
    TEST_ASSERT_EQUAL_UINT16(450, DW_FACE_BROADCAST.wake_len_ms);
    TEST_ASSERT_EQUAL_INT(2048, DW_B8_RING_NT);   /* the Task 12 review's ruling (PR8's other option) */
    TEST_ASSERT_EQUAL_UINT16(0, DW_FACE_BROADCAST.pulse_len_ms);
    TEST_ASSERT_FALSE(DW_FACE_BROADCAST.reveal);
    TEST_ASSERT_NULL(DW_FACE_BROADCAST.tap);
    /* broadcast.c's ANN_MAX holds the ring's map (its ctx: the map, PSRAM) */
    TEST_ASSERT_TRUE(fk_annulus_bytes(DW_B8_RING_R0, DW_B8_RING_R1, DW_B8_RING_NT) <= 364544u);
    TEST_ASSERT_TRUE(DW_FACE_BROADCAST.ctx_bytes >= 364544u && DW_FACE_BROADCAST.ctx_bytes < 366000u);
    printf("#8's ctx: %u B (the ring's map %u B)\n", (unsigned)DW_FACE_BROADCAST.ctx_bytes,
           (unsigned)fk_annulus_bytes(DW_B8_RING_R0, DW_B8_RING_R1, DW_B8_RING_NT));
    /* the stale marker centred (233, 333) (staleMark(cx, cy + 100)) */
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_B8_STALE.x0 + DW_B8_STALE.x1);
    TEST_ASSERT_EQUAL_INT(2 * 333, DW_B8_STALE.y0 + DW_B8_STALE.y1);
    /* the face is the build's: its id appended, the carousel's last before Vortex (wave 2 Task 14 appended #12) */
    TEST_ASSERT_EQUAL_INT(14, DW_F_BROADCAST);
    TEST_ASSERT_EQUAL_INT(DW_F_BROADCAST + 1, DW_F_VORTEX);
    TEST_ASSERT_EQUAL_UINT8(DW_F_BROADCAST, DW_DEF_CAROUSEL[DW_DEF_CAROUSEL_LEN - 2]);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_BROADCAST, DW_FACES[DW_FACE_COUNT - 2]);
}

/* R147 PR6: face_generic's timer period (fk_face_timer_ms): a cadence face keeps its cadence whatever runs; a face of
 * no cadence gets the 40 ms timer only while its own wake or a pulse runs, none at rest; a face with neither has none */
static void broadcast_wake_timer(void)
{
    TEST_ASSERT_EQUAL_UINT16(40, FK_FACE_TIMER_WAKE_MS);
    TEST_ASSERT_EQUAL_UINT16(40, fk_face_timer_ms(&DW_FACE_BROADCAST, true));
    TEST_ASSERT_EQUAL_UINT16(0, fk_face_timer_ms(&DW_FACE_BROADCAST, false));
    for (unsigned i = 0; i < DW_FACE_COUNT; i++) {
        const fk_face_ops_t *o = DW_FACES[i];
        if (o->cadence_ms) {
            TEST_ASSERT_EQUAL_UINT16(o->cadence_ms, fk_face_timer_ms(o, false));   /* unchanged for every cadence face */
            TEST_ASSERT_EQUAL_UINT16(o->cadence_ms, fk_face_timer_ms(o, true));
        } else if (!o->wake_len_ms && !o->pulse_len_ms) {
            TEST_ASSERT_EQUAL_UINT16(0, fk_face_timer_ms(o, false));   /* #16: no timer, as before */
            TEST_ASSERT_EQUAL_UINT16(0, fk_face_timer_ms(o, true));
        }
    }
    const fk_face_ops_t pulse_only = {.key = "p", .cadence_ms = 0, .pulse_len_ms = 300};
    TEST_ASSERT_EQUAL_UINT16(40, fk_face_timer_ms(&pulse_only, true));
    TEST_ASSERT_EQUAL_UINT16(0, fk_face_timer_ms(&pulse_only, false));
}

/* the howlaround turns once in 90 s, stepped each second (D3): k = round((t mod 90) n / 90), offset (n - k) mod n, n 2048
 * (the Task 12 review's ruling; 22 or 23 entries a second) */
static void broadcast_ring_period(void)
{
    for (int j = 0; j < 90; j++) {
        const fk_face_in_t a = in_at(10, 9, 0, 500);   /* 10:09:00 is 36 540 s, a multiple of 90 */
        const fk_face_in_t b = shifted(a, j * 1000L);
        const int k = (int)lround(j * 2048.0 / 90.0);
        TEST_ASSERT_EQUAL_UINT16((2048 - k) % 2048, dw_b8_ring_off(&b));
    }
    static const int hms[4][3] = {{0, 0, 0}, {10, 9, 30}, {23, 58, 59}, {3, 47, 12}};
    for (int c = 0; c < 4; c++)
        for (int ms = 0; ms < 1000; ms += 333) {
            const fk_face_in_t t = in_at(hms[c][0], hms[c][1], hms[c][2], ms);
            const fk_face_in_t t90 = shifted(t, 90000), t1 = shifted(t, 1000), t40 = shifted(t, 40);
            TEST_ASSERT_EQUAL_UINT16(dw_b8_ring_off(&t), dw_b8_ring_off(&t90));
            const int d = (dw_b8_ring_off(&t) - dw_b8_ring_off(&t1) + 2048) % 2048;   /* clockwise: the offset falls */
            TEST_ASSERT_TRUE_MESSAGE(d == 22 || d == 23, "a second's step is not 22 or 23 entries");
            if (t.clk.s == t40.clk.s) TEST_ASSERT_EQUAL_UINT16(dw_b8_ring_off(&t), dw_b8_ring_off(&t40));
        }
    /* midnight: 86 400 is a multiple of 90, so 23:59:59 -> 00:00:00 is an ordinary step */
    const fk_face_in_t e = in_at(23, 59, 59, 0), z = in_at(0, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT(23, (dw_b8_ring_off(&e) - dw_b8_ring_off(&z) + 2048) % 2048);
    /* unset: the free-running mono_ms (the clock's fields are not read); reduced: held at 0 */
    fk_face_in_t u = in_at(10, 9, 31, 0);
    u.clk.set = false;
    u.mono_ms = 0;
    TEST_ASSERT_EQUAL_UINT16(0, dw_b8_ring_off(&u));
    u.mono_ms = 1000;
    TEST_ASSERT_EQUAL_UINT16(2025, dw_b8_ring_off(&u));
    u.mono_ms = 91999;
    TEST_ASSERT_EQUAL_UINT16(2025, dw_b8_ring_off(&u));
    fk_face_in_t r = in_at(10, 9, 31, 0);
    r.reduced = true;
    TEST_ASSERT_EQUAL_UINT16(0, dw_b8_ring_off(&r));
    u.reduced = true;
    TEST_ASSERT_EQUAL_UINT16(0, dw_b8_ring_off(&u));
}

static int ch5r(uint16_t p) { return (p >> 11) & 31; }
static int ch6g(uint16_t p) { return (p >> 5) & 63; }
static int ch5b(uint16_t p) { return p & 31; }

/* the scanlines over everything (scanlines(), :823-828): on every row with y mod 3 == 2 each channel is at most half the
 * channel of the same pixel drawn without them (dw_b8_frame's scan false, the test hook); the other rows are equal;
 * awake, settled, stale, unset, reduced and in the glance */
static void broadcast_scanlines(void)
{
    /* The switch-on's frames are left out by design: from 120 ms they are the scanned picture scaled vertically (each
     * screen row shows the composed frame's row at its place, scanlines and all, then the flash), as the mockup scales
     * bc.tmp after its scanlines are drawn, so their dark rows do not fall on y mod 3 == 2 (test_broadcast_switch_on_*
     * cover those frames; Task 12 review M3). */
    const row_t *r = row_of("broadcast");
    for (int c = 0; c < 7; c++) {
        fk_face_in_t in = in_at(c == 1 ? 3 : 10, c == 1 ? 47 : 9, c == 1 ? 12 : 34, 500);
        in.clk.stale = c == 2;
        in.clk.set = c != 3 && c != 6;
        in.reduced = c == 4;
        in.glance = c >= 5;
        if (in.glance) in.odx = 3, in.ody = -2;
        const omni_rect_t all = {0, 0, W, W};
        dw_b8_frame(r->ctx, &in, all, s_a, W, true);
        dw_b8_frame(r->ctx, &in, all, s_b, W, false);
        row_frame(r, in.glance ? r->ops->glance : r->ops->draw, &in, s_c);
        assert_same(s_c, s_a, "the face's frame is dw_b8_frame with the scanlines");
        long dark = 0, lit = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                const uint16_t p = s_a[y * W + x], q = s_b[y * W + x];
                if (y % 3 != 2) {
                    if (p != q) TEST_FAIL_MESSAGE("a row off the scanlines changes");
                    continue;
                }
                if (2 * ch5r(p) > ch5r(q) || 2 * ch6g(p) > ch6g(q) || 2 * ch5b(p) > ch5b(q)) {
                    char m[128];
                    snprintf(m, sizeof m, "case %d: scanline pixel (%d, %d) %04x is more than half of %04x", c, x, y, p, q);
                    TEST_FAIL_MESSAGE(m);
                }
                lit += q != 0;
                dark += p != q;
            }
        TEST_ASSERT_TRUE_MESSAGE(lit > 300 && dark * 2 > lit, "the scanlines darken too little");
    }
}

/* the switch-on (:864-871), built in bands, is the frame built whole: the line at 60 ms, the picture opening at 300 */
static void broadcast_switch_on_band_exact(void)
{
    const row_t *r = row_of("broadcast");
    static const uint32_t at[3] = {300, 60, 130};
    for (int k = 0; k < 3; k++)
        for (int st = 0; st < 2; st++) {
            fk_face_in_t in = in_at(10, 9, 30, 500);
            in.clk.set = st == 0;
            in.wake_ms = at[k];
            row_frame(r, r->ops->draw, &in, s_a);
            in_clips(r, r->ops->draw, &in, 8, W, s_b);
            assert_same(s_a, s_b, "the switch-on in 8-row bands");
            in_clips(r, r->ops->draw, &in, 6, 37, s_b);
            assert_same(s_a, s_b, "the switch-on in 6 x 37 blocks");
        }
}

/* Reduced motion: the ring is held (offset 0) and there is no hum bar, within and across seconds; every frame of the
 * minute is the moving frame at 10:09:00, whose ring offset is 0 and whose bar lies above the screen (hy -60); dirty()
 * reports nothing within the minute; and there is no switch-on */
static void broadcast_reduced_still(void)
{
    const row_t *r = row_of("broadcast");
    const fk_face_in_t zero = in_at(10, 9, 0, 0);
    TEST_ASSERT_EQUAL_UINT16(0, dw_b8_ring_off(&zero));
    row_frame(r, r->ops->draw, &zero, s_c);
    fk_face_in_t prev = zero;
    prev.reduced = true;
    for (int s = 0; s < 60; s += 7)
        for (int ms = 0; ms < 1000; ms += 450) {
            fk_face_in_t in = in_at(10, 9, s, ms);
            in.reduced = true;
            TEST_ASSERT_EQUAL_UINT16(0, dw_b8_ring_off(&in));
            row_frame(r, r->ops->draw, &in, s_a);
            assert_same(s_c, s_a, "a reduced frame differs from 10:09:00's (a ring turn or a hum bar)");
            omni_rect_t rects[8];
            TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &prev, &in, rects));
            prev = in;
            in.wake_ms = 200;   /* no switch-on */
            row_frame(r, r->ops->draw, &in, s_a);
            assert_same(s_c, s_a, "a reduced wake frame is not the settled frame");
        }
    /* unset under reduced: still at any mono_ms */
    fk_face_in_t u = in_at(10, 9, 0, 0);
    u.clk.set = false, u.reduced = true;
    row_frame(r, r->ops->draw, &u, s_c);
    u.mono_ms = 37123;
    row_frame(r, r->ops->draw, &u, s_a);
    assert_same(s_c, s_a, "a reduced unset frame moves with mono_ms");
}

/* the hum bar (:857-859), stepped each second: at 10:09:04 its peak lies at hy = 4 x 586 / 9 - 60 (200.4), so the black
 * pixel at (233, 201) (between the day and the time, off the scanlines) carries it, and only there in time */
static void broadcast_hum_bar(void)
{
    const row_t *r = row_of("broadcast");
    fk_face_in_t in = in_at(10, 9, 4, 500);
    row_frame(r, r->ops->draw, &in, s_a);
    TEST_ASSERT_TRUE_MESSAGE(s_a[201 * W + 233] != 0, "no hum bar at its peak");
    in.reduced = true;
    row_frame(r, r->ops->draw, &in, s_b);
    TEST_ASSERT_EQUAL_HEX16(0, s_b[201 * W + 233]);
    fk_face_in_t far = in_at(10, 9, 8, 500);   /* hy 460.9: the bar is at the bottom */
    row_frame(r, r->ops->draw, &far, s_b);
    TEST_ASSERT_EQUAL_HEX16(0, s_b[201 * W + 233]);
}

/* R147 PR6: each of the switch-on's frames at 0, 40, .. 440 ms differs from the one before, so the 40 ms timer face_generic
 * runs through the wake always has a new frame to show; dirty() names the whole frame at each of those steps and at the
 * wake's end */
static void broadcast_switch_on_frames(void)
{
    const row_t *r = row_of("broadcast");
    for (int st = 0; st < 2; st++) {
        fk_face_in_t in = in_at(10, 9, 30, 500), prev = in;
        in.clk.set = st == 0;
        for (uint32_t t = 0; t <= 440; t += 40) {
            in.wake_ms = t;
            row_frame(r, r->ops->draw, &in, s_a);
            if (t) {
                long differ = 0;
                for (int i = 0; i < W * W; i++) differ += s_a[i] != s_b[i];
                char m[80];
                snprintf(m, sizeof m, "the switch-on at %u ms is its frame at %u ms", (unsigned)t, (unsigned)(t - 40));
                TEST_ASSERT_TRUE_MESSAGE(differ > 0, m);
                omni_rect_t rects[8];
                TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &prev, &in, rects));
                TEST_ASSERT_TRUE(rects[0].x0 == 0 && rects[0].y0 == 0 && rects[0].x1 == W && rects[0].y1 == W);
                dirty_pair(r, r->ops->draw, &prev, &in, "a switch-on step");
            }
            memcpy(s_b, s_a, sizeof s_a);
            prev = in;
        }
        long black = 0;   /* t 0: the line has no width yet */
        in.wake_ms = 0;
        row_frame(r, r->ops->draw, &in, s_a);
        for (int i = 0; i < W * W; i++) black += s_a[i] == 0;
        TEST_ASSERT_EQUAL_INT32(W * W, black);
        in.wake_ms = 80;   /* the line: bright on row 233, nothing off rows 227..238 */
        row_frame(r, r->ops->draw, &in, s_a);
        TEST_ASSERT_TRUE(ch5r(s_a[233 * W + 233]) >= 28);
        for (int i = 0; i < W * W; i++)
            if (i / W < 227 || i / W > 238) TEST_ASSERT_EQUAL_HEX16(0, s_a[i]);
        prev = in, prev.wake_ms = 440;
        in.wake_ms = UINT32_MAX;   /* the end */
        dirty_pair(r, r->ops->draw, &prev, &in, "the switch-on's end");
    }
}

/* at rest the face draws once a second (cadence 0): each second's step is the whole frame (WD34: the ring reaches the
 * rim), nothing within a second; the unset ring steps on mono_ms */
static void broadcast_dirty_seconds(void)
{
    const row_t *r = row_of("broadcast");
    omni_rect_t rects[8];
    fk_face_in_t a = in_at(10, 9, 30, 0);
    for (int ms = 40; ms < 1000; ms += 40) {
        const fk_face_in_t b = in_at(10, 9, 30, ms);
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
    }
    const fk_face_in_t b = in_at(10, 9, 31, 0);
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &a, &b, rects));
    TEST_ASSERT_EQUAL_INT32((long)W * W, rects_union(rects, 1));
    fk_face_in_t u = a, v = a;
    u.clk.set = v.clk.set = false;
    u.mono_ms = 5000, v.mono_ms = 5960;
    TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &u, &v, rects));
    v.mono_ms = 6000;
    TEST_ASSERT_EQUAL_UINT8(1, r->ops->dirty(r->ctx, &u, &v, rects));
    dirty_pair(r, r->ops->draw, &u, &v, "the unset ring's step");
}

/* ---- one set of tests per face (Tasks 13-15 add theirs) */

/* ---- #12: Vortex (wave 2 Task 14) */
static void vortex_registered(void)
{
    TEST_ASSERT_EQUAL_STRING("vortex", DW_FACE_VORTEX.key);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_VORTEX, dw_face_by_key("vortex"));
    TEST_ASSERT_EQUAL_UINT16(50, DW_FACE_VORTEX.cadence_ms);
    TEST_ASSERT_EQUAL_UINT16(900, DW_FACE_VORTEX.wake_len_ms);
    TEST_ASSERT_EQUAL_UINT16(1900, DW_FACE_VORTEX.pulse_len_ms);
    TEST_ASSERT_EQUAL_UINT16(0, DW_FACE_VORTEX.pulse_guard_ms);   /* every tap restarts the name */
    TEST_ASSERT_NOT_NULL(DW_FACE_VORTEX.tap);
    TEST_ASSERT_FALSE(DW_FACE_VORTEX.reveal);
    TEST_ASSERT_TRUE(DW_FACE_VORTEX.direct);   /* its rest motion goes through the direct path (dw_view) */
    for (unsigned i = 0; i < DW_FACE_COUNT; i++)   /* the only face that asks for it, and never with doors */
        TEST_ASSERT_TRUE(DW_FACES[i] == &DW_FACE_VORTEX ? DW_FACES[i]->direct : !DW_FACES[i]->direct);
    TEST_ASSERT_EQUAL_INT(1, DW_V12_Q);   /* the gate's verdict: half resolution (WD38) */
    TEST_ASSERT_TRUE(DW_FACE_VORTEX.ctx_bytes < 4096u);   /* views of the mapped assets only */
    /* the last face: id 15 appended, the carousel's last, DW_F__COUNT 16 (the 16-face limit, WD23) */
    TEST_ASSERT_EQUAL_INT(15, DW_F_VORTEX);
    TEST_ASSERT_EQUAL_INT(16, DW_F__COUNT);
    TEST_ASSERT_EQUAL_INT(DW_F__ALL, DW_F__COUNT);
    TEST_ASSERT_EQUAL_UINT8(DW_F_VORTEX, DW_DEF_CAROUSEL[DW_DEF_CAROUSEL_LEN - 1]);
    TEST_ASSERT_EQUAL_PTR(&DW_FACE_VORTEX, DW_FACES[DW_FACE_COUNT - 1]);
    TEST_ASSERT_EQUAL_INT(DW_ERA_MODERN, dw_face_era(DW_F_VORTEX));
    /* the stale marker centred (233, 333), below the palette's name */
    TEST_ASSERT_EQUAL_INT(2 * 233, DW_V12_STALE.x0 + DW_V12_STALE.x1);
    TEST_ASSERT_EQUAL_INT(2 * 333, DW_V12_STALE.y0 + DW_V12_STALE.y1);
    TEST_ASSERT_TRUE(DW_V12_NAME.y1 <= DW_V12_STALE.y0);
}

/* in at 10:09:30.500 on palette v, the tunnel's clock at mono ms */
static fk_face_in_t vx_in(uint8_t v, uint32_t mono)
{
    fk_face_in_t in = in_at(10, 9, 30, 500);
    in.variant = v;
    in.mono_ms = mono;
    return in;
}

/* a tap anywhere cycles the palette: the corners of the disc's inscribed square, the centre, the screen's edges */
static void vortex_tap_cycles(void)
{
    const row_t *r = row_of("vortex");
    static const int16_t pts[5][2] = {{68, 68}, {397, 68}, {68, 397}, {397, 397}, {233, 233}};
    for (int k = 0; k < 5; k++) TEST_ASSERT_EQUAL_INT(FK_TAP_CYCLE, r->ops->tap(r->ctx, pts[k][0], pts[k][1]));
    fk_test_tap_edges(r->ops, r->ctx, (omni_rect_t){0, 0, W, W}, FK_TAP_CYCLE);
}

static bool vx_same(const uint16_t *a, const uint16_t *b, omni_rect_t box)
{
    for (int y = box.y0; y < box.y1; y++)
        for (int x = box.x0; x < box.x1; x++)
            if (a[y * W + x] != b[y * W + x]) return false;
    return true;
}

static long vx_differ(const uint16_t *a, const uint16_t *b, omni_rect_t box)
{
    long n = 0;
    for (int y = box.y0; y < box.y1; y++)
        for (int x = box.x0; x < box.x1; x++) n += a[y * W + x] != b[y * W + x];
    return n;
}

/* variants 0, 1, 2 are three palettes: three different rings, the same calm centre; variant 3 is palette 0 again; the
 * glance shows no palette */
static void vortex_palette_variant(void)
{
    const row_t *r = row_of("vortex");
    static uint16_t f[3][W * W];
    for (int v = 0; v < 3; v++) {
        TEST_ASSERT_EQUAL_UINT8(v, dw_v12_palette(&(fk_face_in_t){.variant = (uint8_t)(v + 3)}));
        const fk_face_in_t in = vx_in((uint8_t)v, 12345);
        row_frame(r, r->ops->draw, &in, f[v]);
    }
    for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 3; b++) {
            TEST_ASSERT_TRUE_MESSAGE(vx_differ(f[a], f[b], (omni_rect_t){0, 0, W, W}) > 20000, "two palettes' rings");
            TEST_ASSERT_TRUE_MESSAGE(vx_same(f[a], f[b], DW_V12_CENTRE), "the centre is the palette's own");
        }
    const fk_face_in_t v3 = vx_in(3, 12345);
    row_frame(r, r->ops->draw, &v3, s_a);
    assert_same(f[0], s_a, "variant 3 is palette 0");
    fk_face_in_t g = vx_in(0, 12345);
    g.glance = true;
    row_frame(r, r->ops->glance, &g, s_a);
    g.variant = 2;
    row_frame(r, r->ops->glance, &g, s_b);
    assert_same(s_a, s_b, "the glance has no palette");
    /* a palette change is dirty: the whole ring at least */
    const fk_face_in_t a = vx_in(0, 12345), b = vx_in(1, 12345);
    TEST_ASSERT_TRUE(dirty_pair(r, r->ops->draw, &a, &b, "the palette changes") >= 196420L);
}

/* reduced motion: the tunnel is still (rot = rush = 0): frames 40 ms apart, by the tunnel's clock and the wall clock, are
 * equal; moving, they are not */
static void vortex_reduced_still(void)
{
    const row_t *r = row_of("vortex");
    for (int v = 0; v < 3; v++) {
        fk_face_in_t a = vx_in((uint8_t)v, 70000), b = a;
        b.mono_ms += 40;
        b.clk.ms += 40;
        a.reduced = b.reduced = true;
        row_frame(r, r->ops->draw, &a, s_a);
        row_frame(r, r->ops->draw, &b, s_b);
        assert_same(s_a, s_b, "reduced: 40 ms apart");
        omni_rect_t rects[8];
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &b, rects));
        fk_face_in_t z = a;   /* reduced is the tunnel at rot = rush = 0, whatever the clock */
        z.mono_ms = 0;
        row_frame(r, r->ops->draw, &z, s_c);
        assert_same(s_c, s_a, "reduced is the tunnel at 0");
        a.reduced = b.reduced = false;
        row_frame(r, r->ops->draw, &a, s_a);
        row_frame(r, r->ops->draw, &b, s_b);
        TEST_ASSERT_TRUE_MESSAGE(vx_differ(s_a, s_b, (omni_rect_t){0, 0, W, W}) > 10000, "moving: the tunnel turns");
    }
}

/* the palette's name: full for 1.4 s, then a 0.5 s fade; its box differs from rest only while pulsing and pulse_ms <
 * 1900, and nothing else does */
static void vortex_name_pulse(void)
{
    const row_t *r = row_of("vortex");
    TEST_ASSERT_EQUAL_UINT8(255, dw_v12_name_level(&(fk_face_in_t){.pulsing = true, .pulse_ms = 0}));
    TEST_ASSERT_EQUAL_UINT8(255, dw_v12_name_level(&(fk_face_in_t){.pulsing = true, .pulse_ms = 1400}));
    TEST_ASSERT_EQUAL_UINT8(128, dw_v12_name_level(&(fk_face_in_t){.pulsing = true, .pulse_ms = 1650}));
    TEST_ASSERT_EQUAL_UINT8(0, dw_v12_name_level(&(fk_face_in_t){.pulsing = true, .pulse_ms = 1900}));
    TEST_ASSERT_EQUAL_UINT8(0, dw_v12_name_level(&(fk_face_in_t){.pulsing = false, .pulse_ms = 100}));
    for (int red = 0; red < 2; red++)
        for (int v = 0; v < 3; v++) {
            fk_face_in_t rest = vx_in((uint8_t)v, 4321);
            rest.reduced = red != 0;   /* a tap's own feedback still animates under reduced motion */
            row_frame(r, r->ops->draw, &rest, s_c);
            static const uint16_t ms[] = {0, 700, 1400, 1650, 1880, 1900, 2500};
            for (unsigned k = 0; k < sizeof ms / sizeof ms[0]; k++) {
                fk_face_in_t p = rest;
                p.pulsing = true;
                p.pulse_ms = ms[k];
                row_frame(r, r->ops->draw, &p, s_a);
                char what[80];
                snprintf(what, sizeof what, "palette %d%s, pulse %u ms", v, red ? " reduced" : "", ms[k]);
                for (int y = 0; y < W; y++)
                    for (int x = 0; x < W; x++)
                        if (s_a[y * W + x] != s_c[y * W + x] && !inside(&DW_V12_NAME, x, y)) TEST_FAIL_MESSAGE(what);
                const long d = vx_differ(s_a, s_c, DW_V12_NAME);
                if (ms[k] <= 1650) TEST_ASSERT_TRUE_MESSAGE(d > 50, what);
                if (ms[k] >= 1900) TEST_ASSERT_EQUAL_INT32_MESSAGE(0, d, what);
                dirty_pair(r, r->ops->draw, &rest, &p, what);
                if (k) {   /* the fade's steps are dirty in the name's box */
                    fk_face_in_t q = p;
                    q.pulse_ms = ms[k - 1];
                    dirty_pair(r, r->ops->draw, &q, &p, what);
                }
            }
            /* the three names differ */
            fk_face_in_t p = rest;
            p.pulsing = true;
            p.pulse_ms = 100;
            row_frame(r, r->ops->draw, &p, s_a);
            p.variant = (uint8_t)((v + 1) % 3);
            row_frame(r, r->ops->draw, &p, s_b);
            TEST_ASSERT_TRUE(vx_differ(s_a, s_b, DW_V12_NAME) > 50);
        }
}

/* the tunnel's motion (mono_ms) is dirty in DW_V12_TUNNEL only: forward, skipped, backward; never the calm centre */
static void vortex_dirty_tunnel(void)
{
    const row_t *r = row_of("vortex");
    static const long step[] = {1, 14, 40, 46, 1000, 61000, -40, -5000};
    for (int v = 0; v < 3; v++)
        for (unsigned k = 0; k < sizeof step / sizeof step[0]; k++) {
            const fk_face_in_t a = vx_in((uint8_t)v, 100000), b = vx_in((uint8_t)v, (uint32_t)(100000 + step[k]));
            char what[64];
            snprintf(what, sizeof what, "palette %d, the tunnel's clock %+ld ms", v, step[k]);
            const long u = dirty_pair(r, r->ops->draw, &a, &b, what);
            TEST_ASSERT_TRUE_MESSAGE(u <= 196420L, what);
        }
    TEST_ASSERT_EQUAL_INT32(196420L, rects_union(DW_V12_TUNNEL, 4));
    const fk_face_in_t a = vx_in(0, 100000), b = vx_in(0, 100040);
    row_frame(r, r->ops->draw, &a, s_a);
    row_frame(r, r->ops->draw, &b, s_b);
    TEST_ASSERT_TRUE(vx_same(s_a, s_b, DW_V12_CENTRE));
    TEST_ASSERT_TRUE(vx_differ(s_a, s_b, (omni_rect_t){0, 0, W, W}) > 50000);
    /* the same rot and rush (1 ms on): nothing */
    const fk_face_in_t c = vx_in(0, 100001);
    omni_rect_t rects[8];
    if (dw_vortex_rot(100000) == dw_vortex_rot(100001) && dw_vortex_rush(100000) == dw_vortex_rush(100001))
        TEST_ASSERT_EQUAL_UINT8(0, r->ops->dirty(r->ctx, &a, &c, rects));
}

/* the wake: the tunnel and the eye fade in by easeOut3 over 900 ms; the text is there from the start; under reduced motion
 * the settled frame; band-exact at every step, as the direct path's 8-row bands draw it */
static void vortex_wake_fade(void)
{
    const row_t *r = row_of("vortex");
    fk_face_in_t s = vx_in(1, 2222);
    row_frame(r, r->ops->draw, &s, s_c);
    fk_face_in_t w = s;
    w.wake_ms = 0;
    row_frame(r, r->ops->draw, &w, s_a);
    long ring = 0;   /* past r 140 (the time's glow ends inside it) */
    for (int i = 0; i < W * W; i++) {
        const int dx = i % W - 233, dy = i / W - 233;
        ring += s_a[i] != 0 && dx * dx + dy * dy > 140 * 140;
    }
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, ring, "at 0 ms the tunnel is dark");
    long lit_c = 0;
    for (int i = 0; i < W * W; i++) lit_c += s_a[i] != 0;
    TEST_ASSERT_TRUE_MESSAGE(lit_c > 1500, "the time shows from the start");
    uint32_t prev = 0;
    for (uint32_t t = 100; t <= 900; t += 200) {
        w.wake_ms = t < 900 ? t : UINT32_MAX;
        row_frame(r, r->ops->draw, &w, s_a);
        uint32_t sum = 0;
        for (int i = 0; i < W * W; i++) sum += (uint32_t)(s_a[i] >> 11);
        TEST_ASSERT_TRUE_MESSAGE(sum >= prev, "the fade only brightens");
        prev = sum;
        in_clips(r, r->ops->draw, &w, 8, W, s_b);
        assert_same(s_a, s_b, "a wake frame in 8-row bands");
    }
    assert_same(s_c, s_a, "settled at the end");
    w.wake_ms = 300;
    w.reduced = true;
    fk_face_in_t sr = s;
    sr.reduced = true;
    row_frame(r, r->ops->draw, &sr, s_c);
    row_frame(r, r->ops->draw, &w, s_a);
    assert_same(s_c, s_a, "reduced: no fade");
    /* a moving, pulsing frame is band-exact too */
    fk_face_in_t m = vx_in(2, 98765);
    m.pulsing = true, m.pulse_ms = 1500, m.wake_ms = 450;
    row_frame(r, r->ops->draw, &m, s_a);
    in_clips(r, r->ops->draw, &m, 8, W, s_b);
    assert_same(s_a, s_b, "moving and pulsing in 8-row bands");
    in_clips(r, r->ops->draw, &m, 6, 37, s_b);
    assert_same(s_a, s_b, "moving and pulsing in 6 x 37 blocks");
}

/* (wave 2 Task 14) a direct frame's input dt ms on: the tunnel's clock moves; a wake and a pulse run on and end at the
 * face's lengths; nothing else changes */
static void face_in_advance(void)
{
    fk_face_in_t a = vx_in(2, 1000), b = a;
    a.wake_ms = 100, a.pulsing = true, a.pulse_ms = 300;
    b = a;
    fk_face_in_advance(&DW_FACE_VORTEX, &b, 46);
    TEST_ASSERT_EQUAL_UINT32(1046, b.mono_ms);
    TEST_ASSERT_EQUAL_UINT32(146, b.wake_ms);
    TEST_ASSERT_TRUE(b.pulsing);
    TEST_ASSERT_EQUAL_UINT16(346, b.pulse_ms);
    b.mono_ms = a.mono_ms, b.wake_ms = a.wake_ms, b.pulse_ms = a.pulse_ms;
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);   /* nothing else */
    b = a;
    fk_face_in_advance(&DW_FACE_VORTEX, &b, 800);   /* the wake (900) ends at 900; the pulse (1900) runs on */
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, b.wake_ms);
    TEST_ASSERT_EQUAL_UINT16(1100, b.pulse_ms);
    fk_face_in_advance(&DW_FACE_VORTEX, &b, 800);
    TEST_ASSERT_FALSE(b.pulsing);
    TEST_ASSERT_EQUAL_UINT16(0, b.pulse_ms);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, b.wake_ms);
    b = a;   /* a settled input stays settled; the tunnel's clock wraps */
    b.wake_ms = UINT32_MAX, b.pulsing = false, b.pulse_ms = 0, b.mono_ms = UINT32_MAX - 10;
    fk_face_in_advance(&DW_FACE_VORTEX, &b, 20);
    TEST_ASSERT_EQUAL_UINT32(9, b.mono_ms);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, b.wake_ms);
    TEST_ASSERT_FALSE(b.pulsing);
    b = a;   /* a long stall ends both */
    fk_face_in_advance(&DW_FACE_VORTEX, &b, 100000);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, b.wake_ms);
    TEST_ASSERT_FALSE(b.pulsing);
}

#define FACE_TESTS(key)                                                                \
    static void test_face_band_exact_##key(void) { band_exact(#key); }                 \
    static void test_face_dirty_covers_##key(void) { dirty_covers(#key); }             \
    static void test_face_glance_##key(void) { glance_dim(#key); }                     \
    static void test_face_ctx_bytes_##key(void) { ctx_bytes(#key); }                   \
    static void test_face_dirty_jumps_##key(void) { dirty_jumps(#key); }               \
    static void test_face_reduced_still_##key(void) { reduced_still(#key); }           \
    static void test_face_glance_day_##key(void) { glance_day(#key); }
FACE_TESTS(crystal)
FACE_TESTS(roundel)
FACE_TESTS(callbox)
FACE_TESTS(rotor)
FACE_TESTS(coral)
FACE_TESTS(circles)
FACE_TESTS(learn)
FACE_TESTS(key)
FACE_TESTS(room)
FACE_TESTS(amber)
FACE_TESTS(fob)
FACE_TESTS(console)
FACE_TESTS(wall)
FACE_TESTS(doorway)
FACE_TESTS(broadcast)
FACE_TESTS(vortex)
/* the key's pages 1 and 2 (page 0 is FACE_TESTS(key)) */
#define KEY_PAGE_TESTS(p)                                                                                   \
    static void test_face_band_exact_key##p(void) { s_page = p, band_exact("key"), s_page = 0; }           \
    static void test_face_dirty_covers_key##p(void) { s_page = p, dirty_covers("key"), s_page = 0; }       \
    static void test_face_glance_key##p(void) { s_page = p, glance_dim("key"), s_page = 0; }               \
    static void test_face_glance_quiet_key##p(void) { s_page = p, glance_quiet("key"), s_page = 0; }       \
    static void test_face_dirty_jumps_key##p(void) { s_page = p, dirty_jumps("key"), s_page = 0; }         \
    static void test_face_reduced_still_key##p(void) { s_page = p, reduced_still("key"), s_page = 0; }     \
    static void test_face_glance_day_key##p(void) { s_page = p, glance_day("key"), s_page = 0; }
KEY_PAGE_TESTS(1)
KEY_PAGE_TESTS(2)
/* page 2 unset: empty rings, the same frame at any time; a set clock draws its reading (the glance, the 0-9 strip, reads
 * no clock on any page) */
static void test_face_unset_key2(void)
{
    s_page = 2;
    const row_t *r = row_of("key");
    s_page = 0;
    fk_face_in_t ref;
    memset(&ref, 0, sizeof ref);
    ref.wake_ms = UINT32_MAX, ref.battery = -1;
    row_frame(r, r->ops->draw, &ref, s_c);
    static const int hms[3][3] = {{10, 9, 30}, {3, 47, 12}, {12, 0, 1}};
    for (int k = 0; k < 3; k++) {
        fk_face_in_t in = in_at(hms[k][0], hms[k][1], hms[k][2], 640);
        in.clk.set = false;
        row_frame(r, r->ops->draw, &in, s_a);
        assert_same(s_c, s_a, "the key's page 3 unset at another time");
        in.clk.set = true;
        row_frame(r, r->ops->draw, &in, s_b);
        long differ = 0;
        for (int i = 0; i < W * W; i++) differ += s_a[i] != s_b[i];
        TEST_ASSERT_TRUE_MESSAGE(differ > 500, "page 3 draws the same set and unset");
        in.glance = true;
        row_frame(r, r->ops->glance, &in, s_a);
        in.clk.set = false;
        row_frame(r, r->ops->glance, &in, s_b);
        assert_same(s_a, s_b, "the key's glance reads no clock");
    }
}
static void test_face_unset_crystal(void) { crystal_unset(); }
static void test_face_stale_crystal(void) { stale_marker_only("crystal", &DW_C1_STALE); }
static void test_face_glance_quiet_crystal(void) { glance_quiet("crystal"); }
static void test_crystal_stale_under_hands(void) { crystal_stale_under_hands(); }
static void test_crystal_tail_end_is_settled(void) { crystal_tail_end_is_settled(); }
static void test_crystal_doors_end_is_still(void) { crystal_doors_end_is_still(); }
static void test_crystal_unset_monitor_cells(void) { crystal_unset_monitor_cells(); }
static void test_crystal_monitor_text(void) { crystal_monitor_text(); }
static void test_crystal_script(void) { crystal_script(); }
static void test_verify_crc_current(void) { verify_crc_current(); }

/* rd wave 1 Task 1 review: fk_test_lit_th counts a pixel lit when any channel is above th; fk_test_lit is it at 12.
 * One pixel at the centre with a channel between 8 and 12 (RGB565 has no channel at exactly 10: green 3 of 63 is
 * 3 x 255 / 63 = 12, the highest value still unlit at 12): lit at th 8, unlit at th 12. */
static void test_fk_test_lit_th(void)
{
    memset(s_a, 0, sizeof s_a);
    s_a[233 * W + 233] = (uint16_t)(3 << 5);
    float p8, p12, p;
    uint8_t m8, m12, m;
    fk_test_lit_th(s_a, 8, &p8, &m8);
    fk_test_lit_th(s_a, 12, &p12, &m12);
    fk_test_lit(s_a, &p, &m);
    TEST_ASSERT_EQUAL_UINT8(12, m8);
    TEST_ASSERT_TRUE(p8 > 0.f);
    TEST_ASSERT_EQUAL_FLOAT(0.f, p12);
    TEST_ASSERT_EQUAL_FLOAT(p12, p);
    TEST_ASSERT_EQUAL_UINT8(m12, m);
    s_a[233 * W + 233] = (uint16_t)(1 << 11);   /* red 1 of 31: 8, unlit at th 8 too */
    fk_test_lit_th(s_a, 8, &p8, &m8);
    TEST_ASSERT_EQUAL_FLOAT(0.f, p8);
}
static void test_verify_crc_supplement(void) { verify_crc_supplement(); }
static void test_face_unset_roundel(void) { unset_timeless("roundel"); }
static void test_face_stale_roundel(void) { stale_marker_only("roundel", &DW_R4_STALE); }
static void test_face_glance_quiet_roundel(void) { glance_quiet("roundel"); }
static void test_roundel_lit_hour(void) { roundel_lit_hour(); }
static void test_second_overshoot(void) { second_overshoot(); }
static void test_face_unset_callbox(void) { unset_timeless("callbox"); }
static void test_face_stale_callbox(void) { stale_marker_only("callbox", &DW_B5_STALE); }
static void test_face_glance_quiet_callbox(void) { glance_quiet("callbox"); }
static void test_callbox_panes(void) { callbox_panes(); }
static void test_callbox_geometry_both_sets(void) { callbox_geometry(); }
static void test_callbox_one_box_asset(void) { callbox_one_box_asset(); }
static void test_callbox_tap_lantern(void) { callbox_tap_lantern(); }
static void test_callbox_lamp(void) { callbox_lamp(); }
static void test_callbox_dirty_regions(void) { callbox_dirty_regions(); }
static void test_roundel_reduced_no_overshoot(void) { roundel_reduced_no_overshoot(); }
static void test_crystal_reduced(void) { crystal_reduced(); }
static void test_callbox_reduced(void) { callbox_reduced(); }
static void test_face_unset_rotor(void) { unset_timeless("rotor"); }
static void test_face_stale_rotor(void) { stale_marker_only("rotor", &DW_T7_STALE); }
static void test_face_glance_quiet_rotor(void) { glance_quiet("rotor"); }
static void test_rotor_motion_confined(void) { rotor_motion_confined(); }
static void test_rotor_lift_period(void) { rotor_lift_period(); }
static void test_rotor_takeoff(void) { rotor_takeoff(); }
static void test_rotor_unset_at_rest(void) { rotor_unset_at_rest(); }
static void test_face_unset_coral(void) { unset_timeless("coral"); }
static void test_face_stale_coral(void) { stale_marker_only("coral", &DW_K11_STALE); }
static void test_face_glance_quiet_coral(void) { glance_quiet("coral"); }
static void test_coral_cells(void) { coral_cells(); }
static void test_coral_underline_no_touch(void) { coral_underline_no_touch(); }
static void test_coral_underline_follows_h12(void) { coral_underline_follows_h12(); }
static void test_coral_motion_confined(void) { coral_motion_confined(); }
static void test_coral_skipped_seconds(void) { coral_skipped_seconds(); }
static void test_coral_glance_worst_case(void) { coral_glance_worst_case(); }
static void test_rotor_reduced_chime_unset_takeoff(void) { rotor_reduced_chime_unset_takeoff(); }
static void test_face_unset_circles(void) { unset_timeless("circles"); }
static void test_face_stale_circles(void) { stale_marker_only("circles", &DW_G14_STALE); }
static void test_face_glance_quiet_circles(void) { glance_quiet("circles"); }
static void test_face_unset_learn(void) { unset_timeless("learn"); }
static void test_face_stale_learn(void) { stale_marker_only("learn", &DW_G16_STALE); }
static void test_face_glance_quiet_learn(void) { glance_quiet("learn"); }
static void test_face_glance_quiet_key(void) { glance_quiet("key"); }
static void test_g14_layout_no_touch(void) { g14_layout_no_touch(); }
static void test_g14_annulus_fits(void)
{
    const size_t n = fk_annulus_bytes(DW_G14_SCRIPT_R0, DW_G14_SCRIPT_R1, DW_G14_SCRIPT_NT);
    TEST_ASSERT_TRUE(n <= 278528u && n > 278528u - 8192u);   /* gface.c's ANN_MAX, not far above the need */
    TEST_ASSERT_TRUE(DW_FACE_CIRCLES.ctx_bytes < 300000u);
}
static void test_g14_band_step(void) { g14_band_step(); }
static void test_g14_24h(void) { g14_24h(); }
static void test_g14_tap_opens_key(void) { g14_tap_opens_key(); }
static void test_g14_motion(void) { g14_motion(); }
static void test_g14_wake_and_reduced(void) { g14_wake_and_reduced(); }
static void test_g14_unset_breath(void) { g14_unset_breath(); }
static void test_key_glyph_size(void) { key_glyph_size(); }
static void test_key_page3_frozen(void) { key_page3_frozen(); }
static void test_key_tap_sum_reduced(void) { key_tap_sum_reduced(); }
static void test_key_page_rows_differ(void) { key_page_rows_differ(); }
static void test_learn_tap(void) { learn_tap(); }
static void test_learn_still(void) { learn_still(); }
static void test_g14_reduced_hint(void) { g14_reduced_hint(); }
static void test_key_page3_reduced(void) { key_page3_reduced(); }
static void test_learn_key_jumps(void) { learn_key_jumps(); }
static void test_face_chime_rest_exact(void) { chime_rest_exact(); }
static void test_face_unset_room(void) { unset_timeless("room"); }
static void test_face_glance_quiet_room(void) { glance_quiet("room"); }
static void test_room_lit_hour(void) { room_lit_hour(); }
static void test_room_rotor_confined(void) { room_rotor_confined(); }
static void test_room_reduced_rotor_still(void) { room_reduced_rotor_still(); }
static void test_room_doors_classic_asset(void) { room_doors_classic_asset(); }
static void test_room_chime_regions(void) { room_chime_regions(); }
static void test_room_unset_no_hands(void) { room_unset_no_hands(); }
static void test_room_stale_rect(void) { room_stale_rect(); }
static void test_room_tail_end_is_settled(void) { room_tail_end_is_settled(); }
static void test_hand_room_band_exact(void) { hand_room_band_exact(); }
static void test_face_unset_amber(void) { unset_timeless("amber"); }
static void test_face_stale_amber(void) { stale_marker_only("amber", &DW_C10_STALE); }
static void test_face_glance_quiet_amber(void) { glance_quiet("amber"); }
static void test_amber_sweep_rects(void) { amber_sweep_rects(); }
static void test_amber_script_step(void) { amber_script_step(); }
static void test_amber_reduced_steps(void) { amber_reduced_steps(); }
static void test_amber_hands_per_minute(void) { amber_hands_per_minute(); }
static void test_hand_wand_band_exact(void) { hand_wand_band_exact(); }
static void test_amber_sweep_and_pulse(void) { amber_sweep_and_pulse(); }
static void test_amber_wake(void) { amber_wake(); }
static void test_amber_unset_text(void) { amber_unset_text(); }
static void test_amber_annulus_fits(void) { amber_annulus_fits(); }
static void test_face_unset_fob(void) { unset_timeless("fob"); }
static void test_face_stale_fob(void) { stale_marker_only("fob", &DW_F13_STALE); }
static void test_face_glance_quiet_fob(void) { glance_quiet("fob"); }
static void test_fob_burn_in(void) { fob_burn_in(); }
static void test_fob_retap_guard(void) { fob_retap_guard(); }
static void test_fob_tap_reopens(void) { fob_tap_reopens(); }
static void test_fob_lid_gone(void) { fob_lid_gone(); }
static void test_fob_sub_ticks(void) { fob_sub_ticks(); }
static void test_fob_rest_dirty(void) { fob_rest_dirty(); }
static void test_fob_reduced_fade(void) { fob_reduced_fade(); }
static void test_fob_wake_band_exact(void) { fob_wake_band_exact(); }
static void test_fob_pulse_over_wake(void) { fob_pulse_over_wake(); }
static void test_fob_wake(void) { fob_wake(); }
static void test_fob_unset_text(void) { fob_unset_text(); }
static void test_hand_breguet_band_exact(void) { hand_breguet_band_exact(); }
static void test_face_unset_console(void) { unset_timeless("console"); }
static void test_face_stale_console(void) { console_stale(); }
static void test_face_glance_quiet_console(void) { glance_quiet("console"); }
static void test_console_geometry(void) { console_geometry(); }
static void test_console_regions(void) { console_regions(); }
static void test_console_unset(void) { console_unset(); }
static void test_console_solar(void) { console_solar(); }
static void test_console_seconds_tick(void) { console_seconds_tick(); }
static void test_console_reduced_lamp_steady(void) { console_reduced_lamp_steady(); }
static void test_console_wake(void) { console_wake(); }
static void test_console_centre(void) { console_centre(); }
static void test_p9_shapes_band_exact(void) { p9_shapes_band_exact(); }
static void test_sun_today_local_date(void) { sun_today_local_date(); }
static void test_face_unset_wall(void) { unset_timeless("wall"); }
static void test_face_stale_wall(void) { stale_marker_only("wall", &DW_W6_STALE); }
static void test_face_glance_quiet_wall(void) { glance_quiet("wall"); }
static void test_wall_sixty_cells(void) { wall_sixty_cells(); }
static void test_wall_minute_cells(void) { wall_minute_cells(); }
static void test_wall_regions(void) { wall_regions(); }
static void test_wall_dirty_fallback(void) { wall_dirty_fallback(); }
static void test_wall_unset_breath(void) { wall_unset_breath(); }
static void test_wall_hour_dirty_whole(void) { wall_hour_dirty_whole(); }
static void test_wall_reduced_steady(void) { wall_reduced_steady(); }
static void test_wall_wake(void) { wall_wake(); }
static void test_wall_glance_worst_59(void) { wall_glance_worst_59(); }
static void test_face_unset_doorway(void) { unset_timeless("doorway"); }
static void test_face_stale_doorway(void) { stale_marker_only("doorway", &DW_D3_STALE); }
static void test_face_glance_quiet_doorway(void) { glance_quiet("doorway"); }
static void test_doorway_registered(void) { doorway_registered(); }
static void test_doorway_doors_live_omits_leaves(void) { doorway_doors_live_omits_leaves(); }
static void test_doorway_end_seam_both_sets(void) { doorway_end_seam_both_sets(); }
static void test_doorway_reduced_keeps_leaves(void) { doorway_reduced_keeps_leaves(); }
static void test_doorway_handoff_frames(void) { doorway_handoff_frames(); }
static void test_doorway_text_threshold_sweep(void) { doorway_text_threshold_sweep(); }
static void test_doorway_column_confined(void) { doorway_column_confined(); }
static void test_doorway_text_radius(void) { doorway_text_radius(); }
static void test_doorway_thumb_generic(void) { doorway_thumb_generic(); }
static void test_doorway_chime_regions(void) { doorway_chime_regions(); }
static void test_doorway_tail(void) { doorway_tail(); }
static void test_doorway_glance_seam(void) { doorway_glance_seam(); }
static void test_face_unset_broadcast(void) { unset_timeless("broadcast"); }
static void test_face_stale_broadcast(void) { stale_marker_only("broadcast", &DW_B8_STALE); }
static void test_face_glance_quiet_broadcast(void) { glance_quiet("broadcast"); }
static void test_broadcast_registered(void) { broadcast_registered(); }
static void test_broadcast_wake_timer(void) { broadcast_wake_timer(); }
static void test_broadcast_ring_period(void) { broadcast_ring_period(); }
static void test_broadcast_scanlines(void) { broadcast_scanlines(); }
static void test_broadcast_switch_on_band_exact(void) { broadcast_switch_on_band_exact(); }
static void test_broadcast_reduced_still(void) { broadcast_reduced_still(); }
static void test_broadcast_hum_bar(void) { broadcast_hum_bar(); }
static void test_broadcast_switch_on_frames(void) { broadcast_switch_on_frames(); }
static void test_broadcast_dirty_seconds(void) { broadcast_dirty_seconds(); }
static void test_face_unset_vortex(void) { unset_timeless("vortex"); }
static void test_face_stale_vortex(void) { stale_marker_only("vortex", &DW_V12_STALE); }
static void test_face_glance_quiet_vortex(void) { glance_quiet("vortex"); }
static /* (the final fix wave) face_generic works out today's sunrise and sunset only for a face with ops->uses_sun; every
 * other face gets -1. So a face whose frames (draw or glance, on every set still and at midday) read them must say so,
 * and #9 (SOLAR) is the one that does. */
void test_face_uses_sun_flag(void)
{
    unsigned readers = 0;
    for (unsigned i = 0; i < s_nrows; i++) {
        const row_t *r = &s_rows[i];
        bool reads = false;
        for (unsigned k = 0; k <= DW_STILL_N && !reads; k++) {
            fk_face_in_t in;
            bool glance = false;
            if (k < DW_STILL_N) {
                dw_still_t st;
                dw_still(k, &st);
                if (!st.in.clk.set) continue;
                in = st.in, glance = st.glance;
            } else {
                in = in_at(13, 0, 0, 0), in.sun_rise_m = 420, in.sun_set_m = 1140;
            }
            fk_face_in_t none = in;
            none.sun_rise_m = none.sun_set_m = -1;
            const draw_fn fn = glance ? r->ops->glance : r->ops->draw;
            row_frame(r, fn, &in, s_a);
            row_frame(r, fn, &none, s_b);
            reads = memcmp(s_a, s_b, sizeof s_a) != 0;
        }
        char m[96];
        snprintf(m, sizeof m, "%s (page %u): reads the day's sun %d, uses_sun %d", r->ops->key, r->page, reads,
                 r->ops->uses_sun);
        TEST_ASSERT_TRUE_MESSAGE(reads == r->ops->uses_sun, m);
        readers += reads && r->page == 0;
    }
    TEST_ASSERT_TRUE(DW_FACE_CONSOLE.uses_sun);
    TEST_ASSERT_EQUAL_UINT(1, readers);
}

void test_vortex_registered(void) { vortex_registered(); }
static void test_vortex_tap_cycles(void) { vortex_tap_cycles(); }
static void test_vortex_palette_variant(void) { vortex_palette_variant(); }
static void test_vortex_reduced_still(void) { vortex_reduced_still(); }
static void test_vortex_name_pulse(void) { vortex_name_pulse(); }
static void test_vortex_dirty_tunnel(void) { vortex_dirty_tunnel(); }
static void test_vortex_wake_fade(void) { vortex_wake_fade(); }
static void test_face_in_advance(void) { face_in_advance(); }
/* (Task 14 review) FK_TAP_CYCLE's next variant wraps modulo 255, a multiple of 3: every tap moves #12 to the next palette,
 * across the wrap too (a uint8_t's 255 -> 0 would repeat palette 0 at the 256th tap) */
static void test_variant_next(void)
{
    TEST_ASSERT_EQUAL_INT(0, 255 % DW_V12_PALETTES);
    uint8_t v = 0;
    for (int tap = 1; tap <= 3 * 255 + 7; tap++) {
        const uint8_t n = fk_variant_next(v);
        TEST_ASSERT_TRUE(n < 255);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((dw_v12_palette(&(fk_face_in_t){.variant = v}) + 1) % DW_V12_PALETTES,
                                        dw_v12_palette(&(fk_face_in_t){.variant = n}), "each tap is the next palette");
        v = n;
    }
    TEST_ASSERT_EQUAL_UINT8(0, fk_variant_next(254));
    TEST_ASSERT_EQUAL_UINT8(1, fk_variant_next(0));
}

int main(void)
{
#if defined(DW_FACES_OVERLAY) && !defined(DW_ART_DIR)
    printf("test_dw_faces_overlay: no -DDW_ART_OVERLAY at configure: skipped\n");
    return 77;
#else
#if defined(DW_FACES_OVERLAY)
    const bool opened = dw_assets_host_open(DW_ASSETS_DIR, DW_ART_DIR "/assets");
#else
    const bool opened = dw_assets_host_open(DW_ASSETS_DIR, NULL);
#endif
    UNITY_BEGIN();
    if (!opened) TEST_FAIL_MESSAGE("assets dir");
    bind_all();
#if defined(DW_FACES_MOCKUP)
    /* test_dw_faces_mockup (wave 2 Task 11, R147 PR11): #3's doors seams on Designer A's 2.3 s timeline only */
    TEST_ASSERT_EQUAL_UINT32(2300, DW_DOORS_END_MS);
    RUN_TEST(test_doorway_end_seam_both_sets);
    RUN_TEST(test_doorway_reduced_keeps_leaves);
    RUN_TEST(test_doorway_handoff_frames);
    RUN_TEST(test_doorway_text_threshold_sweep);
    RUN_TEST(test_doorway_tail);
    {
        const int rc = UNITY_END();
        dw_assets_host_close();
        return rc;
    }
#endif
#if defined(DW_FACES_OVERLAY)
    {   /* the staged private set really is the overlay's: its doors came from there */
        size_t n;
        TEST_ASSERT_NOT_NULL(dw_assets_host_get("dw_doors_m.bin", &n));
        TEST_ASSERT_TRUE(dw_assets_host_from_overlay("dw_doors_m.bin"));
    }
#endif
    RUN_TEST(test_crc32_is_zlibs);
    RUN_TEST(test_sincos_table);
    RUN_TEST(test_clock_from);
    RUN_TEST(test_stills_pinned);
    RUN_TEST(test_hand_shard_band_exact);
    RUN_TEST(test_hand_grow_zero_draws_nothing);
    RUN_TEST(test_glow_sym_and_lut);
    RUN_TEST(test_glow_coarse_large_r);
    RUN_TEST(test_face_band_exact_crystal);
    RUN_TEST(test_face_dirty_covers_crystal);
    RUN_TEST(test_face_glance_crystal);
    RUN_TEST(test_face_unset_crystal);
    RUN_TEST(test_face_stale_crystal);
    RUN_TEST(test_face_glance_quiet_crystal);
    RUN_TEST(test_crystal_stale_under_hands);
    RUN_TEST(test_crystal_tail_end_is_settled);
    RUN_TEST(test_crystal_doors_end_is_still);
    RUN_TEST(test_crystal_unset_monitor_cells);
    RUN_TEST(test_face_ctx_bytes_crystal);
    RUN_TEST(test_crystal_monitor_text);
    RUN_TEST(test_crystal_script);
    RUN_TEST(test_glow_lut_tables);
    RUN_TEST(test_hand_baton_and_second_band_exact);
    RUN_TEST(test_second_overshoot);
    RUN_TEST(test_face_band_exact_roundel);
    RUN_TEST(test_face_dirty_covers_roundel);
    RUN_TEST(test_face_glance_roundel);
    RUN_TEST(test_face_ctx_bytes_roundel);
    RUN_TEST(test_face_unset_roundel);
    RUN_TEST(test_face_stale_roundel);
    RUN_TEST(test_face_glance_quiet_roundel);
    RUN_TEST(test_roundel_lit_hour);
    RUN_TEST(test_face_band_exact_callbox);
    RUN_TEST(test_face_dirty_covers_callbox);
    RUN_TEST(test_face_glance_callbox);
    RUN_TEST(test_face_ctx_bytes_callbox);
    RUN_TEST(test_face_unset_callbox);
    RUN_TEST(test_face_stale_callbox);
    RUN_TEST(test_face_glance_quiet_callbox);
    RUN_TEST(test_callbox_panes);
    RUN_TEST(test_callbox_geometry_both_sets);
    RUN_TEST(test_callbox_one_box_asset);
    RUN_TEST(test_callbox_tap_lantern);
    RUN_TEST(test_callbox_lamp);
    RUN_TEST(test_callbox_dirty_regions);
    RUN_TEST(test_roundel_reduced_no_overshoot);
    RUN_TEST(test_crystal_reduced);
    RUN_TEST(test_callbox_reduced);
    RUN_TEST(test_face_band_exact_rotor);
    RUN_TEST(test_face_dirty_covers_rotor);
    RUN_TEST(test_face_glance_rotor);
    RUN_TEST(test_face_ctx_bytes_rotor);
    RUN_TEST(test_face_unset_rotor);
    RUN_TEST(test_face_stale_rotor);
    RUN_TEST(test_face_glance_quiet_rotor);
    RUN_TEST(test_rotor_motion_confined);
    RUN_TEST(test_rotor_lift_period);
    RUN_TEST(test_rotor_takeoff);
    RUN_TEST(test_rotor_unset_at_rest);
    RUN_TEST(test_face_band_exact_coral);
    RUN_TEST(test_face_dirty_covers_coral);
    RUN_TEST(test_face_glance_coral);
    RUN_TEST(test_face_ctx_bytes_coral);
    RUN_TEST(test_face_unset_coral);
    RUN_TEST(test_face_stale_coral);
    RUN_TEST(test_face_glance_quiet_coral);
    RUN_TEST(test_coral_cells);
    RUN_TEST(test_coral_underline_no_touch);
    RUN_TEST(test_coral_underline_follows_h12);
    RUN_TEST(test_coral_motion_confined);
    RUN_TEST(test_coral_skipped_seconds);
    RUN_TEST(test_coral_glance_worst_case);
    RUN_TEST(test_rotor_reduced_chime_unset_takeoff);
    RUN_TEST(test_face_band_exact_circles);
    RUN_TEST(test_face_dirty_covers_circles);
    RUN_TEST(test_face_glance_circles);
    RUN_TEST(test_face_ctx_bytes_circles);
    RUN_TEST(test_face_unset_circles);
    RUN_TEST(test_face_stale_circles);
    RUN_TEST(test_face_glance_quiet_circles);
    RUN_TEST(test_g14_layout_no_touch);
    RUN_TEST(test_g14_annulus_fits);
    RUN_TEST(test_g14_band_step);
    RUN_TEST(test_g14_24h);
    RUN_TEST(test_g14_tap_opens_key);
    RUN_TEST(test_g14_motion);
    RUN_TEST(test_g14_wake_and_reduced);
    RUN_TEST(test_g14_unset_breath);
    RUN_TEST(test_face_band_exact_learn);
    RUN_TEST(test_face_dirty_covers_learn);
    RUN_TEST(test_face_glance_learn);
    RUN_TEST(test_face_ctx_bytes_learn);
    RUN_TEST(test_face_unset_learn);
    RUN_TEST(test_face_stale_learn);
    RUN_TEST(test_face_glance_quiet_learn);
    RUN_TEST(test_learn_tap);
    RUN_TEST(test_learn_still);
    RUN_TEST(test_face_band_exact_key);
    RUN_TEST(test_face_dirty_covers_key);
    RUN_TEST(test_face_glance_key);
    RUN_TEST(test_face_ctx_bytes_key);
    RUN_TEST(test_face_glance_quiet_key);
    RUN_TEST(test_face_band_exact_key1);
    RUN_TEST(test_face_dirty_covers_key1);
    RUN_TEST(test_face_glance_key1);
    RUN_TEST(test_face_glance_quiet_key1);
    RUN_TEST(test_face_band_exact_key2);
    RUN_TEST(test_face_dirty_covers_key2);
    RUN_TEST(test_face_glance_key2);
    RUN_TEST(test_face_glance_quiet_key2);
    RUN_TEST(test_face_unset_key2);
    RUN_TEST(test_key_glyph_size);
    RUN_TEST(test_key_page3_frozen);
    RUN_TEST(test_key_tap_sum_reduced);
    RUN_TEST(test_key_page_rows_differ);
    RUN_TEST(test_g14_reduced_hint);
    RUN_TEST(test_key_page3_reduced);
    RUN_TEST(test_learn_key_jumps);
    RUN_TEST(test_face_chime_rest_exact);
    /* wave 1's lessons on every row (dw wave 2 Task 2) */
    RUN_TEST(test_face_dirty_jumps_crystal);
    RUN_TEST(test_face_reduced_still_crystal);
    RUN_TEST(test_face_glance_day_crystal);
    RUN_TEST(test_face_dirty_jumps_roundel);
    RUN_TEST(test_face_reduced_still_roundel);
    RUN_TEST(test_face_glance_day_roundel);
    RUN_TEST(test_face_dirty_jumps_callbox);
    RUN_TEST(test_face_reduced_still_callbox);
    RUN_TEST(test_face_glance_day_callbox);
    RUN_TEST(test_face_dirty_jumps_rotor);
    RUN_TEST(test_face_reduced_still_rotor);
    RUN_TEST(test_face_glance_day_rotor);
    RUN_TEST(test_face_dirty_jumps_coral);
    RUN_TEST(test_face_reduced_still_coral);
    RUN_TEST(test_face_glance_day_coral);
    RUN_TEST(test_face_dirty_jumps_circles);
    RUN_TEST(test_face_reduced_still_circles);
    RUN_TEST(test_face_glance_day_circles);
    RUN_TEST(test_face_dirty_jumps_learn);
    RUN_TEST(test_face_reduced_still_learn);
    RUN_TEST(test_face_glance_day_learn);
    RUN_TEST(test_face_dirty_jumps_key);
    RUN_TEST(test_face_reduced_still_key);
    RUN_TEST(test_face_glance_day_key);
    RUN_TEST(test_face_dirty_jumps_key1);
    RUN_TEST(test_face_reduced_still_key1);
    RUN_TEST(test_face_glance_day_key1);
    RUN_TEST(test_face_dirty_jumps_key2);
    RUN_TEST(test_face_reduced_still_key2);
    RUN_TEST(test_face_glance_day_key2);
    RUN_TEST(test_face_band_exact_room);
    RUN_TEST(test_face_dirty_covers_room);
    RUN_TEST(test_face_glance_room);
    RUN_TEST(test_face_ctx_bytes_room);
    RUN_TEST(test_face_unset_room);
    RUN_TEST(test_face_glance_quiet_room);
    RUN_TEST(test_face_dirty_jumps_room);
    RUN_TEST(test_face_reduced_still_room);
    RUN_TEST(test_face_glance_day_room);
    RUN_TEST(test_hand_room_band_exact);
    RUN_TEST(test_room_lit_hour);
    RUN_TEST(test_room_rotor_confined);
    RUN_TEST(test_room_reduced_rotor_still);
    RUN_TEST(test_room_doors_classic_asset);
    RUN_TEST(test_room_chime_regions);
    RUN_TEST(test_room_unset_no_hands);
    RUN_TEST(test_room_stale_rect);
    RUN_TEST(test_room_tail_end_is_settled);
    RUN_TEST(test_face_band_exact_amber);
    RUN_TEST(test_face_dirty_covers_amber);
    RUN_TEST(test_face_glance_amber);
    RUN_TEST(test_face_ctx_bytes_amber);
    RUN_TEST(test_face_unset_amber);
    RUN_TEST(test_face_stale_amber);
    RUN_TEST(test_face_glance_quiet_amber);
    RUN_TEST(test_face_dirty_jumps_amber);
    RUN_TEST(test_face_reduced_still_amber);
    RUN_TEST(test_face_glance_day_amber);
    RUN_TEST(test_hand_wand_band_exact);
    RUN_TEST(test_amber_sweep_rects);
    RUN_TEST(test_amber_script_step);
    RUN_TEST(test_amber_reduced_steps);
    RUN_TEST(test_amber_hands_per_minute);
    RUN_TEST(test_amber_sweep_and_pulse);
    RUN_TEST(test_amber_wake);
    RUN_TEST(test_amber_unset_text);
    RUN_TEST(test_amber_annulus_fits);
    RUN_TEST(test_face_band_exact_fob);
    RUN_TEST(test_face_dirty_covers_fob);
    RUN_TEST(test_face_glance_fob);
    RUN_TEST(test_face_ctx_bytes_fob);
    RUN_TEST(test_face_unset_fob);
    RUN_TEST(test_face_stale_fob);
    RUN_TEST(test_face_glance_quiet_fob);
    RUN_TEST(test_face_dirty_jumps_fob);
    RUN_TEST(test_face_reduced_still_fob);
    RUN_TEST(test_face_glance_day_fob);
    RUN_TEST(test_hand_breguet_band_exact);
    RUN_TEST(test_fob_burn_in);
    RUN_TEST(test_fob_retap_guard);
    RUN_TEST(test_fob_tap_reopens);
    RUN_TEST(test_fob_lid_gone);
    RUN_TEST(test_fob_sub_ticks);
    RUN_TEST(test_fob_rest_dirty);
    RUN_TEST(test_fob_reduced_fade);
    RUN_TEST(test_fob_wake_band_exact);
    RUN_TEST(test_fob_pulse_over_wake);
    RUN_TEST(test_fob_wake);
    RUN_TEST(test_fob_unset_text);
    RUN_TEST(test_face_band_exact_console);
    RUN_TEST(test_face_dirty_covers_console);
    RUN_TEST(test_face_glance_console);
    RUN_TEST(test_face_ctx_bytes_console);
    RUN_TEST(test_face_unset_console);
    RUN_TEST(test_face_stale_console);
    RUN_TEST(test_face_glance_quiet_console);
    RUN_TEST(test_face_dirty_jumps_console);
    RUN_TEST(test_face_reduced_still_console);
    RUN_TEST(test_face_glance_day_console);
    RUN_TEST(test_p9_shapes_band_exact);
    RUN_TEST(test_console_geometry);
    RUN_TEST(test_console_regions);
    RUN_TEST(test_console_unset);
    RUN_TEST(test_console_solar);
    RUN_TEST(test_console_seconds_tick);
    RUN_TEST(test_console_reduced_lamp_steady);
    RUN_TEST(test_console_wake);
    RUN_TEST(test_console_centre);
    RUN_TEST(test_sun_today_local_date);
    RUN_TEST(test_face_band_exact_wall);
    RUN_TEST(test_face_dirty_covers_wall);
    RUN_TEST(test_face_glance_wall);
    RUN_TEST(test_face_ctx_bytes_wall);
    RUN_TEST(test_face_unset_wall);
    RUN_TEST(test_face_stale_wall);
    RUN_TEST(test_face_glance_quiet_wall);
    RUN_TEST(test_face_dirty_jumps_wall);
    RUN_TEST(test_face_reduced_still_wall);
    RUN_TEST(test_face_glance_day_wall);
    RUN_TEST(test_wall_sixty_cells);
    RUN_TEST(test_wall_minute_cells);
    RUN_TEST(test_wall_regions);
    RUN_TEST(test_wall_dirty_fallback);
    RUN_TEST(test_wall_unset_breath);
    RUN_TEST(test_wall_hour_dirty_whole);
    RUN_TEST(test_wall_reduced_steady);
    RUN_TEST(test_wall_wake);
    RUN_TEST(test_wall_glance_worst_59);
    RUN_TEST(test_face_band_exact_doorway);
    RUN_TEST(test_face_dirty_covers_doorway);
    RUN_TEST(test_face_glance_doorway);
    RUN_TEST(test_face_ctx_bytes_doorway);
    RUN_TEST(test_face_unset_doorway);
    RUN_TEST(test_face_stale_doorway);
    RUN_TEST(test_face_glance_quiet_doorway);
    RUN_TEST(test_face_dirty_jumps_doorway);
    RUN_TEST(test_face_reduced_still_doorway);
    RUN_TEST(test_face_glance_day_doorway);
    RUN_TEST(test_doorway_registered);
    RUN_TEST(test_doorway_doors_live_omits_leaves);
    RUN_TEST(test_doorway_end_seam_both_sets);
    RUN_TEST(test_doorway_reduced_keeps_leaves);
    RUN_TEST(test_doorway_handoff_frames);
    RUN_TEST(test_doorway_text_threshold_sweep);
    RUN_TEST(test_doorway_column_confined);
    RUN_TEST(test_doorway_text_radius);
    RUN_TEST(test_doorway_thumb_generic);
    RUN_TEST(test_doorway_chime_regions);
    RUN_TEST(test_doorway_tail);
    RUN_TEST(test_doorway_glance_seam);
    RUN_TEST(test_face_band_exact_broadcast);
    RUN_TEST(test_face_dirty_covers_broadcast);
    RUN_TEST(test_face_glance_broadcast);
    RUN_TEST(test_face_ctx_bytes_broadcast);
    RUN_TEST(test_face_unset_broadcast);
    RUN_TEST(test_face_stale_broadcast);
    RUN_TEST(test_face_glance_quiet_broadcast);
    RUN_TEST(test_face_dirty_jumps_broadcast);
    RUN_TEST(test_face_reduced_still_broadcast);
    RUN_TEST(test_face_glance_day_broadcast);
    RUN_TEST(test_broadcast_registered);
    RUN_TEST(test_broadcast_wake_timer);
    RUN_TEST(test_broadcast_ring_period);
    RUN_TEST(test_broadcast_scanlines);
    RUN_TEST(test_broadcast_switch_on_band_exact);
    RUN_TEST(test_broadcast_reduced_still);
    RUN_TEST(test_broadcast_hum_bar);
    RUN_TEST(test_broadcast_switch_on_frames);
    RUN_TEST(test_broadcast_dirty_seconds);
    RUN_TEST(test_face_band_exact_vortex);
    RUN_TEST(test_face_dirty_covers_vortex);
    RUN_TEST(test_face_glance_vortex);
    RUN_TEST(test_face_ctx_bytes_vortex);
    RUN_TEST(test_face_unset_vortex);
    RUN_TEST(test_face_stale_vortex);
    RUN_TEST(test_face_glance_quiet_vortex);
    RUN_TEST(test_face_dirty_jumps_vortex);
    RUN_TEST(test_face_reduced_still_vortex);
    RUN_TEST(test_face_glance_day_vortex);
    RUN_TEST(test_face_uses_sun_flag);
    RUN_TEST(test_vortex_registered);
    RUN_TEST(test_vortex_tap_cycles);
    RUN_TEST(test_vortex_palette_variant);
    RUN_TEST(test_vortex_reduced_still);
    RUN_TEST(test_vortex_name_pulse);
    RUN_TEST(test_vortex_dirty_tunnel);
    RUN_TEST(test_vortex_wake_fade);
    RUN_TEST(test_face_in_advance);
    RUN_TEST(test_variant_next);
    RUN_TEST(test_faces_no_numerals_wave2);
    RUN_TEST(test_verify_crc_current);
    RUN_TEST(test_fk_test_lit_th);
    RUN_TEST(test_verify_crc_supplement);
    const int rc = UNITY_END();
    dw_assets_host_close();
    return rc;
#endif
}
