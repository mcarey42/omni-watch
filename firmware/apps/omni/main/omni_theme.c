/* The Omni's theme descriptor (watch_theme.h): its faces in picker order by id, the Omni limits, the face
 * registration Stage 4a's app_main did, and the console commands only the Omni has (`globe` here; `dial` and `ring`
 * in dial_console.c). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "omni/globe.h"
#include "omni/nav.h"
#include "omni/ring.h"
#include "omni/settings.h"
#include "omni/tmr_alarm.h"
#include "watch_ui.h"
#include "omni_faces.h"
#include "mmap_generate_assets.h"   /* generated at build time from firmware/apps/omni/assets/ (main/CMakeLists.txt) */
#include "ui/assets_meta.h"
#include "ui/dial_view.h"
#include "ui/globe_face.h"
#include "ui/globe_pie.h"
#include "ui/globe_view.h"
#include "ui/timer_glance.h"
#include "ui/timer_svc.h"

/* Titles and groups of the mockup faces; thumbnails from the asset manifest (assets_meta.h: tools/export names them
 * thumb_<id>, R48/C8), so a renamed export fails the build here rather than showing a plain disc. Keys: `face <key>`
 * (the planned faces have none). */
static const watch_face_info_t FACES[FACE__COUNT] = {
    [FACE_OMNI] = {"omni", "Omni dial", "Omni", ASSET_THUMB_OMNI_DIAL},
    [FACE_GLOBE_LIVE] = {"live", "Real-time globe", "Faces", ASSET_THUMB_GLOBE_LIVE},
    [FACE_GLOBE_ANALOG] = {"analog", "Globe analog", "Faces", ASSET_THUMB_GLOBE_ANALOG},
    [FACE_DIGITAL] = {"digital", "Digital", "Faces", ASSET_THUMB_DIGITAL},
    [FACE_WORLD] = {"world", "World time", "Faces", ASSET_THUMB_WORLD},
    [FACE_ALMANAC] = {"almanac", "Almanac", "Faces", ASSET_THUMB_ALMANAC},
    [FACE_TIMER] = {"timer", "Timer & stopwatch", "Faces", ASSET_THUMB_TIMER},
    [FACE_GLOBE_SPIN] = {"spin", "Spinning globe", "Faces", ASSET_THUMB_GLOBE_SPIN},
};
_Static_assert(ASSET_THUMB_OMNI_DIAL_W == WATCH_THUMB_PX && ASSET_THUMB_GLOBE_LIVE_W == WATCH_THUMB_PX &&
                   ASSET_THUMB_GLOBE_ANALOG_W == WATCH_THUMB_PX && ASSET_THUMB_DIGITAL_W == WATCH_THUMB_PX &&
                   ASSET_THUMB_WORLD_W == WATCH_THUMB_PX && ASSET_THUMB_ALMANAC_W == WATCH_THUMB_PX &&
                   ASSET_THUMB_TIMER_W == WATCH_THUMB_PX && ASSET_THUMB_GLOBE_SPIN_W == WATCH_THUMB_PX,
               "the exported thumbnails are WATCH_THUMB_PX square");
_Static_assert(FACE__COUNT <= SHELL_FACE_MAX, "the shell hosts every Omni face id");

/* The default carousel (Stage 4a's omni_settings_defaults order; Task 6 moves the defaults here). */
static const uint8_t DEF_CAROUSEL[] = {FACE_OMNI, FACE_GLOBE_LIVE, FACE_GLOBE_ANALOG, FACE_DIGITAL,
                                       FACE_WORLD, FACE_ALMANAC, FACE_TIMER, FACE_GLOBE_SPIN};
/* By enum omni_reveal (Settings' Reveal labels) */
static const char *const REVEAL_NAMES[] = {[OMNI_REVEAL_FLIP] = "Lid flip", [OMNI_REVEAL_IRIS] = "Brass iris",
                                           [OMNI_REVEAL_NONE] = "Instant"};

/* Stage 4a's order and rule: the digital face always, the ones that need the mapped assets only when they are. The
 * timer service (its alarm and bell) runs from here on with the timer face (Stage 5). */
static void register_faces(bool assets_ok)
{
    shell_register_face(FACE_DIGITAL, &FACE_DIGITAL_VT);
    if (!assets_ok) return;
    shell_register_face(FACE_GLOBE_LIVE, &FACE_GLOBE_LIVE_VT);
    shell_register_face(FACE_GLOBE_SPIN, &FACE_GLOBE_SPIN_VT);
    shell_register_face(FACE_GLOBE_ANALOG, &FACE_GLOBE_ANALOG_VT);
    shell_register_face(FACE_OMNI, &FACE_OMNI_VT);   /* registration alone undims its picker thumbnail */
    tmr_svc_init();
    shell_register_face(FACE_TIMER, &FACE_TIMER_VT);
    shell_register_face(FACE_ALMANAC, &FACE_ALMANAC_VT);
    shell_register_face(FACE_WORLD, &FACE_WORLD_VT);
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* globe [hold <lon>|off | bench | selftest | demo spin|live|analog|off [deg/s]]: the current globe view (globe_view_current).
 * Plain `globe`: its stats and PSRAM free. demo: a bare view of that face's desc at its box origin on the screen,
 * re-rendered at the face's cadence with lon0 0 and the current sun (it does not follow sleep: a debug tool). */
static globe_face_t *s_demo;
static float s_demo_rate;   /* deg/s: `globe demo <face> <deg/s>` turns the globe at full rate, like a drag (Task 11) */

static void demo_tick(globe_face_t *gf, const globe_tick_t *t, globe_view_frame_t *f, bool *animating)
{
    const globe_view_t *v = globe_face_view(gf);
    const omni_globe_cfg_t *cfg = globe_view_cfg(v);
    const char *name = globe_view_name(v);
    const double lon0 = fmod(s_demo_rate * (t->now_ms % 3600000u) / 1000.0, 360.0);
    *animating = s_demo_rate != 0;
    omni_sun_t raw;
    const bool set = globe_face_sun((int64_t)time(NULL), cfg->lat0_deg, lon0, &f->sun, &raw);
    if (cfg->studio) omni_globe_sun_studio(&f->sun);
    f->lon0_deg = lon0;
    if (strcmp(name, "live") == 0) {   /* plan Task 8: lobe toward the projected sun when z > -0.3; strength 0.9 */
        f->halo = true;
        f->halo_strength = 0.9f;
        float sp[3] = { 0, 0, -1 };
        if (set) omni_globe_project(cfg, raw.subsolar_lat, raw.subsolar_lon, lon0, sp);
        if (sp[2] > -0.3f) {
            f->halo_dir[0] = sp[0];
            f->halo_dir[1] = sp[1];
        }
    } else if (strcmp(name, "spin") == 0) {   /* plan Task 9: a static lobe */
        f->halo = true;
        f->halo_strength = 0.75f;
        f->halo_dir[0] = -0.6f;
        f->halo_dir[1] = -0.55f;
    }
}

/* the face's view and cadence, no reveal, on top of the screen */
static const globe_face_ops_t DEMO[] = {
    { .view = "live", .cad = OMNI_CAD_LIVE, .z = -1, .tick = demo_tick },
    { .view = "spin", .cad = OMNI_CAD_SPIN, .z = -1, .tick = demo_tick },
    { .view = "analog", .cad = OMNI_CAD_ANALOG, .z = -1, .tick = demo_tick },
};

static void globe_print_stats(globe_view_t *v)
{
    globe_view_stats_t st;
    globe_view_stats(v, &st);
    printf("GLOBE %s: %s, %u frames, render avg %u us max %u us, base max %u us, build %u ms, %u bytes, worker stack "
           "free %u, helper %u; resets %u, frames dropped %u, unseen %u (shown seq %u)\n",
           globe_view_name(v), globe_view_ready(v) ? "ready" : "building", (unsigned)st.frames,
           (unsigned)st.render_us_avg, (unsigned)st.render_us_max, (unsigned)st.base_us_max, (unsigned)st.build_ms,
           (unsigned)st.bytes, globe_view_worker_stack_free(v), globe_view_helper_stack_free(v), (unsigned)st.resets,
           (unsigned)st.dropped, (unsigned)st.unseen, (unsigned)globe_view_shown(v));
}

static int cmd_globe(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    int rc = 0;
    bsp_display_lock(-1);
    globe_view_t *v = globe_view_current();
    if (strcmp(sub, "demo") == 0 && (argc == 3 || argc == 4)) {
        s_demo_rate = argc == 4 ? (float)atof(argv[3]) : 0;
        const globe_face_ops_t *ops = NULL;
        for (size_t i = 0; i < sizeof DEMO / sizeof DEMO[0]; i++)
            if (strcmp(argv[2], DEMO[i].view) == 0) ops = &DEMO[i];
        int64_t t0 = esp_timer_get_time();
        if (s_demo) {
            globe_face_destroy(s_demo);
            s_demo = NULL;
        }
        const unsigned destroy_ms = (unsigned)((esp_timer_get_time() - t0) / 1000);
        if (strcmp(argv[2], "off") == 0) {
            printf("GLOBE demo off (destroy %u ms)\n", destroy_ms);
        } else if (!ops) {
            printf("usage: globe demo spin|live|analog|off [deg/s]\n");
            rc = 1;
        } else {
            s_demo = globe_face_create(lv_screen_active(), ops, NULL);   /* at the face's box origin, made even */
            globe_face_wake(s_demo, false);
            printf("GLOBE demo %s %s\n", argv[2], s_demo ? "on" : "FAILED");
            rc = s_demo ? 0 : 1;
        }
    } else if (strcmp(sub, "kernel") == 0 && argc == 3 &&
               (strcmp(argv[2], "ref") == 0 || strcmp(argv[2], "fast") == 0 || strcmp(argv[2], "pie") == 0 ||
                strcmp(argv[2], "none") == 0)) {
        /* none: LVGL's ceiling (frames swapped, never rendered), on this view only (R100/R101) */
        globe_view_null_render(v, argv[2][0] == 'n');
        /* LVGL lock held: no frame is being submitted (a frame in flight finishes with either kernel, both exact) */
        globe_view_kernel = argv[2][0] == 'r' ? omni_globe_span_ref : omni_globe_span_fast;   /* planar: no PIE */
        globe_view_ckernel = argv[2][0] == 'r' ? omni_globe_cspan_ref
                             : argv[2][0] == 'f' ? omni_globe_cspan_fast
                                                 : globe_cspan_pie;
        globe_view_over_run = argv[2][0] == 'p' ? globe_over_pie : NULL;   /* NULL: the C overlay */
        printf("GLOBE kernel %s%s\n", argv[2], argv[2][0] != 'n' ? "" : v ? " (this view only)" : " ignored: no view");
    } else if (argc == 1 || (strcmp(sub, "hold") == 0 && argc == 3) || strcmp(sub, "bench") == 0 ||
               strcmp(sub, "selftest") == 0) {
        if (!v) {
            printf("GLOBE no view\n");
            rc = 1;
        } else if (argc == 1) {
            globe_print_stats(v);
        } else if (sub[0] == 'h') {
            const bool off = strcmp(argv[2], "off") == 0;
            globe_view_hold(v, off ? NAN : atof(argv[2]));
            printf("GLOBE hold %s\n", off ? "off" : argv[2]);
        } else if (sub[0] == 'b') {
            globe_view_bench(v, 30);
        } else {
            rc = globe_view_selftest(v) ? 0 : 1;
        }
    } else {
        printf("usage: globe [hold <lon>|off | bench | selftest | demo spin|live|analog|off [deg/s] | kernel ref|fast|pie|none]\n");
        rc = 1;
    }
    bsp_display_unlock();
    printf("GLOBE psram free %u (min %u since boot; internal %u, min %u)\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    return rc;
}

/* `assets` (watch_ui): the prebuilt dial map (R80), its header checked as the view checks it. Moved here from watch_ui's
 * cmd_assets (Stage 4b Task 7) so the dw image links no omni_dial_* code; the line is unchanged. */
static bool omni_asset_line(const char *name, const void *data, size_t size)
{
    const char *ext = strrchr(name, '.');
    if (!ext || strcmp(ext, ".bin") != 0 || size < OMNI_PMAP_HDR || memcmp(data, "OPM1", 4) != 0) return false;
    const size_t need = omni_pmap_file_mem(data, size);
    void *mem = need ? heap_caps_malloc(need, MALLOC_CAP_SPIRAM) : NULL;
    const uint8_t *h = data;
    omni_pmap_t m;
    char why[64] = "no memory";
    const omni_pmap_cfg_t cfg = omni_dial_pmap_cfg((uint16_t)(h[6] | h[7] << 8));
    const bool ok = mem && omni_pmap_load(&m, &cfg, data, size, mem, need, why, sizeof why);
    printf("%-16s %8u dial map N %u, %u spans, %s\n", name, (unsigned)size, cfg.n_theta,
           ok ? (unsigned)m.nspans : 0u, ok ? "header and checksum ok" : why);
    heap_caps_free(mem);
    return true;
}

/* tmr [count <s> | sw | reset | postfail <n>]: the timer's mode, remaining and elapsed (R101: console builds only).
 * count: a countdown of s seconds (decimal digits only, a typo is refused; capped at 1 h, D15); sw: a stopwatch from 0;
 * reset: idle; postfail: the timer service's next n posts fail (F5's retry and re-post paths, Task 9). Under the LVGL
 * lock, as the face's own calls. */
static int cmd_tmr(int argc, char **argv)
{
    static const char *const MODE[] = {"idle", "set", "count", "paused", "done", "sw", "swpause", "armed"};
    const char *sub = argc >= 2 ? argv[1] : "";
    if (!shell_face_ready(FACE_TIMER)) {
        printf("TMR no timer (assets not mapped)\n");
        return 1;
    }
    int rc = 0;
    bsp_display_lock(-1);
    omni_tmr_t *t = tmr_svc();
    const uint32_t now = tmr_svc_now_ms();
    if (argc == 1) {
        rc = 0;   /* the state only */
    } else if (strcmp(sub, "count") == 0 && argc == 3) {
        uint32_t secs;
        if (omni_tmr_parse_seconds(argv[2], &secs)) omni_tmr_start_seconds(t, now, secs);
        else rc = 1;
    } else if (strcmp(sub, "postfail") == 0 && argc == 3) {
        uint32_t n;
        if (omni_tmr_parse_seconds(argv[2], &n)) tmr_svc_debug_postfail(n);   /* the same digits-only rule */
        else rc = 1;
    } else if (strcmp(sub, "sw") == 0 && argc == 2) {
        omni_tmr_reset(t);
        omni_tmr_tap(t, now);   /* idle -> stopwatch */
    } else if (strcmp(sub, "reset") == 0 && argc == 2) {
        omni_tmr_reset(t);
    } else {
        rc = 1;
    }
    if (rc == 0 && argc > 1 && strcmp(sub, "postfail") != 0) {
        tmr_svc_changed();
        shell_refresh();
    }
    const unsigned m = (unsigned)t->mode;
    const uint32_t rem = omni_tmr_remaining_ms(t, now);
    const uint64_t el = omni_tmr_elapsed_ms(t, now);
    bsp_display_unlock();
    if (rc) {
        printf("usage: tmr [count <s> | sw | reset | postfail <n>]\n");
        return rc;
    }
    printf("TMR %s remaining %u ms elapsed %llu ms postfail %u\n", m < sizeof MODE / sizeof MODE[0] ? MODE[m] : "?",
           (unsigned)rem, (unsigned long long)el, (unsigned)tmr_svc_debug_postfail_left());
    return 0;
}

static void omni_console(void)
{
    debug_console_reg("tmr", "tmr [count <s> | sw | reset | postfail <n>]: the timer's mode, remaining and elapsed; a "
        "countdown of s seconds, a stopwatch, idle; the timer service's next n posts fail", &cmd_tmr);
    debug_console_reg("globe", "globe [hold <lon>|off | bench | selftest | demo spin|live|analog|off [deg/s] | kernel ref|fast|pie|none]: the "
        "globe view's stats, a held longitude, kernel timings, the bit-exact selftest, a bare demo view (turning at "
        "deg/s, full rate), the span kernel",
        &cmd_globe);
    dial_console_register();
}
#endif

const watch_theme_t OMNI_THEME = {
    .id = "omni",
    .name = "Omni",
    .faces = FACES,
    .face_count = FACE__COUNT,
    .fallback_face = FACE_DIGITAL,
    .limits = {.face_count = FACE__COUNT, .reveal_count = 3, .reveal_default = OMNI_REVEAL_FLIP,
               .def_len = sizeof DEF_CAROUSEL, .def_carousel = DEF_CAROUSEL},
    .reveal_names = REVEAL_NAMES,
    .omni_legacy = true,
    .assets_files = MMAP_ASSETS_FILES,
    .assets_checksum = MMAP_ASSETS_CHECKSUM,
    .register_faces = register_faces,
    .glance_override = timer_glance_override,   /* the countdown takes the glance on every face */
#if CONFIG_OMNI_DEBUG_CONSOLE
    .console = omni_console,
    .asset_line = omni_asset_line,
#endif
};
