/* dw_core's doors (dw/doors.h; dw wave 1 Task 5, spec §10, §11): the column maps, the timeline and the band compositor.
 * Built twice (CMakeLists.txt): test_dw_doors on the device default timeline, and test_dw_doors_mockup with its own
 * doors.c and DW_DOORS_MOCKUP=1 (Designer A's 2.3 s A/B), as test_lid_mockup is built.
 * Every image here is synthetic (R132): the code works for any door art of the declared geometry, so the tests use a
 * pattern in which every source column and row is distinct, never the shipped or the private doors.
 * Built twice more (Task 11): test_dw_doors_real runs the closed-doors and band-split cases over the committed generic
 * dw_doors_m.bin and dw_glow.bin, and test_dw_doors_real_overlay over the private overlay's doors (skipped, 77,
 * without -DDW_ART_OVERLAY at configure). Wave 2 Task 11: test_dw_doors_libm builds doors.c with sinf and cosf off
 * (doors_libm_perturb.c) and runs the kept leaves' case: their still geometry takes no libm. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "unity.h"
#include "dw/doors.h"
#include "dw/faces.h"   /* fk_crc32 */
#include "omni/blend565.h"
#include "omni/lid.h"
#if defined(DW_DOORS_DIGEST_GEN)
#pragma GCC diagnostic ignored "-Wunused-function"   /* this build prints the digest only */
#endif
#if defined(DW_DOORS_LIBM_PERTURB)
#pragma GCC diagnostic ignored "-Wunused-function"   /* this build runs the kept leaves' case only */
#endif
#if defined(DW_DOORS_REAL)
#include "dw_assets_host.h"
#pragma GCC diagnostic ignored "-Wunused-function"   /* this build runs two of the cases */
#endif

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define D2R 0.017453292519943295f
#if defined(DW_DOORS_MOCKUP) && DW_DOORS_MOCKUP
#define MOCKUP 1
#else
#define MOCKUP 0
#endif

static uint16_t s_door[W * W], s_interior[W * W], s_a[W * W], s_b[W * W];
static uint8_t s_glow[64 * 256];
static fk_col_t s_cols[W];

static uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

/* Synthetic art: the door's red follows the column and its green the row (each source column and row distinct, so a
 * wrong u or v shows); the interior is another pattern that differs from the door almost everywhere. The glow is
 * glowColumn's shape: alpha stops 0 / .35 / 1 / .35 / 0 across, and 0 -> 1 -> 1 -> 0 at 0 / .18 / .82 / 1 down. */
static void make_images(void)
{
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            s_door[y * W + x] = (uint16_t)(rgb565((x * 255) / 465, (y * 255) / 465, 40) ^ (uint16_t)((x ^ y) & 7));
            s_interior[y * W + x] = rgb565((y * 3 + x) & 255, 200 - (x & 127), (x * 5 + y) & 255);
        }
    static const float P[5] = { 0, 0.38f, 0.5f, 0.62f, 1 }, A[5] = { 0, 0.35f, 1, 0.35f, 0 };
    for (int sy = 0; sy < 256; sy++) {
        const float t = ((float)sy + 0.5f) / 256.0f;
        const float v = t < 0.18f ? t / 0.18f : t > 0.82f ? (1 - t) / 0.18f : 1.0f;
        for (int sx = 0; sx < 64; sx++) {
            const float s = ((float)sx + 0.5f) / 64.0f;
            int i = 1;
            while (i < 4 && s > P[i]) i++;
            const float h = A[i - 1] + (A[i] - A[i - 1]) * (s - P[i - 1]) / (P[i] - P[i - 1]);
            s_glow[sy * 64 + sx] = (uint8_t)floorf(h * v * 255.0f + 0.5f);
        }
    }
}

/* The frame for st (its own column maps) built in bands of `rows` rows and `cols_w` columns into out (stride W). */
static void frame(const fk_cols_state_t *st, int rows, int cols_w, uint16_t *out)
{
    dw_doors_cols(st->th_l, st->th_r, s_cols);
    for (int y0 = 0; y0 < W; y0 += rows)
        for (int x0 = 0; x0 < W; x0 += cols_w) {
            const omni_rect_t b = { (int16_t)x0, (int16_t)y0, (int16_t)(x0 + cols_w < W ? x0 + cols_w : W),
                                    (int16_t)(y0 + rows < W ? y0 + rows : W) };
            fk_cols_band(&DW_DOORS_OUT, st, s_cols, s_door, s_interior, s_glow, b, out + (size_t)y0 * W);
        }
}

/* the round panel (Task 12), in double: pixel centre inside the 466 px circle */
static bool on_panel(int x, int y)
{
    const double dx = x + 0.5 - 233.0, dy = y + 0.5 - 233.0;
    return dx * dx + dy * dy < 233.0 * 233.0;
}

/* got is want on the panel and black off it (the doors draw nothing off the round panel) */
static void assert_frame_is(const uint16_t *want, const uint16_t *got)
{
    for (int i = 0; i < W * W; i++)
        if ((on_panel(i % W, i / W) ? want[i] : 0) != got[i]) {
            char m[96];
            snprintf(m, sizeof m, "pixel (%d, %d): want %04x got %04x", i % W, i / W, want[i], got[i]);
            TEST_FAIL_MESSAGE(m);
        }
}

static double free_edge(float th)   /* the left leaf's free edge on screen (drawLeaf's proj(233)) */
{
    const double s = 560.0 / (560.0 - 233.0 * sin(th));
    return 233.0 + (233.0 * cos(th) - 233.0) * s;
}

/* ---- the column maps ---- */

static void test_door_gone_angle(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 67.38f, omni_hinge_gone_rad(&FK_COLS_HINGE) / D2R);
    TEST_ASSERT_EQUAL_INT(OMNI_HINGE_VERTICAL, FK_COLS_HINGE.axis);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, FK_COLS_HINGE.pos);
    TEST_ASSERT_EQUAL_FLOAT(560.0f, FK_COLS_HINGE.focal);
    TEST_ASSERT_EQUAL_INT(466, FK_COLS_HINGE.n);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, FK_COLS_HINGE.dome_r);
}

static void test_cols_closed(void)
{
    dw_doors_cols(0, 0, s_cols);
    for (int x = 0; x < W; x++) {
        TEST_ASSERT_EQUAL_INT(x, s_cols[x].u);
        TEST_ASSERT_EQUAL_UINT32(65536u, s_cols[x].inv_s_q16);
        TEST_ASSERT_EQUAL_UINT8(0, s_cols[x].shade);
    }
}

static void test_cols_mirror(void)
{
    static const float th[] = { 2.0f, 10.0f, 33.3f, 50.0f, 66.0f };
    for (unsigned k = 0; k < sizeof th / sizeof th[0]; k++) {
        dw_doors_cols(th[k] * D2R, th[k] * D2R, s_cols);
        for (int x = 0; x < FK_LEAF_W; x++) {
            const fk_col_t *l = &s_cols[x], *r = &s_cols[465 - x];
            if (l->u < 0) {
                TEST_ASSERT_TRUE(r->u < 0);
                continue;
            }
            TEST_ASSERT_EQUAL_INT(465 - l->u, r->u);
            TEST_ASSERT_EQUAL_UINT32(l->inv_s_q16, r->inv_s_q16);
            TEST_ASSERT_EQUAL_UINT8(l->shade, r->shade);
        }
    }
}

static void test_cols_monotonic(void)
{
    for (float deg = 0.5f; deg < 67.0f; deg += 0.5f) {
        const float th = deg * D2R;
        dw_doors_cols(th, th, s_cols);
        const double edge = free_edge(th);
        int last = -1;
        for (int x = 0; x < FK_LEAF_W; x++) {
            const int u = s_cols[x].u;
            if ((double)x + 0.5 > edge + 1e-3) TEST_ASSERT_TRUE_MESSAGE(u < 0, "a leaf past the free edge");
            if ((double)x + 0.5 < edge - 1e-3) TEST_ASSERT_TRUE_MESSAGE(u >= 0, "a hole inside the leaf");
            if (u < 0) continue;
            TEST_ASSERT_TRUE(u < FK_LEAF_W);
            TEST_ASSERT_TRUE(u >= last);   /* the left leaf: u rises from the hinge */
            last = u;
            TEST_ASSERT_TRUE(s_cols[x].inv_s_q16 <= 65536u);   /* outward: the leaf only grows, so every row is on it */
        }
        last = 466;
        for (int x = W - 1; x >= FK_LEAF_W; x--) {   /* the right leaf: u falls from the hinge at 466 */
            const int u = s_cols[x].u;
            if ((double)(465 - x) + 0.5 > edge + 1e-3) TEST_ASSERT_TRUE(u < 0);
            if (u < 0) continue;
            TEST_ASSERT_TRUE(u >= FK_LEAF_W && u <= last);
            last = u;
        }
    }
}

/* Task 12: dw_doors_cols works the columns out itself (without the lid's strip shading); its u and inv_s must be the
 * hinge engine's, bit for bit, at every angle (both use single precision in the same order, -ffp-contract=off). And the
 * warm rim it now carries is the band's old per-band formula. */
static void test_cols_hinge_engine(void)
{
    static omni_hinge_line_t lines[FK_LEAF_W];
    int on = 0;
    for (int k = 0; k <= 1400; k++) {
        const float th = (float)k * 0.001f;   /* 0 .. 1.4 rad: closed, the pop, the swing, past gone */
        dw_doors_cols(th, th, s_cols);
        omni_hinge_lines_at(&FK_COLS_HINGE, th, 0, FK_LEAF_W, lines);
        const float light = th / (DW_DOORS_POP_DEG * D2R) < 1 ? th / (DW_DOORS_POP_DEG * D2R) : 1;
        for (int m = 0; m < FK_LEAF_W; m++) {
            const omni_hinge_line_t *l = &lines[m];
            const bool leaf = l->v >= 0 && l->v < FK_LEAF_W;
            const fk_col_t *c = &s_cols[m], *rc = &s_cols[W - 1 - m];
            TEST_ASSERT_EQUAL_INT(leaf ? l->v : -1, c->u);
            TEST_ASSERT_EQUAL_INT(leaf ? W - 1 - l->v : -1, rc->u);
            if (!leaf) continue;
            on++;
            TEST_ASSERT_EQUAL_UINT32(l->inv_s_q16, c->inv_s_q16);
            TEST_ASSERT_EQUAL_UINT32(l->inv_s_q16, rc->inv_s_q16);
            const float f = (float)(l->v + 1) / (float)FK_LEAF_W, f2 = f * f, w = light * (f2 * f2 * f2 * f) * 0.55f;
            const uint8_t want = w > 0.01f ? (uint8_t)floorf((w < 1 ? w : 1) * 255.0f + 0.5f) : 0;
            TEST_ASSERT_EQUAL_UINT8(want, c->warm);
            TEST_ASSERT_EQUAL_UINT8(want, rc->warm);
        }
    }
    TEST_ASSERT_TRUE(on > 100000);
}

static void test_cols_gone(void)
{
    const float gone = omni_hinge_gone_rad(&FK_COLS_HINGE);
    static const float past[] = { 0.0005f, 0.01f, 0.1f, 0.2f };
    for (unsigned k = 0; k < sizeof past / sizeof past[0]; k++) {
        dw_doors_cols(gone + past[k], gone + past[k], s_cols);
        for (int x = 0; x < W; x++) TEST_ASSERT_TRUE(s_cols[x].u < 0);
    }
    dw_doors_cols(DW_DOORS_OPEN_DEG * D2R, DW_DOORS_OPEN_DEG * D2R, s_cols);
    for (int x = 0; x < W; x++) TEST_ASSERT_TRUE(s_cols[x].u < 0);
    /* one leaf gone, the other closed: each half follows its own angle */
    dw_doors_cols(gone + 0.01f, 0, s_cols);
    for (int x = 0; x < W; x++) TEST_ASSERT_EQUAL_INT(x < FK_LEAF_W ? -1 : x, s_cols[x].u);
}

/* ---- the timeline ---- */

static fk_cols_state_t at(uint32_t t, bool reduced)
{
    fk_cols_state_t st;
    memset(&st, 0xA5, sizeof st);
    dw_doors_at(t, reduced, &st);
    return st;
}

/* the swing starts at t0: the leaf holds the pop up to t0 and rises after it */
static void assert_swing_starts(uint32_t t0, bool right)
{
    const float pop = DW_DOORS_POP_DEG * D2R;
    fk_cols_state_t a = at(t0 - 1, false), b = at(t0, false), c = at(t0 + 1, false);
    const float ta = right ? a.th_r : a.th_l, tb = right ? b.th_r : b.th_l, tc = right ? c.th_r : c.th_l;
    TEST_ASSERT_FLOAT_WITHIN(0.01f * D2R, pop, ta);
    TEST_ASSERT_FLOAT_WITHIN(0.01f * D2R, pop, tb);
    TEST_ASSERT_TRUE(tc > tb);   /* an in-out cubic: barely, at first */
}

static void test_timeline_common(void)
{
    fk_cols_state_t st = at(0, false);
    TEST_ASSERT_EQUAL_UINT8(0, st.fade);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.th_l);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, st.th_r);
    TEST_ASSERT_TRUE(st.leaves);
    TEST_ASSERT_FALSE(st.done);
    TEST_ASSERT_EQUAL_UINT8(255, st.xfade);
    /* the right leaf leads by DW_DOORS_LEAD_MS; each ends at DW_DOORS_OPEN_DEG and never goes back */
    assert_swing_starts(DW_DOORS_SWING_T0_MS, true);
    assert_swing_starts(DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS, false);
    float pl = 0, pr = 0;
    bool seen_seam = false, seen_glow = false;
    uint8_t tail = 0;
    for (uint32_t t = 0; t <= DW_DOORS_END_MS + 50; t++) {
        st = at(t, false);
        TEST_ASSERT_TRUE(st.th_l >= pl && st.th_r >= pr);
        TEST_ASSERT_TRUE(st.th_r >= st.th_l);
        TEST_ASSERT_TRUE(st.tail >= tail);
        pl = st.th_l, pr = st.th_r, tail = st.tail;
        seen_seam |= st.seam > 0;
        seen_glow |= st.glow > 0;
        /* leaves: any leaf column (or a leaf's edge, below) on screen; without one, and with the light out and the
         * fade up, the frame is the interior already */
        dw_doors_cols(st.th_l, st.th_r, s_cols);
        bool any = false;
        for (int x = 0; x < W; x++) any |= s_cols[x].u >= 0;
        if (any) TEST_ASSERT_TRUE(st.leaves);
        if (!st.leaves && !st.done && st.seam == 0 && st.glow == 0 && st.fade == 255 && t % 10 == 0) {
            frame(&st, W, W, s_a);
            assert_frame_is(s_interior, s_a);
        }
        TEST_ASSERT_EQUAL_INT(t >= DW_DOORS_END_MS, st.done);
        if (t >= DW_DOORS_SWING_T0_MS) TEST_ASSERT_EQUAL_UINT8(255, st.fade);
    }
    TEST_ASSERT_TRUE(seen_seam);
    TEST_ASSERT_TRUE(seen_glow);
    const uint32_t both_end = DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS + DW_DOORS_SWING_MS;
    st = at(both_end, false);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, DW_DOORS_OPEN_DEG * D2R, st.th_l);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, DW_DOORS_OPEN_DEG * D2R, st.th_r);
    TEST_ASSERT_FALSE(st.leaves);
    TEST_ASSERT_EQUAL_UINT8(0, st.seam);
    TEST_ASSERT_EQUAL_UINT8(0, st.glow);
    TEST_ASSERT_EQUAL_UINT8(255, at(DW_DOORS_END_MS, false).tail);
}

#if !MOCKUP
static void test_timeline_device(void)
{
    const float gone = omni_hinge_gone_rad(&FK_COLS_HINGE);
    fk_cols_state_t st = at(150, false);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, st.th_l / D2R);   /* the pop */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.0f, st.th_r / D2R);
    assert_swing_starts(150, true);
    assert_swing_starts(230, false);
    st = at(830, false);
    TEST_ASSERT_TRUE(st.th_l >= gone && st.th_r >= gone);
    TEST_ASSERT_FALSE(st.leaves);
    /* preflight A9: the right leaf leaves at about 544 ms, the left at about 624 */
    TEST_ASSERT_TRUE(at(540, false).th_r < gone && at(548, false).th_r >= gone);
    TEST_ASSERT_TRUE(at(620, false).th_l < gone && at(628, false).th_l >= gone);
    TEST_ASSERT_FALSE(at(1799, false).done);
    TEST_ASSERT_TRUE(at(1800, false).done);
    TEST_ASSERT_TRUE(at(5000, false).done);
}
#else
static void test_timeline_mockup(void)
{
    const float gone = omni_hinge_gone_rad(&FK_COLS_HINGE);
    assert_swing_starts(380, true);
    assert_swing_starts(460, false);
    TEST_ASSERT_TRUE(at(380 + 780 + 80, false).th_l >= gone);
    TEST_ASSERT_FALSE(at(2299, false).done);
    TEST_ASSERT_TRUE(at(2300, false).done);
    /* Designer A: the seam is lit by 300 and the fade is up by 180 */
    TEST_ASSERT_TRUE(at(300, false).seam > 0);
    TEST_ASSERT_EQUAL_UINT8(255, at(180, false).fade);
    TEST_ASSERT_TRUE(at(179, false).fade < 255);
}
#endif

static void test_doors_reduced(void)
{
    uint8_t prev = 255;
    for (uint32_t t = 0; t <= 700; t++) {
        fk_cols_state_t st = at(t, true);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, st.th_l);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, st.th_r);
        TEST_ASSERT_EQUAL_UINT8(0, st.seam);
        TEST_ASSERT_EQUAL_UINT8(0, st.glow);
        TEST_ASSERT_TRUE(st.xfade <= prev);
        prev = st.xfade;
        TEST_ASSERT_EQUAL_INT(t >= 500, st.done);
        TEST_ASSERT_EQUAL_INT(st.xfade > 0, st.leaves);
    }
    TEST_ASSERT_EQUAL_UINT8(255, at(0, true).xfade);
    TEST_ASSERT_TRUE(at(250, true).xfade > 0 && at(250, true).xfade < 255);
    TEST_ASSERT_TRUE(at(400, true).xfade > 0);   /* 1 - outCubic: below half a level from about 438 ms */
    TEST_ASSERT_EQUAL_UINT8(0, at(500, true).xfade);
    TEST_ASSERT_EQUAL_UINT8(255, at(0, true).tail);   /* nothing grows */
    /* Designer A's order (revealState): the fade from black first, then the cross-fade. The closed doors are shown at
     * full strength once the fade is up, and they hold their whole weight through the fade. */
    uint32_t up = 0;
    while (at(up, true).fade < 255) {
        TEST_ASSERT_EQUAL_UINT8(255, at(up, true).xfade);
        up++;
    }
    TEST_ASSERT_TRUE(up > 0 && up <= 200);
    TEST_ASSERT_EQUAL_UINT8(255, at(up, true).xfade);   /* the door's weight fade x xfade is 1 here */
    TEST_ASSERT_TRUE(at(up + 40, true).xfade < 255);   /* and the cross-fade is under way soon after */
    fk_cols_state_t st = at(up, true);
    frame(&st, 8, W, s_a);
    assert_frame_is(s_door, s_a);
}

/* ---- the compositor ---- */

static void assert_state_eq(const fk_cols_state_t *a, const fk_cols_state_t *b)   /* field by field: padding */
{
    TEST_ASSERT_EQUAL_FLOAT(a->th_l, b->th_l);
    TEST_ASSERT_EQUAL_FLOAT(a->th_r, b->th_r);
    TEST_ASSERT_EQUAL_UINT8(a->fade, b->fade);
    TEST_ASSERT_EQUAL_UINT8(a->seam, b->seam);
    TEST_ASSERT_EQUAL_UINT8(a->glow, b->glow);
    TEST_ASSERT_EQUAL_UINT8(a->xfade, b->xfade);
    TEST_ASSERT_EQUAL_UINT8(a->tail, b->tail);
    TEST_ASSERT_EQUAL_INT(a->leaves, b->leaves);
    TEST_ASSERT_EQUAL_INT(a->done, b->done);
#if !defined(DW_DOORS_DIGEST_GEN)   /* wave 2 Task 3's fields (the gen mode builds at the wave-1 base, without them) */
    TEST_ASSERT_EQUAL_INT(a->keep, b->keep);
    TEST_ASSERT_EQUAL_INT(a->inward, b->inward);
#endif
}

static void test_doors_skip_is_end_frame(void)
{
    fk_cols_state_t st, end;
    memset(&st, 0, sizeof st);
    st.fade = 7;
    st.seam = st.glow = 200;   /* a mid-swing state, then the tap */
    st.th_l = st.th_r = 0.3f;
    dw_doors_skip(&st);
    TEST_ASSERT_TRUE(st.done);
    TEST_ASSERT_FALSE(st.leaves);
    TEST_ASSERT_EQUAL_UINT8(255, st.tail);
    end = at(DW_DOORS_END_MS, false);
    assert_state_eq(&end, &st);
    memset(s_a, 0, sizeof s_a);
    frame(&st, W, W, s_a);
    assert_frame_is(s_interior, s_a);
    memset(s_a, 0, sizeof s_a);
    frame(&st, 8, W, s_a);
    assert_frame_is(s_interior, s_a);
}

static void test_doors_end_exact(void)
{
    fk_cols_state_t st = at(DW_DOORS_END_MS, false);
    frame(&st, W, W, s_a);
    assert_frame_is(s_interior, s_a);
    st = at(DW_DOORS_REDUCED_MS, true);
    frame(&st, 8, W, s_a);
    assert_frame_is(s_interior, s_a);
    /* the leaves gone and the light out, before the end: already the interior */
    st = at(DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS + DW_DOORS_SWING_MS, false);
    frame(&st, 8, W, s_a);
    assert_frame_is(s_interior, s_a);
}

static void test_doors_closed_exact(void)
{
    fk_cols_state_t st;
    memset(&st, 0, sizeof st);
    st.th_l = st.th_r = 0;
    st.fade = 255;
    st.seam = st.glow = 0;
    st.xfade = 255;
    st.leaves = true;
    frame(&st, W, W, s_a);
    assert_frame_is(s_door, s_a);
    frame(&st, 8, W, s_a);
    assert_frame_is(s_door, s_a);
}

static int count_diff(const uint16_t *a, const uint16_t *b)
{
    int n = 0;
    for (int i = 0; i < W * W; i++) n += a[i] != b[i];
    return n;
}

static void test_doors_band_split(void)
{
    fk_cols_state_t st = at(450, false);
    frame(&st, W, W, s_a);
    memset(s_b, 0x5A, sizeof s_b);
    frame(&st, 8, W, s_b);
    TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
    /* the frame is a real mid-swing one: neither the door nor the interior */
    TEST_ASSERT_TRUE(count_diff(s_a, s_door) > W * 20);
    TEST_ASSERT_TRUE(count_diff(s_a, s_interior) > W * 20);
    /* every phase (fade, seam, pop, both swings, glow, the reduced cross-fade), in rows and in odd column blocks */
    static const uint32_t ts[] = { 0, 60, 100, 140, 200, 260, 300, 380, 420, 500, 600, 700, 900, 1200 };
    for (unsigned k = 0; k < sizeof ts / sizeof ts[0]; k++)
        for (int red = 0; red < 2; red++) {
            st = at(ts[k], red != 0);
            frame(&st, W, W, s_a);
            memset(s_b, 0x5A, sizeof s_b);
            frame(&st, 8, W, s_b);
            TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
            memset(s_b, 0xC3, sizeof s_b);
            frame(&st, 6, 37, s_b);
            TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
        }
}

/* the band writes its own pixels and nothing else */
static void test_doors_band_bounds(void)
{
    fk_cols_state_t st = at(450, false);
    dw_doors_cols(st.th_l, st.th_r, s_cols);
    memset(s_a, 0x77, sizeof s_a);
    const omni_rect_t b = { 101, 40, 303, 48 };
    fk_cols_band(&DW_DOORS_OUT, &st, s_cols, s_door, s_interior, s_glow, b, s_a);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (!(y < 8 && x >= 101 && x < 303)) TEST_ASSERT_EQUAL_HEX16(0x7777, s_a[y * W + x]);
}

/* The light: the seam brightens the centre line before the swing, and the open gap glows warm. */
static void test_doors_light(void)
{
    /* Designated, so it builds both here and in the digest gen mode at the wave-1 base, whose state has no keep or
     * inward (wave 2 Task 3 appended them; both false here); a positional list would need its last two values dropped
     * there. */
    const fk_cols_state_t closed = { .th_l = 0, .th_r = 0, .fade = 255, .seam = 0, .glow = 0, .xfade = 255,
                                      .tail = 0, .leaves = true, .done = false };
    frame(&closed, W, W, s_b);
    fk_cols_state_t lit = closed;
    lit.seam = 255;
    frame(&lit, W, W, s_a);
    const int c = 233 * W + 233, edge = 233 * W + 100;
    TEST_ASSERT_TRUE((s_a[c] >> 11) >= (s_b[c] >> 11));
    TEST_ASSERT_TRUE(((s_a[c] >> 5) & 63) > ((s_b[c] >> 5) & 63));
    TEST_ASSERT_EQUAL_HEX16(s_b[edge], s_a[edge]);   /* the crack is a column: x 100 is untouched */
    /* the fade: black at 0 */
    fk_cols_state_t dark = closed;
    dark.fade = 0;
    frame(&dark, 8, W, s_a);
    for (int i = 0; i < W * W; i++) TEST_ASSERT_EQUAL_HEX16(0, s_a[i]);
}

/* The door's thickness outlives the leaf's face: just past the gone angle no column holds the leaf, but its edge is
 * still drawn and the leaf still counts as on screen; once the edge has gone too, the frame is the interior. */
static void test_doors_edge_outlives_face(void)
{
    const float gone = omni_hinge_gone_rad(&FK_COLS_HINGE);
    fk_cols_state_t st;
    memset(&st, 0, sizeof st);
    st.fade = 255;
    st.xfade = 255;
    st.th_l = st.th_r = gone + 0.6f * D2R;   /* the back line is still near x 11 (and 455) */
    dw_doors_cols(st.th_l, st.th_r, s_cols);
    for (int x = 0; x < W; x++) TEST_ASSERT_TRUE(s_cols[x].u < 0);
    frame(&st, 8, W, s_a);
    TEST_ASSERT_TRUE(count_diff(s_a, s_interior) > W * 10);
    TEST_ASSERT_TRUE(s_a[233 * W + 5] != s_interior[233 * W + 5]);
    TEST_ASSERT_TRUE(s_a[233 * W + 460] != s_interior[233 * W + 460]);
    TEST_ASSERT_EQUAL_HEX16(s_interior[233 * W + 40], s_a[233 * W + 40]);
    /* the timeline counts it */
    uint32_t t = DW_DOORS_SWING_T0_MS;
    while (at(t, false).th_l < st.th_l) t++;
    TEST_ASSERT_TRUE(at(t, false).leaves);
    st.th_l = st.th_r = 70.0f * D2R;
    frame(&st, 8, W, s_a);
    assert_frame_is(s_interior, s_a);
}

/* M2: mid-swing pixels against an independent model, not the code's own output. The door here encodes its source
 * column and row (red = u & 31, blue = u >> 5, green = v & 63), and the column/row map is the projection inverted in
 * double: a screen column centre X of the left leaf comes from distance d = X F / (F cos th + (X - C) sin th) from the
 * hinge (from X = C + (d cos th - C) F / (F - d sin th)), u = floor(d) while d < 233, 1 / s = 1 - d sin th / F, and
 * v = floor(C + (y + .5 - C) / s); the right leaf mirrored. */
static uint16_t s_code[W * W];

static double s_frac;   /* the model's last d, for the boundary allowance */
static int oracle_u(double th, int x, double *inv)   /* -1: no leaf; the source column otherwise */
{
    const bool right = x >= 233;
    const double X = right ? 466.0 - (x + 0.5) : x + 0.5, sn = sin(th), cs = cos(th);
    const double d = X * 560.0 / (560.0 * cs + (X - 233.0) * sn);
    if (!(d >= 0 && d < 233.0)) return -1;
    *inv = 1.0 - d * sn / 560.0;
    s_frac = d - floor(d);
    return right ? 465 - (int)floor(d) : (int)floor(d);
}

/* exact, unless the model's value lies within eps of an integer boundary (single precision may round across it) */
static bool near_match(int got, int want, double frac, double eps)
{
    return got == want || (abs(got - want) == 1 && (frac < eps || frac > 1 - eps));
}

static void test_doors_mid_swing_oracle(void)
{
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) s_code[y * W + x] = (uint16_t)((x & 31) << 11 | (y & 63) << 5 | (x >> 5));
    static const float TH[][2] = { { 17.0f, 41.0f }, { 33.0f, 8.0f }, { 55.0f, 62.0f } };
    int checked = 0;
    for (unsigned k = 0; k < sizeof TH / sizeof TH[0]; k++) {
        const double thl = TH[k][0] * D2R, thr = TH[k][1] * D2R;
        dw_doors_cols(TH[k][0] * D2R, TH[k][1] * D2R, s_cols);
        /* (1) the gather alone: the maps with the shade taken off and a state with no light, edge or fade */
        fk_col_t raw[W];
        memcpy(raw, s_cols, sizeof raw);
        for (int x = 0; x < W; x++) raw[x].shade = raw[x].warm = 0;
        fk_cols_state_t plain;
        memset(&plain, 0, sizeof plain);
        plain.fade = plain.xfade = 255;
        plain.leaves = true;
        for (int y0 = 0; y0 < W; y0 += 8) {
            const omni_rect_t b = { 0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W) };
            fk_cols_band(&DW_DOORS_OUT, &plain, raw, s_code, s_interior, s_glow, b, s_a + (size_t)y0 * W);
        }
        for (int x = 1; x < W; x += 7)
            for (int y = 3; y < W; y += 11) {
                double inv = 0;
                const int u = oracle_u(x < 233 ? thl : thr, x, &inv);
                const uint16_t got = s_a[y * W + x];
                if (!on_panel(x, y)) {   /* off the round panel: black */
                    TEST_ASSERT_EQUAL_HEX16(0, got);
                    continue;
                }
                if ((u >= 0) != (raw[x].u >= 0)) {   /* allowed only on the model's own free-edge column */
                    double i2;
                    const double th = x < 233 ? thl : thr;
                    TEST_ASSERT_TRUE_MESSAGE((oracle_u(th, x - 1, &i2) >= 0) != (oracle_u(th, x + 1, &i2) >= 0),
                                             "the leaf's extent differs from the model's away from its edge");
                    continue;
                }
                if (u < 0) {
                    TEST_ASSERT_EQUAL_HEX16(s_interior[y * W + x], got);
                    continue;
                }
                const double ufrac = s_frac, vm = 233.0 + (y + 0.5 - 233.0) * inv;
                const int du = (got >> 11) + 32 * (got & 31);
                const int v = (int)floor(vm);
                const int dv = (got >> 5) & 63;
                char m[96];
                snprintf(m, sizeof m, "th %u at (%d, %d): u %d (model %d), v&63 %d (model %d)", k, x, y, du, u, dv,
                         v & 63);
                TEST_ASSERT_TRUE_MESSAGE(near_match(du, u, ufrac, 1e-3), m);
                /* v's low 6 bits; the code's inv_s is Q16 (2^-16), so allow a boundary within 1e-2 of a row */
                const int e = ((dv - (v & 63)) % 64 + 64) % 64;
                TEST_ASSERT_TRUE_MESSAGE(near_match(e == 63 ? -1 : e, 0, vm - floor(vm), 1e-2), m);
                checked++;
            }
        /* (2) the shade, against drawLeaf's formula in double, at f = (d + 1) / 233 */
        for (int x = 0; x < W; x++) {
            double inv = 0;
            const int u = oracle_u(x < 233 ? thl : thr, x, &inv);
            if (u < 0 || s_cols[x].u < 0) continue;
            const double th = x < 233 ? thl : thr, d = x < 233 ? u : 465 - u, f = (d + 1) / 233.0;
            double dark = 0.72 * (1 - cos(th)) + 0.1 * sin(th) * (1 - f);
            dark = dark < 0 ? 0 : dark > 0.8 ? 0.8 : dark;
            const int want = dark > 0.01 ? (int)floor(dark * 255 + 0.5) : 0;
            TEST_ASSERT_INT_WITHIN(1, want, s_cols[x].shade);
        }
        /* (3) the composite near each hinge (no warm rim, no edge): the shaded door pixel, at the model's (u, v) */
        fk_cols_state_t st = plain;
        st.th_l = TH[k][0] * D2R;
        st.th_r = TH[k][1] * D2R;
        for (int y0 = 0; y0 < W; y0 += 8) {
            const omni_rect_t b = { 0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W) };
            fk_cols_band(&DW_DOORS_OUT, &st, s_cols, s_door, s_interior, s_glow, b, s_a + (size_t)y0 * W);
        }
        for (int x = 2; x < W; x += 9) {
            double inv = 0;
            const int u = oracle_u(x < 233 ? thl : thr, x, &inv);
            if (u < 0 || (x < 233 ? u : 465 - u) > 100) continue;   /* f <= .43: drawLeaf's warm is below .01 */
            for (int y = 5; y < W; y += 23) {
                if (!on_panel(x, y)) {
                    TEST_ASSERT_EQUAL_HEX16(0, s_a[y * W + x]);
                    continue;
                }
                const int v = (int)floor(233.0 + (y + 0.5 - 233.0) * inv);
                bool ok = false;
                for (int a = -1; a <= 1 && !ok; a++)
                    for (int c = -1; c <= 1 && !ok; c++) {
                        const int uu = u + a, vv = v + c;
                        if (uu < 0 || uu >= W || vv < 0 || vv >= W) continue;
                        ok = s_a[y * W + x] == omni_blend565(s_door[vv * W + uu], 0, s_cols[x].shade);
                    }
                TEST_ASSERT_TRUE_MESSAGE(ok, "the composite is not the shaded door at the model's (u, v)");
                checked++;
            }
        }
    }
    TEST_ASSERT_TRUE(checked > 1000);
}

static void test_doors_burn_in(void)
{
    for (int red = 0; red < 2; red++) {
        uint32_t covered = 0;
        for (uint32_t t = 0; t < DW_DOORS_END_MS + 200; t++) {
            fk_cols_state_t st = at(t, red != 0);
            long area = 0;
            if (red) {
                area = st.xfade > 0 && !st.done ? (long)W * W : 0;   /* the whole door, at its weight */
            } else {
                dw_doors_cols(st.th_l, st.th_r, s_cols);
                for (int x = 0; x < W; x++)
                    if (s_cols[x].u >= 0) {
                        TEST_ASSERT_TRUE(s_cols[x].inv_s_q16 <= 65536u);   /* every row of the column */
                        area += W;
                    }
            }
            covered += area * 2 > (long)W * W;
        }
        printf("burn-in (%s): the door covers more than half the screen for %u ms\n", red ? "reduced" : "swing",
               (unsigned)covered);
        TEST_ASSERT_TRUE(covered <= 1500);
    }
}

/* Not a gate, and not in the default run (it asserts nothing): DW_DOORS_BENCH=1 runs it. The host's time per door
 * frame (Task 12 measures the watch), in 8-row bands across the swing. */
static void test_doors_frame_time(void)
{
    struct timespec t0, t1;
    int n = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int rep = 0; rep < 3; rep++)
        for (uint32_t t = DW_DOORS_SWING_T0_MS; t < DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS + DW_DOORS_SWING_MS;
             t += 20) {
            fk_cols_state_t st = at(t, false);
            frame(&st, 8, W, s_a);
            n++;
        }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double ms = ((double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6) / n;
    printf("host time per door frame (cols + 8-row bands, %d frames): %.3f ms\n", n, ms);
}

/* Task 12: the panel's rows against the circle in double, and nothing off the panel in any frame (the closed doors,
 * mid-swing with its edges, the fade, the cross-fade, the end), whatever the bands' split. */
static void test_panel_rows_and_black_off_it(void)
{
    int area = 0;
    for (int y = -2; y < W + 2; y++) {
        int x0, x1;
        fk_panel_row(y, &x0, &x1);
        TEST_ASSERT_TRUE(x0 <= x1);
        for (int x = -1; x <= W; x++) {
            const bool in = x >= x0 && x < x1;
            TEST_ASSERT_EQUAL_INT(y >= 0 && y < W && x >= 0 && x < W && on_panel(x, y), in);
            area += in;
        }
    }
    TEST_ASSERT_INT_WITHIN(400, 170550, area);   /* pi 233^2 */
    static const uint32_t T[] = { 0, 60, 140, 260, 450, 600, 700, DW_DOORS_END_MS };
    for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++)
        for (int red = 0; red < 2; red++) {
            fk_cols_state_t st = at(T[k], red != 0);
            memset(s_a, 0x77, sizeof s_a);
            frame(&st, 8, 100, s_a);
            for (int y = 0; y < W; y++)
                for (int x = 0; x < W; x++)
                    if (!on_panel(x, y)) TEST_ASSERT_EQUAL_HEX16(0, s_a[y * W + x]);
        }
}

/* ---- wave 1's outward doors, pinned (dw wave 2 Task 3, R147 PR11) ----
 * A digest of the outward doors at 20 sample times (16 of the swing, 4 of the reduced cross-fade): each time's state
 * (every wave-1 field), its column maps (every column's u, inv_s, shade and warm) and its frame over the synthetic
 * door, interior and glow in 8-row bands. Computed at $BASE (0458bfa; c743778's doors are the same files) by this very
 * code, built there with -DDW_DOORS_DIGEST_GEN against the wave-1 doors.c, and pinned below: the default timeline's and
 * the A/B timeline's (test_dw_doors_mockup). Wave 1's functions and the _k forms with &DW_DOORS_OUT must both give it.
 * A mid-swing digest is this host's libm (doors.h): it pins this machine's frames, never the device's. */
typedef void (*at_fn)(uint32_t t, bool reduced, fk_cols_state_t *st);
typedef void (*cols_fn)(float th_l, float th_r, fk_col_t cols[466]);
#define DIGEST_N 20
static const struct { uint32_t t; bool reduced; } DIGEST_T[DIGEST_N] = {
    { 0, false }, { 60, false }, { 100, false }, { 130, false }, { 150, false }, { 200, false }, { 260, false },
    { 300, false }, { 380, false }, { 450, false }, { 520, false }, { 600, false }, { 700, false }, { 830, false },
    { 1200, false }, { DW_DOORS_END_MS, false },
    { 0, true }, { 150, true }, { 300, true }, { DW_DOORS_REDUCED_MS, true },
};

static uint32_t digest_at(unsigned i, at_fn at, cols_fn cols)
{
    fk_cols_state_t st;
    memset(&st, 0, sizeof st);
    at(DIGEST_T[i].t, DIGEST_T[i].reduced, &st);
    uint8_t h[15];
    memcpy(h, &st.th_l, 4);
    memcpy(h + 4, &st.th_r, 4);
    h[8] = st.fade, h[9] = st.seam, h[10] = st.glow, h[11] = st.xfade, h[12] = st.tail;
    h[13] = st.leaves, h[14] = st.done;
    uint32_t crc = fk_crc32(0, h, sizeof h);
    cols(st.th_l, st.th_r, s_cols);
    for (int x = 0; x < W; x++) {
        const fk_col_t *c = &s_cols[x];
        const uint8_t b[8] = { (uint8_t)c->u, (uint8_t)((uint16_t)c->u >> 8), (uint8_t)c->inv_s_q16,
                               (uint8_t)(c->inv_s_q16 >> 8), (uint8_t)(c->inv_s_q16 >> 16),
                               (uint8_t)(c->inv_s_q16 >> 24), c->shade, c->warm };
        crc = fk_crc32(crc, b, sizeof b);
    }
    memset(s_a, 0x5A, sizeof s_a);
    for (int y0 = 0; y0 < W; y0 += 8) {
        const omni_rect_t b = { 0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W) };
        fk_cols_band(&DW_DOORS_OUT, &st, s_cols, s_door, s_interior, s_glow, b, s_a + (size_t)y0 * W);
    }
    for (int i2 = 0; i2 < W * W; i2++) {
        const uint8_t p[2] = { (uint8_t)s_a[i2], (uint8_t)(s_a[i2] >> 8) };
        crc = fk_crc32(crc, p, 2);
    }
    return crc;
}

#if !defined(DW_DOORS_DIGEST_GEN)
#if !MOCKUP
static const uint32_t DIGEST_OUT[DIGEST_N] = {
    0x05120688u, 0x79ab0a8fu, 0x2d63654au, 0x5a370309u, 0xec0c15b8u,
    0x3f30bbe3u, 0x87f05e91u, 0x36647bf1u, 0x3c27651au, 0xacb68ee7u,
    0xb87b5262u, 0x4844629fu, 0x8a508519u, 0x278419e7u, 0x14b2a465u,
    0xde18f9f9u, 0x1c6bd829u, 0x33ca5a3au, 0xe1889610u, 0x3a112c50u,
};
#else
static const uint32_t DIGEST_OUT[DIGEST_N] = {
    0x05120688u, 0x326eb3e8u, 0x88434ba3u, 0xf951a095u, 0x595b518du,
    0x1e3eacf4u, 0xebd7448cu, 0xbe4223f0u, 0xec0c15b8u, 0x1bf6cb31u,
    0xc0756a15u, 0x21c3ae7fu, 0x75244501u, 0xe060fae8u, 0x39f343feu,
    0xde18f9f9u, 0x1c6bd829u, 0x1a8f9476u, 0x2697cdc5u, 0x3a112c50u,
};
#endif

static void at_out_k(uint32_t t, bool reduced, fk_cols_state_t *st) { dw_doors_at_k(&DW_DOORS_OUT, t, reduced, st); }
static void cols_out_k(float l, float r, fk_col_t c[466]) { fk_cols_k(&DW_DOORS_OUT, l, r, c); }

static void test_doors_out_unchanged(void)
{
    TEST_ASSERT_FALSE(DW_DOORS_OUT.inward);
    TEST_ASSERT_FALSE(DW_DOORS_OUT.keep);
    TEST_ASSERT_EQUAL_FLOAT(DW_DOORS_OPEN_DEG, DW_DOORS_OUT.open_deg);
    TEST_ASSERT_TRUE(DW_DOORS_IN_KEEP.inward);
    TEST_ASSERT_TRUE(DW_DOORS_IN_KEEP.keep);
    TEST_ASSERT_EQUAL_FLOAT(86.0f, DW_DOORS_IN_KEEP.open_deg);
    for (unsigned i = 0; i < DIGEST_N; i++) {
        char m[96];
        snprintf(m, sizeof m, "t %u%s: wave 1's functions", (unsigned)DIGEST_T[i].t, DIGEST_T[i].reduced ? " reduced" : "");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(DIGEST_OUT[i], digest_at(i, dw_doors_at, dw_doors_cols), m);
        snprintf(m, sizeof m, "t %u%s: the _k forms", (unsigned)DIGEST_T[i].t, DIGEST_T[i].reduced ? " reduced" : "");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(DIGEST_OUT[i], digest_at(i, at_out_k, cols_out_k), m);
    }
    /* the skip, too: the end state (keep false: the interior exactly) */
    fk_cols_state_t a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0x33, sizeof b);
    dw_doors_skip(&a);
    dw_doors_skip_k(&DW_DOORS_OUT, &b);
    assert_state_eq(&a, &b);
    TEST_ASSERT_FALSE(b.keep);
}

/* ---- the inward kept leaves (#3: notes-reveal.md "inward, holds at 86") ---- */

static double free_edge_in(double th)   /* the left inward leaf's free edge (leafProj with inward: Z = -d sin th) */
{
    const double s = 560.0 / (560.0 + 233.0 * sin(th));
    return 233.0 + (233.0 * cos(th) - 233.0) * s;
}

static fk_cols_state_t at_k(const fk_cols_kind_t *k, uint32_t t, bool reduced)
{
    fk_cols_state_t st;
    memset(&st, 0xA5, sizeof st);
    dw_doors_at_k(k, t, reduced, &st);
    return st;
}

/* the frame for st over interior (its kind's column maps), in bands of rows x cols_w, into out */
static void frame_k(const fk_cols_kind_t *k, const fk_cols_state_t *st, const uint16_t *interior, int rows,
                    int cols_w, uint16_t *out)
{
    fk_cols_k(k, st->th_l, st->th_r, s_cols);
    for (int y0 = 0; y0 < W; y0 += rows)
        for (int x0 = 0; x0 < W; x0 += cols_w) {
            const omni_rect_t b = { (int16_t)x0, (int16_t)y0, (int16_t)(x0 + cols_w < W ? x0 + cols_w : W),
                                    (int16_t)(y0 + rows < W ? y0 + rows : W) };
            fk_cols_band(k, st, s_cols, s_door, interior, s_glow, b, out + (size_t)y0 * W);
        }
}

/* I0, the face without its leaves: the synthetic interior, black off the round panel (as every face frame the band
 * shows is), so a byte-for-byte comparison reads the whole square */
static uint16_t s_i0[W * W], s_i1[W * W];
static void make_i0(void)
{
    for (int i = 0; i < W * W; i++) s_i0[i] = on_panel(i % W, i / W) ? s_interior[i] : 0;
}

/* the kept leaves over src into out, drawn in clips of rows x cols_w (fk_cols_leaves at k's open angle) */
static void leaves_over(const fk_cols_kind_t *k, const uint16_t *src, int rows, int cols_w, uint16_t *out)
{
    memcpy(out, src, sizeof s_a);
    static fk_col_t cols[W];
    const float th = k->open_deg * D2R;
    fk_cols_k(k, th, th, cols);
    for (int y0 = 0; y0 < W; y0 += rows)
        for (int x0 = 0; x0 < W; x0 += cols_w) {
            const omni_rect_t c = { (int16_t)x0, (int16_t)y0, (int16_t)(x0 + cols_w < W ? x0 + cols_w : W),
                                    (int16_t)(y0 + rows < W ? y0 + rows : W) };
            fk_cols_leaves(k, cols, s_door, c, out + (size_t)y0 * W + x0, W);
        }
}

static void test_cols_inward_narrow(void)
{
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    static omni_hinge_line_t lines[FK_LEAF_W];
    for (float deg = 0.5f; deg <= 86.0f; deg += 0.5f) {
        const float th = deg * D2R;
        fk_cols_k(k, th, th, s_cols);
        const double edge = free_edge_in(th);
        /* the hinge engine's lines at -theta (the leaf turned away from the eye) give every column's u and inv_s */
        omni_hinge_lines_at(&FK_COLS_HINGE, -th, 0, FK_LEAF_W, lines);
        int last = -1, n_l = 0, n_r = 0;
        for (int x = 0; x < FK_LEAF_W; x++) {
            const int u = s_cols[x].u;
            const omni_hinge_line_t *l = &lines[x];
            TEST_ASSERT_EQUAL_INT(l->v >= 0 && l->v < FK_LEAF_W ? l->v : -1, u);
            if ((double)x + 0.5 > edge + 1e-3) TEST_ASSERT_TRUE_MESSAGE(u < 0, "a leaf past the free edge");
            if ((double)x + 0.5 < edge - 1e-3) TEST_ASSERT_TRUE_MESSAGE(u >= 0, "a hole inside the leaf");
            if (u < 0) continue;
            n_l++;
            TEST_ASSERT_TRUE(u >= last);   /* u rises from the hinge */
            last = u;
            TEST_ASSERT_EQUAL_UINT32(l->inv_s_q16, s_cols[x].inv_s_q16);
            TEST_ASSERT_TRUE(s_cols[x].inv_s_q16 >= 65536u);   /* inward: the leaf only shrinks */
        }
        last = W;
        for (int x = W - 1; x >= FK_LEAF_W; x--) {
            const int u = s_cols[x].u;
            if ((double)(465 - x) + 0.5 > edge + 1e-3) TEST_ASSERT_TRUE(u < 0);
            if (u < 0) continue;
            n_r++;
            TEST_ASSERT_TRUE(u >= FK_LEAF_W && u <= last);
            last = u;
        }
        TEST_ASSERT_TRUE(edge < 233.0);   /* it never crosses the centre line */
        TEST_ASSERT_INT_WITHIN(1, n_l, n_r);
    }
    /* at 86 deg: [0, 80 +- 2) and [386 +- 2, 466) */
    fk_cols_k(k, 86.0f * D2R, 86.0f * D2R, s_cols);
    int e_l = 0, e_r = W;
    while (e_l < W && s_cols[e_l].u >= 0) e_l++;
    while (e_r > 0 && s_cols[e_r - 1].u >= 0) e_r--;
    printf("inward at 86 deg: the leaves span [0, %d) and [%d, 466)\n", e_l, e_r);
    TEST_ASSERT_INT_WITHIN(2, 80, e_l);
    TEST_ASSERT_INT_WITHIN(2, 386, e_r);
    for (int x = e_l; x < e_r; x++) TEST_ASSERT_TRUE(s_cols[x].u < 0);
}

/* drawLeaf's inward shading (:174-175): dark = clamp(.35 sin th (1 - f) + .15 sin th, 0, .6), warm = light sin th
 * (.08 + .3 f^2), both skipped at .01 or below; f = (d + 1) / 233 at the column's source distance d */
static void test_cols_inward_shading(void)
{
    static const float degs[] = { 0.5f, 2.0f, 20.0f, 45.0f, 70.0f, 86.0f };
    for (unsigned j = 0; j < sizeof degs / sizeof degs[0]; j++) {
        const float th = degs[j] * D2R;
        fk_cols_k(&DW_DOORS_IN_KEEP, th, th, s_cols);
        const double sn = sin(th), light = th / (DW_DOORS_POP_DEG * D2R) < 1 ? th / (DW_DOORS_POP_DEG * D2R) : 1;
        for (int x = 0; x < FK_LEAF_W; x++) {
            if (s_cols[x].u < 0) continue;
            const double f = (s_cols[x].u + 1) / 233.0;
            double dark = 0.35 * sn * (1 - f) + 0.15 * sn, warm = light * sn * (0.08 + 0.3 * f * f);
            dark = dark < 0 ? 0 : dark > 0.6 ? 0.6 : dark;
            warm = warm > 1 ? 1 : warm;
            const int want_s = dark > 0.01 ? (int)floor(dark * 255 + 0.5) : 0;
            const int want_w = warm > 0.01 ? (int)floor(warm * 255 + 0.5) : 0;
            /* the threshold itself may round either way in single precision */
            if (fabs(dark - 0.01) > 1e-5) TEST_ASSERT_INT_WITHIN(1, want_s, s_cols[x].shade);
            if (fabs(warm - 0.01) > 1e-5) TEST_ASSERT_INT_WITHIN(1, want_w, s_cols[x].warm);
        }
    }
    /* at 86 deg, the leaf darkens toward its hinge and warms toward its free edge */
    fk_cols_k(&DW_DOORS_IN_KEEP, 86.0f * D2R, 86.0f * D2R, s_cols);
    TEST_ASSERT_TRUE(s_cols[0].shade > s_cols[70].shade);
    TEST_ASSERT_TRUE(s_cols[70].warm > s_cols[0].warm);
}

static void test_cols_inward_mirror(void)
{
    static const float degs[] = { 2.0f, 10.0f, 33.3f, 50.0f, 66.0f, 80.0f, 86.0f };
    for (unsigned j = 0; j < sizeof degs / sizeof degs[0]; j++) {
        const float th = degs[j] * D2R;
        fk_cols_k(&DW_DOORS_IN_KEEP, th, th, s_cols);
        for (int x = 0; x < FK_LEAF_W; x++) {
            const fk_col_t *l = &s_cols[x], *r = &s_cols[465 - x];
            if (l->u < 0) {
                TEST_ASSERT_TRUE(r->u < 0);
                continue;
            }
            TEST_ASSERT_EQUAL_INT(465 - l->u, r->u);
            TEST_ASSERT_EQUAL_UINT32(l->inv_s_q16, r->inv_s_q16);
            TEST_ASSERT_EQUAL_UINT8(l->shade, r->shade);
            TEST_ASSERT_EQUAL_UINT8(l->warm, r->warm);
        }
    }
}

static void test_doors_inward_skip_keeps_leaves(void)
{
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    fk_cols_state_t st;
    memset(&st, 0, sizeof st);
    st.th_l = st.th_r = 0.3f;
    st.seam = st.glow = 200;
    dw_doors_skip_k(k, &st);
    TEST_ASSERT_TRUE(st.done);
    TEST_ASSERT_TRUE(st.keep);
    TEST_ASSERT_TRUE(st.leaves);   /* the kept leaves are on screen */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 86.0f * D2R, st.th_l);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 86.0f * D2R, st.th_r);
    TEST_ASSERT_EQUAL_UINT8(255, st.fade);
    TEST_ASSERT_EQUAL_UINT8(0, st.seam);
    TEST_ASSERT_EQUAL_UINT8(0, st.glow);
    TEST_ASSERT_EQUAL_UINT8(255, st.tail);
    /* the end of the timeline is the skip's state */
    fk_cols_state_t end = at_k(k, DW_DOORS_END_MS, false);
    assert_state_eq(&st, &end);
    TEST_ASSERT_TRUE(end.keep);
    end = at_k(k, DW_DOORS_END_MS + 5000, false);
    assert_state_eq(&st, &end);
    /* the inward timeline: the device default's, with open_deg 86, and the leaves never leave */
    float pl = 0, pr = 0;
    for (uint32_t t = 0; t < DW_DOORS_END_MS; t++) {
        const fk_cols_state_t s = at_k(k, t, false);
        TEST_ASSERT_TRUE(s.th_l >= pl && s.th_r >= pr && s.th_r >= s.th_l);
        TEST_ASSERT_TRUE(s.th_l <= 86.0f * D2R + 1e-6f && s.th_r <= 86.0f * D2R + 1e-6f);
        TEST_ASSERT_TRUE(s.leaves);
        TEST_ASSERT_FALSE(s.done);
        TEST_ASSERT_TRUE(s.keep);
        pl = s.th_l, pr = s.th_r;
        /* outward and inward share the timeline's light and pace: only the angle and the gap's glow differ */
        const fk_cols_state_t o = at(t, false);
        TEST_ASSERT_EQUAL_UINT8(o.fade, s.fade);
        TEST_ASSERT_EQUAL_UINT8(o.seam, s.seam);
        TEST_ASSERT_EQUAL_UINT8(o.tail, s.tail);
        TEST_ASSERT_EQUAL_UINT8(o.xfade, s.xfade);
    }
    const uint32_t both_end = DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS + DW_DOORS_SWING_MS;
    const fk_cols_state_t h = at_k(k, both_end, false);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 86.0f * D2R, h.th_l);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 86.0f * D2R, h.th_r);
    TEST_ASSERT_EQUAL_UINT8(0, h.glow);
    /* the skip's frame shows the leaves (not the interior alone) */
    make_i0();
    frame_k(k, &st, s_i0, W, W, s_a);
    TEST_ASSERT_TRUE(count_diff(s_a, s_i0) > W * 60);   /* about 39 000 of the panel's pixels */
}

/* The end seam: the band at the inward done state over I0 is fk_cols_leaves over I0, byte for byte; and the held
 * swing's last frames (both leaves at 86, the light out) are that frame already, so the end changes nothing. */
static void test_doors_inward_end_seam(void)
{
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    make_i0();
    fk_cols_state_t st;
    dw_doors_skip_k(k, &st);
    memset(s_a, 0x5A, sizeof s_a);
    frame_k(k, &st, s_i0, W, W, s_a);
    leaves_over(k, s_i0, W, W, s_b);
    TEST_ASSERT_EQUAL_MEMORY(s_b, s_a, sizeof s_a);
    memset(s_a, 0xC3, sizeof s_a);
    frame_k(k, &st, s_i0, 8, W, s_a);   /* in the device's bands */
    TEST_ASSERT_EQUAL_MEMORY(s_b, s_a, sizeof s_a);
    leaves_over(k, s_i0, 6, 37, s_a);   /* the leaves in a face's clips are the leaves drawn whole */
    TEST_ASSERT_EQUAL_MEMORY(s_b, s_a, sizeof s_a);
    /* the leaves only: I0 between them and above and below them; a leaf pixel is the door's, never I0's */
    TEST_ASSERT_EQUAL_HEX16(s_i0[233 * W + 233], s_b[233 * W + 233]);
    TEST_ASSERT_TRUE(s_b[233 * W + 40] != s_i0[233 * W + 40]);
    TEST_ASSERT_TRUE(s_b[233 * W + 425] != s_i0[233 * W + 425]);
    /* the hold before the end: the same frame */
    const uint32_t both_end = DW_DOORS_SWING_T0_MS + DW_DOORS_LEAD_MS + DW_DOORS_SWING_MS;
    for (uint32_t t = both_end; t < DW_DOORS_END_MS; t += 97) {
        const fk_cols_state_t h = at_k(k, t, false);
        if (h.seam || h.glow) continue;
        memset(s_a, 0x11, sizeof s_a);
        frame_k(k, &h, s_i0, 8, W, s_a);
        TEST_ASSERT_EQUAL_MEMORY(s_b, s_a, sizeof s_a);
    }
    /* idempotent: the kept leaves over a face that already shows them (the cross-fade's interior) change nothing */
    leaves_over(k, s_b, W, W, s_a);
    TEST_ASSERT_EQUAL_MEMORY(s_b, s_a, sizeof s_a);
    memset(s_a, 0x5A, sizeof s_a);
    frame_k(k, &st, s_b, 8, W, s_a);
    TEST_ASSERT_EQUAL_MEMORY(s_b, s_a, sizeof s_a);
}

/* Reduced motion and Fade (both are dw_doors_at's reduced: the closed doors cross-fade over the face, kept leaves and
 * all, doors_live false): the end state with keep has both leaves at open_deg, and the band over a synthetic interior
 * is fk_cols_leaves over it, byte for byte; the cross-fade's last frames lead into it without a step. */
static void test_doors_inward_reduced_end_seam(void)
{
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    make_i0();
    leaves_over(k, s_i0, W, W, s_i1);   /* the face with its kept leaves (doors_live false) */
    const uint32_t ends[] = { DW_DOORS_REDUCED_MS, DW_DOORS_REDUCED_MS + 1, 5000 };
    for (unsigned j = 0; j < sizeof ends / sizeof ends[0]; j++) {
        const fk_cols_state_t st = at_k(k, ends[j], true);
        TEST_ASSERT_TRUE(st.done);
        TEST_ASSERT_TRUE(st.keep);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 86.0f * D2R, st.th_l);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 86.0f * D2R, st.th_r);
        memset(s_a, 0x5A, sizeof s_a);
        frame_k(k, &st, s_i0, 8, W, s_a);
        TEST_ASSERT_EQUAL_MEMORY(s_i1, s_a, sizeof s_a);
        memset(s_a, 0x5A, sizeof s_a);
        frame_k(k, &st, s_i1, 8, W, s_a);   /* over the face as the cross-fade draws it */
        TEST_ASSERT_EQUAL_MEMORY(s_i1, s_a, sizeof s_a);
    }
    /* the cross-fade itself: the closed doors (no theta leaves 0) over the face with its leaves, the doors' weight
     * falling to 0; at weight 0 the frame is the face, which is the end frame */
    uint8_t prev = 255;
    bool zero = false;
    for (uint32_t t = 0; t < DW_DOORS_REDUCED_MS; t++) {
        const fk_cols_state_t st = at_k(k, t, true);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, st.th_l);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, st.th_r);
        TEST_ASSERT_FALSE(st.done);
        TEST_ASSERT_TRUE(st.xfade <= prev);
        prev = st.xfade;
        const fk_cols_state_t o = at(t, true);   /* the outward cross-fade's own weights */
        TEST_ASSERT_EQUAL_UINT8(o.xfade, st.xfade);
        TEST_ASSERT_EQUAL_UINT8(o.fade, st.fade);
        if (st.xfade == 0 && st.fade == 255 && !zero) {
            zero = true;
            frame_k(k, &st, s_i1, 8, W, s_a);
            TEST_ASSERT_EQUAL_MEMORY(s_i1, s_a, sizeof s_a);
        }
    }
    fk_cols_state_t st = at_k(k, 250, true);   /* mid cross-fade: neither the doors nor the face */
    frame_k(k, &st, s_i1, 8, W, s_a);
    TEST_ASSERT_TRUE(count_diff(s_a, s_i1) > W * 100);
}

/* rows of a column the door image covers (v = floor(233 + (y + .5 - 233) inv_s) on the image) */
static long col_rows(const fk_col_t *c)
{
    if (c->u < 0) return 0;
    const double inv = c->inv_s_q16 / 65536.0;
    const double r = W / inv;
    return r >= W ? W : (long)r;
}

static void test_doors_burn_in_kinds(void)
{
    const fk_cols_kind_t *kinds[2] = { &DW_DOORS_OUT, &DW_DOORS_IN_KEEP };
    for (int kk = 0; kk < 2; kk++)
        for (int red = 0; red < 2; red++) {
            const fk_cols_kind_t *k = kinds[kk];
            uint32_t covered = 0;
            long kept = 0;
            for (uint32_t t = 0; t < DW_DOORS_END_MS + 200; t++) {
                const fk_cols_state_t st = at_k(k, t, red != 0);
                long area = 0;
                if (red && !st.done) {
                    area = st.xfade > 0 ? (long)W * W : 0;   /* the whole door, at its weight */
                } else if (!st.done || st.keep) {
                    fk_cols_k(k, st.th_l, st.th_r, s_cols);
                    for (int x = 0; x < W; x++) area += col_rows(&s_cols[x]);
                    if (st.done) kept = area;
                }
                covered += area * 2 > (long)W * W;
            }
            printf("burn-in (%s, %s): more than half the screen for %u ms; kept leaves %.1f %%\n",
                   kk ? "inward kept" : "outward", red ? "reduced" : "swing", (unsigned)covered,
                   100.0 * (double)kept / ((double)W * W));
            TEST_ASSERT_TRUE(covered <= 1500);
            TEST_ASSERT_TRUE(kept * 2 < (long)W * W);
        }
}

static void test_doors_band_split_inward(void)
{
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    fk_cols_state_t st = at_k(k, 450, false);
    frame_k(k, &st, s_interior, W, W, s_a);
    memset(s_b, 0x5A, sizeof s_b);
    frame_k(k, &st, s_interior, 8, W, s_b);
    TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
    TEST_ASSERT_TRUE(count_diff(s_a, s_door) > W * 20);   /* a real mid-swing frame */
    TEST_ASSERT_TRUE(count_diff(s_a, s_interior) > W * 20);
    static const uint32_t ts[] = { 0, 60, 100, 140, 200, 260, 300, 380, 420, 500, 600, 700, 900, 1200, 1900, 2400 };
    for (unsigned j = 0; j < sizeof ts / sizeof ts[0]; j++)
        for (int red = 0; red < 2; red++) {
            st = at_k(k, ts[j], red != 0);
            frame_k(k, &st, s_interior, W, W, s_a);
            memset(s_b, 0x5A, sizeof s_b);
            frame_k(k, &st, s_interior, 8, W, s_b);
            TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
            memset(s_b, 0xC3, sizeof s_b);
            frame_k(k, &st, s_interior, 6, 37, s_b);
            TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
            for (int y = 0; y < W; y++)   /* black off the panel */
                for (int x = 0; x < W; x++)
                    if (!on_panel(x, y)) TEST_ASSERT_EQUAL_HEX16(0, s_a[y * W + x]);
        }
}

/* ---- wave 2 Task 11: the kept leaves' still geometry does not depend on libm (the Task 3 review: newlib on the device,
 * glibc on the host, may differ by an ulp, and #3's stills show the kept leaves). Their digest: every column at the kept
 * angle (u, inv_s, shade, warm) and fk_cols_leaves over the synthetic I0 (the free-edge quads and the leaves), pinned
 * here. test_dw_doors_libm builds doors.c with sinf and cosf off (doors_libm_perturb.c): the kept digest must
 * not move there, while a mid-swing inward angle's must (so the perturbation bites). */
static uint32_t kept_digest(float th)
{
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    static fk_col_t cols[W];
    fk_cols_k(k, th, th, cols);
    uint32_t crc = 0;
    for (int x = 0; x < W; x++) {
        const uint8_t b[8] = { (uint8_t)cols[x].u, (uint8_t)((uint16_t)cols[x].u >> 8), (uint8_t)cols[x].inv_s_q16,
                               (uint8_t)(cols[x].inv_s_q16 >> 8), (uint8_t)(cols[x].inv_s_q16 >> 16),
                               (uint8_t)(cols[x].inv_s_q16 >> 24), cols[x].shade, cols[x].warm };
        crc = fk_crc32(crc, b, sizeof b);
    }
    make_i0();
    memcpy(s_a, s_i0, sizeof s_a);
    fk_cols_leaves(k, cols, s_door, (omni_rect_t){ 0, 0, W, W }, s_a, W);
    return fk_crc32(crc, s_a, sizeof s_a);
}
#define KEPT_DIGEST_PINNED 0xf10a87e6u   /* the kept angle's (86 deg) */
#define MID_DIGEST_PINNED 0x21f2efa8u    /* a mid-swing inward angle's (45 deg), host libm */

static void test_doors_kept_leaves_libm_free(void)
{
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    fk_cols_state_t st;
    dw_doors_skip_k(k, &st);
    TEST_ASSERT_TRUE(st.th_l == k->open_deg * D2R && st.th_r == st.th_l);   /* the done state's angle is the kept one */
    dw_doors_at_k(k, DW_DOORS_REDUCED_MS, true, &st);
    TEST_ASSERT_TRUE(st.th_l == k->open_deg * D2R && st.th_r == st.th_l);
    const uint32_t kept = kept_digest(k->open_deg * D2R), mid = kept_digest(45.0f * D2R);
    printf("kept digest 0x%08x, mid-swing 0x%08x\n", (unsigned)kept, (unsigned)mid);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(KEPT_DIGEST_PINNED, kept, "the kept leaves' digest moved");
#if defined(DW_DOORS_LIBM_PERTURB)
    TEST_ASSERT_TRUE_MESSAGE(mid != MID_DIGEST_PINNED, "the perturbed sinf and cosf change nothing (the test is blind)");
#else
    TEST_ASSERT_EQUAL_HEX32(MID_DIGEST_PINNED, mid);
    /* the constants are the correctly rounded values, and this host's libm gives them too (test_cols_inward_narrow pins
     * the columns against omni_hinge_lines_at(-th) at the kept angle as well) */
    const float th = k->open_deg * D2R;
    TEST_ASSERT_TRUE((float)sin((double)th) == sinf(th) && (float)cos((double)th) == cosf(th));
#endif
}
#endif /* !DW_DOORS_DIGEST_GEN */

#if defined(DW_DOORS_LIBM_PERTURB)
/* doors_libm_perturb.c builds doors.c with these in place of sinf and cosf: the host's, off by 1e-4 (an ulp is far
 * smaller, but it seldom moves a quantised column at all: this is large enough that any libm call shows) */
float perturbed_sinf(float x) { return sinf(x) * 1.0001f; }
float perturbed_cosf(float x) { return cosf(x) * 1.0001f; }
int main(void)
{
    make_images();
    UNITY_BEGIN();
    RUN_TEST(test_doors_kept_leaves_libm_free);
    return UNITY_END();
}
#elif defined(DW_DOORS_DIGEST_GEN)
/* HISTORICAL, NOT BUILDABLE (rd wave 1 Task 1): fk_cols_band now takes the kind first, so this mode no longer compiles
 * against the wave-1 doors.c it was written for; it is kept only as the record of how DIGEST_OUT was made.
 * $BASE's digest (test_doors_out_unchanged): built at $BASE with -DDW_DOORS_DIGEST_GEN, it prints the table to pin.
 * This file builds there unchanged (wave 2 Task 4, the Task 3 review): the code above this mode's cut names no
 * keep or inward field outside #if !defined(DW_DOORS_DIGEST_GEN) (assert_state_eq), and test_doors_light's state is a
 * designated initializer (a positional one would need its last two values, keep and inward, dropped there). */
int main(void)
{
    make_images();
    printf("%s:", MOCKUP ? "DIGEST_MOCKUP_PINNED" : "DIGEST_DEFAULT_PINNED");
    for (unsigned i = 0; i < DIGEST_N; i++) printf(" 0x%08xu,", (unsigned)digest_at(i, dw_doors_at, dw_doors_cols));
    printf("\n");
    return 0;
}
#elif defined(DW_DOORS_REAL)
/* The real art: a 466 x 466 RGB565 door and the 64 x 256 A8 glow, LVGL v9 bins (12-byte header, then the plane). */
static void load_real(void)
{
    size_t n = 0;
    const uint8_t *d = dw_assets_host_get("dw_doors_m.bin", &n);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT(12 + (size_t)W * W * 2, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, d[1]);
    memcpy(s_door, d + 12, sizeof s_door);
    const uint8_t *g = dw_assets_host_get("dw_glow.bin", &n);
    TEST_ASSERT_NOT_NULL(g);
    TEST_ASSERT_EQUAL_UINT(12 + sizeof s_glow, n);
    memcpy(s_glow, g + 12, sizeof s_glow);
#if defined(DW_DOORS_REAL_OVERLAY)
    TEST_ASSERT_TRUE_MESSAGE(dw_assets_host_from_overlay("dw_doors_m.bin"), "the overlay's doors were not found");
#endif
}

#if !MOCKUP && !defined(DW_DOORS_LIBM_PERTURB) && !defined(DW_DOORS_REAL_OVERLAY)
/* rd wave 1 Task 1 (PR2): the doors' frames at $BASE, before face_kit took the column renderer (fixtures/
 * dw_doors_frames_crc.h has how they were made). The generic doors only: the overlay's art has frames of its own. */
#include "fixtures/dw_doors_frames_crc.h"
static uint16_t s_leaf_c[W * W], s_crystal[W * W];
static void test_dw_doors_frames_golden(void)
{
    size_t n = 0;
    const uint8_t *d = dw_assets_host_get("dw_doors_c.bin", &n);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_UINT(12 + (size_t)W * W * 2, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, d[1]);
    memcpy(s_leaf_c, d + 12, sizeof s_leaf_c);
    void *ctx = calloc(1, DW_FACE_CRYSTAL.ctx_bytes);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_TRUE(DW_FACE_CRYSTAL.bind(ctx, dw_assets_host_get));
    dw_still_frame(&DW_FACE_CRYSTAL, ctx, 0, s_crystal);
    unsigned bad = 0;
    for (unsigned i = 0; i < DW_DOORS_FRAMES_N; i++) {
        const dw_doors_frame_crc_t *r = &DW_DOORS_FRAMES_CRC[i];
        const fk_cols_kind_t *k = r->kind ? &DW_DOORS_IN_KEEP : &DW_DOORS_OUT;
        fk_cols_state_t st;
        dw_doors_at_k(k, r->t, r->reduced != 0, &st);
        fk_cols_k(k, st.th_l, st.th_r, s_cols);
        memset(s_a, 0xA5, sizeof s_a);
        for (int y0 = 0; y0 < W; y0 += 8) {
            const omni_rect_t b = { 0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W) };
            fk_cols_band(k, &st, s_cols, r->leaf ? s_leaf_c : s_door, s_crystal, s_glow, b, s_a + (size_t)y0 * W);
        }
        const uint32_t crc = fk_crc32(0, s_a, sizeof s_a);
        if (crc != r->crc && bad++ < 8)
            printf("kind %u leaf %u t %u reduced %u: 0x%08x, recorded 0x%08x\n", r->kind, r->leaf, r->t, r->reduced,
                   (unsigned)crc, (unsigned)r->crc);
    }
    free(ctx);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, bad, "frames differ from $BASE's");
}

/* rd wave 2 Task 4 (M4): the explicit look. The same frames replayed with a copy of each kind whose look is
 * &FK_COLS_LOOK_DW pin the look's path (not only the NULL one) to $BASE's bytes; an inward kind takes the first theme's
 * look whatever its own (a look on one is unsupported: ignored), so a copy with another look gives the same bytes too.
 * dw's own kinds name no look, and fk_cols_leaf_on_screen_k with no kind is fk_cols_leaf_on_screen. */
static void test_fk_cols_look_dw_golden(void)
{
    TEST_ASSERT_NULL(DW_DOORS_OUT.look);
    TEST_ASSERT_NULL(DW_DOORS_IN_KEEP.look);
    static const fk_cols_look_t OTHER = { 0.7f, 0.08f, 0.8f, 0.5f, 6, { 255, 110, 60 }, 12.0f, { 0x12, 0x14, 0x16 },
                                          { 255, 120, 64 }, 0.6f };
    size_t n = 0;
    const uint8_t *d = dw_assets_host_get("dw_doors_c.bin", &n);
    TEST_ASSERT_NOT_NULL(d);
    memcpy(s_leaf_c, d + 12, sizeof s_leaf_c);
    void *ctx = calloc(1, DW_FACE_CRYSTAL.ctx_bytes);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_TRUE(DW_FACE_CRYSTAL.bind(ctx, dw_assets_host_get));
    dw_still_frame(&DW_FACE_CRYSTAL, ctx, 0, s_crystal);
    unsigned bad = 0, inward = 0;
    for (unsigned i = 0; i < DW_DOORS_FRAMES_N; i++) {
        const dw_doors_frame_crc_t *r = &DW_DOORS_FRAMES_CRC[i];
        for (int v = 0; v < 2; v++) {
            fk_cols_kind_t k = r->kind ? DW_DOORS_IN_KEEP : DW_DOORS_OUT;
            if (v == 1 && !k.inward) continue;
            k.look = v == 0 ? &FK_COLS_LOOK_DW : &OTHER;
            inward += v == 1;
            fk_cols_state_t st;
            dw_doors_at_k(&k, r->t, r->reduced != 0, &st);
            fk_cols_k(&k, st.th_l, st.th_r, s_cols);
            memset(s_a, 0xA5, sizeof s_a);
            for (int y0 = 0; y0 < W; y0 += 8) {
                const omni_rect_t b = { 0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W) };
                fk_cols_band(&k, &st, s_cols, r->leaf ? s_leaf_c : s_door, s_crystal, s_glow, b, s_a + (size_t)y0 * W);
            }
            const uint32_t crc = fk_crc32(0, s_a, sizeof s_a);
            if (crc != r->crc && bad++ < 8)
                printf("look %d, kind %u leaf %u t %u reduced %u: 0x%08x, recorded 0x%08x\n", v, r->kind, r->leaf, r->t,
                       r->reduced, (unsigned)crc, (unsigned)r->crc);
        }
    }
    /* review M1: an outward kind's own look reaches the pixels: at 30 deg the columns carry its shade and warm, and the
     * frames of every mid-swing row differ from the first theme's */
    {
        fk_cols_kind_t k = DW_DOORS_OUT;
        k.look = &OTHER;
        const float th = 30.0f * D2R;
        fk_cols_k(&k, th, th, s_cols);
        unsigned checked = 0;
        for (int x = 0; x < 233; x++) {
            const fk_col_t *c = &s_cols[x];
            if (c->u < 0) continue;
            const float f = (float)(c->u + 1) / 233.0f;
            float dark = 0.7f * (1.0f - cosf(th)) + 0.08f * sinf(th) * (1.0f - f);
            dark = dark < 0 ? 0 : dark > 0.8f ? 0.8f : dark;
            const float f2 = f * f, w = 1.0f * (f2 * f2 * f2) * 0.5f;   /* light 1: past the pop */
            TEST_ASSERT_INT_WITHIN(1, dark > 0.01f ? (int)(dark * 255.0f + 0.5f) : 0, c->shade);
            TEST_ASSERT_INT_WITHIN(1, w > 0.01f ? (int)(w * 255.0f + 0.5f) : 0, c->warm);
            checked++;
        }
        TEST_ASSERT_TRUE(checked > 100);
        unsigned differ = 0, mid = 0;
        for (unsigned i = 0; i < DW_DOORS_FRAMES_N; i++) {
            const dw_doors_frame_crc_t *r = &DW_DOORS_FRAMES_CRC[i];
            if (r->kind || r->reduced) continue;
            fk_cols_state_t st;
            dw_doors_at_k(&k, r->t, false, &st);
            if (st.done || !(st.th_l > 0.2f && st.th_l < 1.0f)) continue;
            fk_cols_k(&k, st.th_l, st.th_r, s_cols);
            for (int y0 = 0; y0 < W; y0 += 8) {
                const omni_rect_t b = { 0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W) };
                fk_cols_band(&k, &st, s_cols, r->leaf ? s_leaf_c : s_door, s_crystal, s_glow, b, s_a + (size_t)y0 * W);
            }
            mid++, differ += fk_crc32(0, s_a, sizeof s_a) != r->crc;
        }
        TEST_ASSERT_TRUE(mid > 4);
        TEST_ASSERT_EQUAL_UINT(mid, differ);
    }
    free(ctx);
    TEST_ASSERT_TRUE(inward > 0);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, bad, "an explicit look's frames differ from $BASE's");
    const float gone = omni_hinge_gone_rad(&FK_COLS_HINGE);
    for (int i = 0; i <= 900; i++) {   /* 0 to 90 deg in tenths */
        const float th = (float)i * 0.1f * D2R;
        TEST_ASSERT_EQUAL(fk_cols_leaf_on_screen(th, gone, false), fk_cols_leaf_on_screen_k(NULL, th));
        TEST_ASSERT_EQUAL(fk_cols_leaf_on_screen(th, gone, false), fk_cols_leaf_on_screen_k(&DW_DOORS_OUT, th));
        TEST_ASSERT_TRUE(fk_cols_leaf_on_screen_k(&DW_DOORS_IN_KEEP, th));
    }
}
#endif

int main(void)
{
#if defined(DW_DOORS_REAL_OVERLAY) && !defined(DW_ART_DIR)
    printf("test_dw_doors_real_overlay: no -DDW_ART_OVERLAY at configure: skipped\n");
    return 77;
#else
#if defined(DW_DOORS_REAL_OVERLAY)
    dw_assets_host_open(DW_ASSETS_DIR, DW_ART_DIR "/assets");
#else
    dw_assets_host_open(DW_ASSETS_DIR, NULL);
#endif
    make_images();   /* the interior stays synthetic */
    UNITY_BEGIN();
    RUN_TEST(load_real);
    RUN_TEST(test_doors_closed_exact);
    RUN_TEST(test_doors_band_split);
#if !MOCKUP && !defined(DW_DOORS_LIBM_PERTURB) && !defined(DW_DOORS_REAL_OVERLAY)
    RUN_TEST(test_dw_doors_frames_golden);   /* rd wave 1 Task 1 */
    RUN_TEST(test_fk_cols_look_dw_golden);   /* rd wave 2 Task 4 (M4) */
#endif
    const int rc = UNITY_END();
    dw_assets_host_close();
    return rc;
#endif
}
#else
int main(void)
{
    make_images();
    UNITY_BEGIN();
    RUN_TEST(test_door_gone_angle);
    RUN_TEST(test_cols_closed);
    RUN_TEST(test_cols_mirror);
    RUN_TEST(test_cols_monotonic);
    RUN_TEST(test_cols_hinge_engine);
    RUN_TEST(test_cols_gone);
    RUN_TEST(test_panel_rows_and_black_off_it);
    RUN_TEST(test_timeline_common);
#if !MOCKUP
    RUN_TEST(test_timeline_device);
#else
    RUN_TEST(test_timeline_mockup);
#endif
    RUN_TEST(test_doors_reduced);
    RUN_TEST(test_doors_skip_is_end_frame);
    RUN_TEST(test_doors_end_exact);
    RUN_TEST(test_doors_closed_exact);
    RUN_TEST(test_doors_band_split);
    RUN_TEST(test_doors_band_bounds);
    RUN_TEST(test_doors_light);
    RUN_TEST(test_doors_edge_outlives_face);
    RUN_TEST(test_doors_mid_swing_oracle);
    RUN_TEST(test_doors_burn_in);
    /* wave 2 Task 3: wave 1's doors pinned, and the inward kept leaves */
    RUN_TEST(test_doors_out_unchanged);
    RUN_TEST(test_cols_inward_narrow);
    RUN_TEST(test_cols_inward_shading);
    RUN_TEST(test_cols_inward_mirror);
    RUN_TEST(test_doors_inward_skip_keeps_leaves);
    RUN_TEST(test_doors_inward_end_seam);
    RUN_TEST(test_doors_inward_reduced_end_seam);
    RUN_TEST(test_doors_burn_in_kinds);
    RUN_TEST(test_doors_band_split_inward);
    RUN_TEST(test_doors_kept_leaves_libm_free);   /* wave 2 Task 11 */
    if (getenv("DW_DOORS_BENCH")) RUN_TEST(test_doors_frame_time);
    return UNITY_END();
}
#endif
