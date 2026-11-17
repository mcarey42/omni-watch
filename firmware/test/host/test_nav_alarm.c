#include <string.h>
#include "unity.h"
#include "omni/nav.h"
#include "omni/nav_alarm.h"
#include "omni/local.h"
#include "omni/shared.h"

/* The alarm's face pick (Stage 5 Task 4, preflight F3): omni_nav_alarm picks the face that rang (the Omni's timer, id
 * 6) unless it is not in the carousel, or the provisioning QR is anywhere in the stack (popping it would leave the
 * session holding BLE and Wi-Fi with no screen). */

enum { FACE_TIMER = 6 };

void setUp(void) {}
void tearDown(void) {}

static omni_settings_t S;
static omni_nav_t N;

static const uint8_t k_def[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static const omni_theme_limits_t k_lim = { 8, 3, 0, sizeof k_def, k_def };

static void fresh(void)
{
    omni_shared_t sh;
    omni_local_t lo;
    omni_shared_defaults(&sh);
    omni_local_defaults(&lo, &k_lim);
    omni_settings_join(&sh, &lo, &S);
    omni_nav_init(&N);
}

/* field by field: struct padding is not compared */
static void same_nav(const omni_nav_t *a, const omni_nav_t *b)
{
    TEST_ASSERT_EQUAL_UINT8(a->depth, b->depth);
    TEST_ASSERT_EQUAL_UINT8(a->face_idx, b->face_idx);
    for (uint8_t i = 0; i < a->depth; i++) TEST_ASSERT_EQUAL(a->stack[i], b->stack[i]);
}

static void test_nav_alarm_picks(void)
{
    fresh();
    TEST_ASSERT_EQUAL_UINT8(8, S.carousel_len);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_alarm(&N, &S, FACE_TIMER));
    TEST_ASSERT_EQUAL_UINT8(6, N.face_idx);
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_alarm(&N, &S, FACE_TIMER)); /* already shown: no change */
    TEST_ASSERT_EQUAL_UINT8(6, N.face_idx);
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
}

static void test_nav_alarm_closes_picker(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_PICKER));
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_alarm(&N, &S, FACE_TIMER));
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    TEST_ASSERT_EQUAL(SCR_FACE, omni_nav_top(&N));
    TEST_ASSERT_EQUAL_UINT8(6, N.face_idx);
}

static void test_nav_alarm_never_over_qr(void)
{
    fresh();
    N.face_idx = 2;
    omni_nav_push(&N, SCR_PROVISION);
    omni_nav_t before = N;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_alarm(&N, &S, FACE_TIMER));
    same_nav(&before, &N);

    omni_nav_push(&N, SCR_PICKER); /* BOOT long over the QR */
    before = N;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_alarm(&N, &S, FACE_TIMER));
    same_nav(&before, &N);
    TEST_ASSERT_EQUAL_UINT8(3, N.depth);
    TEST_ASSERT_EQUAL(SCR_PICKER, omni_nav_top(&N));
}

static void test_nav_alarm_not_in_carousel(void)
{
    fresh();
    const uint8_t c[] = { 0, 1, 3 };
    memset(S.carousel, 0, sizeof S.carousel);
    memcpy(S.carousel, c, sizeof c);
    S.carousel_len = sizeof c;
    N.face_idx = 1;
    omni_nav_t before = N;
    /* Task 4 (rim keyboard): a hidden face is a transient show now; face_idx is kept */
    TEST_ASSERT_EQUAL(NAV_TRANSIENT, omni_nav_alarm(&N, &S, FACE_TIMER));
    TEST_ASSERT_EQUAL_UINT8(before.face_idx, N.face_idx);
    TEST_ASSERT_EQUAL_UINT8(FACE_TIMER, omni_nav_current_face(&N, &S));
}

/* Task 4 m4: the picker open over the face that rang: the pick only closes it (SCREEN_POPPED, depth 1, same face) */
static void test_nav_alarm_picker_over_current(void)
{
    fresh();
    N.face_idx = 6;
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_PICKER));
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_alarm(&N, &S, FACE_TIMER));
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    TEST_ASSERT_EQUAL(SCR_FACE, omni_nav_top(&N));
    TEST_ASSERT_EQUAL_UINT8(6, N.face_idx);
}

/* Task 4 m4: UI_ALARM_KEEP (shell.h's 0xFF; the ringing alarm's activity poll) is never a face: NAV_NONE, nothing
 * moved, on a face or under the picker. on_ui skips the pick for it too; this holds without that guard. */
#define UI_ALARM_KEEP 0xFF
static void test_nav_alarm_keep_is_no_pick(void)
{
    fresh();
    N.face_idx = 2;
    omni_nav_t before = N;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_alarm(&N, &S, UI_ALARM_KEEP));
    same_nav(&before, &N);
    omni_nav_push(&N, SCR_PICKER);
    before = N;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_alarm(&N, &S, UI_ALARM_KEEP));
    same_nav(&before, &N);
    TEST_ASSERT_EQUAL(SCR_PICKER, omni_nav_top(&N));
}

/* Task 4: a face not in the carousel is shown transiently (the bound is the shell's 16); never over the QR */
static void test_nav_alarm_hidden_transient(void)
{
    fresh();
    memset(S.carousel, 0, sizeof S.carousel);
    for (uint8_t i = 0; i < 6; i++) S.carousel[i] = i;
    S.carousel_len = 6;
    N.face_idx = 2;
    TEST_ASSERT_EQUAL(NAV_TRANSIENT, omni_nav_alarm(&N, &S, FACE_TIMER));
    TEST_ASSERT_EQUAL_UINT8(FACE_TIMER, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL_UINT8(2, N.face_idx);
    fresh();
    S.carousel_len = 6;
    omni_nav_push(&N, SCR_PROVISION);
    omni_nav_t before = N;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_alarm(&N, &S, 12));
    same_nav(&before, &N);
}

static void test_nav_alarm_closes_keyboard(void)
{
    fresh();
    omni_nav_push(&N, SCR_SETTINGS);
    omni_nav_push(&N, SCR_NETLIST);
    omni_nav_push(&N, SCR_KEYBOARD);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_alarm(&N, &S, FACE_TIMER));
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    TEST_ASSERT_EQUAL(SCR_FACE, omni_nav_top(&N));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nav_alarm_picks);
    RUN_TEST(test_nav_alarm_closes_picker);
    RUN_TEST(test_nav_alarm_never_over_qr);
    RUN_TEST(test_nav_alarm_not_in_carousel);
    RUN_TEST(test_nav_alarm_picker_over_current);
    RUN_TEST(test_nav_alarm_keep_is_no_pick);
    RUN_TEST(test_nav_alarm_hidden_transient);
    RUN_TEST(test_nav_alarm_closes_keyboard);
    return UNITY_END();
}
