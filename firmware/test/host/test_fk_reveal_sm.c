/* The doors' life on the device as pure decisions (fk/reveal_sm.h; dw wave 1 Task 12, the Task 11 re-review's N1 and
 * its "dw_view's state machine needs host coverage"): a reveal through the direct path and through the canvas, the
 * latch, the cut, a worker that does not come back (its buffers left, then freed once it is idle), the destroy (N1:
 * leaked while the worker runs), and the frame plan and job columns the two cores draw. */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "dw/doors.h"
#include "fk/reveal_sm.h"

void setUp(void) {}
void tearDown(void) {}

#define POP 100u

static fk_rt_t tick(fk_rs_t *s, uint32_t t, bool done, bool active, bool *latch)
{
    return fk_rs_tick(s, t, POP, done, active, latch);
}

/* A Doors reveal through the direct path: the latch once at the pop, frames admitted, the worker's end, the release,
 * the buffers freed and the still frame. */
static void test_direct_reveal_done(void)
{
    fk_rs_t s = { FK_RS_OFF, false, false };
    bool latch;
    TEST_ASSERT_EQUAL_INT(FK_RT_IDLE, tick(&s, 0, false, false, &latch));
    TEST_ASSERT_FALSE(latch);
    TEST_ASSERT_EQUAL_INT(FK_RS_GO, fk_rs_gate(&s, false));
    fk_rs_started(&s, true, true);
    TEST_ASSERT_EQUAL_INT(FK_RS_DIRECT, s.mode);
    TEST_ASSERT_EQUAL_INT(FK_RS_REFUSE, fk_rs_gate(&s, false));   /* one reveal at a time */
    TEST_ASSERT_EQUAL_INT(FK_RT_CHECK, tick(&s, 10, false, true, &latch));
    TEST_ASSERT_FALSE(latch);
    TEST_ASSERT_EQUAL_INT(FK_RT_CHECK, tick(&s, POP + 3, false, true, &latch));
    TEST_ASSERT_TRUE(latch);
    TEST_ASSERT_EQUAL_INT(FK_RT_CHECK, tick(&s, POP + 13, false, true, &latch));
    TEST_ASSERT_FALSE(latch);   /* once */
    TEST_ASSERT_EQUAL_INT(FK_RT_END, tick(&s, 1800, true, true, &latch));
    TEST_ASSERT_TRUE(fk_rs_end_releases(&s));
    TEST_ASSERT_EQUAL_UINT(FK_RE_FREE | FK_RE_STILL, fk_rs_end(&s, false, false));
    TEST_ASSERT_EQUAL_INT(FK_RS_OFF, s.mode);
    TEST_ASSERT_FALSE(s.stuck);
    TEST_ASSERT_EQUAL_INT(FK_RT_IDLE, tick(&s, 1810, false, true, &latch));
}

/* The worker's own done wins over an inactive path in the same tick (it said whether it was cut); an inactive path
 * alone cuts; a cut is never a still frame. */
static void test_direct_cut(void)
{
    fk_rs_t s = { FK_RS_OFF, false, false };
    bool latch;
    fk_rs_started(&s, true, false);
    TEST_ASSERT_EQUAL_INT(FK_RT_END, tick(&s, 300, true, false, &latch));
    TEST_ASSERT_EQUAL_INT(FK_RT_END_CUT, tick(&s, 300, false, false, &latch));
    TEST_ASSERT_EQUAL_UINT(FK_RE_FREE, fk_rs_end(&s, true, false));
    TEST_ASSERT_EQUAL_INT(FK_RS_OFF, s.mode);
}

/* No latch for the cross-fade or the bench, and none after the end even if the pop is reached later. */
static void test_latch_rules(void)
{
    fk_rs_t s = { FK_RS_OFF, false, false };
    bool latch;
    fk_rs_started(&s, true, false);
    for (uint32_t t = 0; t < 600; t += 10) {
        tick(&s, t, false, true, &latch);
        TEST_ASSERT_FALSE(latch);
    }
    fk_rs_end(&s, false, false);
    fk_rs_started(&s, false, true);   /* through the canvas: the latch still plays */
    TEST_ASSERT_EQUAL_INT(FK_RT_CANVAS_FRAME, tick(&s, POP, false, false, &latch));
    TEST_ASSERT_TRUE(latch);
    fk_rs_end(&s, true, false);
    fk_rs_started(&s, true, true);
    tick(&s, 50, false, true, &latch);
    fk_rs_end(&s, true, false);   /* cut before the pop */
    TEST_ASSERT_FALSE(s.latch);
    TEST_ASSERT_EQUAL_INT(FK_RT_IDLE, tick(&s, POP + 50, false, true, &latch));
    TEST_ASSERT_FALSE(latch);
}

/* M1: a direct reveal whose worker is still busy at the end leaves its buffers (stuck); no new reveal while it is
 * busy; once idle the next gate frees them first; the canvas never leaves anything (no worker reads its buffers). */
static void test_stuck_worker(void)
{
    fk_rs_t s = { FK_RS_OFF, false, false };
    fk_rs_started(&s, true, true);
    TEST_ASSERT_TRUE(fk_rs_end_releases(&s));
    TEST_ASSERT_EQUAL_UINT(0u, fk_rs_end(&s, true, true));
    TEST_ASSERT_TRUE(s.stuck);
    TEST_ASSERT_EQUAL_INT(FK_RS_OFF, s.mode);
    TEST_ASSERT_EQUAL_INT(FK_RS_REFUSE, fk_rs_gate(&s, true));
    TEST_ASSERT_TRUE(s.stuck);
    TEST_ASSERT_EQUAL_INT(FK_RS_GO_FREE, fk_rs_gate(&s, false));
    TEST_ASSERT_FALSE(s.stuck);
    TEST_ASSERT_EQUAL_INT(FK_RS_GO, fk_rs_gate(&s, false));
    fk_rs_started(&s, false, false);
    TEST_ASSERT_FALSE(fk_rs_end_releases(&s));
    TEST_ASSERT_EQUAL_UINT(FK_RE_FREE | FK_RE_STILL, fk_rs_end(&s, false, true));
    TEST_ASSERT_FALSE(s.stuck);
}

/* A lift's wake: the panel is not on yet at the claim, so the doors wait (no latch, nothing released) and claim
 * again each tick, then run direct; or end while waiting (a sleep): nothing to release, the buffers freed. */
static void test_wait_for_the_panel(void)
{
    fk_rs_t s = { FK_RS_OFF, false, false };
    bool latch;
    TEST_ASSERT_EQUAL_INT(FK_RS_GO, fk_rs_gate(&s, false));
    fk_rs_waiting(&s, true);
    TEST_ASSERT_EQUAL_INT(FK_RS_REFUSE, fk_rs_gate(&s, false));
    for (uint32_t t = 0; t < 3 * POP; t += 10) {
        TEST_ASSERT_EQUAL_INT(FK_RT_CLAIM, tick(&s, t, false, false, &latch));
        TEST_ASSERT_FALSE(latch);
    }
    fk_rs_started(&s, true, s.latch);
    TEST_ASSERT_EQUAL_INT(FK_RT_CHECK, tick(&s, POP, false, true, &latch));
    TEST_ASSERT_TRUE(latch);
    fk_rs_end(&s, false, false);
    fk_rs_waiting(&s, true);
    TEST_ASSERT_FALSE(fk_rs_end_releases(&s));
    TEST_ASSERT_EQUAL_UINT(FK_RE_FREE, fk_rs_end(&s, true, true));
    TEST_ASSERT_FALSE(s.stuck);
}

/* N1: the destroy frees only once the worker has stopped; otherwise everything is left to it, stuck kept. */
static void test_destroy(void)
{
    fk_rs_t s = { FK_RS_OFF, true, false };
    TEST_ASSERT_FALSE(fk_rs_destroy_frees(&s, false));
    TEST_ASSERT_TRUE(s.stuck);
    TEST_ASSERT_TRUE(fk_rs_destroy_frees(&s, true));
    TEST_ASSERT_FALSE(s.stuck);
    fk_rs_t c = { FK_RS_OFF, false, false };
    TEST_ASSERT_TRUE(fk_rs_destroy_frees(&c, true));
}

/* The plan: nothing while the leaves meet; the gap's columns during the swing (exactly the columns no leaf covers);
 * the settled frame once at the end and in the cross-fade, then nothing. */
static void test_plan(void)
{
    static fk_col_t cols[466];
    fk_cols_state_t st;
    fk_rp_t p;
    bool settled = false;
    dw_doors_at(0, false, &st);
    dw_doors_cols(st.th_l, st.th_r, cols);
    fk_rs_plan(&st, cols, false, &settled, &p);
    TEST_ASSERT_EQUAL_INT(FK_RP_NONE, p.kind);
    int swings = 0;
    for (uint32_t t = DW_DOORS_SWING_T0_MS; t < DW_DOORS_END_MS; t += 7) {
        dw_doors_at(t, false, &st);
        dw_doors_cols(st.th_l, st.th_r, cols);
        fk_rs_plan(&st, cols, false, &settled, &p);
        TEST_ASSERT_FALSE(settled);
        if (p.kind == FK_RP_NONE) {
            for (int x = 0; x < 466; x++) TEST_ASSERT_TRUE(cols[x].u >= 0);
            continue;
        }
        TEST_ASSERT_EQUAL_INT(FK_RP_SWING, p.kind);
        swings++;
        for (int x = 0; x < 466; x++) TEST_ASSERT_EQUAL_INT(x >= p.xa && x < p.xb, cols[x].u < 0);
    }
    TEST_ASSERT_TRUE(swings > 100);
    dw_doors_at(DW_DOORS_END_MS, false, &st);
    dw_doors_cols(st.th_l, st.th_r, cols);
    fk_rs_plan(&st, cols, false, &settled, &p);
    TEST_ASSERT_EQUAL_INT(FK_RP_SETTLED, p.kind);
    TEST_ASSERT_EQUAL_INT(0, p.xa);
    TEST_ASSERT_EQUAL_INT(466, p.xb);
    TEST_ASSERT_TRUE(settled);
    fk_rs_plan(&st, cols, false, &settled, &p);
    TEST_ASSERT_EQUAL_INT(FK_RP_NONE, p.kind);   /* once */
    settled = false;   /* reduced: settled from the first frame, the closed doors cross-fading over it */
    dw_doors_at(0, true, &st);
    dw_doors_cols(st.th_l, st.th_r, cols);
    fk_rs_plan(&st, cols, true, &settled, &p);
    TEST_ASSERT_EQUAL_INT(FK_RP_SETTLED, p.kind);
    dw_doors_at(250, true, &st);
    fk_rs_plan(&st, cols, true, &settled, &p);
    TEST_ASSERT_EQUAL_INT(FK_RP_NONE, p.kind);
}

/* (wave 2 Task 3) The inward kept leaves: a leaf turned away from the eye shrinks, so the interior shows above and below
 * it and no column is covered whole (inv_s > 1). The swing's plan is then the columns not covered whole, which for an
 * inward leaf is every column once it has moved (the outward plan, test_plan, is unchanged: an outward column a leaf
 * holds is always covered whole). The end with keep is still the settled frame once (the kept leaves are the band's). */
static void test_plan_inward(void)
{
    static fk_col_t cols[466];
    const fk_cols_kind_t *k = &DW_DOORS_IN_KEEP;
    fk_cols_state_t st;
    fk_rp_t p;
    bool settled = false;
    dw_doors_at_k(k, 0, false, &st);
    fk_cols_k(k, st.th_l, st.th_r, cols);
    fk_rs_plan(&st, cols, false, &settled, &p);
    TEST_ASSERT_EQUAL_INT(FK_RP_NONE, p.kind);   /* closed: the door covers every column whole */
    int swings = 0;
    for (uint32_t t = DW_DOORS_SWING_T0_MS; t < DW_DOORS_END_MS; t += 7) {
        dw_doors_at_k(k, t, false, &st);
        fk_cols_k(k, st.th_l, st.th_r, cols);
        fk_rs_plan(&st, cols, false, &settled, &p);
        TEST_ASSERT_FALSE(settled);
        TEST_ASSERT_EQUAL_INT(FK_RP_SWING, p.kind);
        swings++;
        for (int x = 0; x < 466; x++) {
            const bool whole = cols[x].u >= 0 && cols[x].inv_s_q16 <= 65536u;
            TEST_ASSERT_EQUAL_INT(x >= p.xa && x < p.xb, !whole);
        }
        TEST_ASSERT_EQUAL_INT(0, p.xa);
        TEST_ASSERT_EQUAL_INT(466, p.xb);
    }
    TEST_ASSERT_TRUE(swings > 100);
    dw_doors_skip_k(k, &st);
    fk_cols_k(k, st.th_l, st.th_r, cols);
    fk_rs_plan(&st, cols, false, &settled, &p);
    TEST_ASSERT_EQUAL_INT(FK_RP_SETTLED, p.kind);
    fk_rs_plan(&st, cols, false, &settled, &p);
    TEST_ASSERT_EQUAL_INT(FK_RP_NONE, p.kind);
}

/* The jobs draw every panel pixel of the plan's columns in their rows, and nothing outside the plan's columns. */
static void test_job_cols_cover_the_panel(void)
{
    static const int span[][2] = { { 0, 466 }, { 233, 234 }, { 100, 380 }, { 0, 40 }, { 430, 466 }, { 5, 5 } };
    for (unsigned k = 0; k < sizeof span / sizeof span[0]; k++)
        for (int rows = 8; rows <= 32; rows += 8)
            for (int y0 = 0; y0 < 466; y0 += rows) {
                const int y1 = y0 + rows < 466 ? y0 + rows : 466;
                int a, b;
                fk_rs_job_cols(y0, y1, span[k][0], span[k][1], &a, &b);
                if (a < b) {
                    TEST_ASSERT_TRUE(a >= span[k][0]);
                    TEST_ASSERT_TRUE(b <= span[k][1]);
                }
                for (int y = y0; y < y1; y++) {
                    int p0, p1;
                    fk_panel_row(y, &p0, &p1);
                    for (int x = span[k][0]; x < span[k][1]; x++)
                        if (x >= p0 && x < p1) TEST_ASSERT_TRUE_MESSAGE(x >= a && x < b, "a panel pixel no job draws");
                }
            }
}

/* Task 13 review I1: a worker whose claim has moved (a later view claimed the path, so disp_direct_active() reads
 * true again) takes no job and sends no band, on either core, for the rest of the frame */
static void test_job_take_refuses_a_moved_claim(void)
{
    int next = 0;
    bool fail = false;
    TEST_ASSERT_EQUAL_INT(0, fk_rs_job_take(&next, 3, &fail, 7, 7));
    TEST_ASSERT_EQUAL_INT(1, fk_rs_job_take(&next, 3, &fail, 7, 7));
    TEST_ASSERT_TRUE(fk_rs_claim_ours(&fail, 7, 7));
    TEST_ASSERT_FALSE(fk_rs_claim_ours(&fail, 7, 8));   /* the release moved it: no band */
    TEST_ASSERT_TRUE(fail);
    TEST_ASSERT_EQUAL_INT(-1, fk_rs_job_take(&next, 3, &fail, 7, 8));
    TEST_ASSERT_EQUAL_INT(-1, fk_rs_job_take(&next, 3, &fail, 7, 7));   /* failed stays failed for the frame */
    TEST_ASSERT_FALSE(fk_rs_claim_ours(&fail, 7, 7));
    next = 0, fail = false;   /* a job take seeing the move fails the frame too */
    TEST_ASSERT_EQUAL_INT(-1, fk_rs_job_take(&next, 3, &fail, 7, 9));
    TEST_ASSERT_TRUE(fail);
    TEST_ASSERT_EQUAL_INT(0, next);
    next = 3, fail = false;   /* all taken: none left, not a failure */
    TEST_ASSERT_EQUAL_INT(-1, fk_rs_job_take(&next, 3, &fail, 7, 7));
    TEST_ASSERT_FALSE(fail);
}


/* (wave 2 Task 14) One holder of the direct path at a time: disp_direct_claim hands an active claim to a second claimer
 * (the Task 13 review), so dw_view refuses any claim while another holds it: a lift's doors and the Vortex's rest
 * motion never share one, and neither runs into the console's bench (whose bracket refuses every claim but its own). */
static void test_dm_one_holder(void)
{
    static const int doors_view = 1, vortex_view = 2, bench = 3;   /* three holders, told apart by address */
    TEST_ASSERT_TRUE(fk_dm_may_claim(NULL, &doors_view, false, false));
    TEST_ASSERT_TRUE(fk_dm_may_claim(NULL, &vortex_view, false, false));
    TEST_ASSERT_FALSE_MESSAGE(fk_dm_may_claim(&vortex_view, &doors_view, false, false), "the Vortex holds: no doors");
    TEST_ASSERT_FALSE_MESSAGE(fk_dm_may_claim(&doors_view, &vortex_view, false, false), "the doors hold: no Vortex");
    TEST_ASSERT_TRUE_MESSAGE(fk_dm_may_claim(&vortex_view, &vortex_view, false, false), "its own claim again");
    TEST_ASSERT_FALSE_MESSAGE(fk_dm_may_claim(NULL, &doors_view, true, false), "the bench's bracket: no doors");
    TEST_ASSERT_FALSE_MESSAGE(fk_dm_may_claim(NULL, &vortex_view, true, false), "the bench's bracket: no Vortex");
    TEST_ASSERT_TRUE(fk_dm_may_claim(NULL, &bench, true, true));
    TEST_ASSERT_FALSE_MESSAGE(fk_dm_may_claim(&vortex_view, &bench, true, true), "a view still holds: no bench");
}

/* (wave 2 Task 14) The rest motion's claims: wanted, awake (not the glance), not reduced (the tunnel is still), not under
 * the bench's bracket, and not before the retry time; after a refusal the next try waits: none for the panel coming on
 * (DISPLAY, a wait, B16), FK_DM_POLL_RETRY_MS for the transport's back-off or another holder, FK_DM_RETRY_MS for any
 * other refusal (OVERLAY, HIDDEN, RAM, PROV, OFF) and after a cut */
static void test_dm_want_and_retry(void)
{
    TEST_ASSERT_TRUE(fk_dm_want_claim(true, false, false, false, 1000, 1000));
    TEST_ASSERT_FALSE(fk_dm_want_claim(false, false, false, false, 1000, 0));
    TEST_ASSERT_FALSE(fk_dm_want_claim(true, true, false, false, 1000, 0));
    TEST_ASSERT_FALSE(fk_dm_want_claim(true, false, true, false, 1000, 0));
    TEST_ASSERT_FALSE(fk_dm_want_claim(true, false, false, true, 1000, 0));
    TEST_ASSERT_FALSE(fk_dm_want_claim(true, false, false, false, 999, 1000));
    TEST_ASSERT_EQUAL_INT64(5000, fk_dm_retry_at(OMNI_DD_DISPLAY, 5000));
    TEST_ASSERT_EQUAL_INT64(5000 + FK_DM_POLL_RETRY_MS * 1000LL, fk_dm_retry_at(OMNI_DD_BACKOFF, 5000));
    static const omni_dd_why_t long_wait[] = {OMNI_DD_OFF, OMNI_DD_PROV, OMNI_DD_HIDDEN, OMNI_DD_OVERLAY, OMNI_DD_RAM,
                                              OMNI_DD_OK};
    for (unsigned i = 0; i < sizeof long_wait / sizeof long_wait[0]; i++)
        TEST_ASSERT_EQUAL_INT64(5000 + FK_DM_RETRY_MS * 1000LL, fk_dm_retry_at(long_wait[i], 5000));
    TEST_ASSERT_TRUE(FK_DM_RETRY_MS >= 500);   /* never inside the transport's own back-off (DISP_DIRECT_BACKOFF_MS) */
    TEST_ASSERT_TRUE(FK_DM_POLL_RETRY_MS > 0 && FK_DM_POLL_RETRY_MS < FK_DM_RETRY_MS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_direct_reveal_done);
    RUN_TEST(test_direct_cut);
    RUN_TEST(test_latch_rules);
    RUN_TEST(test_stuck_worker);
    RUN_TEST(test_wait_for_the_panel);
    RUN_TEST(test_destroy);
    RUN_TEST(test_plan);
    RUN_TEST(test_plan_inward);
    RUN_TEST(test_job_cols_cover_the_panel);
    RUN_TEST(test_job_take_refuses_a_moved_claim);
    RUN_TEST(test_dm_one_holder);
    RUN_TEST(test_dm_want_and_retry);
    return UNITY_END();
}
