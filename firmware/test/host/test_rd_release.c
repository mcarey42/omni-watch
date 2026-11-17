/* Stasis Release (rd/release.h; rd wave 1 Task 10, spec sections 9.1-9.3): the timeline (rd_rel_at), the mist, the
 * counter's text, and RD_REVEAL_STASIS driven as fk_view drives it (frame, the interior drawn into the frame's need at
 * its wake_ms, then every band), over the Stasis Clock and the committed rd assets. The view's own parts (the jobs, the
 * cores, the canvas fallback) are face_kit's and the device's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "rd_assets_host.h"
#include "fk/compose.h"
#include "fk/shapes.h"
#include "omni/blend565.h"
#include "rd/assets_meta.h"
#include "rd/atlas2.h"
#include "rd/faces.h"
#include "rd/release.h"
#include "rd/soft_lut.h"

void setUp(void) {}
void tearDown(void) {}

#define W 466
#define D2R 0.017453292519943295f
#define C_X 233

static void *s_face, *s_rctx;
static uint16_t s_interior[W * W], s_a[W * W], s_b[W * W], s_c[W * W];

static void bind_all(void)
{
    TEST_ASSERT_TRUE(rd_assets_host_open(RD_ASSETS_DIR));
    s_face = calloc(1, RD_FACE_STASIS.ctx_bytes);
    TEST_ASSERT_NOT_NULL(s_face);
    TEST_ASSERT_TRUE(RD_FACE_STASIS.bind(s_face, rd_assets_host_get));
    s_rctx = calloc(1, RD_REVEAL_STASIS.rctx_bytes);   /* fk_view's: rctx_bytes of zeroed PSRAM */
    TEST_ASSERT_NOT_NULL(s_rctx);
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
}

/* the face's input at the reveal: still row `still`'s clock and readout */
static fk_face_in_t base_in(unsigned still)
{
    rd_still_t st;
    rd_still(still, &st);
    return st.in;
}

static bool empty(omni_rect_t r) { return r.x0 >= r.x1 || r.y0 >= r.y1; }

static fk_face_in_t s_last_in;   /* the last render's input and setting (a new reveal when they change) */
static bool s_last_reduced, s_rendered;

/* One frame at t as fk_view makes it: the plan (frame), the interior drawn into its need at the plan's wake_ms and
 * reveal_live (poison: the interior is first filled with a colour no frame shows, so a band that reads outside the need
 * shows it; not on a frame that reuses the settled face the interior holds), then every band of `rows` rows. Returns
 * the plan. */
static fk_rframe_t render(uint32_t t, bool skip, bool reduced, const fk_face_in_t *in0, int rows, bool poison,
                          uint16_t *out)
{
    /* the view binds every reveal afresh, with one input and one reduced setting for its frames (the final review,
     * M4: the settled face drawn once is reused): another input or setting than the last render's is a new reveal */
    if (s_rendered && (memcmp(in0, &s_last_in, sizeof s_last_in) != 0 || reduced != s_last_reduced))
        TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    memcpy(&s_last_in, in0, sizeof s_last_in);   /* bytes, padding too, for the memcmp */
    s_last_reduced = reduced, s_rendered = true;
    fk_rframe_t fr;
    memset(&fr, 0, sizeof fr);
    for (int i = 0; i < W * W; i++) out[i] = 0x5A5A;   /* a pixel no band writes shows */
    RD_REVEAL_STASIS.frame(NULL, s_rctx, t, skip, reduced, &fr);
    const bool reuse = empty(fr.need) && fr.wake_ms == UINT32_MAX;   /* M4: the settled face the interior holds */
    if (poison && !reuse)
        for (int i = 0; i < W * W; i++) s_interior[i] = 0xF81F;
    const omni_rect_t need = poison ? fr.need : (omni_rect_t){0, 0, W, W};
    if (!empty(need) || !poison) {
        fk_face_in_t in = *in0;
        in.wake_ms = fr.wake_ms;
        in.reveal_live = fr.live;
        in.reduced = reduced;
        if (!poison && empty(fr.need) && fr.wake_ms != UINT32_MAX)
            in.wake_ms = t;   /* a whole interior where the plan wanted none (the shut lid covers all) */
        RD_FACE_STASIS.draw(s_face, &in, need, s_interior + (size_t)need.y0 * W + need.x0, W);
    }
    for (int y0 = 0; y0 < W; y0 += rows) {
        const omni_rect_t b = {0, (int16_t)y0, W, (int16_t)(y0 + rows < W ? y0 + rows : W)};
        RD_REVEAL_STASIS.band(NULL, s_rctx, s_interior, b, out + (size_t)y0 * W);
    }
    return fr;
}

static bool on_panel(int x, int y)
{
    const int dx = 2 * x + 1 - W, dy = 2 * y + 1 - W;
    return dx * dx + dy * dy <= W * W;
}

/* ---- the timeline */
static void test_rd_rel_timeline(void)
{
    rd_rel_state_t st;
    rd_rel_at(0, false, true, &st);
    TEST_ASSERT_EQUAL_UINT8(0, st.rows.fade);
    rd_rel_at(160, false, true, &st);
    TEST_ASSERT_EQUAL_UINT8(255, st.rows.fade);
    rd_rel_at(120, false, true, &st);
    TEST_ASSERT_EQUAL_UINT32(0, st.counter);
    rd_rel_at(500, false, true, &st);
    TEST_ASSERT_TRUE(st.counter > 0 && st.counter < 3000000u);
    static const uint32_t FULL[] = {860, 870, 880, 1000, 1500, 2150};
    for (unsigned i = 0; i < sizeof FULL / sizeof FULL[0]; i++) {
        rd_rel_at(FULL[i], false, true, &st);
        TEST_ASSERT_EQUAL_UINT32(3000000u, st.counter);
    }
    rd_rel_at(879, false, true, &st);
    TEST_ASSERT_NOT_EQUAL(2, st.lamp);
    TEST_ASSERT_FALSE(st.final_lid);
    rd_rel_at(880, false, true, &st);
    TEST_ASSERT_EQUAL_UINT8(2, st.lamp);
    TEST_ASSERT_TRUE(st.final_lid);
    rd_rel_at(880, false, false, &st);   /* unset: the lamp stays red */
    TEST_ASSERT_NOT_EQUAL(2, st.lamp);
    rd_rel_at(140, false, true, &st);
    const uint8_t l140 = st.lamp;
    rd_rel_at(280, false, true, &st);
    TEST_ASSERT_TRUE(l140 != st.lamp && l140 <= 1 && st.lamp <= 1);   /* the 140 ms blink */
    rd_rel_at(899, false, true, &st);
    TEST_ASSERT_EQUAL_INT16(0, st.rows.dy);
    rd_rel_at(955, false, true, &st);
    TEST_ASSERT_EQUAL_INT16(4, st.rows.dy);   /* 3.5 sin(pi / 2) through the table, rounded: pinned */
    rd_rel_at(1010, false, true, &st);
    TEST_ASSERT_EQUAL_INT16(0, st.rows.dy);
    TEST_ASSERT_EQUAL_FLOAT(0.f, st.rows.theta);
    rd_rel_at(1620, false, true, &st);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 67.f, st.rows.theta / D2R);   /* PR11: never "at least the gone angle" */
    TEST_ASSERT_TRUE(st.rows.theta < omni_hinge_gone_rad(&RD_LID_HINGE));
    TEST_ASSERT_FALSE(st.lid_gone);
    static const uint32_t GONE[] = {1621, 1640, 2000, 2150};
    for (unsigned i = 0; i < sizeof GONE / sizeof GONE[0]; i++) {
        rd_rel_at(GONE[i], false, true, &st);
        TEST_ASSERT_TRUE(st.lid_gone);
    }
    rd_rel_at(1260, false, true, &st);
    TEST_ASSERT_INT_WITHIN(3, 33, st.flash_a8);   /* 13 % +- 1 of 255 */
    rd_rel_at(1050, false, true, &st);
    TEST_ASSERT_EQUAL_UINT8(0, st.flash_a8);
    rd_rel_at(1700, false, true, &st);
    TEST_ASSERT_EQUAL_UINT8(0, st.flash_a8);
    for (uint32_t t = 0; t <= 2200; t += 50) {
        rd_rel_at(t, false, true, &st);
        TEST_ASSERT_EQUAL_UINT32(t, st.wake_ms);
        TEST_ASSERT_EQUAL(t >= RD_REL_MS, st.done);
    }
    TEST_ASSERT_EQUAL_UINT16(RD_REL_MS, RD_REVEAL_STASIS.len_ms);
    TEST_ASSERT_EQUAL_UINT16(RD_REL_FADE_MS, RD_REVEAL_STASIS.reduced_ms);
    TEST_ASSERT_EQUAL_UINT16(RD_REL_POP_MS, RD_REVEAL_STASIS.pop_ms);
    TEST_ASSERT_NULL(RD_REVEAL_STASIS.pop_asset);
    TEST_ASSERT_EQUAL(OMNI_HINGE_HORIZONTAL, RD_LID_HINGE.axis);
    TEST_ASSERT_EQUAL_FLOAT(0.f, RD_LID_HINGE.pos);
    TEST_ASSERT_EQUAL_FLOAT(560.f, RD_LID_HINGE.focal);
    TEST_ASSERT_EQUAL_UINT16(466, RD_LID_HINGE.n);
    rd_rel_skip(&st);
    TEST_ASSERT_TRUE(st.done && st.lid_gone);
    TEST_ASSERT_EQUAL_UINT8(255, st.rows.fade);
    TEST_ASSERT_EQUAL_UINT8(0, st.flash_a8);
    /* the swing's look: the shade .55 (1 - cos theta), the 10 px edge's quad, the rim's cold light */
    rd_rel_at(1400, false, true, &st);
    TEST_ASSERT_TRUE(st.rows.theta > 0 && st.rows.shade > 0 && st.rows.edge_px > 0 && st.rows.rim_a8 > 0);
    TEST_ASSERT_EQUAL_HEX16(RD_RGB(0x0C, 0x0E, 0x0F), st.rows.edge_rgb);
    TEST_ASSERT_EQUAL_HEX16(RD_RGB(170, 230, 190), st.rows.rim_rgb);
    /* reduced: the cross-fade alone */
    rd_rel_at(0, true, true, &st);
    TEST_ASSERT_EQUAL_UINT8(255, st.door_a8);
    TEST_ASSERT_TRUE(st.final_lid && st.rows.fade == 255 && st.flash_a8 == 0 && st.rows.dy == 0);
    rd_rel_at(RD_REL_FADE_MS, true, true, &st);
    TEST_ASSERT_EQUAL_UINT8(0, st.door_a8);
    TEST_ASSERT_TRUE(st.done);
}

/* PR11: after 1620 ms no lid row, edge quad or rim is drawn: the frame is the interior, the flash and the mist alone */
static void expected_no_lid(uint32_t t, const fk_face_in_t *in0, uint16_t *out)
{
    rd_rel_state_t st;
    rd_rel_at(t, false, in0->clk.set, &st);
    fk_face_in_t in = *in0;
    in.wake_ms = t;
    in.reveal_live = true;
    RD_FACE_STASIS.draw(s_face, &in, (omni_rect_t){0, 0, W, W}, out, W);
    const omni_rect_t all = {0, 0, W, W};
    if (st.flash_a8) {   /* 'lighter': each channel + 8-bit (200, 255, 215) x a' >> 8, saturating */
        const int ap = omni_amul(st.flash_a8);
        const int fr = (200 * ap) >> 8, fg = (255 * ap) >> 8, fb = (215 * ap) >> 8;
        for (int i = 0; i < W * W; i++) {
            const uint16_t p = out[i];
            int r = omni_ex_r(p) + fr, g = omni_ex_g(p) + fg, b = omni_ex_b(p) + fb;
            r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
            out[i] = omni_pack565(r, g, b);
        }
    }
    rd_puff_t pf[14];
    static const int R[] = RD_MIST_RADII;
    const int n = rd_rel_mist(t, pf);
    for (int i = 0; i < n; i++)
        fk_glow_add_lut(out, W, &all, pf[i].x, pf[i].y, R[pf[i].r_idx], rd_glow_lut(R[pf[i].r_idx]),
                        RD_RGB(190, 215, 200), pf[i].a8);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (!on_panel(x, y)) out[y * W + x] = 0;
}

static unsigned diff_count(const uint16_t *a, const uint16_t *b)
{
    unsigned n = 0;
    int first = -1, last = -1;
    for (int i = 0; i < W * W; i++)
        if (a[i] != b[i]) {
            if (first < 0) first = i;
            last = i;
            n++;
        }
    if (n)
        printf("  %u px differ, first (%d, %d) %04x vs %04x, last (%d, %d)\n", n, first % W, first / W, a[first], b[first],
               last % W, last / W);
    return n;
}

static void test_rd_rel_lid_gone(void)
{
    const fk_face_in_t in = base_in(0);
    char what[64];
    for (uint32_t t = 1640; t <= RD_REL_MS; t += 20) {
        render(t, false, false, &in, 8, true, s_a);
        expected_no_lid(t, &in, s_b);
        snprintf(what, sizeof what, "t %u: a lid pixel after 1620 ms", (unsigned)t);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_b), what);
    }
    render(1621, false, false, &in, 8, true, s_a);
    expected_no_lid(1621, &in, s_b);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
    /* at 1620 the sliver (and its edge quad) is still drawn */
    render(1620, false, false, &in, 8, true, s_a);
    expected_no_lid(1620, &in, s_b);
    TEST_ASSERT_TRUE(diff_count(s_a, s_b) > 0);
}

/* ---- the counter's text (PR41) */
static const fk_atlas_t *s_core_want;
static int s_box_x0, s_box_x1, s_boxes;
static void glyph_rec(const fk_atlas_t *at, int x0, int y0, int x1, int y1)
{
    (void)y0, (void)y1;
    if (!s_core_want || at->img->a != s_core_want->img->a) return;
    if (x0 < s_box_x0) s_box_x0 = x0;
    if (x1 > s_box_x1) s_box_x1 = x1;
    s_boxes++;
}

static void test_rd_rel_counter_groups(void)
{
    char s[16];
    TEST_ASSERT_EQUAL_INT(13, rd_rel_counter_str(1234567u, s));
    TEST_ASSERT_EQUAL_STRING("1 234 567 YRS", s);
    TEST_ASSERT_EQUAL_INT(13, rd_rel_counter_str(0, s));   /* the mockup's padStart(7, '0') (review I1) */
    TEST_ASSERT_EQUAL_STRING("0 000 000 YRS", s);
    rd_rel_counter_str(678012u, s);
    TEST_ASSERT_EQUAL_STRING("0 678 012 YRS", s);
    TEST_ASSERT_EQUAL_INT(13, rd_rel_counter_str(3000000u, s));
    TEST_ASSERT_EQUAL_STRING("3 000 000 YRS", s);
    rd_rel_counter_str(999u, s);
    TEST_ASSERT_EQUAL_STRING("0 000 999 YRS", s);
    rd_rel_counter_str(1000u, s);
    TEST_ASSERT_EQUAL_STRING("0 001 000 YRS", s);
    rd_rel_counter_str(45678u, s);
    TEST_ASSERT_EQUAL_STRING("0 045 678 YRS", s);
    /* review M1: any n fits out[16] (clamped to seven digits); a canary past the NUL stays */
    char big[17];
    memset(big, 0x7E, sizeof big);
    TEST_ASSERT_EQUAL_INT(13, rd_rel_counter_str(UINT32_MAX, big));
    TEST_ASSERT_EQUAL_STRING("9 999 999 YRS", big);
    TEST_ASSERT_EQUAL_HEX8(0x7E, big[14]);
    TEST_ASSERT_EQUAL_HEX8(0x7E, big[16]);
    rd_rel_counter_str(10000000u, s);
    TEST_ASSERT_EQUAL_STRING("9 999 999 YRS", s);
    for (uint32_t t = 0; t <= 880; t += 20) {   /* the roll keeps its width */
        rd_rel_state_t q;
        rd_rel_at(t, false, true, &q);
        TEST_ASSERT_EQUAL_INT(13, rd_rel_counter_str(q.counter, s));
    }
    /* drawn right-aligned at x 318: the core plane's glyphs end at the text's width less nothing past 318 */
    size_t n = 0;
    const void *b = rd_assets_host_get(ASSET_RD_F_VT28, &n);
    static rd_atlas2_t vt;
    TEST_ASSERT_TRUE(rd_atlas2_from_bin(b, n, &vt));
    rd_rel_state_t st;
    rd_rel_at(500, false, true, &st);
    rd_rel_counter_str(st.counter, s);
    const int w = fk_atlas_width(&vt.core, s, 0);
    int ink_r = 0, first_ox = 0;
    {   /* the text's own ink, pen 0: its first glyph's left edge and its last glyph's right edge */
        int pen = 0;
        for (const char *c = s; *c; c++) {
            const char *g = strchr(vt.core.glyphs, *c);
            TEST_ASSERT_NOT_NULL(g);
            const fk_cell_t *cl = &vt.core.cells[g - vt.core.glyphs];
            if (c == s) first_ox = cl->ox;
            if (cl->w) ink_r = pen + cl->ox + cl->w;
            pen += cl->adv;
        }
    }
    s_core_want = &vt.core;
    s_box_x0 = 1 << 20, s_box_x1 = -(1 << 20), s_boxes = 0;
    fk_test_glyph_hook = glyph_rec;
    const fk_face_in_t in = base_in(0);
    render(500, false, false, &in, W, true, s_a);
    fk_test_glyph_hook = NULL;
    s_core_want = NULL;
    TEST_ASSERT_TRUE(s_boxes >= 9);
    TEST_ASSERT_EQUAL_INT(RD_REL_COUNTER_X - w + first_ox, s_box_x0);
    TEST_ASSERT_EQUAL_INT(RD_REL_COUNTER_X - w + ink_r, s_box_x1);
}

/* ---- the mist */
static void test_rd_rel_mist(void)
{
    rd_puff_t a[14], b[14];
    static const uint32_t NONE[] = {0, 500, 919, 920, 1900, 1901, 2150};
    for (unsigned i = 0; i < sizeof NONE / sizeof NONE[0]; i++) TEST_ASSERT_EQUAL_INT(0, rd_rel_mist(NONE[i], a));
    int alive = 0;
    for (uint32_t t = 921; t < 1900; t += 7) {
        const int n = rd_rel_mist(t, a);
        TEST_ASSERT_TRUE(n >= 0 && n <= 14);
        TEST_ASSERT_EQUAL_INT(n, rd_rel_mist(t, b));
        if (n) TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(rd_puff_t) * (size_t)n);   /* deterministic */
        for (int i = 0; i < n; i++) {
            TEST_ASSERT_TRUE(a[i].r_idx < RD_MIST_N_RADII);
            TEST_ASSERT_TRUE(a[i].a8 > 0 && a[i].a8 <= 29);   /* .11 sin(pi p) at most */
            TEST_ASSERT_TRUE(a[i].y > 233 - 30);               /* from the lower rim */
        }
        alive += n;
    }
    TEST_ASSERT_TRUE(alive > 100);
    rd_rel_mist(1400, a);
    TEST_ASSERT_EQUAL_INT(14, rd_rel_mist(1400, a));
}

/* ---- the frames */
static void test_rd_release_band_exact(void)
{
    static const uint32_t T[] = {0, 100, 300, 600, 879, 880, 950, 1010, 1200, 1500, 1620, 2000, 2150};
    static const unsigned STILL[] = {0, 7, 8};
    char what[64];
    for (unsigned k = 0; k < 3; k++) {
        const fk_face_in_t in = base_in(STILL[k]);
        for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
            render(T[i], false, false, &in, W, true, s_a);
            render(T[i], false, false, &in, 8, true, s_b);
            render(T[i], false, false, &in, 8, false, s_c);   /* the whole interior: the need covers what bands read */
            snprintf(what, sizeof what, "still %u, t %u", STILL[k], (unsigned)T[i]);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_b), what);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(0, diff_count(s_a, s_c), what);
        }
    }
    /* out of order (a skip back, a replay): every frame is its time's alone */
    const fk_face_in_t in = base_in(0);
    render(950, false, false, &in, 8, true, s_a);
    render(300, false, false, &in, 8, true, s_b);
    render(2000, false, false, &in, 8, true, s_c);
    render(950, false, false, &in, 8, true, s_c);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_c));
    render(300, false, false, &in, 8, true, s_c);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_b, s_c));
}

static void test_rd_release_end_seam(void)
{
    static const unsigned STILL[] = {0, 3, 7, 8};
    for (unsigned k = 0; k < 4; k++) {
        const fk_face_in_t in = base_in(STILL[k]);
        rd_still_frame(&RD_FACE_STASIS, s_face, STILL[k], s_b);
        fk_rframe_t fr = render(RD_REL_MS, false, false, &in, 8, true, s_a);
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
        fr = render(400, true, false, &in, 8, true, s_a);   /* a tap: the end frame */
        TEST_ASSERT_TRUE(fr.settled);
        TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
        fr = render(RD_REL_MS - 40, false, false, &in, 8, true, s_a);
        TEST_ASSERT_FALSE(fr.settled);
    }
    /* every second of a minute: the seconds slot's glow reaches past the panel's rim at some, where the reveal's band
     * is black (the panel's mask, as fk_rows_band's) and the face's own frame is not: equal on every panel pixel */
    fk_face_in_t in = base_in(0);
    for (int sec = 0; sec < 60; sec++) {
        in.clk.s = (uint8_t)sec;
        fk_face_in_t f = in;
        RD_FACE_STASIS.draw(s_face, &f, (omni_rect_t){0, 0, W, W}, s_b, W);
        render(RD_REL_MS, false, false, &in, 8, true, s_a);
        unsigned bad = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) bad += on_panel(x, y) ? s_a[y * W + x] != s_b[y * W + x] : s_a[y * W + x] != 0;
        TEST_ASSERT_EQUAL_UINT(0, bad);
    }
}

static void test_rd_release_reduced(void)
{
    const fk_face_in_t in = base_in(0);
    render(0, false, true, &in, 8, true, s_a);   /* the final lid */
    fk_face_in_t rin = in;   /* the settled face under reduced motion (its seconds slot steps once a second) */
    rin.reduced = true;
    RD_FACE_STASIS.draw(s_face, &rin, (omni_rect_t){0, 0, W, W}, s_b, W);
    fk_rframe_t fr = render(RD_REL_FADE_MS, false, true, &in, 8, true, s_c);
    TEST_ASSERT_TRUE(fr.settled);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_b, s_c));   /* t 500: the settled face */
    /* t 0 is the final lid: the printed counter (amber in the recess) and the green lamp */
    unsigned amber = 0;
    for (int y = 286; y < 310; y++)
        for (int x = 160; x < 320; x++) {
            const uint16_t p = s_a[y * W + x];
            amber += omni_ex_r(p) > 180 && omni_ex_g(p) > 100 && omni_ex_b(p) < 110;
        }
    TEST_ASSERT_TRUE(amber > 100);
    const uint16_t lamp = s_a[276 * W + 311];
    TEST_ASSERT_TRUE(omni_ex_g(lamp) > 150 && omni_ex_r(lamp) < 150);
    render(0, false, false, &in, 8, true, s_c);   /* the full reveal's t 0: the shut lid at fade 0 is black */
    for (int i = 0; i < W * W; i++) TEST_ASSERT_EQUAL_HEX16(0, s_c[i]);
    rd_rel_state_t st;
    rd_rel_at(250, true, true, &st);
    TEST_ASSERT_TRUE(st.door_a8 > 0 && st.door_a8 < 255);
    render(250, false, true, &in, 8, true, s_c);
    unsigned bad = 0;
    for (int i = 0; i < W * W; i++) bad += s_c[i] != omni_blend565(s_b[i], s_a[i], st.door_a8);
    TEST_ASSERT_EQUAL_UINT(0, bad);   /* a pure blend: no mist, no counter roll */
}

static uint32_t fnv(const uint16_t *p)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < W * W; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* the final review, M4: the settled face is drawn whole on the first settled frame and reused after it (an empty
 * need), and every frame equals one drawn over a whole interior; a frame of the reveal's time draws it again */
static void test_rd_release_settled_once(void)
{
    const fk_face_in_t in = base_in(0);
    uint32_t h[RD_REL_FADE_MS / 50 + 1];
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    for (uint32_t t = 0; t <= RD_REL_FADE_MS; t += 50) {   /* reduced: the cross-fade, every frame settled */
        const fk_rframe_t fr = render(t, false, true, &in, 8, true, s_a);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, fr.wake_ms);
        TEST_ASSERT_EQUAL(t == 0, !empty(fr.need));
        h[t / 50] = fnv(s_a);
    }
    for (uint32_t t = 0; t <= RD_REL_FADE_MS; t += 50) {   /* each frame over a whole fresh interior */
        TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
        render(t, false, true, &in, 8, false, s_a);
        TEST_ASSERT_EQUAL_HEX32(h[t / 50], fnv(s_a));
    }
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    fk_rframe_t fr = render(1000, false, false, &in, 8, true, s_a);    /* the reveal's time */
    TEST_ASSERT_TRUE(fr.wake_ms == 1000 && !fr.settled);
    fr = render(RD_REL_MS, false, false, &in, 8, true, s_a);           /* the settled end: whole */
    TEST_ASSERT_TRUE(fr.settled && fr.need.x0 == 0 && fr.need.y0 == 0 && fr.need.x1 == W && fr.need.y1 == W);
    fr = render(RD_REL_MS, false, false, &in, 8, true, s_b);           /* again: reused, the same pixels */
    TEST_ASSERT_TRUE(fr.settled && empty(fr.need));
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
    fr = render(1500, false, false, &in, 8, true, s_b);                /* the reveal's time draws again */
    TEST_ASSERT_FALSE(empty(fr.need));
    fr = render(RD_REL_MS, false, false, &in, 8, true, s_b);           /* so the settled end is whole once more */
    TEST_ASSERT_TRUE(fr.need.x1 == W && fr.need.y1 == W && fr.need.x0 == 0 && fr.need.y0 == 0);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
}

/* the interior under the reveal: the Stasis Clock settles (1150-2000 ms) and thaws (1350-2100 ms) by wake_ms, and is
 * its settled frame from 2100 on (so the reveal's last frames meet the end seam) */
static void test_rd_release_interior_settle(void)
{
    fk_face_in_t in = base_in(0);
    rd_still_frame(&RD_FACE_STASIS, s_face, 0, s_b);
    static const uint32_t SETTLED[] = {2100, 2120, 2149};
    for (unsigned i = 0; i < 3; i++) {
        in.wake_ms = SETTLED[i];
        RD_FACE_STASIS.draw(s_face, &in, (omni_rect_t){0, 0, W, W}, s_a, W);
        TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
    }
    in.wake_ms = 1100;   /* before the settle: no header, date, readout or seconds slot; the time in frost */
    RD_FACE_STASIS.draw(s_face, &in, (omni_rect_t){0, 0, W, W}, s_a, W);
    unsigned lit = 0;
    for (int y = RD_ST_READOUT.y0; y < RD_ST_READOUT.y1; y++)
        for (int x = RD_ST_READOUT.x0; x < RD_ST_READOUT.x1; x++) lit += s_a[y * W + x] != 0;
    for (int y = RD_ST_DATE.y0; y < RD_ST_DATE.y1; y++)
        for (int x = RD_ST_DATE.x0; x < RD_ST_DATE.x1; x++) lit += s_a[y * W + x] != 0;
    TEST_ASSERT_EQUAL_UINT(0, lit);
    in.wake_ms = 1700;   /* settling and thawing: neither the frost-only nor the settled frame */
    RD_FACE_STASIS.draw(s_face, &in, (omni_rect_t){0, 0, W, W}, s_c, W);
    TEST_ASSERT_TRUE(diff_count(s_c, s_b) > 0 && diff_count(s_c, s_a) > 0);
}

/* PR11: only STATIC bright area counts: a disc pixel with any channel above 40 that equals itself 40 ms earlier */
static void test_rd_release_burn_in(void)
{
    const fk_face_in_t in = base_in(0);
    long disc = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) disc += on_panel(x, y);
    render(0, false, false, &in, 8, true, s_a);
    uint32_t over_ms = 0, worst = 0;
    for (uint32_t t = 40; t <= RD_REL_MS; t += 40) {
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
    /* the floor (review M2): the settled face's own static bright area (2110 -> 2150, after the thaw), which the
     * measure must see, so the test is not vacuous */
    render(2110, false, false, &in, 8, true, s_a);
    render(2150, false, false, &in, 8, true, s_b);
    long floor_px = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const int i = y * W + x;
            const uint16_t p = s_b[i];
            floor_px += on_panel(x, y) && s_a[i] == p && (omni_ex_r(p) > 40 || omni_ex_g(p) > 40 || omni_ex_b(p) > 40);
        }
    printf("burn-in: static bright area above half the disc for %u ms (worst frame %u px of %ld; the settled face's "
           "%ld)\n", (unsigned)over_ms, (unsigned)worst, disc, floor_px);
    TEST_ASSERT_TRUE(floor_px > 1000);
    TEST_ASSERT_TRUE((long)worst >= floor_px);
    TEST_ASSERT_TRUE(over_ms <= 1010u);
}

static void test_rd_release_unset_interior(void)
{
    const fk_face_in_t in = base_in(7);
    rd_still_frame(&RD_FACE_STASIS, s_face, 7, s_b);
    render(RD_REL_MS, false, false, &in, 8, true, s_a);
    TEST_ASSERT_EQUAL_UINT(0, diff_count(s_a, s_b));
    /* the lamp stays red past 880, the counter still rolls to 3 000 000 */
    rd_rel_state_t st;
    rd_rel_at(1000, false, false, &st);
    TEST_ASSERT_TRUE(st.lamp <= 1 && st.counter == 3000000u);
    /* the unset lid differs from the set one in the lamp alone (red, not green), not in the interior under it; the
     * theme says the clock's state before the reveal binds (rd_rel_set_clock) */
    const fk_face_in_t set = base_in(0);
    render(1000, false, false, &set, 8, true, s_a);
    rd_rel_set_clock(false);
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    render(1000, false, false, &in, 8, true, s_b);
    unsigned n = 0, far = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++)
            if (s_a[y * W + x] != s_b[y * W + x]) {
                n++;
                const int dx = x - 311, dy = y - 276 - 3;
                far += dx * dx + dy * dy > 20 * 20;
            }
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT(0, far);
    render(0, false, true, &in, 8, true, s_c);   /* unset, reduced: the final lid's lamp is the dark housing (M3) */
    const uint16_t lamp = s_c[276 * W + 311];
    TEST_ASSERT_TRUE(omni_ex_r(lamp) < 60 && omni_ex_g(lamp) < 60);
    /* review M3: unset after 880 the live red disc and its glow keep blinking together until the swing (the lamp at
     * 880 is the dim phase, at 980 the bright one; the lid shifted by the pop's dy there) */
    rd_rel_state_t q0, q1;
    rd_rel_at(880, false, false, &q0);
    rd_rel_at(980, false, false, &q1);
    TEST_ASSERT_TRUE(q0.final_lid && q1.final_lid && q0.lamp == 0 && q1.lamp == 1);
    render(880, false, false, &in, 8, true, s_a);
    render(980, false, false, &in, 8, true, s_c);
    const uint16_t dim = s_a[(276 + q0.rows.dy) * W + 311], lit = s_c[(276 + q1.rows.dy) * W + 311];
    TEST_ASSERT_TRUE(omni_ex_r(dim) > omni_ex_g(dim) + 40 && omni_ex_r(lit) > omni_ex_g(lit) + 40);
    TEST_ASSERT_TRUE(omni_ex_r(lit) > omni_ex_r(dim) + 40);
    rd_rel_set_clock(true);
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
}

/* review M5: the frames' wall time by window (each frame's interval to the next, from the view's times) */
static void test_rd_release_timing(void)
{
    const fk_face_in_t in = base_in(0);
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, rd_assets_host_get, s_rctx));
    rd_rel_timing_reset();
    for (uint32_t t = 0; t <= RD_REL_MS; t += 50) render(t, false, false, &in, W, true, s_a);
    rd_rel_timing_t tm;
    rd_rel_timing_get(&tm);
    /* frames every 50 ms to 2150: each interval by its first frame's window */
    TEST_ASSERT_EQUAL_UINT32(21, tm.frames[0]);   /* 0 .. 1000 */
    TEST_ASSERT_EQUAL_UINT32(12, tm.frames[1]);   /* 1050 .. 1600 */
    TEST_ASSERT_EQUAL_UINT32(5, tm.frames[2]);    /* 1650 .. 1850 (1600's interval is the swing's) */
    TEST_ASSERT_EQUAL_UINT32(5, tm.frames[3]);    /* 1900 .. 2100 */
    for (int w = 0; w < 4; w++) TEST_ASSERT_EQUAL_UINT32(50u * tm.frames[w], tm.ms[w]);
    TEST_ASSERT_EQUAL_UINT32(50, tm.max_ms[2]);
}

/* the mist's and the shimmer's glow tables are const data (lesson 3, RW9): never built at bind */
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

static void test_rd_release_glow_luts_const(void)
{
    static const int R[] = RD_MIST_RADII;
    static uint8_t lut[FK_GLOW_LUT_MAX];
    for (unsigned i = 0; i <= sizeof R / sizeof R[0]; i++) {
        const int r = i < sizeof R / sizeof R[0] ? R[i] : RD_SHIMMER_R;
        const uint8_t *t = rd_glow_lut(r);
        TEST_ASSERT_NOT_NULL(t);
        TEST_ASSERT_TRUE(read_only_mapping(t));
        rd_soft_lut_build(lut, r);   /* the mockup's softSprite profile (rd/soft_lut.h) */
        TEST_ASSERT_EQUAL_MEMORY(lut, t, FK_GLOW_LUT_N(r));
    }
    static const int SR[] = RD_SOFT_LUT_RADII;
    /* the list's first entries are the reveal's (the mist's and the shimmer's); any after them are later faces' (wave 2
     * Task 3: Bay Mouth's pip glow 18 and chime glow 255, checked in test_rd_by_face) */
    TEST_ASSERT_TRUE(sizeof SR / sizeof SR[0] >= sizeof R / sizeof R[0] + 1);
    for (unsigned i = 0; i <= sizeof R / sizeof R[0]; i++) TEST_ASSERT_TRUE(SR[i] == RD_SHIMMER_R || SR[i] == R[i]);
    static const int RAD[] = RD_GLOW_LUT_RADII;
    for (unsigned i = 0; i < sizeof RAD / sizeof RAD[0]; i++) TEST_ASSERT_TRUE(read_only_mapping(rd_glow_lut(RAD[i])));
}

/* the reveal is the Stasis Clock's (PR12: in rd_core, so this test links it from rd_core alone) */
static void test_rd_release_face(void)
{
    TEST_ASSERT_TRUE(RD_FACE_STASIS.reveal);
    TEST_ASSERT_EQUAL_PTR(&RD_REVEAL_STASIS, RD_FACE_STASIS.reveal_ops);
    TEST_ASSERT_NULL(RD_FACE_STASIS.reveal_asset);
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.rctx_bytes >= (size_t)W * W * 2);   /* the composed leaf (RW6) */
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.rctx_bytes <= 520000u);
    /* the ramp: its stops run 0 .. 255; the steel run #141618 -> #2B2F34 */
    TEST_ASSERT_EQUAL_UINT8(0, RD_LID_RAMP[0].i);
    TEST_ASSERT_EQUAL_UINT8(255, RD_LID_RAMP[RD_LID_RAMP_STOPS - 1].i);
    static uint16_t ramp[256];
    rd_lid_ramp(ramp);
    bool lo = false, hi = false;
    for (int i = 0; i < 256; i++) lo |= ramp[i] == RD_RGB(0x14, 0x16, 0x18), hi |= ramp[i] == RD_RGB(0x2B, 0x2F, 0x34);
    TEST_ASSERT_TRUE(lo && hi);
    /* the table row names the same stops (tools/export/rd_layers.json; the export quantises against them) */
    FILE *f = fopen(RD_LAYERS_JSON, "r");
    TEST_ASSERT_NOT_NULL(f);
    static char js[1 << 17];
    const size_t n = fread(js, 1, sizeof js - 1, f);
    fclose(f);
    js[n] = 0;
    const char *row = strstr(js, "\"name\": \"rd_st_lid\"");
    TEST_ASSERT_NOT_NULL(row);
    const char *rp = strstr(row, "\"ramp\": [");
    TEST_ASSERT_NOT_NULL(rp);
    rp += strlen("\"ramp\": [");
    for (int k = 0; k < RD_LID_RAMP_STOPS; k++) {
        unsigned i = 0, c = 0;
        TEST_ASSERT_EQUAL_INT(2, sscanf(rp, " [%u, \"#%6x\"]", &i, &c));
        TEST_ASSERT_EQUAL_UINT(RD_LID_RAMP[k].i, i);
        TEST_ASSERT_EQUAL_HEX32((unsigned)RD_LID_RAMP[k].r << 16 | (unsigned)RD_LID_RAMP[k].g << 8 | RD_LID_RAMP[k].b, c);
        rp = strchr(rp, ']') + 1;
        if (*rp == ',') rp++;
    }
    TEST_ASSERT_EQUAL_CHAR(']', *rp);
}

/* the swing (drawLid): the leaf's rows from the hinge engine, then the 10 px edge's quad in #0C0E0F under its last row,
 * its rim's cold light at the quad's foot; the leaf rows darkened by the shade */
static void test_rd_release_swing_edge(void)
{
    const fk_face_in_t in = base_in(0);
    static const uint32_t T[] = {1400, 1500, 1580};   /* the lower edge on screen (earlier its thickness is hidden) */
    for (unsigned k = 0; k < 3; k++) {
        rd_rel_state_t st;
        rd_rel_at(T[k], false, true, &st);
        static fk_row_t rows[W];
        fk_rows_map(&RD_LID_HINGE, &st.rows, 0, W, rows);
        int L = -1;
        for (int y = 0; y < W; y++)
            if (rows[y].v >= 0) L = y;
        TEST_ASSERT_TRUE(L > 0 && st.rows.edge_px > 1 && L + st.rows.edge_px < W);
        render(T[k], false, false, &in, 8, true, s_a);
        for (int y = L + 1; y < L + st.rows.edge_px; y++) TEST_ASSERT_EQUAL_HEX16(st.rows.edge_rgb, s_a[y * W + C_X]);
        const uint16_t rim = s_a[(L + st.rows.edge_px) * W + C_X];
        TEST_ASSERT_EQUAL_HEX16(omni_blend565(st.rows.edge_rgb, st.rows.rim_rgb, st.rows.rim_a8), rim);
        /* below the rim: the interior, the cold flash and the mist, exactly (the need drew those rows) */
        TEST_ASSERT_TRUE(st.flash_a8 > 0);
        expected_no_lid(T[k], &in, s_b);
        const int y0 = L + st.rows.edge_px + 2;
        unsigned bad = 0;
        for (int i = y0 * W; i < W * W; i++) bad += s_a[i] != s_b[i];
        TEST_ASSERT_EQUAL_UINT(0, bad);
    }
}

/* the bind's asset reads fit the view's copy (the final review, M2: with copy on, a sixth read past the old limit of 4
 * would have made Stasis Release a plain wake) */
static unsigned s_gets;
static const void *counting_get(const char *name, size_t *size)
{
    s_gets++;
    return rd_assets_host_get(name, size);
}

static void test_rd_release_bind_fits_copy(void)
{
    void *rctx = calloc(1, RD_REVEAL_STASIS.rctx_bytes);
    TEST_ASSERT_NOT_NULL(rctx);
    s_gets = 0;
    TEST_ASSERT_TRUE(RD_REVEAL_STASIS.bind(NULL, NULL, counting_get, rctx));
    TEST_ASSERT_EQUAL_UINT(5, s_gets);   /* the lid, its frame, the hazard band, the frost, the counter's atlas */
    TEST_ASSERT_TRUE(s_gets <= FK_REVEAL_BIND_ASSETS_MAX);
    free(rctx);
}

int main(void)
{
    UNITY_BEGIN();
    bind_all();
    RUN_TEST(test_rd_rel_timeline);
    RUN_TEST(test_rd_rel_lid_gone);
    RUN_TEST(test_rd_rel_counter_groups);
    RUN_TEST(test_rd_rel_mist);
    RUN_TEST(test_rd_release_band_exact);
    RUN_TEST(test_rd_release_end_seam);
    RUN_TEST(test_rd_release_reduced);
    RUN_TEST(test_rd_release_settled_once);
    RUN_TEST(test_rd_release_swing_edge);
    RUN_TEST(test_rd_release_interior_settle);
    RUN_TEST(test_rd_release_burn_in);
    RUN_TEST(test_rd_release_unset_interior);
    RUN_TEST(test_rd_release_timing);
    RUN_TEST(test_rd_release_glow_luts_const);
    RUN_TEST(test_rd_release_face);
    RUN_TEST(test_rd_release_bind_fits_copy);
    free(s_rctx);
    free(s_face);
    rd_assets_host_close();
    return UNITY_END();
}
