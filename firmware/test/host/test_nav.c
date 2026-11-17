#include <string.h>
#include "unity.h"
#include "omni/nav.h"
#include "omni/prov.h"
#include "omni/local.h"
#include "omni/shared.h"

/* The Omni's face ids (apps/omni/main/omni_faces.h), test-local: omni_core's face ids are plain uint8_t (plan PD2). */
enum { FACE_OMNI, FACE_GLOBE_LIVE, FACE_GLOBE_ANALOG, FACE_DIGITAL, FACE_WORLD, FACE_ALMANAC, FACE_TIMER, FACE_GLOBE_SPIN,
       FACE__COUNT };

void setUp(void) {}
void tearDown(void) {}

static omni_settings_t S;
static omni_nav_t N;

/* the Omni theme's limits (apps/omni/main/omni_theme.c): nav runs on the joined RAM view, whose carousel is
 * t_<id>/local's (Task 6), the defaults' today's order */
static const uint8_t k_def[] = { FACE_OMNI, FACE_GLOBE_LIVE, FACE_GLOBE_ANALOG, FACE_DIGITAL,
                                 FACE_WORLD, FACE_ALMANAC, FACE_TIMER, FACE_GLOBE_SPIN };
static const omni_theme_limits_t k_lim = { FACE__COUNT, 3, 0, sizeof k_def, k_def };

static void fresh(void)
{
    omni_shared_t sh;
    omni_local_t lo;
    omni_shared_defaults(&sh);
    omni_local_defaults(&lo, &k_lim);
    omni_settings_join(&sh, &lo, &S);
    omni_nav_init(&N);
}

static omni_nav_effect_t gest(omni_gesture_kind_t k)
{
    omni_gesture_t g = {.kind = k};
    return omni_nav_gesture(&N, &S, &g);
}

void test_carousel_wraps(void)
{
    fresh();
    TEST_ASSERT_EQUAL(FACE_OMNI, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_R));
    TEST_ASSERT_EQUAL(S.carousel_len - 1, N.face_idx);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_L));
    TEST_ASSERT_EQUAL(0, N.face_idx);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_L));
    TEST_ASSERT_EQUAL(FACE_GLOBE_LIVE, omni_nav_current_face(&N, &S));
}

/* Swipe, pick and BOOT as today with the carousel from omni_local_t: the owner's {omni, live, analog, digital, spin}
 * as t_omni/local holds it after the migration. */
void test_nav_with_local_limits(void)
{
    omni_shared_t sh;
    omni_local_t lo;
    omni_shared_defaults(&sh);
    omni_local_defaults(&lo, &k_lim);
    const uint8_t owner[] = { FACE_OMNI, FACE_GLOBE_LIVE, FACE_GLOBE_ANALOG, FACE_DIGITAL, FACE_GLOBE_SPIN };
    memcpy(lo.carousel, owner, sizeof owner);
    lo.carousel_len = sizeof owner;
    TEST_ASSERT_TRUE(omni_local_validate(&lo, &k_lim));
    omni_settings_join(&sh, &lo, &S);
    omni_nav_init(&N);
    TEST_ASSERT_EQUAL(FACE_OMNI, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_R));
    TEST_ASSERT_EQUAL(FACE_GLOBE_SPIN, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_L));
    TEST_ASSERT_EQUAL(FACE_OMNI, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(NAV_NONE, gest(G_SWIPE_UP));   /* no timer in this carousel */
    gest(G_LONG_PRESS);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_pick(&N, &S, 3));
    TEST_ASSERT_EQUAL(FACE_DIGITAL, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(1, N.depth);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_boot(&N, &S, B_SHORT));   /* BOOT short on the face: next */
    TEST_ASSERT_EQUAL(FACE_GLOBE_SPIN, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_boot(&N, &S, B_SHORT));
    TEST_ASSERT_EQUAL(FACE_OMNI, omni_nav_current_face(&N, &S));
}

void test_long_press_opens_picker(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, gest(G_LONG_PRESS));
    TEST_ASSERT_EQUAL(2, N.depth);
    TEST_ASSERT_EQUAL(SCR_PICKER, N.stack[1]);
    TEST_ASSERT_EQUAL(NAV_NONE, gest(G_SWIPE_L));
    TEST_ASSERT_EQUAL(0, N.face_idx);
}

void test_quick_and_stack_limit(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, gest(G_SWIPE_DOWN_FROM_TOP));
    TEST_ASSERT_EQUAL(SCR_QUICK, N.stack[1]);
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_SETTINGS));
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_PROVISION));
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_push(&N, SCR_PICKER));
    TEST_ASSERT_EQUAL(4, N.depth);
}

void test_boot_short_back_then_next(void)
{
    fresh();
    gest(G_LONG_PRESS);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_boot(&N, &S, B_SHORT));
    TEST_ASSERT_EQUAL(1, N.depth);
    TEST_ASSERT_EQUAL(SCR_FACE, N.stack[0]);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_boot(&N, &S, B_SHORT));
    TEST_ASSERT_EQUAL(1, N.face_idx);
}

void test_boot_long_pushes_picker_once(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_boot(&N, &S, B_LONG));
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_boot(&N, &S, B_LONG));
    TEST_ASSERT_EQUAL(2, N.depth);
}

void test_boot_double_replay(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_REPLAY_REVEAL, omni_nav_boot(&N, &S, B_DOUBLE));
    gest(G_LONG_PRESS);
    TEST_ASSERT_EQUAL(NAV_REPLAY_REVEAL, omni_nav_boot(&N, &S, B_DOUBLE));
    TEST_ASSERT_EQUAL(2, N.depth);
}

void test_swipe_up_without_timer_noop(void)
{
    fresh();
    S.carousel_len = 3;
    S.carousel[0] = FACE_OMNI; S.carousel[1] = FACE_DIGITAL; S.carousel[2] = FACE_WORLD;
    TEST_ASSERT_EQUAL(NAV_NONE, gest(G_SWIPE_UP));
    TEST_ASSERT_EQUAL(0, N.face_idx);
    TEST_ASSERT_EQUAL(FACE_OMNI, omni_nav_current_face(&N, &S));
}

/* R144: the swipe-up timer shortcut is retired; the timer is reached through the carousel and the picker only. */
void test_swipe_up_never_navigates(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_NONE, gest(G_SWIPE_UP));
    TEST_ASSERT_EQUAL(FACE_OMNI, omni_nav_current_face(&N, &S));
    N.face_idx = 6;   /* on the timer itself, too */
    TEST_ASSERT_EQUAL(NAV_NONE, gest(G_SWIPE_UP));
    TEST_ASSERT_EQUAL(FACE_TIMER, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(1, N.depth);
}

void test_pick_commits_and_pops(void)
{
    fresh();
    gest(G_LONG_PRESS);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_pick(&N, &S, 3));
    TEST_ASSERT_EQUAL(1, N.depth);
    TEST_ASSERT_EQUAL(FACE_DIGITAL, omni_nav_current_face(&N, &S));
    gest(G_LONG_PRESS);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_pick(&N, &S, 3));
    gest(G_LONG_PRESS);
    omni_nav_pick(&N, &S, 200);
    TEST_ASSERT_EQUAL(S.carousel_len - 1, N.face_idx);
    TEST_ASSERT_EQUAL(1, N.depth);
}

void test_pop_and_top(void)
{
    fresh();
    TEST_ASSERT_EQUAL(SCR_FACE, omni_nav_top(&N));
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_pop(&N));        /* never pops the face */
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_PROVISION));
    TEST_ASSERT_EQUAL(SCR_PROVISION, omni_nav_top(&N));
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_pop(&N));
    TEST_ASSERT_EQUAL(SCR_FACE, omni_nav_top(&N));
    TEST_ASSERT_EQUAL(1, N.depth);
}

/* Stage 5: the owner's carousel [0..7] (t_omni/local, Stage 4b Task 9) once world, almanac and timer have views:
 * swipe up does nothing (R144); swipe left walks digital -> world -> almanac. */
void test_nav_owner_carousel_stage5(void)
{
    fresh();
    TEST_ASSERT_EQUAL_UINT8(8, S.carousel_len);
    for (uint8_t i = 0; i < 8; i++) TEST_ASSERT_EQUAL_UINT8(i, S.carousel[i]);
    TEST_ASSERT_EQUAL_UINT8(0, N.face_idx);
    TEST_ASSERT_EQUAL(NAV_NONE, gest(G_SWIPE_UP));
    TEST_ASSERT_EQUAL_UINT8(0, N.face_idx);
    N.face_idx = 3;
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_L));
    TEST_ASSERT_EQUAL_UINT8(4, N.face_idx);
    TEST_ASSERT_EQUAL(FACE_WORLD, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_L));
    TEST_ASSERT_EQUAL_UINT8(5, N.face_idx);
    TEST_ASSERT_EQUAL(FACE_ALMANAC, omni_nav_current_face(&N, &S));
}

void test_prov_name_from_mac(void)
{
    const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
    char name[16];
    omni_prov_name(mac, name);
    TEST_ASSERT_EQUAL_STRING("PROV_OMNI_3456", name);
}

void test_prov_qr_json_exact(void)
{
    char out[128];
    omni_prov_qr_json("PROV_OMNI_3456", "7c1e94b2", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"ver\":\"v1\",\"name\":\"PROV_OMNI_3456\",\"pop\":\"7c1e94b2\",\"transport\":\"ble\"}", out);
}

void test_prov_qr_json_truncates(void)
{
    char out[20];
    memset(out, 'x', sizeof out);
    omni_prov_qr_json("PROV_OMNI_3456", "7c1e94b2", out, sizeof out);
    TEST_ASSERT_EQUAL(19, strlen(out));
    char one[1] = {'x'};
    omni_prov_qr_json("a", "b", one, 1);
    TEST_ASSERT_EQUAL('\0', one[0]);
}

/* R151: the console's `net` prints the payload with the PoP redacted (a dummy PoP here, never a real one) */
void test_prov_qr_redact(void)
{
    static const char DUMMY[] = "dummy0pop0value0dummy0pop0value0";
    char qr[128], out[128];
    omni_prov_qr_json("PROV_OMNI_3456", DUMMY, qr, sizeof qr);
    omni_prov_qr_redact(qr, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"ver\":\"v1\",\"name\":\"PROV_OMNI_3456\",\"pop\":\"<redacted>\",\"transport\":\"ble\"}", out);
    TEST_ASSERT_NULL(strstr(out, "dummy"));
    /* an empty payload (a new session's code on its way) stays empty; no "pop" key is copied as it is */
    omni_prov_qr_redact("", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("", out);
    omni_prov_qr_redact(NULL, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("", out);
    omni_prov_qr_redact("{\"ver\":\"v1\"}", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"ver\":\"v1\"}", out);
    /* a truncated value (no closing quote) ends at the marker: no byte of it is copied */
    omni_prov_qr_redact("{\"pop\":\"dummy0pop", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("{\"pop\":\"<redacted>", out);
    /* a small out truncates, NUL-terminated, and never reaches the value */
    char small[12];
    memset(small, 'x', sizeof small);
    omni_prov_qr_redact(qr, small, sizeof small);
    TEST_ASSERT_EQUAL(11, strlen(small));
    TEST_ASSERT_NULL(strstr(small, "dummy"));
    char one[1] = {'x'};
    omni_prov_qr_redact(qr, one, 1);
    TEST_ASSERT_EQUAL('\0', one[0]);
    /* the value is cut just after the key: the marker is cut too, never the value shown */
    char cut[40];
    omni_prov_qr_redact(qr, cut, sizeof cut);
    TEST_ASSERT_NULL(strstr(cut, "dummy"));
}

/* dw wave 1 Task 10 (A19/B7): the theme's own pushed screen, and the sentinel the shell sizes its table by. */
void test_nav_theme_screen(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_THEME));
    TEST_ASSERT_EQUAL(SCR_THEME, omni_nav_top(&N));
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_pop(&N));
    TEST_ASSERT_EQUAL(SCR_FACE, omni_nav_top(&N));
}

void test_nav_screen_count(void)
{
    TEST_ASSERT_EQUAL_INT(SCR_FACES + 1, SCR__COUNT);   /* Task 4 appended SCR_KEYBOARD, SCR_NETLIST, SCR_FACES */
    TEST_ASSERT_EQUAL_INT(SCR_PROVISION + 1, SCR_THEME);   /* appended: the existing ids keep their values */
}

/* dw wave 1 Task 11 (Task 10 review m1, m2; B9): a Flip-open lift shows the lift face as a transient view, and only
 * with the face on top. */
void test_nav_lift_guard(void)
{
    static const omni_screen_t over[] = { SCR_PICKER, SCR_QUICK, SCR_SETTINGS, SCR_PROVISION, SCR_THEME };
    for (unsigned k = 0; k < sizeof over / sizeof over[0]; k++) {
        fresh();
        N.face_idx = 2;
        omni_nav_push(&N, over[k]);
        const omni_nav_t before = N;
        TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_lift(&N, &S, 0));
        TEST_ASSERT_EQUAL_MEMORY(&before, &N, sizeof N);   /* the screen stays, the face index too */
        TEST_ASSERT_EQUAL(over[k], omni_nav_top(&N));
    }
}

void test_nav_lift_moves_face_only(void)
{
    fresh();
    N.face_idx = 3;
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_lift(&N, &S, 0));
    TEST_ASSERT_EQUAL_UINT8(0, N.face_idx);
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    TEST_ASSERT_EQUAL(SCR_FACE, omni_nav_top(&N));
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_lift(&N, &S, 0));   /* already shown */
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_lift(&N, &S, -1));  /* no lift face */
    TEST_ASSERT_EQUAL_UINT8(0, N.face_idx);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_lift(&N, &S, 200));   /* clamped to the carousel */
    TEST_ASSERT_EQUAL_UINT8(S.carousel_len - 1, N.face_idx);
    S.carousel_len = 0;
    N.face_idx = 0;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_lift(&N, &S, 1));
}

void test_theme_set_pack(void)
{
    uint8_t row = 0xEE, v = 0xEE;
    for (int r = 0; r < 8; r++)
        for (int x = 0; x < 256; x += 17) {
            const int arg = omni_theme_set_pack((uint8_t)r, (uint8_t)x);
            TEST_ASSERT_EQUAL_INT(r | x << 8, arg);
            TEST_ASSERT_TRUE(omni_theme_set_unpack(arg, 8, &row, &v));
            TEST_ASSERT_EQUAL_UINT8(r, row);
            TEST_ASSERT_EQUAL_UINT8(x, v);
        }
    /* through the event's own packing (shell.h's UI_EV: code | arg << 8, then arg = v >> 8) */
    const int ev = 70 | omni_theme_set_pack(3, 23) << 8;
    TEST_ASSERT_TRUE(omni_theme_set_unpack(ev >> 8, 4, &row, &v));
    TEST_ASSERT_EQUAL_UINT8(3, row);
    TEST_ASSERT_EQUAL_UINT8(23, v);
    row = v = 0xEE;
    TEST_ASSERT_FALSE(omni_theme_set_unpack(omni_theme_set_pack(4, 1), 4, &row, &v));   /* a row past the count */
    TEST_ASSERT_FALSE(omni_theme_set_unpack(omni_theme_set_pack(0, 1), 0, &row, &v));   /* no rows (NULL settings) */
    TEST_ASSERT_EQUAL_UINT8(0xEE, row);
    TEST_ASSERT_EQUAL_UINT8(0xEE, v);
}

void test_theme_rows_filter_and_clamp(void)
{
    const omni_theme_row_t ok_choice = { true, true, true, false, 2 }, ok_hours = { true, false, false, true, 0 };
    const omni_theme_row_t rows[] = {
        ok_choice,
        { false, true, true, false, 2 },   /* no get */
        { true, false, true, false, 2 },   /* a choice row with NULL opts */
        { true, true, false, false, 2 },   /* ... with NULL shorts */
        { true, false, false, false, 0 },  /* neither a choice nor an hour dial */
        ok_hours,
    };
    uint8_t keep[16];
    memset(keep, 0xEE, sizeof keep);
    TEST_ASSERT_EQUAL_INT(2, omni_theme_rows(rows, 6, 8, keep));
    TEST_ASSERT_EQUAL_UINT8(0, keep[0]);
    TEST_ASSERT_EQUAL_UINT8(5, keep[1]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, keep[2]);
    /* the count is clamped to max: rows past it are never read */
    omni_theme_row_t many[12];
    for (int k = 0; k < 12; k++) many[k] = ok_choice;
    TEST_ASSERT_EQUAL_INT(8, omni_theme_rows(many, 12, 8, keep));
    for (int k = 0; k < 8; k++) TEST_ASSERT_EQUAL_UINT8(k, keep[k]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, keep[8]);
    TEST_ASSERT_EQUAL_INT(0, omni_theme_rows(NULL, 4, 8, keep));   /* NULL settings */
    TEST_ASSERT_EQUAL_INT(0, omni_theme_rows(many, -1, 8, keep));
}

/* Task 11 fix round (M4): after a transient lift face_idx differs from the saved face; picking that same face (or an
 * alarm's pick) must save it although the pick changes nothing on screen. */
void test_nav_pick_saves_after_lift(void)
{
    fresh();
    N.face_idx = 3;
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_lift(&N, &S, 0));   /* shown, not saved: the store still says 3 */
    const omni_nav_effect_t e = omni_nav_pick(&N, &S, 0);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, e);
    TEST_ASSERT_TRUE(omni_nav_pick_saves(e, N.face_idx, 3));
    TEST_ASSERT_FALSE(omni_nav_pick_saves(e, N.face_idx, 0));          /* already the saved face */
    TEST_ASSERT_TRUE(omni_nav_pick_saves(NAV_FACE_CHANGED, 2, 2));
    TEST_ASSERT_FALSE(omni_nav_pick_saves(NAV_NONE, 1, 3));            /* nothing picked (an empty carousel) */
    TEST_ASSERT_FALSE(omni_nav_pick_saves(NAV_SCREEN_PUSHED, 1, 3));
}

/* ---- Task 4 (rim keyboard and Settings): the new screens, B1's replace, the transient show, O4's full-wake close ---- */
static void push_all(const omni_screen_t *p, int n)
{
    for (int i = 0; i < n; i++) TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, p[i]));
}

/* spec section 8.5: every path, built from push and replace calls, fits four slots */
void test_nav_paths_fit_four(void)
{
    for (int phone = 0; phone < 2; phone++) {
        omni_screen_t a[] = {SCR_SETTINGS, SCR_NETLIST, SCR_KEYBOARD};
        fresh(); push_all(a, 3); TEST_ASSERT_TRUE(N.depth <= 4);
        fresh(); push_all((omni_screen_t[]){SCR_SETTINGS, SCR_KEYBOARD}, 2); TEST_ASSERT_TRUE(N.depth <= 4);

        fresh(); push_all((omni_screen_t[]){SCR_PROVISION}, 1);
        TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_NETLIST));
        if (phone) { TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_PROVISION));
                     TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_NETLIST)); }
        push_all((omni_screen_t[]){SCR_KEYBOARD}, 1); TEST_ASSERT_TRUE(N.depth <= 4);

        fresh(); push_all((omni_screen_t[]){SCR_SETTINGS, SCR_PROVISION}, 2);
        TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_NETLIST));
        if (phone) { TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_PROVISION));
                     TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_NETLIST)); }
        push_all((omni_screen_t[]){SCR_KEYBOARD}, 1); TEST_ASSERT_TRUE(N.depth <= 4);

        fresh(); push_all((omni_screen_t[]){SCR_QUICK, SCR_PROVISION}, 2);
        TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_NETLIST));
        if (phone) { TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_PROVISION));
                     TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_NETLIST)); }
        push_all((omni_screen_t[]){SCR_KEYBOARD}, 1); TEST_ASSERT_TRUE(N.depth <= 4);
        TEST_ASSERT_EQUAL_UINT8(4, N.depth);

        fresh(); push_all((omni_screen_t[]){SCR_PICKER}, 1);
        TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_SETTINGS));
        push_all((omni_screen_t[]){SCR_NETLIST, SCR_KEYBOARD}, 2); TEST_ASSERT_TRUE(N.depth <= 4);
        if (phone) { /* from the list: Use your phone replaces it */
            omni_nav_pop(&N);
            TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_PROVISION));
            TEST_ASSERT_TRUE(N.depth <= 4);
        }
    }
}

void test_nav_fifth_push_refused(void)
{
    fresh();
    push_all((omni_screen_t[]){SCR_SETTINGS, SCR_NETLIST, SCR_KEYBOARD}, 3);
    TEST_ASSERT_EQUAL_UINT8(4, N.depth);
    omni_nav_t before = N;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_push(&N, SCR_FACES));
    TEST_ASSERT_EQUAL_UINT8(before.depth, N.depth);
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL(before.stack[i], N.stack[i]);
}

void test_nav_replace_on_face(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_replace(&N, SCR_NETLIST));
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    TEST_ASSERT_EQUAL(SCR_FACE, omni_nav_top(&N));
}

void test_nav_home(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_home(&N));
    push_all((omni_screen_t[]){SCR_SETTINGS, SCR_NETLIST}, 2);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_home(&N));
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
}

void test_nav_show_id_in_carousel(void)
{
    fresh();
    N.face_idx = 1;
    omni_nav_t ref = N;
    omni_nav_effect_t want = omni_nav_pick(&ref, &S, 4);
    TEST_ASSERT_EQUAL(want, omni_nav_show_id(&N, &S, 4, FACE__COUNT));
    TEST_ASSERT_EQUAL_UINT8(ref.face_idx, N.face_idx);
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
    TEST_ASSERT_EQUAL(FACE_WORLD, omni_nav_current_face(&N, &S));
    omni_nav_push(&N, SCR_PICKER);   /* already current, a screen open: as pick does */
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_show_id(&N, &S, 4, FACE__COUNT));
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
}

static void carousel_012(void)
{
    fresh();
    memset(S.carousel, 0, sizeof S.carousel);
    S.carousel[0] = 0; S.carousel[1] = 1; S.carousel[2] = 2;
    S.carousel_len = 3;
    N.face_idx = 1;
}

void test_nav_show_id_hidden(void)
{
    carousel_012();
    omni_nav_push(&N, SCR_QUICK);
    TEST_ASSERT_EQUAL(NAV_TRANSIENT, omni_nav_show_id(&N, &S, 5, FACE__COUNT));
    TEST_ASSERT_EQUAL_UINT8(5, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL_UINT8(1, N.face_idx);
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_L));
    TEST_ASSERT_EQUAL_UINT8(2, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
    N.face_idx = 1;
    TEST_ASSERT_EQUAL(NAV_TRANSIENT, omni_nav_show_id(&N, &S, 5, FACE__COUNT));
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, gest(G_SWIPE_R));
    TEST_ASSERT_EQUAL_UINT8(0, omni_nav_current_face(&N, &S));   /* from the saved face_idx 1 */
    /* a pick and a lift clear it too */
    TEST_ASSERT_EQUAL(NAV_TRANSIENT, omni_nav_show_id(&N, &S, 6, FACE__COUNT));
    omni_nav_pick(&N, &S, 0);
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
    omni_nav_show_id(&N, &S, 6, FACE__COUNT);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_lift(&N, &S, 2));
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
    TEST_ASSERT_EQUAL_UINT8(2, omni_nav_current_face(&N, &S));
}

void test_nav_show_id_out_of_range(void)
{
    carousel_012();
    omni_nav_t before = N;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_show_id(&N, &S, 8, 8));
    TEST_ASSERT_EQUAL_UINT8(before.depth, N.depth);
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
}

void test_nav_wake_full_closes_tools(void)
{
    const omni_screen_t tools[] = {SCR_SETTINGS, SCR_NETLIST, SCR_FACES, SCR_KEYBOARD};
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(omni_nav_is_tool(tools[i]));
        fresh(); push_all(&tools[i], 1);
        TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_wake_full(&N));
        TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    }
    fresh(); push_all((omni_screen_t[]){SCR_SETTINGS, SCR_NETLIST}, 2);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_wake_full(&N));
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
    const omni_screen_t keep[] = {SCR_PROVISION, SCR_PICKER, SCR_QUICK, SCR_THEME};
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_FALSE(omni_nav_is_tool(keep[i]));
        fresh(); push_all(&keep[i], 1);
        omni_nav_t before = N;
        TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_wake_full(&N));
        TEST_ASSERT_EQUAL_UINT8(before.depth, N.depth);
        TEST_ASSERT_EQUAL(keep[i], omni_nav_top(&N));
    }
    fresh();
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_wake_full(&N));
}

void test_nav_wake_full_clears_transient(void)
{
    carousel_012();
    omni_nav_show_id(&N, &S, 5, FACE__COUNT);
    TEST_ASSERT_EQUAL(NAV_TRANSIENT, omni_nav_wake_full(&N));
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
    TEST_ASSERT_EQUAL_UINT8(1, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_wake_full(&N));
}

void test_nav_wake_full_keeps_provisioning(void)
{
    fresh();
    push_all((omni_screen_t[]){SCR_PROVISION, SCR_PICKER}, 2);   /* face -> QR -> picker (BOOT long) */
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_SETTINGS));
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_wake_full(&N));
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
    TEST_ASSERT_EQUAL(SCR_FACE, N.stack[0]);
    TEST_ASSERT_EQUAL(SCR_PROVISION, N.stack[1]);
    /* a tool above the topmost QR only */
    fresh();
    push_all((omni_screen_t[]){SCR_SETTINGS, SCR_PROVISION, SCR_NETLIST}, 3);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_wake_full(&N));
    TEST_ASSERT_EQUAL_UINT8(3, N.depth);
    TEST_ASSERT_EQUAL(SCR_PROVISION, omni_nav_top(&N));
}

void test_nav_b9_within_window(void)
{
    fresh();
    push_all((omni_screen_t[]){SCR_SETTINGS}, 1);
    omni_nav_t before = N;
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_lift(&N, &S, 3));
    TEST_ASSERT_EQUAL_UINT8(before.face_idx, N.face_idx);
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
}

void test_nav_sleep_s(void)
{
    fresh();
    TEST_ASSERT_EQUAL_UINT8(15, omni_nav_sleep_s(&N, 15));
    push_all((omni_screen_t[]){SCR_SETTINGS}, 1);
    TEST_ASSERT_EQUAL_UINT8(30, omni_nav_sleep_s(&N, 5));
    TEST_ASSERT_EQUAL_UINT8(60, omni_nav_sleep_s(&N, 60));
    push_all((omni_screen_t[]){SCR_NETLIST}, 1);
    TEST_ASSERT_EQUAL_UINT8(30, omni_nav_sleep_s(&N, 5));
    omni_nav_replace(&N, SCR_FACES);
    TEST_ASSERT_EQUAL_UINT8(30, omni_nav_sleep_s(&N, 5));
    push_all((omni_screen_t[]){SCR_KEYBOARD}, 1);
    TEST_ASSERT_EQUAL_UINT8(61, omni_nav_sleep_s(&N, 5));
    TEST_ASSERT_EQUAL_UINT8(61, omni_nav_sleep_s(&N, 120));
    fresh(); push_all((omni_screen_t[]){SCR_PICKER}, 1);
    TEST_ASSERT_EQUAL_UINT8(5, omni_nav_sleep_s(&N, 5));
}

void test_nav_screen_ids_appended(void)
{
    TEST_ASSERT_EQUAL_INT(SCR_THEME + 1, SCR_KEYBOARD);
    TEST_ASSERT_EQUAL_INT(SCR_KEYBOARD + 1, SCR_NETLIST);
    TEST_ASSERT_EQUAL_INT(SCR_NETLIST + 1, SCR_FACES);
    TEST_ASSERT_EQUAL_INT(SCR_FACES + 1, SCR__COUNT);
}

/* Fix round 1 */
void test_nav_wake_full_tool_over_transient(void)
{
    carousel_012();
    omni_nav_show_id(&N, &S, 5, FACE__COUNT);
    push_all((omni_screen_t[]){SCR_SETTINGS}, 1);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_wake_full(&N));
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
    TEST_ASSERT_EQUAL_UINT8(1, omni_nav_current_face(&N, &S));   /* the saved face: the loop redraws it */
    TEST_ASSERT_EQUAL_UINT8(1, N.depth);
}

void test_nav_pick_saved_face_from_transient(void)
{
    carousel_012();                                    /* face_idx 1 */
    omni_nav_show_id(&N, &S, 5, FACE__COUNT);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_pick(&N, &S, 1));   /* the shown face changes: re-created */
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
    TEST_ASSERT_EQUAL_UINT8(1, omni_nav_current_face(&N, &S));
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_pick(&N, &S, 1));  /* no transient: as before */
    omni_nav_show_id(&N, &S, 5, FACE__COUNT);
    TEST_ASSERT_EQUAL(NAV_FACE_CHANGED, omni_nav_show_id(&N, &S, 1, FACE__COUNT));
    TEST_ASSERT_EQUAL_UINT8(0, N.transient);
}

void test_nav_in_stack(void)
{
    fresh();
    TEST_ASSERT_FALSE(omni_nav_in_stack(&N, SCR_PROVISION));
    push_all((omni_screen_t[]){SCR_SETTINGS, SCR_PROVISION}, 2);
    TEST_ASSERT_TRUE(omni_nav_in_stack(&N, SCR_PROVISION));
    TEST_ASSERT_TRUE(omni_nav_in_stack(&N, SCR_SETTINGS));
    TEST_ASSERT_FALSE(omni_nav_in_stack(&N, SCR_QUICK));
}

/* R212: face -> QR -> picker -> (Settings in its place) -> open_netlist -> keyboard */
void test_nav_open_netlist(void)
{
    fresh();
    push_all((omni_screen_t[]){SCR_PROVISION, SCR_PICKER}, 2);
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_SETTINGS));
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_netlist(&N));
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_KEYBOARD));
    TEST_ASSERT_EQUAL_UINT8(3, N.depth);
    TEST_ASSERT_EQUAL(SCR_FACE, N.stack[0]);
    TEST_ASSERT_EQUAL(SCR_NETLIST, N.stack[1]);
    TEST_ASSERT_EQUAL(SCR_KEYBOARD, N.stack[2]);
    /* no QR: a push */
    fresh();
    push_all((omni_screen_t[]){SCR_SETTINGS}, 1);
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_netlist(&N));
    TEST_ASSERT_EQUAL_UINT8(3, N.depth);
    TEST_ASSERT_EQUAL(SCR_SETTINGS, N.stack[1]);
    TEST_ASSERT_EQUAL(SCR_NETLIST, N.stack[2]);
    /* on the face: a push; the QR alone: replaced */
    fresh();
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_netlist(&N));
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
    fresh();
    push_all((omni_screen_t[]){SCR_PROVISION}, 1);
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_netlist(&N));
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
    TEST_ASSERT_EQUAL(SCR_NETLIST, omni_nav_top(&N));
    /* a full stack without a QR: refused */
    fresh();
    push_all((omni_screen_t[]){SCR_SETTINGS, SCR_NETLIST, SCR_KEYBOARD}, 3);
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_open_netlist(&N));
    TEST_ASSERT_EQUAL_UINT8(4, N.depth);
}

/* Final review M2: no screen is in the stack twice */
static void assert_no_twice(void)
{
    for (uint8_t i = 0; i < N.depth; i++)
        for (uint8_t j = (uint8_t)(i + 1); j < N.depth; j++) TEST_ASSERT_NOT_EQUAL(N.stack[i], N.stack[j]);
}

/* M2: BOOT long from a tool screen, then the picker's Settings: down to the Settings already there, so Choose network,
 * Find city and Faces still open */
void test_nav_open_settings_boot_long_collapses(void)
{
    fresh();
    push_all((omni_screen_t[]){SCR_SETTINGS, SCR_NETLIST}, 2);
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_boot(&N, &S, B_LONG));   /* face, Settings, list, picker */
    TEST_ASSERT_EQUAL_UINT8(4, N.depth);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_open_settings(&N));
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
    TEST_ASSERT_EQUAL(SCR_SETTINGS, omni_nav_top(&N));
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_netlist(&N));          /* Choose network */
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_KEYBOARD));   /* a network's password */
    assert_no_twice();
    /* Faces and Find city after the detour from the editor and from the keyboard */
    static const omni_screen_t TOP[] = {SCR_FACES, SCR_KEYBOARD};
    for (int k = 0; k < 2; k++) {
        fresh();
        push_all((omni_screen_t[]){SCR_SETTINGS, TOP[k]}, 2);
        TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_boot(&N, &S, B_LONG));
        TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_open_settings(&N));
        TEST_ASSERT_EQUAL_UINT8(2, N.depth);
        TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_FACES));
        TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_pop(&N));
        TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_KEYBOARD));
        assert_no_twice();
    }
    /* Settings on top: nothing */
    fresh();
    push_all((omni_screen_t[]){SCR_SETTINGS}, 1);
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_open_settings(&N));
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
}

/* M2: the list without Settings below (it replaced the QR, B1): the picker's Settings closes it, so the list is never in
 * the stack twice */
void test_nav_open_settings_no_second_list(void)
{
    fresh();
    push_all((omni_screen_t[]){SCR_PROVISION}, 1);
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_replace(&N, SCR_NETLIST));   /* "Type it instead" */
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_boot(&N, &S, B_LONG));
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_settings(&N));
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
    TEST_ASSERT_EQUAL(SCR_SETTINGS, omni_nav_top(&N));
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_netlist(&N));
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_push(&N, SCR_KEYBOARD));
    TEST_ASSERT_EQUAL_UINT8(4, N.depth);
    assert_no_twice();
}

/* M2: the rule's other cases. The picker on the face: Settings in its place. Quick and the QR stay below. Settings
 * above the QR: popped down to it (the event loop applies that only while the session may stop). A full stack of
 * non-tool screens: refused, untouched. */
void test_nav_open_settings_cases(void)
{
    fresh();
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_boot(&N, &S, B_LONG));
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_settings(&N));
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
    TEST_ASSERT_EQUAL(SCR_SETTINGS, omni_nav_top(&N));
    fresh();
    push_all((omni_screen_t[]){SCR_QUICK}, 1);
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_settings(&N));
    TEST_ASSERT_EQUAL_UINT8(3, N.depth);
    TEST_ASSERT_EQUAL(SCR_QUICK, N.stack[1]);
    fresh();
    push_all((omni_screen_t[]){SCR_PROVISION, SCR_PICKER}, 2);
    TEST_ASSERT_EQUAL(NAV_SCREEN_PUSHED, omni_nav_open_settings(&N));
    TEST_ASSERT_EQUAL_UINT8(3, N.depth);
    TEST_ASSERT_EQUAL(SCR_PROVISION, N.stack[1]);
    TEST_ASSERT_EQUAL(SCR_SETTINGS, N.stack[2]);
    fresh();
    push_all((omni_screen_t[]){SCR_SETTINGS, SCR_PROVISION, SCR_PICKER}, 3);
    TEST_ASSERT_EQUAL(NAV_SCREEN_POPPED, omni_nav_open_settings(&N));
    TEST_ASSERT_EQUAL_UINT8(2, N.depth);
    TEST_ASSERT_FALSE(omni_nav_in_stack(&N, SCR_PROVISION));
    fresh();
    push_all((omni_screen_t[]){SCR_PROVISION, SCR_THEME, SCR_QUICK}, 3);
    TEST_ASSERT_EQUAL(NAV_NONE, omni_nav_open_settings(&N));
    TEST_ASSERT_EQUAL_UINT8(4, N.depth);
    TEST_ASSERT_EQUAL(SCR_QUICK, omni_nav_top(&N));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_carousel_wraps);
    RUN_TEST(test_nav_with_local_limits);
    RUN_TEST(test_long_press_opens_picker);
    RUN_TEST(test_quick_and_stack_limit);
    RUN_TEST(test_boot_short_back_then_next);
    RUN_TEST(test_boot_long_pushes_picker_once);
    RUN_TEST(test_boot_double_replay);
    RUN_TEST(test_swipe_up_without_timer_noop);
    RUN_TEST(test_swipe_up_never_navigates);
    RUN_TEST(test_pick_commits_and_pops);
    RUN_TEST(test_pop_and_top);
    RUN_TEST(test_nav_owner_carousel_stage5);
    RUN_TEST(test_prov_name_from_mac);
    RUN_TEST(test_prov_qr_json_exact);
    RUN_TEST(test_prov_qr_json_truncates);
    RUN_TEST(test_prov_qr_redact);
    RUN_TEST(test_nav_theme_screen);
    RUN_TEST(test_nav_screen_count);
    RUN_TEST(test_nav_lift_guard);
    RUN_TEST(test_nav_lift_moves_face_only);
    RUN_TEST(test_theme_set_pack);
    RUN_TEST(test_theme_rows_filter_and_clamp);
    RUN_TEST(test_nav_pick_saves_after_lift);
    RUN_TEST(test_nav_paths_fit_four);
    RUN_TEST(test_nav_fifth_push_refused);
    RUN_TEST(test_nav_replace_on_face);
    RUN_TEST(test_nav_home);
    RUN_TEST(test_nav_show_id_in_carousel);
    RUN_TEST(test_nav_show_id_hidden);
    RUN_TEST(test_nav_show_id_out_of_range);
    RUN_TEST(test_nav_wake_full_closes_tools);
    RUN_TEST(test_nav_wake_full_clears_transient);
    RUN_TEST(test_nav_wake_full_keeps_provisioning);
    RUN_TEST(test_nav_b9_within_window);
    RUN_TEST(test_nav_sleep_s);
    RUN_TEST(test_nav_screen_ids_appended);
    RUN_TEST(test_nav_wake_full_tool_over_transient);
    RUN_TEST(test_nav_pick_saved_face_from_transient);
    RUN_TEST(test_nav_in_stack);
    RUN_TEST(test_nav_open_netlist);
    RUN_TEST(test_nav_open_settings_boot_long_collapses);
    RUN_TEST(test_nav_open_settings_no_second_list);
    RUN_TEST(test_nav_open_settings_cases);
    return UNITY_END();
}
