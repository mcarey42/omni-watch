/* The `rd` console command (rd wave 1 Task 4; R101: built only with CONFIG_OMNI_DEBUG_CONSOLE, registered by log and
 * continue through debug_console_reg). The prompt, "rd> ", is built at run time from the descriptor's id (the shared
 * console), so no image holds it.
 *   rd prefs          the theme's preferences (the published snapshot), the offered mask and the carousel
 *   rd offer reset    clear the offered bit of every listed face the carousel lacks (on the event loop, R26/R27), so
 *                     the next boot appends those faces again: the recovery after a rollback to a build with fewer
 *                     faces dropped them. Refused on a no-write boot or a read-only record; prints the event loop's
 *                     own verdict
 *   rd faces          every face id: its key, and whether it is built (its own face) or a placeholder (the shared
 *                     Digital face in its place, RW11); Digital itself is the shared face
 *   rd verify         every built face's still frames (rd_still) against rd/verify_crc.h: MATCH or DIFF per still,
 *                     then the count (a frame in PSRAM, each face bound over the assets image)
 *   rd asleep         the time-asleep record (Task 9, spec section 9.6): valid or none, consumed, the token's boot,
 *                     the sleep's clocks, what a wake now would give and by which rule, and what the readout shows
 *   rd asleep forget  invalidate the record and the shown value (the on-device stand-in for a cold boot: the readout
 *                     shows "--" until the next sleep and wake)
 *   rd chime          the chime service (Task 8): the state, the next boundary, the last verdict, the plays, then the
 *                     plan's (the sound, its misses)
 *   rd chime at +N | at off
 *                     the real path against a boundary N s ahead (a scheduler offset, H7; RAM only: cleared by `at off`
 *                     and a reboot)
 *   rd lid bench [n]  Task 10: n Stasis Releases (default 5) through the real path on the face shown (the Stasis
 *                     Clock, shown and idle): fps, ms a frame, the bands lost, the PSRAM and internal minimum, the
 *                     interior's ms a frame (fk_view_reveal_bench, as `dw doors bench`); then each frame's wall time
 *                     by the reveal's window (shut, swing, the mist after 1620 ms, the settle; rd_rel_timing)
 *   rd reveal bench [n]
 *                     wave 2 Task 4: the same on whichever revealing face is shown (Stasis Release, Bay Mouth's
 *                     airlock, or Task 5's hull pass on Drift Plate), its frames by that reveal's own windows (the
 *                     airlock's: shut 0-450, jam 450-1080, kick 1080-1680, settle 1680-1950; the pass's: rush 0-1000,
 *                     slow 1000-1500, park 1500-1800, settle 1800-2050)
 *   rd scope step [0 | 250]
 *                     wave 2 Task 7: Nav Scope's sweep mode for measuring (W2-D11): 0 the continuous sweep, 250 the
 *                     4 Hz stepped sweep (C8); no argument prints it. RAM only (a reboot gives RD_SC_STEP_MS again);
 *                     `rd verify` refuses while it differs from RD_SC_STEP_MS, since the stills pin that mode
 *   rd stacks         the stack margins (FreeRTOS high-water marks, the least ever free, in bytes) of the LVGL task
 *                     (where the canvas faces draw: Curry's steam and wake, Task 15's review), the esp_timer task (the
 *                     chime's scheduler) and the event task, for the device session's readings (W1-S1)
 *   rd chime play [recipe]
 *                     the chime now through the service (fk_chime_svc_play_now: the quiet window's gain, the sound's own
 *                     gate); recipe plays the recipe as written (rd_chime_r.pcm) once instead of the default voicing,
 *                     for the owner's listening check */
#include "rd_console.h"
#include "sdkconfig.h"
#if CONFIG_OMNI_DEBUG_CONSOLE
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "debug_console.h"
#include "shell.h"
#include "store.h"
#include "watch_theme.h"
#include "omni/nav.h"
#include "sfx.h"
#include "rd/chime_env.h"
#include "fk_chime_svc.h"
#include "rd_chime.h"
#include "rd_prefs_svc.h"
#include "rd_asleep.h"
#include "rd/faces.h"
#include "rd/verify_crc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "assets.h"
#include "bsp/esp-bsp.h"
#include "fk_view.h"
#include "omni/timefmt.h"
#include "rd/airlock.h"
#include "rd/pass.h"
#include "rd/release.h"
#include <time.h>

/* the carousel as the console task can read it (as the shared `face` command does) */
static void prefs_print(void)
{
    const omni_settings_t *s = settings();
    printf("RD PREFS mode %s, chime %s, quiet %02u:00-%02u:00; offered %04x; carousel",
           rd_prefs_now(RD_PF_MODE) == RD_MODE_STASIS ? "stasis" : "always-on",
           rd_prefs_now(RD_PF_CHIME) ? "on" : "off", rd_prefs_now(RD_PF_QUIET_FROM), rd_prefs_now(RD_PF_QUIET_TO),
           rd_prefs_svc_offered());
    for (uint8_t i = 0; i < s->carousel_len && i < OMNI_CAROUSEL_MAX; i++) printf(" %u", s->carousel[i]);
    printf("\n");
}

/* the event loop's verdict (rd_prefs_svc_offer_verdict), printed as it published it: never inferred from the mask */
#define OFFER_WAIT_MS 5000
static int offer_reset(void)
{
    const uint16_t was = rd_prefs_svc_offered();
    const uint32_t before = rd_prefs_svc_offer_verdict();
    if (!shell_post(UI_THEME_SET, omni_theme_set_pack(RD_ROW_OFFER_RESET, RD_OFFER_RESET_GO))) {
        printf("RD OFFER RESET: the event queue is full; try again\n");
        return 1;
    }
    uint32_t v = before;
    for (int i = 0; i < OFFER_WAIT_MS / 50 && (v = rd_prefs_svc_offer_verdict()) >> 8 == before >> 8; i++)
        vTaskDelay(pdMS_TO_TICKS(50));
    if (v >> 8 == before >> 8) {
        printf("RD OFFER RESET: no verdict from the event loop within %d ms (still queued); check `rd prefs` and the "
               "log\n", OFFER_WAIT_MS);
        return 1;
    }
    const uint16_t now = rd_prefs_svc_offered();
    const rd_offer_verdict_t vd = (rd_offer_verdict_t)(v & 0xFFu);
    switch (vd) {
    case RD_OFFER_V_RESET:
        printf("RD OFFER RESET: offered %04x -> %04x; the next boot appends the missing faces\n", was, now);
        break;
    case RD_OFFER_V_NOTHING:
        printf("RD OFFER RESET: nothing to reset (offered %04x; every listed face is in the carousel or not yet "
               "offered)\n", now);
        break;
    case RD_OFFER_V_READ_ONLY:
        printf("RD OFFER RESET: refused: a no-write boot or a read-only record; nothing changed (offered %04x)\n", now);
        break;
    default:
        printf("RD OFFER RESET: refused: the commit failed; nothing changed (offered %04x)\n", now);
        break;
    }
    prefs_print();
    return vd == RD_OFFER_V_RESET || vd == RD_OFFER_V_NOTHING ? 0 : 1;
}

/* every face id the descriptor names: built (its ops in RD_FACES), a placeholder (RW11), or the shared Digital */
static int faces_print(void)
{
    const watch_theme_t *t = watch_theme();
    unsigned placeholders = 0;
    for (uint8_t id = 0; t && id < t->face_count; id++) {
        const char *key = t->faces[id].key ? t->faces[id].key : "?";
        const char *what = id == RD_F_DIGITAL          ? "shared"
                           : rd_theme_placeholder(id)  ? "placeholder"
                           : rd_face_by_key(key)       ? "built"
                                                       : "unregistered";
        placeholders += rd_theme_placeholder(id);
        printf("RD FACE %u %s %s\n", id, key, what);
    }
    printf("RD FACES %u ids, %u built in rd_core, %u placeholders\n", t ? t->face_count : 0u, RD_FACE_COUNT,
           placeholders);
    return 0;
}

#define W 466

/* every built face's still frames against rd/verify_crc.h (dw verify's shape: one frame in PSRAM, each face's context
 * bound over the assets image and freed) */
static int verify(void)
{
    static const fk_verify_tab_t tab = {RD_VERIFY, sizeof RD_VERIFY / sizeof RD_VERIFY[0]};
    if (rd_sc_step_ms() != RD_SC_STEP_MS) {   /* W2-D11: the stills pin the committed sweep */
        printf("RD VERIFY REFUSED: rd scope step is %u ms; the stills pin RD_SC_STEP_MS %u ms (rd scope step %u)\n",
               (unsigned)rd_sc_step_ms(), (unsigned)RD_SC_STEP_MS, (unsigned)RD_SC_STEP_MS);
        return 1;
    }
    uint16_t *fb = heap_caps_malloc((size_t)W * W * 2, MALLOC_CAP_SPIRAM);
    if (!fb) {
        printf("RD VERIFY: no PSRAM for a frame\n");
        return 1;
    }
    unsigned diffs = 0, matched = 0;
    for (unsigned f = 0; f < RD_FACE_COUNT; f++) {
        const fk_face_ops_t *ops = RD_FACES[f];
        void *ctx = heap_caps_calloc(1, ops->ctx_bytes ? ops->ctx_bytes : 1, MALLOC_CAP_SPIRAM);
        if (!ctx || !ops->bind(ctx, assets_raw)) {
            printf("RD VERIFY %s: no PSRAM or assets\n", ops->key);
            heap_caps_free(ctx);
            diffs++;
            continue;
        }
        for (unsigned k = 0; k < rd_still_count(ops); k++) {
            const int64_t t0 = esp_timer_get_time();
            rd_still_frame(ops, ctx, k, fb);
            const uint32_t crc = fk_crc32(0, fb, (size_t)W * W * 2);
            const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            unsigned dups = 0;
            const fk_verify_t *want = fk_verify_lookup(&tab, 1, ops->key, k, &dups);
            rd_still_t st;
            rd_still(k % RD_STILL_N, &st);
            if (want && dups == 1 && want->crc == crc) {
                printf("MATCH %s %u (%s) %08" PRIx32 " %" PRIu32 " us\n", ops->key, k, st.what, crc, us);
                matched++;
            } else {
                printf("DIFF %s %08" PRIx32 " (still %u, %s; the table: %s)\n", ops->key, crc, k, st.what,
                       !want ? "no row" : dups != 1 ? "two rows" : "another CRC");
                diffs++;
            }
            vTaskDelay(1);   /* IDLE's turn (the task watchdog, as `dw verify` learned) */
        }
        heap_caps_free(ctx);
    }
    heap_caps_free(fb);
    printf("RD VERIFY %s: %u still frames match, %u differ (the " RD_VERIFY_SET " table, %u rows)\n",
           diffs ? "DIFF" : "MATCH", matched, diffs, tab.n);
    return diffs ? 1 : 0;
}

/* H7: the real path against a boundary N s ahead (the offset is RAM only: cleared by `at off` and a reboot) */
static int chime_at(const char *a)
{
    const bool off = strcmp(a, "off") == 0;
    char *end = NULL;
    const long n = off ? 0 : strtol(a[0] == '+' ? a + 1 : a, &end, 10);
    if (!off && (a[0] != '+' || !end || *end || n < 1 || n > FK_CHIME_AT_MAX_S)) {
        printf("usage: rd chime at +N (N s, 1..%d) | rd chime at off\n", FK_CHIME_AT_MAX_S);
        return 1;
    }
    if (!fk_chime_svc_at((uint32_t)n)) {
        printf("RD CHIME AT: refused (no boundary, or the service is not started)\n");
        return 1;
    }
    if (!off && !rd_prefs_now(RD_PF_CHIME)) printf("RD CHIME AT: the chime is off: nothing will play\n");
    rd_chime_print();
    return 0;
}

/* the chime now (PR4: fk_chime_svc_play_now exists in every build; only this command is the console's) */
static int chime_play(bool recipe)
{
    const char *what = recipe ? "the recipe voicing (" RD_CHIME_R_PCM ")" : "the default voicing (" RD_CHIME_PCM ")";
    if (!rd_chime_play_now(recipe)) {
        printf("RD CHIME PLAY: %s did not sound (the service is not started, or the asset is missing; `rd chime`)\n",
               what);
        return 1;
    }
    bool on = false;
    uint8_t vol = 0;
    sfx_config_get(&on, &vol);
    printf("RD CHIME PLAY: %s, %u ms; sound %s, volume %u%s\n", what, (unsigned)RD_CHIME_MS, on ? "on" : "off", vol,
           on && vol ? "" : " (dropped: the sound is off or at 0)");
    return 0;
}

/* ---- rd lid bench (Task 10) and rd reveal bench (wave 2 Task 4): the dw doors bench's loop, fk_view_reveal_bench */
/* After a bench has started on v: wait for its result, then print it under tag (what: its reveals' noun), then each
 * frame's wall time by the reveal's four windows (rd_rel_timing's accumulator, which the reveal's frame op fills by its
 * own windows) */
static int bench_report(fk_view_t *v, int n, const char *tag, const char *what, const char *const win[4])
{
    const int64_t t0 = esp_timer_get_time();
    fk_reveal_stats_t s;
    bool done = false;
    for (int i = 0; i < n * 60 && !done; i++) {   /* at most 6 s a release */
        vTaskDelay(pdMS_TO_TICKS(100));
        bsp_display_lock(-1);
        done = fk_view_current() == v && fk_view_reveal_bench_result(v, &s);
        const bool gone = fk_view_current() != v;
        bsp_display_unlock();
        if (gone) break;
    }
    const uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (!done) {
        printf("%s: no result (the face went, or it timed out after %" PRIu32 " ms)\n", tag, ms);
        return 1;
    }
    const uint32_t avg = s.frames ? s.us_sum / s.frames : 0, prep = s.frames ? s.prep_sum / s.frames : 0;
    printf("%s %" PRIu32 " %s (%" PRIu32 " direct, %" PRIu32 " through the canvas)%s: %" PRIu32
           " frames in %" PRIu32 " ms, %.1f fps; %" PRIu32 ".%03" PRIu32 " ms a frame (max %" PRIu32 ".%03" PRIu32
           "); lost %" PRIu32 " bands; PSRAM min %" PRIu32 " B, internal min %" PRIu32 " B; worker stack free %" PRIu32
           " B; interior %" PRIu32 ".%03" PRIu32 " ms a frame over both cores (max %" PRIu32 ".%03" PRIu32 ")\n",
           tag, s.swings, what, s.direct, s.refused, s.cut ? " CUT" : "", s.frames, ms,
           ms ? (double)s.frames * 1000.0 / (double)ms : 0.0, avg / 1000, avg % 1000, s.us_max / 1000, s.us_max % 1000,
           s.lost, s.psram_min, s.int_min, s.stack_hwm, prep / 1000, prep % 1000, s.prep_max / 1000, s.prep_max % 1000);
    rd_rel_timing_t tm;
    rd_rel_timing_get(&tm);
    for (int w = 0; w < 4; w++)
        printf("%s %-16s %4" PRIu32 " frames, %" PRIu32 " ms a frame avg (max %" PRIu32 "), %.1f fps\n", tag, win[w],
               tm.frames[w], tm.frames[w] ? tm.ms[w] / tm.frames[w] : 0, tm.max_ms[w],
               tm.ms[w] ? (double)tm.frames[w] * 1000.0 / (double)tm.ms[w] : 0.0);
    return s.cut ? 1 : 0;
}

/* by the reveal's time (review M5): the heavy mist after the lid is gone, 1620-1900 ms, on its own line */
static const char *const LID_WIN[4] = {"shut 0-1010", "swing 1010-1620", "mist 1620-1900", "settle 1900-2150"};
/* the airlock's (wave 2 Task 4): the shut doors, the crack and the jam, the kick, Bay's settle after the leaves */
static const char *const AIR_WIN[4] = {"shut 0-450", "jam 450-1080", "kick 1080-1680", "settle 1680-1950"};
/* the hull pass's (wave 2 Task 5): the rush, the slowing, the overshoot's park, the windows' and labels' settle */
static const char *const PASS_WIN[4] = {"rush 0-1000", "slow 1000-1500", "park 1500-1800", "settle 1800-2050"};

static int lid_bench(int n)
{
    bsp_display_lock(-1);
    fk_view_t *v = fk_view_current();
    const fk_face_ops_t *ops = fk_view_ops(v);
    rd_rel_set_clock(omni_time_is_set((int64_t)time(NULL)));   /* as the woke hook does before a reveal */
    rd_rel_timing_reset();   /* the frames' wall time by window, over these releases (review M5) */
    const bool ok = v && ops && ops->reveal && ops->reveal_ops == &RD_REVEAL_STASIS && fk_view_reveal_bench(v, n, false);
    bsp_display_unlock();
    if (!ok) {
        printf("RD LID BENCH: needs the Stasis Clock shown and idle (face stasis), its assets and %u KB of PSRAM\n",
               (unsigned)((RD_REVEAL_STASIS.rctx_bytes + 466u * 466u * 2u + 1023u) / 1024u));
        return 1;
    }
    return bench_report(v, n, "RD LID BENCH", "releases", LID_WIN);
}

/* rd reveal bench [n]: n reveals of whichever revealing face is shown (Stasis Release, the airlock, the hull pass),
 * each printed by its own windows */
static int reveal_bench(int n)
{
    bsp_display_lock(-1);
    fk_view_t *v = fk_view_current();
    const fk_face_ops_t *ops = fk_view_ops(v);
    const fk_reveal_ops_t *ro = ops && ops->reveal ? ops->reveal_ops : NULL;
    const char *const *win = ro == &RD_REVEAL_STASIS ? LID_WIN : ro == &RD_REVEAL_AIRLOCK ? AIR_WIN
                             : ro == &RD_REVEAL_PASS ? PASS_WIN : NULL;
    rd_rel_set_clock(omni_time_is_set((int64_t)time(NULL)));   /* as the woke hook does before a reveal */
    rd_rel_timing_reset();
    const bool ok = v && win && fk_view_reveal_bench(v, n, false);
    bsp_display_unlock();
    if (!ok) {
        printf("RD REVEAL BENCH: needs a revealing face shown and idle (stasis, bay or drift), its assets and %u KB of "
               "PSRAM\n",
               (unsigned)(((ro ? ro->rctx_bytes : RD_REVEAL_AIRLOCK.rctx_bytes) + 466u * 466u * 2u + 1023u) / 1024u));
        return 1;
    }
    return bench_report(v, n, "RD REVEAL BENCH", "reveals", win);
}

/* rd scope step [0 | 250] (wave 2 Task 7, W2-D11): Nav Scope's sweep mode, RAM only */
static int scope_step(int argc, char **argv)
{
    if (argc == 4) {
        if (strcmp(argv[3], "0") != 0 && strcmp(argv[3], "250") != 0) {
            printf("usage: rd scope step [0 | 250]\n");
            return 1;
        }
        rd_sc_set_step_ms((uint16_t)atoi(argv[3]));
    }
    printf("RD SCOPE STEP %u ms (%s; committed RD_SC_STEP_MS %u ms; RAM only, a reboot restores it%s)\n",
           (unsigned)rd_sc_step_ms(), rd_sc_step_ms() ? "the 4 Hz stepped sweep" : "the continuous sweep",
           (unsigned)RD_SC_STEP_MS, rd_sc_step_ms() != RD_SC_STEP_MS ? "; rd verify refuses until it does" : "");
    return 0;
}

/* rd stacks (W1-S1): the least each task's stack has ever had free, so a reading taken after a wake or a run of steam
 * covers it */
static int stacks_print(void)
{
    static const char *const TASKS[] = {"lvgl", "esp_timer"};
    printf("RD STACKS free (least ever):");
    for (unsigned i = 0; i < sizeof TASKS / sizeof TASKS[0]; i++) {
        TaskHandle_t h = xTaskGetHandle(TASKS[i]);
        if (h)
            printf(" %s %u B,", TASKS[i], (unsigned)uxTaskGetStackHighWaterMark(h));
        else
            printf(" %s (no task),", TASKS[i]);
    }
    printf(" events %u B\n", app_events_stack_hwm());
    return 0;
}

static int cmd_rd(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "prefs") == 0 && argc == 2) {
        prefs_print();
        return 0;
    }
    if (strcmp(sub, "offer") == 0 && argc == 3 && strcmp(argv[2], "reset") == 0) return offer_reset();
    if (strcmp(sub, "faces") == 0 && argc == 2) return faces_print();
    if (strcmp(sub, "verify") == 0 && argc == 2) return verify();
    if (strcmp(sub, "asleep") == 0 && argc == 2) {
        rd_asleep_print();
        return 0;
    }
    if (strcmp(sub, "asleep") == 0 && argc == 3 && strcmp(argv[2], "forget") == 0) {
        rd_asleep_forget();
        printf("RD ASLEEP FORGET: the record is gone; the readout shows --\n");
        rd_asleep_print();
        return 0;
    }
    if (strcmp(sub, "lid") == 0 && argc >= 3 && argc <= 4 && strcmp(argv[2], "bench") == 0) {
        const int n = argc == 4 ? atoi(argv[3]) : 5;
        if (n < 1 || n > 50) {
            printf("usage: rd lid bench [n 1..50]\n");
            return 1;
        }
        return lid_bench(n);
    }
    if (strcmp(sub, "reveal") == 0 && argc >= 3 && argc <= 4 && strcmp(argv[2], "bench") == 0) {
        const int n = argc == 4 ? atoi(argv[3]) : 5;
        if (n < 1 || n > 50) {
            printf("usage: rd reveal bench [n 1..50]\n");
            return 1;
        }
        return reveal_bench(n);
    }
    if (strcmp(sub, "scope") == 0 && (argc == 3 || argc == 4) && strcmp(argv[2], "step") == 0)
        return scope_step(argc, argv);
    if (strcmp(sub, "stacks") == 0 && argc == 2) return stacks_print();
    if (strcmp(sub, "chime") == 0 && argc == 2) {
        rd_chime_print();
        return 0;
    }
    if (strcmp(sub, "chime") == 0 && argc == 4 && strcmp(argv[2], "at") == 0) return chime_at(argv[3]);
    if (strcmp(sub, "chime") == 0 && strcmp(argc >= 3 ? argv[2] : "", "play") == 0 &&
        (argc == 3 || (argc == 4 && strcmp(argv[3], "recipe") == 0)))
        return chime_play(argc == 4);
    printf("usage: rd prefs | rd offer reset | rd faces | rd verify | rd asleep [forget] | rd lid bench [n] | "
           "rd reveal bench [n] | rd scope step [0|250] | rd stacks | "
           "rd chime [at +N | at off | play [recipe]]\n");
    return 1;
}

void rd_console_register(void)
{
    debug_console_reg("rd", "rd prefs | rd offer reset | rd faces | rd verify | rd asleep [forget] | rd lid bench [n] | "
                      "rd reveal bench [n] | rd scope step [0|250] | rd stacks | "
                      "rd chime [at +N | at off | play [recipe]]: the Mining Ship "
                      "theme's preferences, carousel offer, face table, still-frame CRCs, time asleep, the reveals' "
                      "benches, Nav Scope's sweep mode, stack margins and chime",
                      &cmd_rd);
}
#endif
