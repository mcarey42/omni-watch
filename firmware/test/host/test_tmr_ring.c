#include "unity.h"
#include "omni/blend565.h"
#include "omni/raster.h"
#include "omni/timer.h"
#include "omni/tmr_jog.h"
#include "omni/tmr_ring.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define TAU 6.28318530717958647692
#define C 219.0                      /* the ring centre in box px (233 - 14) */
#define NPIX (OMNI_TMR_BOX * OMNI_TMR_BOX)

static omni_tmr_lut_t g_lut;
static void *g_mem;
static uint16_t g_box[NPIX], g_box2[NPIX];

static void lut_once(void)
{
    if (g_mem) return;
    g_mem = malloc(omni_tmr_lut_bytes());
    TEST_ASSERT_NOT_NULL(g_mem);
    TEST_ASSERT_TRUE(omni_tmr_lut_build(&g_lut, g_mem, omni_tmr_lut_bytes()));
}

static void draw(uint16_t *box, float min, bool red, uint8_t alpha, float bead)
{
    const omni_tmr_ring_t s = { min, red, alpha, bead };
    lut_once();
    memset(box, 0, sizeof g_box);
    omni_tmr_ring_draw(&g_lut, &s, box, 0, OMNI_TMR_BOX);
}

/* the box pixel at radius r, clock angle a (rad from 12 clockwise) */
static uint16_t at(const uint16_t *box, double r, double a)
{
    const int x = (int)floor(C + r * sin(a)), y = (int)floor(C - r * cos(a));
    return box[y * OMNI_TMR_BOX + x];
}
static double min_angle(double m) { return m / 60.0 * TAU; }
static int lum(uint16_t c) { return omni_ex_r(c) + omni_ex_g(c) + omni_ex_b(c); }

void test_tmr_lut_shape(void)
{
    lut_once();
    TEST_ASSERT_TRUE(g_lut.n > 71000 && g_lut.n < 72500);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)g_lut.n * 7u, (uint32_t)omni_tmr_lut_bytes());
    TEST_ASSERT_TRUE(omni_tmr_lut_bytes() < 520000u);
    for (uint32_t i = 1; i < g_lut.n; i++) TEST_ASSERT_TRUE(g_lut.idx[i] > g_lut.idx[i - 1]);
    uint8_t maxq = 0;
    for (uint32_t i = 0; i < g_lut.n; i++) if (g_lut.rq[i] > maxq) maxq = g_lut.rq[i];
    TEST_ASSERT_TRUE(maxq <= 240 && maxq >= 238);
    /* a too-small or misaligned block is refused */
    omni_tmr_lut_t l;
    TEST_ASSERT_FALSE(omni_tmr_lut_build(&l, g_mem, omni_tmr_lut_bytes() - 1));
    TEST_ASSERT_FALSE(omni_tmr_lut_build(&l, NULL, omni_tmr_lut_bytes()));
}

void test_tmr_ring_colours(void)
{
    /* the gradient at r 195 (t = 0.5): (120,92,40) -> (201,163,91) at 0.6 */
    const double t = (195.0 - 176.0) / 38.0 / 0.6;
    const double g[3] = { 120 + 81 * t, 92 + 71 * t, 40 + 51 * t };
    const double base[3] = { 11, 10, 8 };
    draw(g_box, 30, false, 191, -1);
    const uint16_t p = at(g_box, 195.0, min_angle(15));   /* 3 o'clock */
    const int got[3] = { omni_ex_r(p), omni_ex_g(p), omni_ex_b(p) };
    for (int k = 0; k < 3; k++) {
        const double want = (g[k] * 191 + base[k] * 65) / 256.0;
        const double wq = k == 1 ? (double)((((int)want) >> 2) * 4 + (((int)want) >> 6)) : (double)((((int)want) >> 3) * 8 + (((int)want) >> 6));
        TEST_ASSERT_INT_WITHIN(6, (int)wq, got[k]);
    }
    const uint16_t q = at(g_box, 195.0, min_angle(45));   /* 9 o'clock: no sector */
    /* RGB565 holds red and blue in steps of 8 (levels 0, 8, 16): #0b0a08's 11 can only be 8, 3 off (green: 10 -> 8, 2 off) */
    TEST_ASSERT_INT_WITHIN(3, 11, omni_ex_r(q));
    TEST_ASSERT_INT_WITHIN(2, 10, omni_ex_g(q));
    TEST_ASSERT_INT_WITHIN(2, 8, omni_ex_b(q));

    draw(g_box, 30, true, 191, -1);
    const uint16_t r = at(g_box, 195.0, min_angle(15));
    TEST_ASSERT_TRUE(omni_ex_r(r) - omni_ex_g(r) > 60);
}

void test_tmr_ring_sector_edges(void)
{
    draw(g_box, 30, false, 255, -1);
    /* just inside and just outside the end at 6 o'clock, and the start at 12: 4 px off the edge line, mid-ring */
    const double da = 4.0 / 195.0;
    TEST_ASSERT_TRUE(lum(at(g_box, 195.0, min_angle(30) - da)) > 200);
    TEST_ASSERT_TRUE(lum(at(g_box, 195.0, min_angle(30) + da)) < 60);
    TEST_ASSERT_TRUE(lum(at(g_box, 195.0, da)) > 200);
    TEST_ASSERT_TRUE(lum(at(g_box, 195.0, TAU - da)) < 60);
    /* the leading edge is the light line #ecd29a on the end angle */
    const uint16_t e = at(g_box, 195.0, min_angle(30));
    TEST_ASSERT_TRUE(omni_ex_r(e) > 200);
    /* nothing outside the annulus except the hairline and the edge's overshoot */
    TEST_ASSERT_EQUAL_UINT16(0, at(g_box, 218.0, min_angle(10)));
    TEST_ASSERT_EQUAL_UINT16(0, at(g_box, 150.0, min_angle(10)));
    /* a full 60 minutes covers everything */
    draw(g_box, 60, false, 255, -1);
    TEST_ASSERT_TRUE(lum(at(g_box, 195.0, TAU - da)) > 200);
    /* zero draws no sector */
    draw(g_box, 0, false, 255, -1);
    TEST_ASSERT_TRUE(lum(at(g_box, 195.0, min_angle(10))) < 60);
}

void test_tmr_ring_ticks(void)
{
    draw(g_box, 30, false, 191, -1);
    const uint16_t on = at(g_box, 207.0, min_angle(25));
    const uint16_t off = at(g_box, 207.0, min_angle(26.5));
    TEST_ASSERT_TRUE(lum(on) > lum(off));
    /* a minute tick reaches only from R1 - 7 = 207 */
    draw(g_box, 0, false, 255, -1);
    TEST_ASSERT_TRUE(lum(at(g_box, 209.0, min_angle(26))) > lum(at(g_box, 209.0, min_angle(26.5))));
    TEST_ASSERT_TRUE(lum(at(g_box, 203.0, min_angle(25))) > lum(at(g_box, 203.0, min_angle(26))));
    /* the hairlines: brass at alpha 0.5 over black, on the 3 o'clock row (full coverage) */
    const int cols[4] = { 219 + 214, 219 + 175, 219 - 215, 219 - 176 };
    const float r0s[4] = { 214, 175, 214, 175 };
    for (int k = 0; k < 4; k++) {
        uint8_t cov;
        omni_cover_ring(219, 219, r0s[k], r0s[k] + 1, 219, cols[k], cols[k] + 1, &cov);
        TEST_ASSERT_TRUE(cov > 180);   /* the 64-gon's sag takes a little off the full pixel */
        const uint16_t want = omni_blend565(0, omni_pack565(201, 163, 91), (128 * cov + 127) / 255);
        TEST_ASSERT_EQUAL_HEX16(want, g_box[219 * OMNI_TMR_BOX + cols[k]]);
    }
    TEST_ASSERT_EQUAL_HEX16(0, g_box[219 * OMNI_TMR_BOX + 219 + 217]);
}

void test_tmr_ring_bead(void)
{
    draw(g_box, 0, false, 191, 0.75f);
    const uint16_t b = at(g_box, 162.0, min_angle(45));
    TEST_ASSERT_TRUE(omni_ex_r(b) > 200 && omni_ex_g(b) > 170);
    /* the track runs from 12 to the bead: lit at 10 o'clock side (r 166), dark past it */
    TEST_ASSERT_TRUE(lum(at(g_box, 166.0, min_angle(30))) > 100);
    TEST_ASSERT_TRUE(lum(at(g_box, 166.0, min_angle(52))) == 0);
    draw(g_box, 0, false, 191, -1);
    TEST_ASSERT_EQUAL_UINT16(0, at(g_box, 162.0, min_angle(45)));
}

void test_tmr_ring_bands_equal(void)
{
    const omni_tmr_ring_t s = { 37.3f, false, 191, 0.33f };
    lut_once();
    memset(g_box, 0, sizeof g_box);
    memset(g_box2, 0, sizeof g_box2);
    omni_tmr_ring_draw(&g_lut, &s, g_box, 0, OMNI_TMR_BOX);
    omni_tmr_ring_draw(&g_lut, &s, g_box2, 0, 219);
    omni_tmr_ring_draw(&g_lut, &s, g_box2, 219, OMNI_TMR_BOX);
    TEST_ASSERT_EQUAL_MEMORY(g_box, g_box2, sizeof g_box);
    /* uneven bands, drawn in a different order, redrawn over old content */
    memset(g_box2, 0x5a, sizeof g_box2);
    for (int y = 0; y < OMNI_TMR_BOX; y += 37) omni_tmr_ring_draw(&g_lut, &s, g_box2, y, y + 37 > OMNI_TMR_BOX ? OMNI_TMR_BOX : y + 37);
    for (uint32_t i = 0; i < g_lut.n; i++) TEST_ASSERT_EQUAL_UINT16(g_box[g_lut.idx[i]], g_box2[g_lut.idx[i]]);
}

static void check_in_rect(const uint16_t *a, const uint16_t *b, const int r[4])
{
    for (int y = 0; y < OMNI_TMR_BOX; y++)
        for (int x = 0; x < OMNI_TMR_BOX; x++)
            if (a[y * OMNI_TMR_BOX + x] != b[y * OMNI_TMR_BOX + x])
                TEST_ASSERT_TRUE_MESSAGE(x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3], "pixel differs outside the dirty rect");
}

void test_tmr_ring_dirty(void)
{
    int r[4];
    omni_tmr_ring_t a = { 10, false, 191, -1 }, b = { 11, false, 191, -1 };
    draw(g_box, a.min, a.red, a.alpha, a.bead);
    draw(g_box2, b.min, b.red, b.alpha, b.bead);
    omni_tmr_ring_dirty(&a, &b, r);
    check_in_rect(g_box, g_box2, r);
    TEST_ASSERT_TRUE((r[2] - r[0] + 1) * (r[3] - r[1] + 1) < NPIX / 4);

    /* a fractional step the other way, across a tick */
    a.min = 12.9f; b.min = 12.2f;
    draw(g_box, a.min, a.red, a.alpha, a.bead);
    draw(g_box2, b.min, b.red, b.alpha, b.bead);
    omni_tmr_ring_dirty(&a, &b, r);
    check_in_rect(g_box, g_box2, r);

    /* an alpha change: the whole sector (12 to 2 o'clock here), and not the whole box */
    a = (omni_tmr_ring_t){ 10, false, 191, -1 };
    b = (omni_tmr_ring_t){ 10, false, 120, -1 };
    draw(g_box, a.min, a.red, a.alpha, a.bead);
    draw(g_box2, b.min, b.red, b.alpha, b.bead);
    omni_tmr_ring_dirty(&a, &b, r);
    check_in_rect(g_box, g_box2, r);
    for (double m = 0.2; m < 10.0; m += 0.4)   /* every sector pixel lies inside */
        for (double rad = 178; rad < 213; rad += 4) {
            const int x = (int)floor(C + rad * sin(min_angle(m))), y = (int)floor(C - rad * cos(min_angle(m)));
            TEST_ASSERT_TRUE(x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]);
        }
    TEST_ASSERT_TRUE((r[2] - r[0] + 1) * (r[3] - r[1] + 1) < NPIX / 3);
    a.min = 40; b.min = 40;
    omni_tmr_ring_dirty(&a, &b, r);
    TEST_ASSERT_TRUE(r[3] - r[1] > 300);   /* the larger sector: most of the box's height */
    /* colour change likewise, and with a shrinking sector */
    b = (omni_tmr_ring_t){ 25, true, 191, -1 };
    draw(g_box2, b.min, b.red, b.alpha, b.bead);
    omni_tmr_ring_dirty(&a, &b, r);
    check_in_rect(g_box, g_box2, r);

    /* the stopwatch bead moving, appearing and going */
    a = (omni_tmr_ring_t){ 0, false, 191, 0.20f };
    b = (omni_tmr_ring_t){ 0, false, 191, 0.21f };
    draw(g_box, a.min, a.red, a.alpha, a.bead);
    draw(g_box2, b.min, b.red, b.alpha, b.bead);
    omni_tmr_ring_dirty(&a, &b, r);
    check_in_rect(g_box, g_box2, r);
    TEST_ASSERT_TRUE((r[2] - r[0] + 1) * (r[3] - r[1] + 1) < NPIX / 16);
    b.bead = -1;
    draw(g_box2, b.min, b.red, b.alpha, b.bead);
    omni_tmr_ring_dirty(&a, &b, r);
    check_in_rect(g_box, g_box2, r);

    /* nothing changes: an empty rect */
    omni_tmr_ring_dirty(&a, &a, r);
    TEST_ASSERT_TRUE(r[2] < r[0] || r[3] < r[1]);
}

void test_tmr_ring_digest(void)
{
    draw(g_box, 25, false, 191, -1);
    uint64_t h = 1469598103934665603ull;
    const uint8_t *p = (const uint8_t *)g_box;
    for (size_t i = 0; i < sizeof g_box; i++) h = (h ^ p[i]) * 1099511628211ull;
    printf("tmr_ring digest %016llx\n", (unsigned long long)h);
    TEST_ASSERT_EQUAL_HEX64(0xa780930f5070a851ull, h);   /* pinned on the first green run (no fused multiply-add) */
}

/* The DONE pulse (final fix, review finding 1): two layers drawn once, blended per frame. Alpha 0 and 255 give the
 * draw's bytes exactly; between them every ring-zone pixel is within 9 levels per 8-bit channel (one red or blue
 * unit, two green ones) of a draw at that alpha, and most are equal; nothing outside the zone is written. */
static int unit_diff(uint16_t a, uint16_t b)
{
    const int dr = abs(omni_ex_r(a) - omni_ex_r(b)), dg = abs(omni_ex_g(a) - omni_ex_g(b)),
              db = abs(omni_ex_b(a) - omni_ex_b(b));
    return dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
}

void test_tmr_pulse_matches_draw(void)
{
    lut_once();
    const size_t bytes = omni_tmr_pulse_bytes(&g_lut);
    TEST_ASSERT_TRUE(bytes >= 4u * g_lut.n && bytes < 4u * g_lut.n + 16384u);
    void *mem = malloc(bytes);
    TEST_ASSERT_NOT_NULL(mem);
    omni_tmr_pulse_t p;
    const omni_tmr_ring_t done = { 60, true, 123, -1 };
    TEST_ASSERT_FALSE(omni_tmr_pulse_build(&p, &g_lut, &done, g_box2, mem, bytes - 1));
    TEST_ASSERT_FALSE(omni_tmr_pulse_build(&p, &g_lut, &done, g_box2, NULL, bytes));
    TEST_ASSERT_TRUE(omni_tmr_pulse_build(&p, &g_lut, &done, g_box2, mem, bytes));
    static const uint8_t AL[] = { 0, 13, 64, 115, 160, 191, 217, 255 };
    for (size_t i = 0; i < sizeof AL; i++) {
        draw(g_box, 60, true, AL[i], -1);
        for (size_t k = 0; k < NPIX; k++) g_box2[k] = 0x1234;   /* outside the zone stays as it was */
        omni_tmr_pulse_draw(&p, AL[i], g_box2);
        int worst = 0;
        uint32_t zone = 0, differ = 0;
        for (uint32_t k = 0; k < g_lut.n; k++) {
            const uint32_t j = g_lut.idx[k];
            const int d = unit_diff(g_box[j], g_box2[j]);
            if (d > worst) worst = d;
            differ += d > 0;
            g_box2[j] = 0x1234;
            zone++;
        }
        TEST_ASSERT_EQUAL_UINT32(g_lut.n, zone);
        if (AL[i] == 0 || AL[i] == 255) TEST_ASSERT_EQUAL_INT(0, worst);
        else TEST_ASSERT_TRUE(worst <= 9 && differ < g_lut.n / 20);
        for (size_t k = 0; k < NPIX; k++) TEST_ASSERT_EQUAL_HEX16(0x1234, g_box2[k]);
    }
    /* the layers do differ: the pulse is visible (the sector at 217 is far brighter than at 13) */
    draw(g_box, 60, true, 13, -1);
    const int dim = lum(at(g_box, 200, 1.0));
    omni_tmr_pulse_draw(&p, 217, g_box2);
    TEST_ASSERT_TRUE(lum(at(g_box2, 200, 1.0)) > dim + 100);
    free(mem);
}

/* Owner bug (fe84ff4): "the timer's bar doesn't go past 30 minutes" although the digits set above 30. The real path:
 * a rim jog (omni_tmr_drag, R144: 30 min per turn) from IDLE, the view, then the face's ring_update: a whole draw,
 * then omni_tmr_ring_dirty from the drawn state and a draw of the dirty rows only. */
static omni_tmr_t JT;
static omni_tmr_drag_t JD;
static int16_t JX0, JY0;
static int16_t jx(double deg) { return (int16_t)lround(OMNI_TMR_CX + 200.0 * sin(deg * TAU / 360.0)); }
static int16_t jy(double deg) { return (int16_t)lround(OMNI_TMR_CY - 200.0 * cos(deg * TAU / 360.0)); }
static void jog_send(omni_gesture_kind_t k, double deg)
{
    omni_gesture_t g;
    memset(&g, 0, sizeof g);
    g.kind = k;
    g.x = jx(deg);
    g.y = jy(deg);
    g.x0 = JX0;
    g.y0 = JY0;
    omni_tmr_drag(&JD, &JT, &g);
}
/* one touch on the rim from a0 clockwise by span degrees in 3 degree samples; lifted unless hold */
static void jog(double a0, double span, bool hold)
{
    JX0 = jx(a0);
    JY0 = jy(a0);
    const int n = (int)ceil(span / 3.0);
    for (int i = 1; i <= n; i++) jog_send(i == 1 ? G_DRAG_START : (i == n && !hold) ? G_DRAG_END : G_DRAG_MOVE, a0 + span * i / n);
}
/* the face's ring_update: whole when nothing is drawn, else the dirty rows from the drawn state */
static omni_tmr_ring_t g_drawn;
static bool g_drawn_ok;
static void face_update(uint16_t *box)
{
    omni_tmr_view_t v;
    omni_tmr_view(&JT, 1000, &v);
    const omni_tmr_ring_t s = { .min = v.sector_min, .red = v.sector_red, .alpha = v.alpha, .bead = v.bead };
    int r[4] = { 0, 0, OMNI_TMR_BOX - 1, OMNI_TMR_BOX - 1 };
    if (g_drawn_ok) omni_tmr_ring_dirty(&g_drawn, &s, r);
    g_drawn = s;
    g_drawn_ok = true;
    if (r[2] < r[0]) return;
    omni_tmr_ring_draw(&g_lut, &s, box, r[1], r[3] + 1);
}
static void assert_sector_to(const uint16_t *box, double upto)
{
    for (double m = 1; m < upto - 0.5; m += 1)
        for (double rad = 182; rad <= 208; rad += 13) {
            char msg[64];
            snprintf(msg, sizeof msg, "no sector at %.0f min, r %.0f", m, rad);
            TEST_ASSERT_TRUE_MESSAGE(lum(at(box, rad, min_angle(m))) > 150, msg);
        }
    TEST_ASSERT_TRUE(lum(at(box, 195, min_angle(upto + 1.5))) < 60);   /* and none past the end */
}

void test_tmr_ring_jog_past_30(void)
{
    lut_once();
    /* IDLE, one 540 degree jog (a turn and a half: 45 min), shown while held (SET) and after the lift (ARMED) */
    omni_tmr_init(&JT);
    memset(&JD, 0, sizeof JD);
    g_drawn_ok = false;
    memset(g_box, 0, sizeof g_box);
    face_update(g_box);                       /* IDLE: no sector */
    jog(0, 540, true);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_SET, JT.mode);
    TEST_ASSERT_EQUAL_UINT32(45u * 60000u, omni_tmr_set_ms(&JT));
    face_update(g_box);
    TEST_ASSERT_TRUE(lum(at(g_box, 195, min_angle(35))) > 150);
    TEST_ASSERT_TRUE(lum(at(g_box, 195, min_angle(40))) > 150);
    assert_sector_to(g_box, 45);
    jog_send(G_DRAG_END, 540);
    TEST_ASSERT_EQUAL_INT(OMNI_TMR_ARMED, JT.mode);
    TEST_ASSERT_EQUAL_UINT8(45, JT.set_min);
    face_update(g_box);
    assert_sector_to(g_box, 45);

    /* the incremental path: 25 min drawn whole, then a second touch winds on to 45 and only the dirty rows redraw */
    omni_tmr_init(&JT);
    memset(&JD, 0, sizeof JD);
    g_drawn_ok = false;
    memset(g_box, 0, sizeof g_box);
    jog(0, 300, false);
    TEST_ASSERT_EQUAL_UINT8(25, JT.set_min);
    face_update(g_box);
    assert_sector_to(g_box, 25);
    jog(90, 246, false);   /* 20.5 min of travel: the detents truncate, and rounded pixel samples lose a little */
    TEST_ASSERT_EQUAL_UINT8(45, JT.set_min);
    face_update(g_box);
    TEST_ASSERT_TRUE(lum(at(g_box, 195, min_angle(35))) > 150);
    TEST_ASSERT_TRUE(lum(at(g_box, 195, min_angle(40))) > 150);
    assert_sector_to(g_box, 45);
    /* and the incremental box equals a whole draw of the same state */
    draw(g_box2, g_drawn.min, g_drawn.red, g_drawn.alpha, g_drawn.bead);
    for (uint32_t i = 0; i < g_lut.n; i++) TEST_ASSERT_EQUAL_HEX16(g_box2[g_lut.idx[i]], g_box[g_lut.idx[i]]);
}

/* Every sector end from 0 to 60 min in quarter minutes: lit just before the end, dark just after it (the same for the
 * stopwatch track: the bead's place 0..1 of the circle). */
void test_tmr_ring_sector_whole_circle(void)
{
    const double da = 4.0 / 195.0;
    for (int q = 4; q < 240; q += 3) {
        const double m = q / 4.0;
        draw(g_box, (float)m, false, 255, -1);
        char msg[64];
        snprintf(msg, sizeof msg, "sector end %.2f min", m);
        TEST_ASSERT_TRUE_MESSAGE(lum(at(g_box, 195.0, min_angle(m) - da)) > 200, msg);
        TEST_ASSERT_TRUE_MESSAGE(lum(at(g_box, 195.0, min_angle(m) + da)) < 60, msg);
        TEST_ASSERT_TRUE_MESSAGE(lum(at(g_box, 195.0, min_angle(m) / 2)) > 200, msg);
    }
    for (int q = 2; q < 20; q++) {
        const double b = q / 20.0;
        draw(g_box, 0, false, 191, (float)b);
        char msg[64];
        snprintf(msg, sizeof msg, "track to bead %.2f", b);
        TEST_ASSERT_TRUE_MESSAGE(lum(at(g_box, 166.0, b * TAU - 0.1)) > 100, msg);
        TEST_ASSERT_TRUE_MESSAGE(lum(at(g_box, 166.0, b * TAU / 2)) > 100, msg);
        TEST_ASSERT_TRUE_MESSAGE(lum(at(g_box, 166.0, b * TAU + 0.1)) == 0, msg);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tmr_lut_shape);
    RUN_TEST(test_tmr_ring_colours);
    RUN_TEST(test_tmr_ring_sector_edges);
    RUN_TEST(test_tmr_ring_ticks);
    RUN_TEST(test_tmr_ring_bead);
    RUN_TEST(test_tmr_ring_bands_equal);
    RUN_TEST(test_tmr_ring_dirty);
    RUN_TEST(test_tmr_ring_digest);
    RUN_TEST(test_tmr_pulse_matches_draw);
    RUN_TEST(test_tmr_ring_jog_past_30);
    RUN_TEST(test_tmr_ring_sector_whole_circle);
    return UNITY_END();
}
