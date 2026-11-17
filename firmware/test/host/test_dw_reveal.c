/* rd wave 1 Task 2: the doors as face_kit's reveal (DW_REVEAL_DOORS, dw/doors.h), the faces' reveal_ops, the reveal's
 * assets (PR3), and the chime plan the app's play callback uses (dw_chime_play_plan, PR21). The golden: the doors'
 * frames recorded at $BASE (fixtures/dw_doors_frames_crc.h, rd wave 1 Task 1), now through the reveal's own bind,
 * frame and band, the interior drawn only where each frame's plan asks (the rest of it poisoned), as the view does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "dw/assets_meta.h"
#include "dw/chime.h"
#include "dw/doors.h"
#include "dw/faces.h"
#include "dw_assets_host.h"
#include "fk/reveal_sm.h"
#include "fixtures/dw_doors_frames_crc.h"

#define W 466

void setUp(void) {}
void tearDown(void) {}

static uint16_t s_crystal[W * W], s_frame[W * W];

/* the crystal face's still 0 over the committed generic assets: every row's interior (the fixture's) */
static void crystal_still(void)
{
    void *ctx = calloc(1, DW_FACE_CRYSTAL.ctx_bytes);
    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_TRUE(DW_FACE_CRYSTAL.bind(ctx, dw_assets_host_get));
    dw_still_frame(&DW_FACE_CRYSTAL, ctx, 0, s_crystal);
    free(ctx);
}

/* One reveal per (kind, leaf, reduced): its own context and interior, its frames in the fixture's order (t rising);
 * arg, and the plan's own "settled drawn" flag for the independent plan below (Task 2 review M1) */
typedef struct { void *rctx; uint16_t *interior; const void *arg; bool settled; uint32_t t_last; } seq_t;

static void test_dw_reveal_doors_golden(void)
{
    const fk_reveal_ops_t *ro = &DW_REVEAL_DOORS;
    crystal_still();
    seq_t seq[2][2][2];
    memset(seq, 0, sizeof seq);
    unsigned bad = 0, drawn = 0;
    for (unsigned i = 0; i < DW_DOORS_FRAMES_N; i++) {
        const dw_doors_frame_crc_t *r = &DW_DOORS_FRAMES_CRC[i];
        seq_t *q = &seq[r->kind][r->leaf][r->reduced];
        const void *arg = r->kind ? (const void *)&DW_DOORS_IN_KEEP : NULL;   /* doorway's; crystal's and room's: */
        if (r->kind == 0 && r->leaf == 1) arg = &DW_DOORS_OUT;                 /* room names DW_DOORS_OUT itself */
        if (q->rctx == NULL) {
            q->arg = arg;
            q->rctx = calloc(1, ro->rctx_bytes);
            q->interior = malloc((size_t)W * W * 2);
            TEST_ASSERT_NOT_NULL(q->rctx);
            TEST_ASSERT_NOT_NULL(q->interior);
            memset(q->interior, 0xA5, (size_t)W * W * 2);   /* poison: a band that reads an undrawn pixel shows */
            TEST_ASSERT_TRUE(ro->bind(arg, r->leaf ? ASSET_DW_DOORS_C : NULL, dw_assets_host_get, q->rctx));
        }
        fk_rframe_t fr;
        ro->frame(arg, q->rctx, r->t, false, r->reduced != 0, &fr);
        q->t_last = r->t;
        /* the plan worked out independently (fk_rs_plan, fk_rs_job_in, as the view ran them itself before Task 2):
         * the frame's need, live and wake_ms are its (M1) */
        {
            const fk_cols_kind_t *k = arg ? arg : &DW_DOORS_OUT;
            fk_cols_state_t st;
            static fk_col_t cols[W];
            dw_doors_at_k(k, r->t, r->reduced != 0, &st);
            fk_cols_k(k, st.th_l, st.th_r, cols);
            fk_rp_t p;
            fk_rs_plan(&st, cols, r->reduced != 0, &q->settled, &p);
            TEST_ASSERT_EQUAL(st.done, fr.settled);
            if (p.kind == FK_RP_NONE) {
                TEST_ASSERT_TRUE(fr.need.x0 >= fr.need.x1 || fr.need.y0 >= fr.need.y1);
            } else {
                uint32_t wake_ms = 0;
                bool live = true;
                fk_rs_job_in(&p, r->t, r->reduced != 0, &wake_ms, &live);
                TEST_ASSERT_EQUAL_INT16(p.xa, fr.need.x0);
                TEST_ASSERT_EQUAL_INT16(p.xb, fr.need.x1);
                TEST_ASSERT_EQUAL_INT16(0, fr.need.y0);
                TEST_ASSERT_EQUAL_INT16(W, fr.need.y1);
                TEST_ASSERT_EQUAL_UINT32(wake_ms, fr.wake_ms);
                TEST_ASSERT_EQUAL(live, fr.live);
            }
        }
        if (fr.need.x0 < fr.need.x1 && fr.need.y0 < fr.need.y1) {   /* the interior's rect from the same still */
            drawn++;
            TEST_ASSERT_TRUE(fr.need.x0 >= 0 && fr.need.y0 >= 0 && fr.need.x1 <= W && fr.need.y1 <= W);
            for (int y = fr.need.y0; y < fr.need.y1; y++)
                memcpy(q->interior + (size_t)y * W + fr.need.x0, s_crystal + (size_t)y * W + fr.need.x0,
                       (size_t)(fr.need.x1 - fr.need.x0) * 2);
        }
        memset(s_frame, 0x5A, sizeof s_frame);
        for (int y0 = 0; y0 < W; y0 += 8) {
            const omni_rect_t b = { 0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W) };
            ro->band(arg, q->rctx, q->interior, b, s_frame + (size_t)y0 * W);
        }
        const uint32_t crc = fk_crc32(0, s_frame, sizeof s_frame);
        if (crc != r->crc && bad++ < 8)
            printf("kind %u leaf %u t %u reduced %u: 0x%08x, recorded 0x%08x\n", r->kind, r->leaf, r->t, r->reduced,
                   (unsigned)crc, (unsigned)r->crc);
    }
    unsigned skips = 0;
    for (unsigned k = 0; k < 2; k++)
        for (unsigned l = 0; l < 2; l++)
            for (unsigned d = 0; d < 2; d++) {
                seq_t *q = &seq[k][l][d];
                if (q->rctx) {   /* one skip per sequence (a tap): the end frame, which ends the reveal (M1) */
                    fk_rframe_t fr;
                    ro->frame(q->arg, q->rctx, q->t_last, true, d != 0, &fr);
                    TEST_ASSERT_TRUE_MESSAGE(fr.settled, "a skip's frame is the settled end");
                    skips++;
                }
                free(seq[k][l][d].rctx);
                free(seq[k][l][d].interior);
            }
    TEST_ASSERT_TRUE(drawn > 0);
    TEST_ASSERT_TRUE(skips >= 3);   /* the fixture's three (kind, leaf) rows */
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, bad, "frames differ from $BASE's");
}

static void test_dw_reveal_pop_ms(void)
{
    TEST_ASSERT_EQUAL_UINT32(dw_doors_pop_ms(), DW_REVEAL_DOORS.pop_ms);
    TEST_ASSERT_EQUAL_UINT32(DW_DOORS_END_MS, DW_REVEAL_DOORS.len_ms);
    TEST_ASSERT_EQUAL_UINT32(DW_DOORS_REDUCED_MS, DW_REVEAL_DOORS.reduced_ms);
    TEST_ASSERT_TRUE(DW_REVEAL_DOORS.rctx_bytes > 0);
    TEST_ASSERT_NOT_NULL(DW_REVEAL_DOORS.bind);
    TEST_ASSERT_NOT_NULL(DW_REVEAL_DOORS.frame);
    TEST_ASSERT_NOT_NULL(DW_REVEAL_DOORS.band);
}

static void test_dw_faces_reveal_ops(void)
{
    unsigned reveals = 0;
    for (unsigned i = 0; i < DW_FACE_COUNT; i++) {
        const fk_face_ops_t *f = DW_FACES[i];
        if (f->reveal) {
            reveals++;
            TEST_ASSERT_EQUAL_PTR_MESSAGE(&DW_REVEAL_DOORS, f->reveal_ops, f->key);
        } else {
            TEST_ASSERT_NULL_MESSAGE(f->reveal_ops, f->key);
        }
    }
    TEST_ASSERT_EQUAL_UINT(3, reveals);   /* crystal, room, doorway */
    TEST_ASSERT_NULL(DW_KEY.reveal_ops);
}

/* a get that records the names asked for, and can withhold one */
static char s_asked[16][32];   /* more than FK_REVEAL_BIND_ASSETS_MAX, so the count can show one past it */
static unsigned s_nasked;
static const char *s_withhold;
static const void *rec_get(const char *name, size_t *size)
{
    if (s_nasked < 16) snprintf(s_asked[s_nasked++], sizeof s_asked[0], "%s", name);
    if (s_withhold && strcmp(name, s_withhold) == 0) {
        *size = 0;
        return NULL;
    }
    return dw_assets_host_get(name, size);
}
static bool asked(const char *name)
{
    for (unsigned i = 0; i < s_nasked; i++)
        if (strcmp(s_asked[i], name) == 0) return true;
    return false;
}

static void test_dw_reveal_assets(void)
{
    TEST_ASSERT_NOT_NULL(DW_REVEAL_DOORS.pop_asset);
    TEST_ASSERT_EQUAL_STRING(ASSET_DW_LATCH, DW_REVEAL_DOORS.pop_asset);   /* the latch at the pop (PR3) */
    TEST_ASSERT_EQUAL_STRING(ASSET_DW_DOORS_C, DW_FACE_ROOM.reveal_asset);
    TEST_ASSERT_NULL(DW_FACE_CRYSTAL.reveal_asset);
    TEST_ASSERT_NULL(DW_FACE_DOORWAY.reveal_asset);
    void *rctx = calloc(1, DW_REVEAL_DOORS.rctx_bytes);
    TEST_ASSERT_NOT_NULL(rctx);
    s_nasked = 0;
    s_withhold = NULL;
    TEST_ASSERT_TRUE(DW_REVEAL_DOORS.bind(NULL, NULL, rec_get, rctx));   /* NULL: the ops' default, dw_doors_m */
    TEST_ASSERT_TRUE(s_nasked <= FK_REVEAL_BIND_ASSETS_MAX);   /* fits the view's copy (the final review, M2) */
    TEST_ASSERT_TRUE(asked(ASSET_DW_DOORS_M));
    TEST_ASSERT_FALSE(asked(ASSET_DW_DOORS_C));
    TEST_ASSERT_TRUE(asked(ASSET_DW_GLOW));
    s_nasked = 0;
    TEST_ASSERT_TRUE(DW_REVEAL_DOORS.bind(&DW_DOORS_OUT, ASSET_DW_DOORS_C, rec_get, rctx));   /* room's */
    TEST_ASSERT_TRUE(asked(ASSET_DW_DOORS_C));
    TEST_ASSERT_FALSE(asked(ASSET_DW_DOORS_M));
    TEST_ASSERT_TRUE(s_nasked <= FK_REVEAL_BIND_ASSETS_MAX);
    s_withhold = ASSET_DW_DOORS_M;   /* the door missing: no reveal (a plain wake) */
    TEST_ASSERT_FALSE(DW_REVEAL_DOORS.bind(NULL, NULL, rec_get, rctx));
    s_withhold = ASSET_DW_GLOW;      /* the glow missing too */
    TEST_ASSERT_FALSE(DW_REVEAL_DOORS.bind(NULL, NULL, rec_get, rctx));
    s_withhold = NULL;
    free(rctx);
}

static void test_dw_chime_theme_plan(void)
{
    const fk_chime_verdict_t play = { FK_CH_PLAY, 3, 40, 0 };
    dw_chime_step_t got[13], want[13];
    memset(got, 0xEE, sizeof got);
    const uint16_t chime_ms = (uint16_t)((uint32_t)ASSET_DW_CHIME_SAMPLES * 1000u / 16000u);   /* the shipped chime */
    const unsigned n = dw_chime_play_plan(&play, 40, chime_ms, got, 13);
    const uint8_t m = dw_chime_plan(3, chime_ms, want);
    TEST_ASSERT_EQUAL_UINT(m, n);
    TEST_ASSERT_EQUAL_UINT(4, n);   /* the chime, then three pings */
    for (unsigned i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_UINT8(want[i].what, got[i].what);
        TEST_ASSERT_EQUAL_UINT16(want[i].at_ms, got[i].at_ms);
    }
    TEST_ASSERT_EQUAL_UINT(2, dw_chime_play_plan(&play, 40, chime_ms, got, 2));   /* at most cap */
    for (unsigned w = 0; w <= FK_CH_BOOT; w++) {
        if (w == FK_CH_PLAY) continue;
        const fk_chime_verdict_t v = { (fk_chime_why_t)w, 3, 40, 0 };
        TEST_ASSERT_EQUAL_UINT(0, dw_chime_play_plan(&v, 40, chime_ms, got, 13));
    }
    TEST_ASSERT_EQUAL_UINT(0, dw_chime_play_plan(NULL, 40, chime_ms, got, 13));
}

int main(void)
{
    dw_assets_host_open(DW_ASSETS_DIR, NULL);
    UNITY_BEGIN();
    RUN_TEST(test_dw_reveal_pop_ms);
    RUN_TEST(test_dw_faces_reveal_ops);
    RUN_TEST(test_dw_reveal_assets);
    RUN_TEST(test_dw_chime_theme_plan);
    RUN_TEST(test_dw_reveal_doors_golden);
    const int rc = UNITY_END();
    dw_assets_host_close();
    return rc;
}
