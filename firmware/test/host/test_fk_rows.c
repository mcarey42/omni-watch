/* rd wave 1 Task 3: the row renderer (fk/rows.h): a flat leaf hinged on a screen row over an interior, its map against
 * the hinge engine (omni_hinge_lines_at's v and inv_s, PR13), the gone angle (PR5), the shut identity, the seal pop's
 * shift, the uniform shade, the lower edge's thickness quad and its rim line, band exactness, the canvas blit's clip,
 * the extent's contract (no interior read under the leaf), and the view's row-shaped job split (fk_rs_job_rows). */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "fk/cols.h"
#include "fk/reveal_sm.h"
#include "fk/rows.h"
#include "omni/blend565.h"
#include "omni/lid.h"

#define N 466
#define PI_F 3.14159265358979f

void setUp(void) {}
void tearDown(void) {}

static const omni_hinge_t H = { OMNI_HINGE_HORIZONTAL, 0.0f, 560.0f, N, 0.0f, 0.0f };
static uint16_t s_leaf[N * N], s_int[N * N], s_a[N * N], s_b[N * N];
static fk_row_t s_rows[N];
static omni_hinge_line_t s_lines[N];

static fk_rows_state_t shut(void)
{
    fk_rows_state_t st;
    memset(&st, 0, sizeof st);
    st.fade = 255;
    return st;
}

/* a leaf and an interior that differ everywhere (no pixel of one equals the other's at the same place) */
static void patterns(void)
{
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            s_leaf[y * N + x] = (uint16_t)(((x * 7 + y * 13) & 0x7FF) | 0x8000u);   /* top bit set */
            s_int[y * N + x] = (uint16_t)((x * 3 + y * 5) & 0x7FFF);                /* top bit clear */
        }
}

static void frame(const fk_rows_state_t *st, const uint16_t *interior, uint16_t *out, int rows)
{
    fk_rows_edge_t e;   /* once per frame, as a reveal's frame does (Task 3 review M2) */
    fk_rows_edge(st, s_rows, N, N, N, &e);
    for (int y0 = 0; y0 < N; y0 += rows) {
        const omni_rect_t b = { 0, (int16_t)y0, N, (int16_t)(y0 + rows < N ? y0 + rows : N) };
        fk_rows_band(st, s_rows, &e, s_leaf, interior, b, out + (size_t)y0 * N);
    }
}

static int last_leaf_row(void)
{
    int l = -1;
    for (int y = 0; y < N; y++)
        if (s_rows[y].v >= 0) l = y;
    return l;
}

static void test_fk_rows_gone_angle(void)
{
    const float g = omni_hinge_gone_rad(&H);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 67.41f, g * 180.0f / PI_F);   /* atan2(560, 233) = 67.409 deg (PR5) */
    fk_rows_state_t st = shut();
    st.theta = g + 1e-3f;
    fk_rows_map(&H, &st, 0, N, s_rows);
    for (int y = 0; y < N; y++) TEST_ASSERT_EQUAL_INT16_MESSAGE(-1, s_rows[y].v, "a leaf row past the gone angle");
    int y0 = -5, y1 = -5;
    fk_rows_extent(&H, &st, &y0, &y1);
    TEST_ASSERT_TRUE(y0 >= y1);   /* no leaf rows */
    st.theta = g - 0.02f;   /* just before it, the leaf still shows */
    fk_rows_map(&H, &st, 0, N, s_rows);
    TEST_ASSERT_TRUE(last_leaf_row() >= 0);
}

static void test_fk_rows_lines_match_omni(void)
{
    static const float TH[4] = { 0.0f, 0.3f, 0.8f, 1.1f };
    for (int i = 0; i < 4; i++) {
        fk_rows_state_t st = shut();
        st.theta = TH[i];
        omni_hinge_lines_at(&H, TH[i], 0, N, s_lines);
        fk_rows_map(&H, &st, 0, N, s_rows);
        unsigned leaf = 0;
        for (int y = 0; y < N; y++) {
            char msg[64];
            snprintf(msg, sizeof msg, "theta %.1f row %d", (double)TH[i], y);
            TEST_ASSERT_EQUAL_INT16_MESSAGE(s_lines[y].v, s_rows[y].v, msg);
            if (s_lines[y].v >= 0) {
                leaf++;
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(s_lines[y].inv_s_q16, s_rows[y].inv_s_q16, msg);
            }
        }
        TEST_ASSERT_TRUE(leaf > 0);
        /* a part of the rows, as a band job maps them: the same rows */
        fk_row_t part[100];
        fk_rows_map(&H, &st, 180, 280, part);
        for (int y = 180; y < 280; y++) {
            TEST_ASSERT_EQUAL_INT16(s_rows[y].v, part[y - 180].v);
            if (part[y - 180].v >= 0) TEST_ASSERT_EQUAL_UINT32(s_rows[y].inv_s_q16, part[y - 180].inv_s_q16);
        }
    }
}

static void test_fk_rows_shut_identity(void)
{
    patterns();
    fk_rows_state_t st = shut();
    fk_rows_map(&H, &st, 0, N, s_rows);
    for (int y = 0; y < N; y++) {
        TEST_ASSERT_EQUAL_INT16(y, s_rows[y].v);
        TEST_ASSERT_EQUAL_UINT32(65536u, s_rows[y].inv_s_q16);
    }
    memset(s_a, 0x5A, sizeof s_a);
    frame(&st, s_int, s_a, N);
    for (int y = 0; y < N; y++) {
        int p0, p1;
        fk_panel_row(y, &p0, &p1);
        for (int x = 0; x < N; x++) {
            const uint16_t want = x >= p0 && x < p1 ? s_leaf[y * N + x] : 0;
            if (s_a[y * N + x] != want) {
                char msg[48];
                snprintf(msg, sizeof msg, "pixel (%d, %d)", x, y);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

static void test_fk_rows_dy(void)
{
    patterns();
    fk_rows_state_t st = shut();
    st.dy = 3;
    fk_rows_map(&H, &st, 0, N, s_rows);
    for (int y = 0; y < 3; y++) TEST_ASSERT_EQUAL_INT16(-1, s_rows[y].v);
    for (int y = 3; y < N; y++) TEST_ASSERT_EQUAL_INT16(y - 3, s_rows[y].v);
    frame(&st, s_int, s_a, 8);
    for (int y = 0; y < N; y++) {
        int p0, p1;
        fk_panel_row(y, &p0, &p1);
        for (int x = p0; x < p1; x++) {
            const uint16_t want = y < 3 ? s_int[y * N + x] : s_leaf[(y - 3) * N + x];
            TEST_ASSERT_EQUAL_HEX16(want, s_a[y * N + x]);
        }
    }
}

static void test_fk_rows_shade(void)
{
    const uint16_t grey = omni_pack565(128, 128, 128);
    for (int i = 0; i < N * N; i++) s_leaf[i] = grey, s_int[i] = 0x001F;
    fk_rows_state_t st = shut();
    st.theta = 0.5f;
    st.shade = 140;
    fk_rows_map(&H, &st, 0, N, s_rows);
    frame(&st, s_int, s_a, 8);
    const uint16_t want = omni_blend565(grey, 0, 140);   /* black at 140 over the leaf: one factor for every pixel */
    TEST_ASSERT_TRUE(omni_ex_r(want) < 128);
    unsigned n = 0;
    for (int y = 0; y < N; y++) {
        if (s_rows[y].v < 0) continue;
        int p0, p1;
        fk_panel_row(y, &p0, &p1);
        for (int x = p0; x < p1; x++, n++) TEST_ASSERT_EQUAL_HEX16(want, s_a[y * N + x]);
    }
    TEST_ASSERT_TRUE(n > 10000);
}

static void test_fk_rows_edge_quad(void)
{
    patterns();
    fk_rows_state_t st = shut();
    st.theta = 1.1f;
    st.edge_rgb = 0x18E3;
    st.edge_px = 10;
    st.rim_rgb = 0xA7F7;
    st.rim_px = 1.5f;
    st.rim_a8 = 0;
    fk_rows_map(&H, &st, 0, N, s_rows);
    const int L = last_leaf_row();
    TEST_ASSERT_TRUE(L > 0 && L + 13 < N);
    frame(&st, s_int, s_a, 8);
    for (int y = L; y <= L + 12; y++) {
        int p0, p1;
        fk_panel_row(y, &p0, &p1);
        for (int x = p0; x < p1; x++) {
            const uint16_t px = s_a[y * N + x];
            if (y == L) TEST_ASSERT_EQUAL_HEX16(s_leaf[s_rows[L].v * N + x] & 0x8000u, px & 0x8000u);   /* the leaf */
            else if (y <= L + 10) TEST_ASSERT_EQUAL_HEX16(st.edge_rgb, px);                             /* the quad */
            else TEST_ASSERT_EQUAL_HEX16(s_int[y * N + x], px);                                         /* below it */
        }
    }
    /* the rim on the quad's lower edge (y = L + 11): 1.5 px covers the rows whose centres lie within .75 of it */
    st.rim_a8 = 255;
    frame(&st, s_int, s_a, 8);
    for (int y = L + 9; y <= L + 12; y++) {
        int p0, p1;
        fk_panel_row(y, &p0, &p1);
        for (int x = p0; x < p1; x++) {
            const uint16_t want = y == L + 9 ? st.edge_rgb : y <= L + 11 ? st.rim_rgb : s_int[y * N + x];
            TEST_ASSERT_EQUAL_HEX16(want, s_a[y * N + x]);
        }
    }
    st.rim_px = 1.0f;   /* 1 px: the quad's last row only */
    st.rim_a8 = 128;
    frame(&st, s_int, s_a, 8);
    for (int y = L + 9; y <= L + 11; y++) {
        int p0, p1;
        fk_panel_row(y, &p0, &p1);
        for (int x = p0; x < p1; x++) {
            const uint16_t want = y == L + 9    ? st.edge_rgb
                                  : y == L + 10 ? omni_blend565(st.edge_rgb, st.rim_rgb, 128)
                                                : s_int[y * N + x];
            TEST_ASSERT_EQUAL_HEX16(want, s_a[y * N + x]);
        }
    }
    st.edge_px = 0;   /* no quad, no rim */
    frame(&st, s_int, s_a, 8);
    for (int y = L + 1; y <= L + 12; y++) {
        int p0, p1;
        fk_panel_row(y, &p0, &p1);
        for (int x = p0; x < p1; x++) TEST_ASSERT_EQUAL_HEX16(s_int[y * N + x], s_a[y * N + x]);
    }
}

static void test_fk_rows_band_exact(void)
{
    patterns();
    static const float TH[5] = { 0.0f, 0.3f, 0.8f, 1.1f, 1.17f };
    for (int i = 0; i < 5; i++) {
        fk_rows_state_t st = shut();
        st.theta = TH[i];
        st.dy = (int16_t)(i == 0 ? 2 : 0);
        st.shade = (uint8_t)(40 * i);
        st.edge_rgb = 0x0861;
        st.edge_px = 10;
        st.rim_rgb = 0xAF37;
        st.rim_a8 = 140;
        st.rim_px = 1.5f;
        st.fade = (uint8_t)(i == 2 ? 200 : 255);
        fk_rows_map(&H, &st, 0, N, s_rows);
        memset(s_a, 0x11, sizeof s_a);
        memset(s_b, 0x77, sizeof s_b);
        frame(&st, s_int, s_a, N);
        frame(&st, s_int, s_b, 8);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_a, s_b, sizeof s_a, "8-row bands");
        memset(s_b, 0x77, sizeof s_b);
        frame(&st, s_int, s_b, 1);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_a, s_b, sizeof s_a, "1-row bands");
        /* narrow bands (Task 3 review I1): left of the panel's top rows, right of them, across its edge, a column */
        fk_rows_edge_t e;
        fk_rows_edge(&st, s_rows, N, N, N, &e);
        static const omni_rect_t NB[6] = {
            { 0, 0, 10, 8 },   { 456, 0, N, 8 },   { 200, 0, 240, 8 },
            { 0, 229, 3, 237 }, { 460, 458, N, N }, { 233, 0, 234, N },
        };
        static uint16_t nb[N * N];
        for (int k = 0; k < 6; k++) {
            const omni_rect_t r = NB[k];
            memset(nb, 0x33, sizeof nb);
            fk_rows_band(&st, s_rows, &e, s_leaf, s_int, r, nb + (size_t)r.y0 * N);
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++) {
                    const bool in = x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
                    const uint16_t want = in ? s_a[y * N + x] : 0x3333;
                    if (nb[y * N + x] != want) {
                        char msg[64];
                        snprintf(msg, sizeof msg, "theta %d band %d pixel (%d, %d)", i, k, x, y);
                        TEST_FAIL_MESSAGE(msg);
                    }
                }
        }
    }
    /* the fade: the frame at 255 under black at 255 - fade (omni_blend565(0, px, fade)) */
    fk_rows_state_t sf = shut();
    sf.theta = 0.8f;
    sf.shade = 30;
    sf.edge_px = 10;
    sf.edge_rgb = 0x0861;
    fk_rows_map(&H, &sf, 0, N, s_rows);
    frame(&sf, s_int, s_a, 8);
    sf.fade = 100;
    frame(&sf, s_int, s_b, 8);
    for (int i = 0; i < N * N; i++) TEST_ASSERT_EQUAL_HEX16(omni_blend565(0, s_a[i], 100), s_b[i]);
    /* fade 0: black */
    fk_rows_state_t st = shut();
    st.fade = 0;
    fk_rows_map(&H, &st, 0, N, s_rows);
    frame(&st, s_int, s_a, 8);
    for (int i = 0; i < N * N; i++) TEST_ASSERT_EQUAL_HEX16(0, s_a[i]);
}

/* The extent: the leaf's rows, which the band draws from the leaf alone (an interior poisoned there changes nothing),
 * so a reveal's need rect is the rest */
static void test_fk_rows_extent(void)
{
    patterns();
    static const float TH[4] = { 0.0f, 0.4f, 0.9f, 1.15f };
    for (int i = 0; i < 4; i++) {
        fk_rows_state_t st = shut();
        st.theta = TH[i];
        st.dy = (int16_t)(i == 0 ? 3 : 0);
        st.shade = 60;
        st.edge_rgb = 0x0861;
        st.edge_px = 10;
        st.rim_rgb = 0xAF37;
        st.rim_a8 = 140;
        st.rim_px = 1.5f;
        fk_rows_map(&H, &st, 0, N, s_rows);
        int y0, y1;
        fk_rows_extent(&H, &st, &y0, &y1);
        int a = -1, z = -1;
        for (int y = 0; y < N; y++)
            if (s_rows[y].v >= 0) {
                if (a < 0) a = y;
                z = y + 1;
            }
        TEST_ASSERT_EQUAL_INT(a, y0);
        TEST_ASSERT_EQUAL_INT(z, y1);
        frame(&st, s_int, s_a, 8);
        static uint16_t poisoned[N * N];
        memcpy(poisoned, s_int, sizeof poisoned);
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < N; x++) poisoned[y * N + x] = 0xF81F;
        frame(&st, poisoned, s_b, 8);
        TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
    }
}

/* The box must be the leaf's (Task 3 review M1): any other box draws nothing */
static void test_fk_rows_blit_box_guard(void)
{
    enum { LW = 120, LH = 100 };
    static uint16_t leaf[LW * LH];
    for (int i = 0; i < LW * LH; i++) leaf[i] = 0xFFFF;
    const omni_hinge_t hb = { OMNI_HINGE_HORIZONTAL, 0.0f, 560.0f, LH, 0.0f, 0.0f };
    fk_row_t rows[LH];
    fk_rows_state_t st = shut();
    st.theta = 0.6f;
    st.edge_px = 6;
    st.rim_a8 = 255;
    st.rim_px = 1.5f;
    fk_rows_map(&hb, &st, 0, LH, rows);
    static const omni_rect_t BOX[4] = {
        { 50, 60, 50 + LW, 60 + LH + 40 }, { 50, 60, 50 + LW, 60 + LH - 1 }, { 50, 60, 50 + LW + 1, 60 + LH },
        { 50, 60, 50 + LW - 20, 60 + LH },
    };
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < N * N; i++) s_a[i] = 0x1234;
        fk_rows_blit(&st, rows, leaf, LW, LH, BOX[k], (omni_rect_t){ 0, 0, N, N }, s_a, N);
        for (int i = 0; i < N * N; i++) TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x1234, s_a[i], "a box that is not the leaf's");
    }
    const omni_rect_t own = { 50, 60, 50 + LW, 60 + LH }, all = { 0, 0, N, N };
    fk_rows_blit(&st, rows, leaf, LW, LH, own, all, s_a, N);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, s_a[60 * N + 50 + LW / 2]);   /* the leaf's own box draws */
}

static void test_fk_rows_blit_clip(void)
{
    enum { LW = 120, LH = 100 };
    static uint16_t leaf[LW * LH];
    for (int i = 0; i < LW * LH; i++) leaf[i] = (uint16_t)(0x8000u | (unsigned)(i * 37 & 0x7FFF));
    const omni_hinge_t hb = { OMNI_HINGE_HORIZONTAL, 0.0f, 560.0f, LH, 0.0f, 0.0f };
    const omni_rect_t box = { 50, 60, 50 + LW, 60 + LH };
    fk_row_t rows[LH];
    static const omni_rect_t CLIPS[4] = {
        { 80, 40, 300, 130 }, { 0, 0, N, N }, { 60, 100, 70, 110 }, { 0, 0, 40, 40 },
    };
    static const float TH[3] = { 0.0f, 0.6f, 1.0f };
    for (int t = 0; t < 3; t++) {
        fk_rows_state_t st = shut();
        st.theta = TH[t];
        st.shade = 50;
        st.edge_rgb = 0x0861;
        st.edge_px = 6;
        st.rim_rgb = 0xAF37;
        st.rim_a8 = 200;
        st.rim_px = 1.5f;
        fk_rows_map(&hb, &st, 0, LH, rows);
        /* the reference: the whole box */
        for (int i = 0; i < N * N; i++) s_b[i] = 0x1234;
        fk_rows_blit(&st, rows, leaf, LW, LH, box, box, s_b + box.y0 * N + box.x0, N);
        if (t == 0)   /* shut: the leaf exactly */
            for (int y = 0; y < LH; y++)
                for (int x = 0; x < LW; x++)
                    TEST_ASSERT_EQUAL_HEX16(omni_blend565(leaf[y * LW + x], 0, 50), s_b[(box.y0 + y) * N + box.x0 + x]);
        for (int c = 0; c < 4; c++) {
            const omni_rect_t cl = CLIPS[c];
            for (int i = 0; i < N * N; i++) s_a[i] = 0x1234;
            fk_rows_blit(&st, rows, leaf, LW, LH, box, cl, s_a + cl.y0 * N + cl.x0, N);
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++) {
                    const bool in = x >= box.x0 && x < box.x1 && y >= box.y0 && y < box.y1 && x >= cl.x0 &&
                                    x < cl.x1 && y >= cl.y0 && y < cl.y1;
                    const uint16_t want = in ? s_b[y * N + x] : 0x1234;
                    if (s_a[y * N + x] != want) {
                        char msg[64];
                        snprintf(msg, sizeof msg, "theta %d clip %d pixel (%d, %d)", t, c, x, y);
                        TEST_FAIL_MESSAGE(msg);
                    }
                }
        }
    }
}

/* The view's job split for a row-shaped need (the doors' need is columns): each job's rows of the need, on the panel */
static void test_fk_rows_job_rows(void)
{
    static const omni_rect_t NEED[5] = {
        { 0, 300, N, N }, { 0, 0, N, 3 }, { 100, 0, 366, N }, { 40, 120, 200, 260 }, { 0, 0, 0, 0 },
    };
    for (int k = 0; k < 5; k++) {
        const omni_rect_t n = NEED[k];
        for (int y0 = 0; y0 < N; y0 += 16) {
            const int y1 = y0 + 16 < N ? y0 + 16 : N;
            omni_rect_t r;
            fk_rs_job_rows(y0, y1, n, &r);
            const bool empty = r.x0 >= r.x1 || r.y0 >= r.y1;
            if (!empty) {   /* inside the job and the need */
                TEST_ASSERT_TRUE(r.y0 >= y0 && r.y1 <= y1 && r.y0 >= n.y0 && r.y1 <= n.y1);
                TEST_ASSERT_TRUE(r.x0 >= n.x0 && r.x1 <= n.x1);
            }
            for (int y = y0; y < y1; y++) {   /* every panel pixel of the need in the job's rows */
                if (y < n.y0 || y >= n.y1) continue;
                int p0, p1;
                fk_panel_row(y, &p0, &p1);
                const int a = p0 > n.x0 ? p0 : n.x0, z = p1 < n.x1 ? p1 : n.x1;
                if (a >= z) continue;
                TEST_ASSERT_FALSE(empty);
                TEST_ASSERT_TRUE(y >= r.y0 && y < r.y1 && a >= r.x0 && z <= r.x1);
            }
            if (n.y0 == 0 && n.y1 == N && n.x0 < n.x1) {   /* a column need: fk_rs_job_cols's columns */
                int a, z;
                fk_rs_job_cols(y0, y1, n.x0, n.x1, &a, &z);
                TEST_ASSERT_EQUAL_INT(a, r.x0);
                TEST_ASSERT_EQUAL_INT(z, r.x1);
            }
        }
    }
}

/* rd wave 1 Task 10: interior NULL draws over what out holds on the panel, in place: the same frame as the interior
 * given, when out holds the interior's band first (a theme lays its own layers between the two) */
static void test_fk_rows_band_in_place(void)
{
    patterns();
    static const float TH[4] = { 0.0f, 0.5f, 1.0f, 1.17f };
    for (int i = 0; i < 4; i++) {
        fk_rows_state_t st = shut();
        st.theta = TH[i];
        st.dy = (int16_t)(i == 0 ? 3 : 0);
        st.shade = (uint8_t)(50 * i);
        st.edge_rgb = 0x0861;
        st.edge_px = 9;
        st.rim_rgb = 0xAF37;
        st.rim_a8 = 140;
        st.rim_px = 1.5f;
        st.fade = (uint8_t)(i == 1 ? 120 : 255);
        fk_rows_map(&H, &st, 0, N, s_rows);
        frame(&st, s_int, s_a, 8);
        memcpy(s_b, s_int, sizeof s_b);
        frame(&st, NULL, s_b, 8);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(s_a, s_b, sizeof s_a, "in place");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fk_rows_gone_angle);
    RUN_TEST(test_fk_rows_lines_match_omni);
    RUN_TEST(test_fk_rows_shut_identity);
    RUN_TEST(test_fk_rows_dy);
    RUN_TEST(test_fk_rows_shade);
    RUN_TEST(test_fk_rows_edge_quad);
    RUN_TEST(test_fk_rows_band_exact);
    RUN_TEST(test_fk_rows_band_in_place);
    RUN_TEST(test_fk_rows_extent);
    RUN_TEST(test_fk_rows_blit_clip);
    RUN_TEST(test_fk_rows_blit_box_guard);
    RUN_TEST(test_fk_rows_job_rows);
    return UNITY_END();
}
