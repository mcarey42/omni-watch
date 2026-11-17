#include "unity.h"
#include "omni/power.h"
#include <dirent.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TRACE_DIR
#error TRACE_DIR must point at firmware/test/host/traces
#endif

void setUp(void) {}
void tearDown(void) {}

static const omni_accel_t HANG = {0, 1, 0};     /* hanging upright (specific force) */
static const omni_accel_t LOOK = {0, 0.5f, 0.85f};

static void test_wake_on_hold(void)
{
    omni_wake_sm_t w; omni_wake_reset(&w);
    int n = 0; uint32_t fired_at = 0;
    for (uint32_t t = 0; t < 1000; t += 40) TEST_ASSERT_FALSE(omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, HANG));
    for (uint32_t t = 1000; t < 1300; t += 40)
        if (omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, LOOK)) { n++; fired_at = t - 1000; }
    TEST_ASSERT_EQUAL(1, n);
    TEST_ASSERT_TRUE(fired_at >= 250 && fired_at <= 320);
    TEST_ASSERT_EQUAL(POSE_LOOKING, omni_wake_pose(&w));
}

static void test_wake_no_refire_until_rearm(void)
{
    omni_wake_sm_t w; omni_wake_reset(&w);
    int n = 0;
    for (uint32_t t = 0; t < 3000; t += 40) n += omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, LOOK);
    TEST_ASSERT_EQUAL(1, n);
    for (uint32_t t = 3000; t < 4000; t += 40) omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, HANG);
    for (uint32_t t = 4000; t < 5000; t += 40) n += omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, LOOK);
    TEST_ASSERT_EQUAL(2, n);
}

static void test_wake_rejects_swing(void)
{
    omni_wake_sm_t w; omni_wake_reset(&w);
    uint32_t seed = 12345;
    const float pi = 3.14159265f;
    for (int i = 0; i < 25 * 60; i++) {
        float t = (float)i / 25.0f;
        float th = 35.0f * pi / 180.0f * sinf(2 * pi * 1.2f * t);
        omni_accel_t a = {0, cosf(th), sinf(th)};
        float j = 0.3f * sinf(2 * pi * 2.0f * t);
        seed = seed * 1664525u + 1013904223u;
        float r = ((float)(seed >> 16) / 32768.0f - 1.0f) * 0.3f;
        a.ax += j + r;
        seed = seed * 1664525u + 1013904223u;
        a.ay += 0.5f * j + ((float)(seed >> 16) / 32768.0f - 1.0f) * 0.3f;
        seed = seed * 1664525u + 1013904223u;
        a.az += -0.5f * j + ((float)(seed >> 16) / 32768.0f - 1.0f) * 0.3f;
        TEST_ASSERT_FALSE(omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, (uint32_t)i * 40u, a));
    }
}

static void test_wake_rejects_brief_tilt(void)
{
    omni_wake_sm_t w; omni_wake_reset(&w);
    for (uint32_t t = 0; t < 1000; t += 40) omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, HANG);
    for (uint32_t t = 1000; t < 1150; t += 40) TEST_ASSERT_FALSE(omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, LOOK));
    for (uint32_t t = 1150; t < 2000; t += 40) TEST_ASSERT_FALSE(omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, HANG));
}

static void test_pose_face_down(void)
{
    omni_wake_sm_t w; omni_wake_reset(&w);
    omni_accel_t down = {0, 0, -1};
    for (uint32_t t = 0; t < 400; t += 40) omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, down);
    TEST_ASSERT_EQUAL(POSE_FACE_DOWN, omni_wake_pose(&w));
    omni_wake_sm_t h; omni_wake_reset(&h);
    omni_wake_feed(&h, &OMNI_WAKE_DEFAULTS, 0, HANG);
    TEST_ASSERT_EQUAL(POSE_HANGING, omni_wake_pose(&h));
    /* z_sign flips the mounting */
    omni_wake_cfg_t c = OMNI_WAKE_DEFAULTS; c.z_sign = -1;
    omni_wake_sm_t f; omni_wake_reset(&f);
    omni_wake_feed(&f, &c, 0, LOOK);
    TEST_ASSERT_NOT_EQUAL(POSE_LOOKING, omni_wake_pose(&f));
}

/* R20: still, not upright, not LOOKING for >= 2 s = pocketed -> FACE_DOWN (the 5 s timeout). */
static void test_pose_pocket_on_edge(void)
{
    const omni_accel_t edge = {-0.93f, -0.24f, -0.10f}; /* the real pocket reading */
    omni_wake_sm_t w; omni_wake_reset(&w);
    for (uint32_t t = 0; t < 1960; t += 40) {
        omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, edge);
        TEST_ASSERT_EQUAL(POSE_HANGING, omni_wake_pose(&w));
    }
    omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, 2000, edge);
    TEST_ASSERT_EQUAL(POSE_FACE_DOWN, omni_wake_pose(&w));
    TEST_ASSERT_EQUAL(2000, w.pose_since);
    /* taken out and hung upright: back to HANGING at once */
    omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, 2040, HANG);
    omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, 2080, HANG);
    TEST_ASSERT_EQUAL(POSE_HANGING, omni_wake_pose(&w));
}

static void test_pose_pocket_needs_stillness(void)
{
    omni_wake_sm_t w; omni_wake_reset(&w);
    for (uint32_t t = 0; t < 6000; t += 40) {
        /* on its edge but jostled every second: never 2 s still */
        omni_accel_t a = {-0.93f, -0.24f, -0.10f};
        if (t % 1000 == 0) a.ax = -1.3f;
        omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, t, a);
        TEST_ASSERT_NOT_EQUAL(POSE_FACE_DOWN, omni_wake_pose(&w));
    }
    /* hanging upright and still never counts as pocketed */
    omni_wake_sm_t h; omni_wake_reset(&h);
    for (uint32_t t = 0; t < 6000; t += 40) omni_wake_feed(&h, &OMNI_WAKE_DEFAULTS, t, HANG);
    TEST_ASSERT_EQUAL(POSE_HANGING, omni_wake_pose(&h));
    /* nor does lying face up (LOOKING) */
    omni_wake_sm_t l; omni_wake_reset(&l);
    const omni_accel_t up = {0, 0, 1};
    for (uint32_t t = 0; t < 6000; t += 40) omni_wake_feed(&l, &OMNI_WAKE_DEFAULTS, t, up);
    TEST_ASSERT_EQUAL(POSE_LOOKING, omni_wake_pose(&l));
}

static void test_pwr_timeouts(void)
{
    omni_pwr_sm_t p = {0};
    TEST_ASSERT_EQUAL(PA_WAKE_FULL, omni_pwr_on_lift(&p, 1000));
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_tick(&p, 15999, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&p, 16000, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PWR_GLANCE, p.st);
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_tick(&p, 25999, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PA_TO_SLEEP, omni_pwr_tick(&p, 26000, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PWR_ASLEEP, p.st);

    omni_pwr_sm_t q = {0};
    omni_pwr_on_lift(&q, 0);
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_tick(&q, 4999, 15, POSE_FACE_DOWN));
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&q, 5000, 15, POSE_FACE_DOWN));

    /* sleep_s below 5 is kept when face down; touch extends the timeout */
    omni_pwr_sm_t r = {0};
    omni_pwr_on_lift(&r, 0);
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_on_touch(&r, 4000));
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_tick(&r, 8999, 5, POSE_HANGING));
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&r, 9000, 5, POSE_HANGING));
    omni_pwr_sm_t s = {0};
    omni_pwr_on_lift(&s, 0);
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&s, 3000, 3, POSE_FACE_DOWN));
}

static void test_pwr_relift_skips_reveal(void)
{
    omni_pwr_sm_t p = {0};
    TEST_ASSERT_EQUAL(PA_WAKE_FULL, omni_pwr_on_lift(&p, 0));
    omni_pwr_tick(&p, 5000, 5, POSE_HANGING);   /* glance */
    TEST_ASSERT_EQUAL(PWR_GLANCE, p.st);
    TEST_ASSERT_EQUAL(PA_WAKE_SKIP_REVEAL, omni_pwr_on_lift(&p, 9000));
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_on_lift(&p, 9500));   /* awake: refresh */
    TEST_ASSERT_EQUAL(9500, p.t_last_touch);
    TEST_ASSERT_EQUAL(PA_TO_SLEEP, omni_pwr_on_button(&p, 10000));
    TEST_ASSERT_EQUAL(PA_WAKE_FULL, omni_pwr_on_button(&p, 30000));   /* > 10 s since last wake (9000) */
    omni_pwr_on_button(&p, 31000);
    TEST_ASSERT_EQUAL(PA_WAKE_SKIP_REVEAL, omni_pwr_on_button(&p, 32000));
}

static void test_pwr_touch(void)
{
    omni_pwr_sm_t p = {0};
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_on_touch(&p, 100));    /* asleep: ignored */
    TEST_ASSERT_EQUAL(PWR_ASLEEP, p.st);
    omni_pwr_on_lift(&p, 1000);
    omni_pwr_tick(&p, 6000, 5, POSE_HANGING);
    TEST_ASSERT_EQUAL(PA_WAKE_SKIP_REVEAL, omni_pwr_on_touch(&p, 7000));
    TEST_ASSERT_EQUAL(PWR_AWAKE, p.st);
}

/* m11: PWR at the glance wakes the watch (it looks like a dimmed screen); PWR while awake still sleeps it. */
static void test_pwr_button_in_glance_wakes(void)
{
    omni_pwr_sm_t p = {0};
    omni_pwr_on_lift(&p, 0);
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&p, 15000, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PA_WAKE_FULL, omni_pwr_on_button(&p, 16000));   /* > 10 s since the last wake */
    TEST_ASSERT_EQUAL(PWR_AWAKE, p.st);
    TEST_ASSERT_EQUAL_UINT32(16000, p.t_state);
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_tick(&p, 30999, 15, POSE_HANGING)); /* a full awake period again */
    TEST_ASSERT_EQUAL(PA_TO_SLEEP, omni_pwr_on_button(&p, 31000));
    TEST_ASSERT_EQUAL(PWR_ASLEEP, p.st);

    omni_pwr_sm_t q = {0};
    omni_pwr_on_lift(&q, 0);
    omni_pwr_tick(&q, 5000, 5, POSE_HANGING);                          /* glance 5 s after the wake */
    TEST_ASSERT_EQUAL(PA_WAKE_SKIP_REVEAL, omni_pwr_on_button(&q, 6000)); /* re-wake within 10 s */
}

/* m5: the ms clock wraps every 49.7 days; the later of the last touch and the state change is found by a signed
 * difference, so a touch just after the wrap still counts. */
static void test_pwr_ms_wrap(void)
{
    const uint32_t t0 = 0xFFFFF000u; /* 4096 ms before the wrap */
    omni_pwr_sm_t p = {0};
    TEST_ASSERT_EQUAL(PA_WAKE_FULL, omni_pwr_on_lift(&p, t0));
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_on_touch(&p, 0x100u));         /* after the wrap */
    TEST_ASSERT_EQUAL_UINT32(0x100u, omni_pwr_idle_ref(&p));
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_tick(&p, 0x100u + 14999u, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&p, 0x100u + 15000u, 15, POSE_HANGING));

    omni_pwr_sm_t q = {.st = PWR_AWAKE, .t_state = 0x10u, .t_last_touch = 0xFFFFFFF0u}; /* touch before the wrap */
    TEST_ASSERT_EQUAL_UINT32(0x10u, omni_pwr_idle_ref(&q));
    q.t_last_touch = 0x20u;
    TEST_ASSERT_EQUAL_UINT32(0x20u, omni_pwr_idle_ref(&q));
}

static void test_awake_hold_expiry(void)
{
    TEST_ASSERT_FALSE(omni_hold_expired(1000, 1000, OMNI_AWAKE_HOLD_MAX_MS));
    TEST_ASSERT_FALSE(omni_hold_expired(1000, 1000 + OMNI_AWAKE_HOLD_MAX_MS - 1, OMNI_AWAKE_HOLD_MAX_MS));
    TEST_ASSERT_TRUE(omni_hold_expired(1000, 1000 + OMNI_AWAKE_HOLD_MAX_MS, OMNI_AWAKE_HOLD_MAX_MS));
    TEST_ASSERT_TRUE(omni_hold_expired(1000, 1000 + 10 * OMNI_AWAKE_HOLD_MAX_MS, OMNI_AWAKE_HOLD_MAX_MS));
    /* across the 49.7-day wrap of the ms clock */
    const uint32_t since = UINT32_MAX - 5000;
    TEST_ASSERT_FALSE(omni_hold_expired(since, since + 6000, OMNI_AWAKE_HOLD_MAX_MS));
    TEST_ASSERT_TRUE(omni_hold_expired(since, since + OMNI_AWAKE_HOLD_MAX_MS, OMNI_AWAKE_HOLD_MAX_MS));
    TEST_ASSERT_EQUAL_UINT32(1800000u, OMNI_AWAKE_HOLD_MAX_MS);
    /* a hold set just after the tick sampled its time (since a little after now): not expired */
    TEST_ASSERT_FALSE(omni_hold_expired(5000, 4990, OMNI_AWAKE_HOLD_MAX_MS));
    TEST_ASSERT_FALSE(omni_hold_expired(0, UINT32_MAX, OMNI_AWAKE_HOLD_MAX_MS));
}

/* M1: power's own backstop on net's Wi-Fi hold. Asleep longer than OMNI_WIFI_HOLD_MAX_MS, a hold still set is ignored
 * (a net task blocked in an esp_wifi/BLE/NVS call never clears it), so light sleep comes on anyway. */
static void test_wifi_hold_backstop(void)
{
    TEST_ASSERT_EQUAL_UINT32(12u * 60u * 1000u, OMNI_WIFI_HOLD_MAX_MS);
    TEST_ASSERT_FALSE(omni_wifi_hold_effective(false, 1000, 1000));
    TEST_ASSERT_TRUE(omni_wifi_hold_effective(true, 1000, 1000));
    TEST_ASSERT_TRUE(omni_wifi_hold_effective(true, 1000, 1000 + OMNI_WIFI_HOLD_MAX_MS - 1));
    TEST_ASSERT_FALSE(omni_wifi_hold_effective(true, 1000, 1000 + OMNI_WIFI_HOLD_MAX_MS));
    TEST_ASSERT_FALSE(omni_wifi_hold_effective(false, 1000, 1000 + OMNI_WIFI_HOLD_MAX_MS));
    const uint32_t since = UINT32_MAX - 1000;   /* across the ms wrap */
    TEST_ASSERT_TRUE(omni_wifi_hold_effective(true, since, since + 60000));
    TEST_ASSERT_FALSE(omni_wifi_hold_effective(true, since, since + OMNI_WIFI_HOLD_MAX_MS + 1));
}

/* Stage 5 (Task 4): a countdown ended. Asleep or in the glance it wakes without the reveal; it is not a lift, so the
 * 10 s reveal rule (t_last_wake, woke) is untouched. */
static void test_pwr_alarm_wakes_from_sleep(void)
{
    omni_pwr_sm_t p = {0};
    TEST_ASSERT_EQUAL(PA_WAKE_SKIP_REVEAL, omni_pwr_on_alarm(&p, 5000));
    TEST_ASSERT_EQUAL(PWR_AWAKE, p.st);
    TEST_ASSERT_FALSE(p.woke);
    TEST_ASSERT_EQUAL_UINT32(5000, p.t_state);
    TEST_ASSERT_EQUAL_UINT32(5000, p.t_last_touch);
    TEST_ASSERT_EQUAL_UINT32(0, p.t_last_wake);
}

static void test_pwr_alarm_from_glance(void)
{
    omni_pwr_sm_t p = {0};
    omni_pwr_on_lift(&p, 0);
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&p, 15000, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PA_WAKE_SKIP_REVEAL, omni_pwr_on_alarm(&p, 20000)); /* > 10 s after the lift: still no reveal */
    TEST_ASSERT_EQUAL(PWR_AWAKE, p.st);
    TEST_ASSERT_EQUAL_UINT32(0, p.t_last_wake);
}

static void test_pwr_alarm_awake_extends(void)
{
    omni_pwr_sm_t p = {0};
    omni_pwr_on_lift(&p, 0);
    const uint32_t t = 10000;
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_on_alarm(&p, t));
    TEST_ASSERT_EQUAL(PWR_AWAKE, p.st);
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_tick(&p, t + 14999, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&p, t + 15000, 15, POSE_HANGING));
}

static void test_pwr_alarm_not_a_lift(void)
{
    omni_pwr_sm_t p = {0};
    TEST_ASSERT_EQUAL(PA_WAKE_SKIP_REVEAL, omni_pwr_on_alarm(&p, 0));
    TEST_ASSERT_EQUAL(PA_TO_SLEEP, omni_pwr_on_button(&p, 1000));
    TEST_ASSERT_EQUAL(PA_WAKE_FULL, omni_pwr_on_lift(&p, 3000)); /* the alarm was no wake for the 10 s rule */
}

/* F2: UI_ALARM_KEEP (the ring's 250 ms poll) is activity only: it never wakes, so a KEEP queued just before PWR
 * silences the ring, or racing the glance, cannot turn the screen back on. */
static void test_pwr_alarm_keep_never_wakes(void)
{
    omni_pwr_sm_t p = {0};
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_on_alarm_keep(&p, 100));
    TEST_ASSERT_EQUAL(PWR_ASLEEP, p.st);
    TEST_ASSERT_EQUAL_UINT32(0, p.t_state);

    omni_pwr_on_lift(&p, 1000);
    TEST_ASSERT_EQUAL(PA_TO_GLANCE, omni_pwr_tick(&p, 16000, 15, POSE_HANGING));
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_on_alarm_keep(&p, 17000));
    TEST_ASSERT_EQUAL(PWR_GLANCE, p.st);
    TEST_ASSERT_EQUAL_UINT32(16000, p.t_state);
    TEST_ASSERT_EQUAL(PA_TO_SLEEP, omni_pwr_tick(&p, 26000, 15, POSE_HANGING)); /* the glance runs its course */

    omni_pwr_sm_t q = {0};
    omni_pwr_on_lift(&q, 0);
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_on_alarm_keep(&q, 9000));
    TEST_ASSERT_EQUAL(PWR_AWAKE, q.st);
    TEST_ASSERT_EQUAL_UINT32(9000, q.t_last_touch);
    TEST_ASSERT_EQUAL_UINT32(9000, omni_pwr_idle_ref(&q));
    TEST_ASSERT_EQUAL(PA_NONE, omni_pwr_tick(&q, 9000 + 14999, 15, POSE_HANGING));
}

static void test_orbit_bounds_and_cycle(void)
{
    int8_t px = 100, py = 100;
    for (int m = -20; m < 200; m++) {
        int8_t x, y, x9, y9;
        omni_orbit_offset((int64_t)m * 60 + 17, &x, &y);
        omni_orbit_offset((int64_t)(m + 9) * 60 + 17, &x9, &y9);
        TEST_ASSERT_TRUE(x >= -4 && x <= 4 && y >= -4 && y <= 4);
        TEST_ASSERT_EQUAL(x, x9);
        TEST_ASSERT_EQUAL(y, y9);
        if (m > -20) TEST_ASSERT_TRUE(x != px || y != py);
        px = x; py = y;
    }
    /* constant within a minute */
    int8_t a, b, c, d;
    omni_orbit_offset(600, &a, &b);
    omni_orbit_offset(659, &c, &d);
    TEST_ASSERT_EQUAL(a, c);
    TEST_ASSERT_EQUAL(b, d);
}

typedef struct {
    int wakes, expect;
    omni_pose_t last_pose;
    uint32_t face_down_max_ms; /* longest uninterrupted FACE_DOWN run */
} replay_t;

static replay_t replay(const char *path)
{
    FILE *f = fopen(path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    omni_wake_sm_t w; omni_wake_reset(&w);
    char line[160];
    replay_t r = {0, -1, POSE_UNKNOWN, 0};
    bool fd = false;
    uint32_t fd_since = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            const char *k = strstr(line, "expect_wakes=");
            if (k) r.expect = atoi(k + 13);
            continue;
        }
        unsigned long t; float x, y, z;
        if (sscanf(line, "%lu,%f,%f,%f", &t, &x, &y, &z) != 4) continue;
        omni_accel_t a = {x, y, z};
        r.wakes += omni_wake_feed(&w, &OMNI_WAKE_DEFAULTS, (uint32_t)t, a);
        r.last_pose = omni_wake_pose(&w);
        if (r.last_pose == POSE_FACE_DOWN) {
            if (!fd) { fd = true; fd_since = (uint32_t)t; }
            if ((uint32_t)t - fd_since > r.face_down_max_ms) r.face_down_max_ms = (uint32_t)t - fd_since;
        } else {
            fd = false;
        }
    }
    fclose(f);
    return r;
}

static replay_t replay_named(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", TRACE_DIR, name);
    return replay(path);
}

static void test_pocket_traces(void)
{
    replay_t p = replay_named("worn_pocket.csv");
    TEST_ASSERT_EQUAL(POSE_FACE_DOWN, p.last_pose);
    replay_t h = replay_named("worn_hang_still.csv");
    TEST_ASSERT_NOT_EQUAL(POSE_FACE_DOWN, h.last_pose);
    TEST_ASSERT_EQUAL_UINT32(0, h.face_down_max_ms);
    replay_t k = replay_named("worn_walk.csv");
    TEST_ASSERT_NOT_EQUAL(POSE_FACE_DOWN, k.last_pose);
    TEST_ASSERT_TRUE(k.face_down_max_ms < 1000);
}

static void test_trace_replay(void)
{
    DIR *d = opendir(TRACE_DIR);
    TEST_ASSERT_NOT_NULL(d);
    struct dirent *e;
    int files = 0;
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".csv") != 0) continue;
        char path[512];
        snprintf(path, sizeof path, "%s/%s", TRACE_DIR, e->d_name);
        replay_t r = replay(path);
        TEST_ASSERT_TRUE_MESSAGE(r.expect >= 0, path);
        TEST_ASSERT_EQUAL_MESSAGE(r.expect, r.wakes, path);
        files++;
    }
    closedir(d);
    TEST_ASSERT_TRUE(files >= 3);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wake_on_hold);
    RUN_TEST(test_wake_no_refire_until_rearm);
    RUN_TEST(test_wake_rejects_swing);
    RUN_TEST(test_wake_rejects_brief_tilt);
    RUN_TEST(test_pose_face_down);
    RUN_TEST(test_pose_pocket_on_edge);
    RUN_TEST(test_pose_pocket_needs_stillness);
    RUN_TEST(test_pocket_traces);
    RUN_TEST(test_pwr_timeouts);
    RUN_TEST(test_pwr_relift_skips_reveal);
    RUN_TEST(test_pwr_touch);
    RUN_TEST(test_pwr_button_in_glance_wakes);
    RUN_TEST(test_pwr_ms_wrap);
    RUN_TEST(test_awake_hold_expiry);
    RUN_TEST(test_wifi_hold_backstop);
    RUN_TEST(test_pwr_alarm_wakes_from_sleep);
    RUN_TEST(test_pwr_alarm_from_glance);
    RUN_TEST(test_pwr_alarm_awake_extends);
    RUN_TEST(test_pwr_alarm_not_a_lift);
    RUN_TEST(test_pwr_alarm_keep_never_wakes);
    RUN_TEST(test_orbit_bounds_and_cycle);
    RUN_TEST(test_trace_replay);
    return UNITY_END();
}
