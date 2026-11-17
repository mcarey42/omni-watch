#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "board.h"
#include "omni_events.h"
#include "power.h"
#include "store.h"
#include "net.h"
#include "sfx.h"
#include "shell.h"
#include "assets.h"
#include "fontasset.h"
#include "theme.h"
#include "disp_stats.h"
#include "disp_direct.h"
#include "omni/boot.h"
#include "omni/ring.h"
#include "omni/cities.h"
#include "omni/timefmt.h"
#include "omni/power.h"
#include "omni/prov.h"
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <strings.h>
#include <math.h>
#include "debug_console.h"
#include "scr_keyboard.h"
#include "omni/kb.h"
#include "watch_theme.h"
#include "esp_rtc_time.h"
#include "esp_rom_crc.h"
#include <inttypes.h>
#include "esp_private/esp_clk.h"
#include "soc/rtc.h"

static const char *TAG = "console";
static const char *const PWR_STATE[] = {"ASLEEP", "AWAKE", "GLANCE"};

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static const char *pwr_state_name(omni_pwr_state_t st)
{
    return (unsigned)st < sizeof PWR_STATE / sizeof PWR_STATE[0] ? PWR_STATE[st] : "?";
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64-encode len bytes into out (needs 4*ceil(len/3)+1 bytes). */
static void b64_encode(const uint8_t *in, size_t len, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= in[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i + 1 < len) ? B64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < len) ? B64[v & 63] : '=';
    }
    out[o] = '\0';
}

/* shot: "SHOT w h", h lines of base64 RGB565 (little-endian) rows, "SHOT END". */
#define SHOT_ROW_MAX (466 * 2)
#define SHOT_LINE_LEN ((SHOT_ROW_MAX + 2) / 3 * 4 + 8)
static int cmd_shot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bsp_display_lock(-1);
    /* spec 6.5: a revealed field would end up in a capture on the host (checked under the lock: no open between) */
    const bool secret = scr_keyboard_secret_open();
    lv_draw_buf_t *snap = secret ? NULL : lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    bsp_display_unlock();
    if (secret) {
        printf("SHOT refused: password field\n");
        return 1;
    }
    if (snap == NULL) {
        printf("SHOT ERR\n");
        return 1;
    }

    uint32_t w = snap->header.w, h = snap->header.h, stride = snap->header.stride;
    /* one base64 row, in PSRAM for the duration of the command (internal RAM is kept for DMA and the radios) */
    char *line = w * 2 <= SHOT_ROW_MAX ? heap_caps_malloc(SHOT_LINE_LEN, MALLOC_CAP_SPIRAM) : NULL;
    if (line == NULL) {
        printf("SHOT ERR\n");
        lv_draw_buf_destroy(snap);
        return 1;
    }
    printf("SHOT %u %u\n", (unsigned)w, (unsigned)h);
    for (uint32_t y = 0; y < h; y++) {
        b64_encode(snap->data + (size_t)y * stride, w * 2, line);
        printf("%s\n", line);
        vTaskDelay(pdMS_TO_TICKS(2)); /* pace the USB-Serial-JTAG TX FIFO; avoids dropped rows */
    }
    printf("SHOT END\n");
    heap_caps_free(line);
    lv_draw_buf_destroy(snap);
    return 0;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* refr: time a full-screen redraw of whatever is shown (render + flush), in ms. */
static int cmd_refr(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bsp_display_lock(-1);
    int64_t t0 = esp_timer_get_time();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    int64_t t1 = esp_timer_get_time();
    bsp_display_unlock();
    printf("REFR %lld ms\n", (t1 - t0) / 1000);
    return 0;
}

/* fps start | fps: frames displayed since "fps start", and the rate. Run it around an `inject drag/arc` to see how
 * smoothly a screen follows the finger. Displayed frames (A12) are LVGL's refreshes (LV_EVENT_RENDER_START), less those
 * whose flushes the direct path dropped (it owned the panel: not displayed), plus the direct path's whole frames. While
 * the direct path is idle the count is LVGL's alone, as before Stage 4a. */
static volatile uint32_t s_frames;
static int64_t s_fps_t0, s_render_t0, s_render_us, s_render_max;
static uint32_t s_fps_dropped0, s_fps_direct0;
static void frame_cb(lv_event_t *e)   /* LVGL task */
{
    const int64_t now = esp_timer_get_time();
    if (lv_event_get_code(e) == LV_EVENT_RENDER_START) {
        s_frames++;
        s_render_t0 = now;
        s_fps_dropped0 = board_direct_dropped();
    } else if (s_render_t0) {   /* RENDER_READY: rendered and flushed (the flush waits for the panel) */
        const int64_t d = now - s_render_t0;
        s_render_us += d;
        if (d > s_render_max) s_render_max = d;
        s_render_t0 = 0;
        if (board_direct_dropped() != s_fps_dropped0 && s_frames) s_frames--;   /* dropped: never on the panel */
    }
}

static uint32_t direct_frames(void)
{
    disp_direct_stats_t d;
    disp_direct_stats(&d);
    return d.frames;
}

static int cmd_fps(int argc, char **argv)
{
    static bool hooked;
    if (argc == 2 && strcmp(argv[1], "start") == 0) {
        bsp_display_lock(-1);
        if (!hooked) {
            lv_display_add_event_cb(lv_display_get_default(), frame_cb, LV_EVENT_RENDER_START, NULL);
            lv_display_add_event_cb(lv_display_get_default(), frame_cb, LV_EVENT_RENDER_READY, NULL);
        }
        hooked = true;
        s_frames = 0;
        s_render_us = 0;
        s_render_max = 0;
        s_fps_direct0 = direct_frames();
        s_fps_t0 = esp_timer_get_time();
        bsp_display_unlock();
        printf("FPS started\n");
        return 0;
    }
    int64_t us = esp_timer_get_time() - s_fps_t0;
    const uint32_t lvgl = s_frames, direct = direct_frames() - s_fps_direct0, n = lvgl + direct;
    printf("FPS %lu frames in %lld ms = %.1f/s (lvgl %lu, direct %lu); LVGL render+flush avg %.1f ms max %.1f ms\n",
           (unsigned long)n, us / 1000, us ? n * 1e6 / us : 0.0, (unsigned long)lvgl, (unsigned long)direct,
           lvgl ? s_render_us / 1000.0 / lvgl : 0.0, s_render_max / 1000.0);
    return 0;
}
#endif

/* state: power state, battery, USB power, charging, pose, settings, net and sync, stacks and internal RAM, failure
 * counters and the last 5 events. */
static int cmd_state(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    static const char *const POSE[] = {"UNKNOWN", "HANGING", "LOOKING", "FACE_DOWN"};
    omni_pose_t pose = board_pose();
    printf("power %s\nbrightness %d%%\nbattery %d%% %d mV\nvbus %s%s\ncharging %s\npose %s\n",
           pwr_state_name(power_state()), bsp_display_brightness_get(), board_battery_percent(), board_vbat_mv(),
           board_vbus_present() ? "yes" : "no",
           board_vbus_assumed() ? " (reads failing: battery assumed for light sleep)" : "", board_charging() ? "yes" : "no",
           (unsigned)pose < sizeof POSE / sizeof POSE[0] ? POSE[pose] : "?");
    const omni_city_t *c = omni_city_at(settings()->city);
    int64_t now = (int64_t)time(NULL);
    char hhmm[6], ampm[3];
    omni_fmt_clock(now, settings()->h24, hhmm, ampm);
    {
        omni_batrec_t br = app_batrec();
        printf("wakes lift %lu touch %lu glance %lu; battery session %s (see batsession)\n", (unsigned long)br.lift,
               (unsigned long)br.touch, (unsigned long)br.glance,
               br.on_battery ? "running" : br.start.valid ? "recorded" : "none");
    }
#if CONFIG_OMNI_DEBUG_CONSOLE
    {   /* m13: a hold left set (a lost console) keeps the watch awake on battery until it lapses (R66) */
        uint32_t held_ms;
        if (app_awake_hold(&held_ms)) {
            const int32_t left_s = ((int32_t)OMNI_AWAKE_HOLD_MAX_MS - (int32_t)held_ms) / 1000;
            printf("awake hold: ON since %lu s, expires in %ld s\n", (unsigned long)(held_ms / 1000),
                   (long)(left_s > 0 ? left_s : 0));
        } else {
            printf("awake hold: off\n");
        }
    }
#endif
    printf("city %s\nlocal %s%s%s%s (unix %lld)\n", c ? c->name : "?", hhmm, ampm[0] ? " " : "", ampm,
           omni_time_is_set(now) ? "" : " (clock unset)", (long long)now);
    const omni_settings_t *st = settings();
    char origin[64], migration[64];
    store_status(origin, sizeof origin, migration, sizeof migration);   /* a snapshot: a force may rewrite them */
    printf("settings: brightness %u, sleep %u s, %s, sound %s vol %u, reveal %u, reduced motion %s (%s)\n",
           st->brightness, st->sleep_s, st->h24 ? "24 h" : "12 h", st->sound_on ? "on" : "off", st->volume,
           st->reveal_style, st->reduced_motion ? "on" : "off", origin);
    {   /* as the boot log's line; `none` when watch/theme was absent (preflight A8) */
        const watch_theme_t *t = watch_theme();
        const char *prev = store_prev_theme();
        printf("theme %s (was %s)\nsettings %s\nmigration %s\n", t ? t->id : "?", prev[0] ? prev : "none",
               origin, migration);
    }
    omni_sync_t sy = app_sync();
    char ssid[33], age[8] = "never", next[8];
    net_ssid(ssid);
    if (sy.last_ok) omni_fmt_age(now - sy.last_ok, age);
    omni_fmt_age(sy.next_try - now, next);
    printf("net %s%s%s ssid \"%s\" %s\nsync last %s, failures %u, next %s%s\n", net_state_name(net_state()),
           net_busy() ? " busy" : "", net_provisioning() ? " provisioning" : "", ssid,
           net_is_provisioned() ? "provisioned" : "not provisioned", age, sy.failures, next,
           omni_sync_stale(&sy, now) ? " (stale)" : "");
#if CONFIG_OMNI_DEBUG_CONSOLE
    printf("forget lock %s\n", net_forget_locked() ? "on" : "off");
#endif
    printf("stack free: events %u, console %u, net %u bytes\nheap free: internal %u (min %u since boot), psram %u\n",
           app_events_stack_hwm(), (unsigned)uxTaskGetStackHighWaterMark(NULL), net_stack_hwm(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    board_heap_sample();
    board_heap_t hp = board_heap();
    printf("dma block: largest internal %u (min %u since boot); alloc failures %u (last %u B caps 0x%lx)\n",
           (unsigned)hp.dma_largest, (unsigned)hp.dma_largest_min, hp.alloc_failures, (unsigned)hp.last_fail_size,
           (unsigned long)hp.last_fail_caps);
    printf("imu read failures %u; unstaged display flushes %u; panel commands deferred %u\n", board_imu_read_failures(),
           board_unstaged_flushes(), power_panel_deferrals());
    omni_event_t ev[5];
    int n = omni_events_recent(ev, 5);
    printf("events %d\n", n);
    char buf[64];
    for (int i = 0; i < n; i++) printf("  %lu %s\n", (unsigned long)ev[i].t_ms, omni_event_format(&ev[i], buf, sizeof buf));
    return 0;
}

/* batsession: the battery session record (unplug .. replug), for the half-day wear test. Run after replug. */
static int cmd_batsession(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    omni_batrec_t r = app_batrec();
    omni_bsession_report_t o;
    omni_batrec_report(&r, &o);
    printf("wake totals: lift %lu, touch/button %lu, glance %lu\n", (unsigned long)r.lift, (unsigned long)r.touch,
           (unsigned long)r.glance);
    if (!o.have) {
        printf("batsession: none yet (unplug starts one)\n");
        return 0;
    }
    char ts[24];
    struct tm tm;
    time_t t0 = (time_t)r.start.unix_s;
    if (t0 != 0 && localtime_r(&t0, &tm) != NULL) strftime(ts, sizeof ts, "%Y-%m-%d %H:%M", &tm);
    else snprintf(ts, sizeof ts, "clock unset");
    printf("batsession: %s, started %s\n", o.in_progress ? "IN PROGRESS (to latest checkpoint)" : o.partial ? "ended (from checkpoint)" : "complete", ts);
    printf("  start %d%% %d mV   end %d%% %d mV   drop %d%%\n", o.start_pct, o.start_mv, o.end_pct, o.end_mv,
           o.start_pct - o.end_pct);
    printf("  duration %lld s = %.2f h\n", (long long)o.duration_s, (double)o.hours);
    printf("  wakes %lu (lift %lu, touch/button %lu), glances %lu\n", (unsigned long)o.wakes, (unsigned long)o.lift,
           (unsigned long)o.touch, (unsigned long)o.glance);
    if (o.hours_per_100_wakes >= 0) printf("  hours per 100 wakes %.2f\n", (double)o.hours_per_100_wakes);
    else printf("  hours per 100 wakes n/a\n");
    if (o.pct_per_hour >= 0) printf("  drain %.2f %%/h -> %.1f h per full charge\n", (double)o.pct_per_hour,
                                    o.pct_per_hour > 0 ? (double)(100.0f / o.pct_per_hour) : 0.0);
    return 0;
}

static void post_ui(int code)
{
    omni_event_t e = {.kind = EV_UI, .t_ms = now_ms(), .ui = code};
    omni_events_post(&e);
}

/* net: provisioning / sync status and the QR payload (its PoP redacted, R151); net sync | prov | forget; net fake <state> [prov] [forget]
 * puts the screens into a state without a phone or a real failure (e.g. `net fake err_password prov`; forget =
 * the second refusal in a row of a background sync). */
static int cmd_net(int argc, char **argv)
{
    if (argc == 1) {
        char ssid[33];
        net_ssid(ssid);
        printf("NET %s name %s ssid \"%s\" %s\n", net_state_name(net_state()), net_prov_name(), ssid,
               net_is_provisioned() ? "provisioned" : "not provisioned");
#if CONFIG_OMNI_DEBUG_CONSOLE
        char qr[128], shown[128];
        net_qr_json(qr, sizeof qr);
        omni_prov_qr_redact(qr, shown, sizeof shown);   /* R151: the PoP is never printed */
        printf("QR %s\n", shown);
#endif
        return 0;
    }
    if (strcmp(argv[1], "sync") == 0) post_ui(UI_SYNC_NOW);
    else if (strcmp(argv[1], "prov") == 0) post_ui(UI_RESCAN);
#if CONFIG_OMNI_DEBUG_CONSOLE
    else if (strcmp(argv[1], "forget") == 0) net_forget();
    else if (strcmp(argv[1], "forget-lock") == 0 && argc >= 3 &&
             (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        net_forget_lock(strcmp(argv[2], "on") == 0);   /* C2: RAM only; a reboot clears it */
        printf("NET FORGET-LOCK %s\n", net_forget_locked() ? "on" : "off");
        return 0;
    }
    else if (strcmp(argv[1], "boot-unprov") == 0) {
        /* written by the event loop (R27), which prints NET BOOT-UNPROV <result>; never on a read-only boot */
        if (store_read_only()) {
            printf("NET BOOT-UNPROV refused: read-only boot\n");
            return 1;
        }
        omni_event_t e = {.kind = EV_UI, .t_ms = now_ms(), .ui = UI_NET_BOOT_UNPROV};
        if (!omni_events_post(&e)) {
            printf("NET BOOT-UNPROV ERR queue full\n");
            return 1;
        }
        return 0;
    }
    else if (strcmp(argv[1], "fake") == 0 && argc >= 3) {
        for (int st = NET_IDLE; st <= NET_ERR_NO_MEM; st++) {
            if (strcasecmp(argv[2], net_state_name((omni_net_state_t)st)) == 0) {
                int flags = 0;
                for (int i = 3; i < argc; i++) {
                    if (strcmp(argv[i], "prov") == 0) flags |= NET_EV_PROV;
                    else if (strcmp(argv[i], "forget") == 0) flags |= NET_EV_FORGET;
                }
                net_debug_force((omni_net_state_t)st, flags);
                printf("NET FAKE %s\n", net_state_name((omni_net_state_t)st));
                return 0;
            }
        }
        printf("NET ERR unknown state '%s'\n", argv[2]);
        return 1;
    }
#endif
    else {
        printf("usage: net | net sync | net prov | net forget | net forget-lock on|off | net fake <state> [prov] [forget] | "
               "net boot-unprov\n");
        return 1;
    }
    printf("NET %s\n", argv[1]);
    return 0;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* netlist (R101): a scan (net_scan: refused while a sync, a scan or a phone session runs), then its rows as the list
 * builds them: the name with every byte outside 0x20..0x7E (and the backslash and the quote) as \xNN, RSSI, the class,
 * the raw auth mode, the channel and the saved mark. The records' copies are in PSRAM (not on this stack). */
static int cmd_netlist(int argc, char **argv)
{
    (void)argc, (void)argv;
    static const char *const CLASS[] = {"open", "psk", "sae", "unsupported"};
    if (!net_scan()) {
        printf("NETLIST busy (a sync, a scan or a phone session)\n");
        return 1;
    }
    const uint32_t t0 = now_ms();
    do vTaskDelay(pdMS_TO_TICKS(100));
    while ((net_busy() || net_state() == NET_SCANNING) && now_ms() - t0 < 10000);   /* busy drops just before IDLE */
    if (net_busy() || net_state() != NET_IDLE) {
        printf("NETLIST ERR %s\n", net_busy() ? "no result in 10 s" : net_state_name(net_state()));
        return 1;
    }
    omni_scan_rec_t *recs = heap_caps_malloc(20 * sizeof *recs, MALLOC_CAP_SPIRAM);
    omni_net_row_t *rows = heap_caps_malloc(OMNI_NETLIST_MAX * sizeof *rows, MALLOC_CAP_SPIRAM);
    if (recs == NULL || rows == NULL) {
        free(recs);
        free(rows);
        printf("NETLIST ERR no memory\n");
        return 1;
    }
    char saved[33] = "";
    if (net_is_provisioned()) net_ssid(saved);
    const int nrec = net_scan_results(recs, 20);
    const int n = omni_netlist_build(recs, nrec, (const uint8_t *)saved, (uint8_t)strlen(saved), rows);
    printf("NETLIST %d rows from %d records\n", n, nrec);
    for (int i = 0; i < n; i++) {
        const omni_net_row_t *r = &rows[i];
        printf("%2d \"", i);
        for (int k = 0; k < r->rec.ssid_len; k++) {
            const uint8_t b = r->rec.ssid[k];
            if (b < 0x20 || b > 0x7e || b == '\\' || b == '"') printf("\\x%02x", b);
            else putchar(b);
        }
        printf("\" %d dBm %s (auth %u) ch %u%s\n", r->rec.rssi, r->auth < 4 ? CLASS[r->auth] : "?", r->rec.authmode,
               r->rec.channel, r->saved ? " saved" : "");
    }
    printf("NETLIST END\n");
    free(recs);
    free(rows);
    return 0;
}
#endif

#if CONFIG_OMNI_DEBUG_CONSOLE
/* kb password|ssid|city (R101): the keyboard opened in that mode (password: for the saved network, as WPA2 PSK). There
 * is no typing from the console: the device sessions type with `inject` gestures. */
static int cmd_kb(int argc, char **argv)
{
    static const char *const MODE[] = {"password", "ssid", "city"};
    int m = 0;
    while (argc == 2 && m < 3 && strcmp(argv[1], MODE[m]) != 0) m++;
    if (argc != 2 || m == 3) {
        printf("usage: kb password|ssid|city\n");
        return 1;
    }
    scr_kb_open_t o = {.mode = (uint8_t)m};
    if (m == KB_MODE_PASSWORD) {
        char name[33] = "";
        if (net_is_provisioned()) net_ssid(name);
        o.ssid_len = (uint8_t)strlen(name);
        memcpy(o.ssid, name, o.ssid_len);
        o.authmode = OMNI_WA_WPA2_PSK;
    }
    bsp_display_lock(-1);
    const bool ok = scr_keyboard_ready();
    if (ok) scr_keyboard_prepare(&o);
    bsp_display_unlock();
    if (!ok) {
        printf("KB refused: its assets did not load\n");
        return 1;
    }
    post_ui(UI_OPEN_KEYBOARD);
    printf("KB %s\n", MODE[m]);
    return 0;
}
#endif

/* city [name]: show or set the home city. Names may contain spaces. The change goes to the event loop like the
 * settings screen's (R27: it is the settings' only writer), which applies it and saves it 2 s later. */
static int cmd_city(int argc, char **argv)
{
    if (argc == 1) {
        const omni_city_t *c = omni_city_at(settings()->city);
        printf("city %s\n", c ? c->name : "?");
        return 0;
    }
    char name[48] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1) strlcat(name, " ", sizeof name);
        strlcat(name, argv[i], sizeof name);
    }
    int idx = omni_city_find(name);
    if (idx < 0) {
        printf("CITY ERR unknown city '%s'\n", name);
        return 1;
    }
    post_ui(UI_EV(UI_SET_CITY, idx));
    printf("CITY %s, %s (applied by the event loop, saved 2 s later)\n", omni_city_at((size_t)idx)->name,
           omni_country_name(omni_city_at((size_t)idx)->country));
    return 0;
}

/* clock: the timekeeping internals. The wall clock is boot time + esp_timer (libc on the high-resolution timer
 * only, sdkconfig.defaults). The RTC counter runs on the RC slow clock: "rtc-hrt" shows how far it drifted from
 * esp_timer, and "rc" compares the slow-clock calibration in use with a fresh one. clock log: the calibration. */static int64_t wall_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* clock log: the clock calibration and the last SNTP corrections (persisted: they survive replugs and reboots). */
static void clock_log(void)
{
    omni_clockcal_t c = app_clockcal();
    printf("CLOCKCAL rate %ld ppm per asleep s (%u estimates), sync interval %lu s, asleep %llu s of %lld s up\n",
           (long)c.saved.rate_ppm, c.saved.n_est, (unsigned long)c.saved.interval_s,
           (unsigned long long)(power_asleep_us() / 1000000), (long long)(esp_timer_get_time() / 1000000));
    omni_clk_entry_t e[OMNI_CLK_LOG_N];
    int n = omni_clk_log(&c, e, OMNI_CLK_LOG_N);
    printf("CLOCKLOG %d (local time, step = after - before, + = watch was behind)\n", n);
    for (int i = 0; i < n; i++) {
        char ts[24] = "clock unset";
        struct tm tm;
        time_t t = (time_t)e[i].unix_s;
        if (localtime_r(&t, &tm) != NULL) strftime(ts, sizeof ts, "%m-%d %H:%M:%S", &tm);
        char step[24];
        if (e[i].step_ms == INT32_MAX || e[i].step_ms == INT32_MIN) snprintf(step, sizeof step, "set (was unset)");
        else snprintf(step, sizeof step, "%+ld ms", (long)e[i].step_ms);
        printf("  %s step %s applied %+ld ms, up %lu s, since %lu s (asleep %lu s), rate %ld ppm\n", ts, step,
               (long)e[i].applied_ms, (unsigned long)e[i].uptime_s, (unsigned long)e[i].since_s,
               (unsigned long)e[i].asleep_s, (long)e[i].rate_ppm);
    }
}

static int cmd_clock(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "log") == 0) {
        clock_log();
        return 0;
    }
    int64_t hrt = esp_timer_get_time(), rtc = (int64_t)esp_rtc_get_time_us(), wall = wall_us();
    uint32_t cal_used = esp_clk_slowclk_cal_get();
    uint32_t cal_now = rtc_clk_cal(RTC_CAL_RTC_MUX, CONFIG_RTC_CLK_CAL_CYCLES);
    double rc_ppm = cal_now ? ((double)cal_used / (double)cal_now - 1.0) * 1e6 : 0.0;
    printf("CLOCK wall %lld.%06lld hrt %lld rtc %lld (rtc-hrt %+lld us)\n", (long long)(wall / 1000000),
           (long long)(wall % 1000000), (long long)hrt, (long long)rtc, (long long)(rtc - hrt));
    printf("CLOCK rc cal used %lu now %lu (%+.0f ppm: rtc time runs %s)\n", (unsigned long)cal_used,
           (unsigned long)cal_now, rc_ppm, rc_ppm >= 0 ? "fast" : "slow");
    printf("CLOCK libc HRT only: wall = boot time + esp_timer (no RTC step)\n");
    return 0;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* settime <unix>: set the system clock (UTC seconds) until NTP sync lands; the face follows within 0.1 s. The event
 * loop sets it (it owns the clock: corrections, and the calibration reference this drops). */
static int cmd_settime(int argc, char **argv)
{
    char *end = NULL;
    long long v = argc == 2 ? strtoll(argv[1], &end, 10) : -1;
    if (argc != 2 || end == argv[1] || *end != '\0' || v < 0) {
        printf("usage: settime <unix seconds>\n");
        return 1;
    }
    omni_event_t e = {.kind = EV_SETTIME, .t_ms = now_ms(), .unix_s = (int64_t)v};
    if (!omni_events_post(&e)) {
        printf("SETTIME ERR queue full\n");
        return 1;
    }
    char hhmm[6], ampm[3];
    omni_fmt_clock((int64_t)v, settings()->h24, hhmm, ampm);
    printf("SETTIME %lld local %s%s%s\n", v, hhmm, ampm[0] ? " " : "", ampm);
    return 0;
}

/* nvs stats: NVS entries used/free/total and the bytes of the three namespaces (entries x 32, the only per-namespace
 * measure; row 17's bar is on the theme namespace). nvs migrate force: posts UI_NVS_MIGRATE_FORCE; the event loop runs
 * it (R27) and prints the result. Nothing here erases NVS. */
static int cmd_nvs(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "stats") == 0) {
        store_nvs_stats_t st;
        esp_err_t err = store_nvs_stats(&st);
        if (err != ESP_OK) {
            printf("NVS STATS ERR %s\n", esp_err_to_name(err));
            return 1;
        }
        const watch_theme_t *t = watch_theme();
        printf("NVS entries used %u free %u total %u\n", (unsigned)st.used_entries, (unsigned)st.free_entries,
               (unsigned)st.total_entries);
        printf("NVS bytes (entries x 32): watch %u, t_%s %u, omni %u\n", (unsigned)st.watch_b, t ? t->id : "?",
               (unsigned)st.theme_b, (unsigned)st.old_b);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "migrate") == 0 && strcmp(argv[2], "force") == 0) {
        omni_event_t e = {.kind = EV_UI, .t_ms = now_ms(), .ui = UI_NVS_MIGRATE_FORCE};
        if (!omni_events_post(&e)) {
            printf("NVS MIGRATE FORCE ERR queue full\n");
            return 1;
        }
        return 0;   /* the event loop prints NVS MIGRATE FORCE <result> */
    }
    printf("usage: nvs stats | nvs migrate force\n");
    return 1;
}
#endif

/* reboot: esp_restart. */
static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("REBOOT\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
/* trace on|off: stream "T,<ms>,<ax>,<ay>,<az>" accelerometer lines at 25 Hz.
 * trace rec <s> [delay_s]: record s seconds into RAM (untethered); trace dump: print the recording. */
static int cmd_trace(int argc, char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "off") == 0)) {
        board_trace(strcmp(argv[1], "on") == 0);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "dump") == 0) {
        board_trace_dump();
        return 0;
    }
    if ((argc == 3 || argc == 4) && strcmp(argv[1], "rec") == 0) {
        unsigned s = (unsigned)atoi(argv[2]), d = argc == 4 ? (unsigned)atoi(argv[3]) : 0;
        esp_err_t err = board_trace_rec(s, d);
        if (err == ESP_OK) printf("TRACE REC %u s after %u s\n", s, d);
        else printf("TRACE REC ERR %s (1..%d s, not while recording)\n", esp_err_to_name(err), BOARD_TRACE_REC_MAX_S);
        return err == ESP_OK ? 0 : 1;
    }
    printf("usage: trace on|off | trace rec <seconds> [delay_s] | trace dump\n");
    return 1;
}

/* inject lift|pwr|touch|boot [double|long] [delay_s]: post the event the hardware would (touch = G_TAP at the
 * centre, boot = BOOT short, or the double / long press), now or after delay_s (e.g. to wake the board from a test sleep after the console went quiet).
 * inject open picker|quick|settings|netlist|faces: push that screen.
 * inject gesture <kind> [x y [x0 y0]]: post that classified gesture as is (no claim asked).
 * inject drag <x0> <y0> <x1> <y1> <ms>: a synthetic finger pressed at x0,y0, moved in a straight line to x1,y1
 * over ms and lifted, fed to the input task's classifier (claims, drags, swipes, long press: same path as the
 * touch controller). The same point twice holds still (ms >= 600 = long press).
 * inject arc <r> <a0> <a1> <ms>: the same along the circle of radius r round the centre, from angle a0 to a1
 * (degrees, 0 = 3 o'clock, clockwise) - turning the bezel / rim. */
static void inject_post(void *arg)
{
    omni_event_t e = *(omni_event_t *)arg;
    free(arg);
    e.t_ms = now_ms();
    omni_events_post(&e);
}

static int gesture_kind(const char *s)
{
    static const char *const K[] = {"none", "tap", "long", "swipe_l", "swipe_r", "swipe_up", "swipe_down",
                                    "drag_start", "drag_move", "drag_end"};
    for (int i = 1; i < (int)(sizeof K / sizeof K[0]); i++)
        if (strcmp(s, K[i]) == 0) return i;
    return -1;
}

#define FINGER_MS 16 /* one input period */

/* Synthetic finger: points along the path every input period, a short still press first and after. */
static void finger(float r_or_x0, float a0_or_y0, float x1_or_a1, float y1, unsigned ms, bool arc, float arc_a1)
{
    unsigned steps = ms / FINGER_MS ? ms / FINGER_MS : 1;
    for (unsigned i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps, x, y;
        if (arc) {
            float a = (a0_or_y0 + (arc_a1 - a0_or_y0) * t) * 3.14159265f / 180;
            x = 233 + cosf(a) * r_or_x0;
            y = 233 + sinf(a) * r_or_x0;
        } else {
            x = r_or_x0 + (x1_or_a1 - r_or_x0) * t;
            y = a0_or_y0 + (y1 - a0_or_y0) * t;
        }
        board_debug_touch(true, (int16_t)lroundf(x), (int16_t)lroundf(y));
        vTaskDelay(pdMS_TO_TICKS(FINGER_MS));
    }
    vTaskDelay(pdMS_TO_TICKS(2 * FINGER_MS));
    board_debug_touch(false, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(3 * FINGER_MS));
}

static int cmd_inject(int argc, char **argv)
{
    omni_event_t e = {0};
    if (argc >= 2 && strcmp(argv[1], "drag") == 0) {
        if (argc != 7) {
            printf("usage: inject drag <x0> <y0> <x1> <y1> <ms>\n");
            return 1;
        }
        finger(atof(argv[2]), atof(argv[3]), atof(argv[4]), atof(argv[5]), (unsigned)atoi(argv[6]), false, 0);
        printf("INJECT drag done\n");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "arc") == 0) {
        if (argc != 6) {
            printf("usage: inject arc <r> <a0_deg> <a1_deg> <ms>\n");
            return 1;
        }
        finger(atof(argv[2]), atof(argv[3]), 0, 0, (unsigned)atoi(argv[5]), true, atof(argv[4]));
        printf("INJECT arc done\n");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "open") == 0) {
        if (strcmp(argv[2], "picker") == 0) scr_picker_open();
        else if (strcmp(argv[2], "quick") == 0) scr_quick_open();
        else if (strcmp(argv[2], "settings") == 0) scr_settings_open();
        else if (strcmp(argv[2], "netlist") == 0) post_ui(UI_OPEN_NETLIST);
        else if (strcmp(argv[2], "faces") == 0) post_ui(UI_OPEN_FACES);
        else {
            printf("usage: inject open picker|quick|settings|netlist|faces\n");
            return 1;
        }
        printf("INJECT open %s\n", argv[2]);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "gesture") == 0) {
        int k = argc >= 3 ? gesture_kind(argv[2]) : -1;
        if (k < 0 || (argc != 3 && argc != 5 && argc != 7)) {
            printf("usage: inject gesture tap|long|swipe_l|swipe_r|swipe_up|swipe_down|drag_start|drag_move|drag_end"
                   " [x y [x0 y0]]\n");
            return 1;
        }
        e.kind = EV_GESTURE;
        e.g.kind = (omni_gesture_kind_t)k;
        e.g.x = e.g.x0 = 233;
        e.g.y = e.g.y0 = 233;
        if (argc >= 5) e.g.x = e.g.x0 = (int16_t)atoi(argv[3]), e.g.y = e.g.y0 = (int16_t)atoi(argv[4]);
        if (argc == 7) e.g.x0 = (int16_t)atoi(argv[5]), e.g.y0 = (int16_t)atoi(argv[6]);
        int dx = e.g.x0 - 233, dy = e.g.y0 - 233;
        e.g.edge_start = dx * dx + dy * dy >= 193 * 193;
        e.t_ms = now_ms();
        omni_events_post(&e);
        printf("INJECT gesture %s %d,%d from %d,%d\n", argv[2], e.g.x, e.g.y, e.g.x0, e.g.y0);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "lift") == 0) e.kind = EV_LIFT;
    else if (argc >= 2 && strcmp(argv[1], "pwr") == 0) e.kind = EV_PWR;
    else if (argc >= 2 && strcmp(argv[1], "touch") == 0) {
        e.kind = EV_GESTURE;
        e.g = (omni_gesture_t){.kind = G_TAP, .x = 233, .y = 233};
    } else if (argc >= 2 && strcmp(argv[1], "boot") == 0) {
        e.kind = EV_BOOT;
        e.press = B_SHORT;
        if (argc >= 3 && (strcmp(argv[2], "double") == 0 || strcmp(argv[2], "long") == 0)) { /* R48/C9, D11 */
            e.press = argv[2][0] == 'd' ? B_DOUBLE : B_LONG;
            argv++; /* [delay_s] follows the press */
            argc--;
        }
    } else {
        printf("usage: inject lift|pwr|touch|boot [double|long] [delay_s] | inject open picker|quick|settings|netlist|faces |"
               " inject gesture <kind> [x y [x0 y0]] |"
               " inject drag <x0> <y0> <x1> <y1> <ms> | inject arc <r> <a0> <a1> <ms>\n");
        return 1;
    }
    unsigned delay_s = argc >= 3 ? (unsigned)atoi(argv[2]) : 0;
    omni_event_t *p = malloc(sizeof *p);
    if (p == NULL) return 1;
    *p = e;
    if (delay_s == 0) {
        inject_post(p);
    } else {
        const esp_timer_create_args_t args = {.callback = inject_post, .arg = p, .name = "inject"};
        esp_timer_handle_t t;
        if (esp_timer_create(&args, &t) != ESP_OK || esp_timer_start_once(t, (uint64_t)delay_s * 1000000) != ESP_OK) {
            free(p);
            return 1;
        }
        /* one-shot timers are not deleted: debug only, a few bytes each */
    }
    printf("INJECT %s%s in %u s\n", e.kind == EV_BOOT && e.press != B_SHORT ? "boot " : "", argv[1], delay_s);
    return 0;
}

/* face <key>: show that face (UI_SHOW_FACE with its id): a carousel face is picked and saved exactly as UI_PICK does
 * (tools/shot_set.py relies on that); any other face is shown transiently, nothing saved. The keys are the theme's
 * (watch_theme()->faces[].key: the Omni's omni, live, analog, digital, spin). */
static void face_usage(void)
{
    const watch_theme_t *t = watch_theme();
    printf("usage: face");
    const char *sep = " ";
    for (uint8_t f = 0; f < t->face_count; f++) {
        if (t->faces[f].key == NULL) continue;
        printf("%s%s", sep, t->faces[f].key);
        sep = "|";
    }
    printf("\n");
}

static int cmd_face(int argc, char **argv)
{
    const watch_theme_t *t = watch_theme();
    for (uint8_t f = 0; argc == 2 && f < t->face_count; f++) {
        if (t->faces[f].key == NULL || strcmp(argv[1], t->faces[f].key) != 0) continue;
        const omni_settings_t *st = settings();
        uint8_t k = 0;
        while (k < st->carousel_len && st->carousel[k] != f) k++;
        post_ui(UI_EV(UI_SHOW_FACE, f));
        if (k < st->carousel_len) printf("FACE %s (carousel %u)\n", t->faces[f].key, k);
        else printf("FACE not in carousel (%s shown, not saved)\n", t->faces[f].key);   /* shot_set.py keys on the prefix */
        return 0;
    }
    face_usage();
    return 1;
}

/* carousel (R101): the swipe order's ids, its length and the saved face. carousel set <id...>: that order posted through
 * the editor's mailbox (ui_carousel_post: the event loop validates and saves it, as an edit), for the benches; the saved
 * face stays current when it is in the new order. A session's close restores the recorded order the same way. */
static int cmd_carousel(int argc, char **argv)
{
    const omni_settings_t *st = settings();
    const uint8_t fi = store_face();
    omni_carousel_t c = {.len = st->carousel_len, .face = st->carousel[fi < st->carousel_len ? fi : 0]};
    memcpy(c.ids, st->carousel, sizeof c.ids);
    if (argc >= 2 && strcmp(argv[1], "set") == 0) {
        if (argc < 3 || argc - 2 > OMNI_CAROUSEL_MAX) {
            printf("usage: carousel set <id>...\n");
            return 1;
        }
        memset(c.ids, 0, sizeof c.ids);
        c.len = (uint8_t)(argc - 2);
        for (int i = 0; i < c.len; i++) c.ids[i] = (uint8_t)atoi(argv[i + 2]);
        printf("CAROUSEL set %s\n", ui_carousel_post(&c) ? "posted" : "dropped (queue full)");
        return 0;
    }
    printf("CAROUSEL len %u face %u ids", c.len, c.face);
    for (int i = 0; i < c.len; i++) printf(" %u", c.ids[i]);
    printf("\n");
    return 0;
}

/* awake hold|release: no glance/sleep timeout while held (R48/C13), until release, a reboot or 30 min (R66). */
static int cmd_awake(int argc, char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "hold") == 0 || strcmp(argv[1], "release") == 0)) {
        bool hold = argv[1][0] == 'h';
        app_hold_awake(hold);
        printf("AWAKE %s\n", hold ? "held" : "released");
        return 0;
    }
    printf("usage: awake hold|release\n");
    return 1;
}

/* setting reveal flip|iris|none | setting motion on|off: what Settings -> Reveal and Reduced motion set (Task 11's device
 * checks), posted to the event loop as the settings screen posts them (R27: it is the settings' only writer). */
static int cmd_setting(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "reveal") == 0) {
        static const char *const N[] = {"flip", "iris", "none"};   /* enum omni_reveal */
        for (int i = 0; i < 3; i++)
            if (strcmp(argv[2], N[i]) == 0) {
                post_ui(UI_EV(UI_SET_REVEAL, i));
                printf("SETTING reveal %s (%d)\n", N[i], i);
                return 0;
            }
    }
    if (argc == 3 && strcmp(argv[1], "motion") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        const bool on = strcmp(argv[2], "on") == 0;
        post_ui(UI_EV(UI_SET_MOTION, on));
        printf("SETTING reduced motion %s\n", on ? "on" : "off");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "sound") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        const bool on = strcmp(argv[2], "on") == 0;
        post_ui(UI_EV(UI_SET_SOUND, on));
        printf("SETTING sound %s\n", on ? "on" : "off");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "volume") == 0) {
        char *end;
        const long v = strtol(argv[2], &end, 10);
        if (*end == '\0' && v >= 0 && v <= 100) {
            post_ui(UI_EV(UI_SET_VOLUME, (int)v));
            printf("SETTING volume %ld\n", v);
            return 0;
        }
    }
    printf("usage: setting reveal flip|iris|none | setting motion on|off | setting sound on|off | setting volume 0..100\n");
    return 1;
}

/* sfx play <name>|stop|stat (Task 13): play an asset ("surge" = sfx_surge.pcm, or a file name), stop, or the player's
 * state: what is created, the codec open (I2S enabled), the amp (GPIO 46) and the PM lock, the counters, and the last
 * effect's end: its last write, then the amp low and the PM lock released, and the 2 s idle close (ms after that write). */
static int cmd_sfx(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "play") == 0) {
        char name[32];
        if (strchr(argv[2], '.')) snprintf(name, sizeof name, "%s", argv[2]);
        else snprintf(name, sizeof name, "sfx_%s.pcm", argv[2]);
        const bool ok = sfx_play_asset(name);
        printf("SFX play %s: %s\n", name, ok ? "true" : "false");
        return ok ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        sfx_stop();
        printf("SFX stop\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "stat") == 0) {
        sfx_stat_t st;
        sfx_debug_stat(&st);
        const int64_t now = esp_timer_get_time(), w = st.last_write_us;
        const double rel = 1e-3;
        printf("SFX sound %s vol %u%s; created: task %s queue %s pm %s channel %s codec %s; open (I2S enabled) %s; "
               "playing %s, PA %d, PM lock held %s\n",
               st.on ? "on" : "off", st.volume, st.blocked ? ", BLOCKED" : "", st.task ? "yes" : "no",
               st.queue ? "yes" : "no", st.pm_lock ? "yes" : "no", st.channel ? "yes" : "no", st.codec ? "yes" : "no",
               st.open ? "yes" : "no", st.playing ? "yes" : "no", st.pa, st.pm_held ? "yes" : "no");
        printf("SFX plays %lu cuts %lu stops %lu dropped %lu rejected %lu failures %lu; internal RAM task+queue+lock %ld B, "
               "channel+codec %ld B; stack free %u\n",
               (unsigned long)st.plays, (unsigned long)st.cuts, (unsigned long)st.stops, (unsigned long)st.dropped,
               (unsigned long)st.rejected, (unsigned long)st.failures, (long)st.ram_task_b, (long)st.ram_codec_b,
               st.stack_free_b);
        if (w)
            printf("SFX last effect %lu samples (%.1f ms at 16 kHz) written over %.1f ms; last write %.1f ms ago (took %lu us); "
                   "after it: PA low %+.1f ms, PM released %+.1f ms, idle close %+.1f ms\n",
                   (unsigned long)st.effect_samples, st.effect_samples / 16.0, st.effect_write_us * rel, (now - w) * rel,
                   (unsigned long)st.last_write_dur_us, st.pa_low_us > w ? (st.pa_low_us - w) * rel : NAN,
                   st.pm_release_us > w ? (st.pm_release_us - w) * rel : NAN,
                   st.close_us > w ? (st.close_us - w) * rel : NAN);   /* nan: not closed since */
        return 0;
    }
    /* sfx race stop|play (T13 I1): a surge plays, provisioning starts (the event loop sets the block and posts its
     * RELEASE), and at once a STOP (scenario A: it coalesces the RELEASE away) or a PLAY that skips the block check
     * (scenario B: a play that raced the block). `sfx stat` after it must show no channel and no codec. */
    if (argc == 3 && strcmp(argv[1], "race") == 0 && (strcmp(argv[2], "stop") == 0 || strcmp(argv[2], "play") == 0)) {
        const bool play = argv[2][0] == 'p';
        sfx_play_asset("sfx_surge.pcm");
        vTaskDelay(pdMS_TO_TICKS(300));
        post_ui(UI_RESCAN);
        vTaskDelay(1);   /* the event loop (priority 3) starts provisioning and blocks */
        sfx_debug_post(play, "sfx_whoosh.pcm");
        printf("SFX race %s posted\n", argv[2]);
        return 0;
    }
    printf("usage: sfx play <name>|stop|stat|race stop|play\n");
    return 1;
}


/* theme dump <0|1|2>: a brass background's pixels (R112: what the screens show, for capturing the assets), as
 * "BG <n> 466 466", 466 lines of base64 RGB565 (little-endian) rows and "BG END <crc32 of the pixels>". bg_get runs
 * under the display lock (B7); the rows are printed after it, from pixels that are never freed. */
static int cmd_theme(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "dump") != 0 || argv[2][0] < '0' || argv[2][0] >= '0' + THEME_BG__COUNT ||
        argv[2][1] != '\0') {
        printf("usage: theme dump <0|1|2>\n");
        return 1;
    }
    const int n = argv[2][0] - '0';
    bsp_display_lock(-1);
    const uint8_t *px = theme_bg_pixels((theme_bg_t)n);
    bsp_display_unlock();
    char *line = px ? heap_caps_malloc(SHOT_LINE_LEN, MALLOC_CAP_SPIRAM) : NULL;
    if (line == NULL) {
        printf("BG ERR\n");
        return 1;
    }
    const size_t row = THEME_W * 2;
    printf("BG %d %d %d\n", n, THEME_W, THEME_W);
    for (int y = 0; y < THEME_W; y++) {
        b64_encode(px + (size_t)y * row, row, line);
        printf("%s\n", line);
        vTaskDelay(pdMS_TO_TICKS(2)); /* pace the USB-Serial-JTAG TX FIFO, as shot does */
    }
    printf("BG END %08" PRIx32 "\n", esp_rom_crc32_le(0, px, row * THEME_W));
    heap_caps_free(line);
    return 0;
}

/* disp: the display pipeline's counters since the last `disp` (disp_stats + board.c's flush hook), then reset.
 * disp bench full|box|off, disp bench full band: the synthetic view (disp_stats.h); a start prints its first 5 s
 * window, off the window since then. disp flush early|late: the flush release (board.c). */
#define DISP_WINDOW_MS 5000
static int64_t s_disp_t0;

static void disp_print(const char *mode)
{
    disp_stats_t d;
    board_flush_stats_t f;
    disp_stats_get(&d, true);
    board_flush_stats(&f, true);
    const int64_t now = esp_timer_get_time();
    const float s = s_disp_t0 ? (float)(now - s_disp_t0) / 1e6f : 0;
    s_disp_t0 = now;
    const float refr = d.refr ? (float)d.refr : 1;
    if (mode)
        printf("DISP %s %.1f fps, lv %.1f ms, flush %.1f ms, shown/rendered %.2f\n", mode,
               s > 0 ? d.frames_shown / s : 0.0f, d.lv_render_us_avg / 1000.0f,
               (f.send_us + d.flush_wait_us) / 1000.0f / refr,
               d.frames_rendered ? (float)d.frames_shown / (float)d.frames_rendered : 0.0f);
    printf("DISP %.1f s: refr %u (%.1f/s), lv render avg %.1f max %.1f ms; flushes %u, %u px (%u per refr), flush_cb max "
           "%.1f ms, LVGL flush wait %.1f ms/refr; frames rendered %u shown %u\n",
           s, (unsigned)d.refr, s > 0 ? d.refr / s : 0.0f, d.lv_render_us_avg / 1000.0f, d.lv_render_us_max / 1000.0f,
           (unsigned)d.flushes, (unsigned)d.flush_px, (unsigned)(d.flush_px / refr), d.flush_us_max / 1000.0f,
           d.flush_wait_us / 1000.0f / refr, (unsigned)d.frames_rendered, (unsigned)d.frames_shown);
    printf("DISP hook: copy %.2f ms/refr, send %.2f ms/refr, max %.1f ms; %s release, unstaged %u; internal free %u "
           "(min %u), largest DMA %u\n",
           f.copy_us / 1000.0f / refr, f.send_us / 1000.0f / refr, f.hook_us_max / 1000.0f,
           f.early ? "early" : "late", board_unstaged_flushes(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    disp_direct_stats_t x;
    disp_direct_stats(&x);
    printf("DISP direct %s%s, reserve %u, %s%s: claims %u, refusals OFF %u PROV %u HIDDEN %u OVERLAY %u RAM %u, "
           "display waits %u, back-offs %u (last by %s); frames %u (%.2f ms avg, %.2f max since the claim), "
           "bands %u, lost %u "
           "(claim %u), dropped LVGL flushes %u; orphans %u, drain time-outs %u; held %u B, permanent %d B, sender "
           "stack free %u B\n",
           x.mode == DISP_DIRECT_MODE_AUTO ? "auto" : "off", DISP_DIRECT_BUILT ? "" : " (CONFIG off)",
           (unsigned)x.reserve,
           x.claimed ? (x.active ? "active" : x.releasing ? "releasing" : "claimed, aborted")
                     : x.stuck ? "stuck (bands kept)" : "idle",
           x.shadow ? ", shadow" : "", (unsigned)x.claims, (unsigned)x.refusals[OMNI_DD_OFF],
           (unsigned)x.refusals[OMNI_DD_PROV], (unsigned)x.refusals[OMNI_DD_HIDDEN],
           (unsigned)x.refusals[OMNI_DD_OVERLAY], (unsigned)x.refusals[OMNI_DD_RAM], (unsigned)x.display_waits,
           (unsigned)x.backoffs, disp_direct_why_name(x.backoff_cause), (unsigned)x.frames, x.frame_us_avg / 1000.0f,
           x.frame_us_max / 1000.0f,
           (unsigned)x.bands, (unsigned)x.lost_total, (unsigned)x.lost, (unsigned)board_direct_dropped(),
           (unsigned)x.orphans, (unsigned)x.drain_timeouts, (unsigned)x.held_bytes, (int)x.perm_bytes,
           (unsigned)x.stack_hwm);
}

/* disp direct selftest: D7's back-off on a temporary full-screen object (no band is drawn). A claim right after a
 * release is refused BACKOFF (N1); a still frame clears it; a claim while the last bands drain back is BACKOFF and no
 * RAM refusal (N2); the back-off also ends by itself after DISP_DIRECT_BACKOFF_MS. */
static bool st_step(const char *what, omni_dd_why_t got, omni_dd_why_t want)
{
    printf("DIRECT SELFTEST %-44s %-8s %s\n", what, disp_direct_why_name(got), got == want ? "ok" : "FAIL");
    return got == want;
}

static int disp_direct_selftest(void)
{
    bool ok = true;
    disp_direct_stats_t x;
    bsp_display_lock(-1);
    if (disp_bench_mode() != DISP_BENCH_OFF) {
        bsp_display_unlock();
        printf("DIRECT SELFTEST: stop the bench first (disp bench off)\n");
        return 1;
    }
    lv_obj_t *o = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_size(o, 466, 466);
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    disp_direct_still();
    ok &= st_step("claim", disp_direct_claim(o), OMNI_DD_OK);
    disp_direct_release();
    ok &= st_step("claim right after a plain release", disp_direct_claim(o), OMNI_DD_BACKOFF);
    disp_direct_still();
    ok &= st_step("claim after a still frame", disp_direct_claim(o), OMNI_DD_OK);
    uint16_t *b = disp_direct_band();   /* a render task holding a band: the release drains */
    disp_direct_release();
    disp_direct_stats(&x);
    const uint32_t ram0 = x.refusals[OMNI_DD_RAM];
    disp_direct_still();
    ok &= st_step("claim while the bands drain (still frame too)", disp_direct_claim(o), OMNI_DD_BACKOFF);
    disp_direct_stats(&x);
    ok &= st_step("  ... RAM refusals unchanged", x.refusals[OMNI_DD_RAM] == ram0 ? OMNI_DD_OK : OMNI_DD_RAM,
                  OMNI_DD_OK);
    if (b) disp_direct_put(b, (omni_rect_t){0, 0, 466, 2});   /* back: the sender discards it */
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(150));   /* the orphan timer finishes the release */
    bsp_display_lock(-1);
    disp_direct_stats(&x);
    ok &= st_step("bands freed after the drain", x.held_bytes == 0 && !x.claimed ? OMNI_DD_OK : OMNI_DD_RAM,
                  OMNI_DD_OK);
    ok &= st_step("claim once drained (the still frame ended it)", disp_direct_claim(o), OMNI_DD_OK);
    disp_direct_release();   /* a plain exit, no still frame after it */
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(150));
    bsp_display_lock(-1);
    ok &= st_step("claim 150 ms after a plain exit", disp_direct_claim(o), OMNI_DD_BACKOFF);
    bsp_display_unlock();
    vTaskDelay(pdMS_TO_TICKS(DISP_DIRECT_BACKOFF_MS - 150 + 50));
    bsp_display_lock(-1);
    ok &= st_step("claim after DISP_DIRECT_BACKOFF_MS", disp_direct_claim(o), OMNI_DD_OK);
    disp_direct_release();
    disp_direct_still();
    lv_obj_delete(o);
    bsp_display_unlock();
    printf("DIRECT SELFTEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

/* disp direct auto|off|reserve <bytes>|shadow on|off|selftest (console builds only: C1) */
static int disp_direct_cmd(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[2], "selftest") == 0) return disp_direct_selftest();
    if (argc == 3 && (strcmp(argv[2], "auto") == 0 || strcmp(argv[2], "off") == 0)) {
        disp_direct_set_mode(argv[2][0] == 'a' ? DISP_DIRECT_MODE_AUTO : DISP_DIRECT_MODE_OFF);
        printf("DISP direct %s\n", argv[2]);
        return 0;
    }
    if (argc == 4 && strcmp(argv[2], "reserve") == 0) {
        char *end;
        const unsigned long v = strtoul(argv[3], &end, 10);
        if (*end == '\0') {
            disp_direct_set_reserve((uint32_t)v);
            printf("DISP direct reserve %lu\n", v);
            return 0;
        }
    }
    if (argc == 4 && strcmp(argv[2], "shadow") == 0 && (strcmp(argv[3], "on") == 0 || strcmp(argv[3], "off") == 0)) {
        bsp_display_lock(-1);
        disp_direct_shadow(strcmp(argv[3], "on") == 0);
        const bool on = disp_direct_shadow_frame() != NULL;
        bsp_display_unlock();
        printf("DISP direct shadow %s\n", on ? "on" : "off");
        return 0;
    }
    printf("usage: disp direct auto|off|reserve <bytes>|shadow on|off|selftest\n");
    return 1;
}

static int cmd_disp(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    if (argc == 1) {
        disp_print(NULL);
        return 0;
    }
    if (strcmp(sub, "direct") == 0) return disp_direct_cmd(argc, argv);
    if (strcmp(sub, "bench") == 0 && argc >= 3) {
        const char *v = argc == 4 ? argv[3] : "";
        disp_bench_t m = strcmp(argv[2], "full") == 0  ? (strcmp(v, "band") == 0 ? DISP_BENCH_BAND : DISP_BENCH_FULL)
                         : strcmp(argv[2], "box") == 0 ? DISP_BENCH_BOX
                         : strcmp(argv[2], "off") == 0 ? DISP_BENCH_OFF
                         : strcmp(argv[2], "direct") == 0
                             ? (strcmp(v, "overlay") == 0 ? DISP_BENCH_DIRECT_OVERLAY
                                : strcmp(v, "inval") == 0 ? DISP_BENCH_DIRECT_INVAL
                                : strcmp(v, "stall") == 0 ? DISP_BENCH_DIRECT_STALL
                                                          : DISP_BENCH_DIRECT)
                             : (disp_bench_t)-1;
        if ((int)m < 0 || argc > 4 ||
            (argc == 4 && m != DISP_BENCH_BAND && m != DISP_BENCH_DIRECT_OVERLAY && m != DISP_BENCH_DIRECT_INVAL &&
             m != DISP_BENCH_DIRECT_STALL)) {
            printf("usage: disp bench full|box|off | disp bench full band | disp bench direct [overlay|inval|stall]\n");
            return 1;
        }
        bsp_display_lock(-1);
        const disp_bench_t was = disp_bench_mode();
        if (m == DISP_BENCH_OFF) {
            disp_bench_stop();
            bsp_display_unlock();
            if (was != DISP_BENCH_OFF) disp_print(disp_bench_name(was));
            printf("DISP bench off\n");
            return 0;
        }
        const bool ok = disp_bench_start(m);
        bsp_display_unlock();
        if (!ok) {
            printf("DISP bench %s FAILED\n", disp_bench_name(m));
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(300));   /* past the first frames */
        disp_print(NULL);
        vTaskDelay(pdMS_TO_TICKS(DISP_WINDOW_MS));
        disp_print(disp_bench_name(m));
        return 0;
    }
    if (argc == 3 && strcmp(sub, "flush") == 0 && (strcmp(argv[2], "early") == 0 || strcmp(argv[2], "late") == 0)) {
        const bool ok = board_display_flush_early(argv[2][0] == 'e');
        printf("DISP flush %s%s\n", argv[2], ok ? "" : " FAILED (LVGL pause)");
        return ok ? 0 : 1;
    }
    printf("usage: disp | disp bench full|box|off | disp bench full band | disp bench direct [overlay|inval|stall] | "
           "disp direct auto|off|reserve <bytes>|shadow on|off|selftest | disp flush early|late\n");
    return 1;
}
#endif /* CONFIG_OMNI_DEBUG_CONSOLE */

#if CONFIG_OMNI_DEBUG_CONSOLE
/* usbsim <off_s> <on_s> [mode]: a USB unplug/replug without touching the cable (Defect A/B investigation). Waits for
 * ASLEEP, then overrides the D+ pull-up off (the host sees a detach, SOFs stop) and, unless mode 1, tells power VBUS
 * is gone so the watch light-sleeps as on battery (mode 2 also holds a BBPLL consumer through the off window). After
 * off_s both are restored at once, as a real replug, and the normal path (power_asleep_poll) runs. on_s seconds are
 * watched for the host to enumerate (usb_serial_jtag_is_connected). If it never does, recovery steps run in turn, 8 s
 * each: R1 a D+ pulse as is; R2 CPU at 240 MHz (BBPLL on) + pulse; R3 + conf0 rewritten (pad on, D+ pull-up on, no
 * override); R4 + USJ peripheral reset; R5 an RTC-watchdog system reset (resets every peripheral, keeps RTC memory).
 * The samples live in RTC slow memory (no-init): plain `usbsim` prints them, after a reset too. */
#include "driver/usb_serial_jtag.h"
#include "hal/usb_serial_jtag_ll.h"
#include "hal/wdt_hal.h"
#include "soc/usb_serial_jtag_struct.h"
#include "esp_attr.h"
#include "esp_private/rtc_clk.h"
#include "esp_private/periph_ctrl.h"
#define USIM_ROWS 150
#define USIM_LOCKS 640
#define USIM_MAGIC 0x55534233u
#define USIM_STEPS 6
typedef struct {
    uint32_t t_ms, asleep_ms, conf0, sleeps, rejects;
    uint16_t sof, mhz;
    uint8_t phase, conn, state, flags; /* flags: bit0 bus reset seen, bit1 pm light sleep on, bit2..3 vbus edges mod 4 */
} usim_row_t;
typedef struct {
    uint32_t magic;
    int16_t off_s, on_s, mode;
    int16_t n;
    uint8_t running, done, wdt_reset;
    int32_t conn_ms[USIM_STEPS]; /* first connected, ms after: replug, R1..R5 (-1 never, -2 not run) */
    usim_row_t row[USIM_ROWS];
    char locks_off[USIM_LOCKS], locks_on[USIM_LOCKS];
} usim_t;
static RTC_NOINIT_ATTR usim_t s_usim;
static volatile bool s_usim_alive; /* the usbsim task runs now (RAM: a reset clears it; s_usim.running survives) */

static void usim_locks(char *buf)
{
    FILE *f = fmemopen(buf, USIM_LOCKS, "w");
    if (!f) return;
    esp_pm_dump_locks(f);
    fclose(f);
    buf[USIM_LOCKS - 1] = 0;
}

static void usim_sample(usim_t *u, int phase, int64_t t0)
{
    if (u->n >= USIM_ROWS) return;
    uint32_t raw = usb_serial_jtag_ll_get_intraw_mask();
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);
    esp_pm_config_t pm = {0};
    esp_pm_get_configuration(&pm);
    uint32_t sleeps, rejects;
    power_debug_sleeps(&sleeps, &rejects);
    u->row[u->n++] = (usim_row_t){
        .sleeps = sleeps,
        .rejects = rejects,
        .t_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000),
        .asleep_ms = (uint32_t)(power_asleep_us() / 1000),
        .conf0 = USB_SERIAL_JTAG.conf0.val,
        .sof = (uint16_t)USB_SERIAL_JTAG.fram_num.sof_frame_index,
        .mhz = (uint16_t)(esp_clk_cpu_freq() / 1000000),
        .phase = (uint8_t)phase,
        .conn = usb_serial_jtag_is_connected(),
        .state = (uint8_t)power_state(),
        .flags = (uint8_t)(((raw & USB_SERIAL_JTAG_INTR_BUS_RESET) ? 1 : 0) | (pm.light_sleep_enable ? 2 : 0) |
                           ((power_debug_vbus_edges() & 3) << 2)),
    };
}

/* Watch for an enumeration for ms, sampling twice a second; returns ms until connected, or -1. */
static int32_t usim_watch(usim_t *u, int phase, int64_t t0, int ms)
{
    const int64_t t1 = esp_timer_get_time();
    int32_t got = -1;
    for (int i = 0; i < ms / 250; i++) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (i % 2 == 1) usim_sample(u, phase, t0);
        if (got < 0 && usb_serial_jtag_is_connected()) got = (int32_t)((esp_timer_get_time() - t1) / 1000);
    }
    return got;
}

/* D+ back as after reset (dp_pullup = 1), then the override lifted: lifting it alone leaves dp_pullup at 0 */
static void usim_attach(void)
{
    const usb_serial_jtag_pull_override_vals_t on = {.dp_pu = true};
    usb_serial_jtag_ll_phy_enable_pull_override(&on);
    usb_serial_jtag_ll_phy_disable_pull_override();
}

static void usim_pulse(void)
{
    const usb_serial_jtag_pull_override_vals_t off = {0};
    usb_serial_jtag_ll_phy_enable_pull_override(&off);
    vTaskDelay(pdMS_TO_TICKS(200));
    usim_attach();
}

static void usim_wdt_reset(void)
{
    wdt_hal_context_t rtc = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rtc);
    wdt_hal_config_stage(&rtc, WDT_STAGE0, 2000, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_enable(&rtc);
    wdt_hal_write_protect_enable(&rtc);
    for (;;) {
    }
}

static void usim_task(void *arg)
{
    usim_t *u = arg;
    const int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < USIM_STEPS; i++) u->conn_ms[i] = -2;
    for (int i = 0; i < 600 && power_state() != PWR_ASLEEP; i++) vTaskDelay(pdMS_TO_TICKS(100));
    usim_sample(u, 0, t0);
    if (power_state() != PWR_ASLEEP) {
        u->running = 0;
        s_usim_alive = false;
        vTaskDelete(NULL);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    usim_sample(u, 0, t0);
    if (u->mode == 9) { /* watch only: a real unplug/replug by hand; samples spread over off_s */
        const int every_ms = u->off_s * 1000 / (USIM_ROWS - 4) > 2000 ? u->off_s * 1000 / (USIM_ROWS - 4) : 2000;
        for (int ms = 0; ms < u->off_s * 1000; ms += every_ms) {
            vTaskDelay(pdMS_TO_TICKS(every_ms));
            usim_sample(u, board_vbus_present() ? 2 : 1, t0);
            if (ms < u->off_s * 500 && ms + every_ms >= u->off_s * 500) usim_locks(u->locks_off);
        }
        usim_locks(u->locks_on);
        u->running = 0;
        u->done = 1;
        s_usim_alive = false;
        vTaskDelete(NULL);
        return;
    }
    const usb_serial_jtag_pull_override_vals_t off = {0};
    if (u->mode == 2) rtc_clk_bbpll_add_consumer();
    usb_serial_jtag_ll_phy_enable_pull_override(&off); /* unplug: D+ released */
    if (u->mode == 3) power_debug_vbus_all(0);
    else if (u->mode != 1) power_debug_vbus(0);
    for (int s = 0; s < u->off_s; s++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        usim_sample(u, 1, t0);
        if (u->mode == 3 && s % 90 == 60) { /* a wrist lift: awake 15 s, glance, asleep again */
            omni_event_t e = {.kind = EV_PWR, .t_ms = now_ms()};
            omni_events_post(&e);
        }
    }
    usim_locks(u->locks_off);
    usim_attach(); /* replug: D+ back and VBUS seen, at once */
    if (u->mode == 3) power_debug_vbus_all(-1);
    power_debug_vbus(-1);
    if (u->mode == 2) rtc_clk_bbpll_remove_consumer();
    u->conn_ms[0] = usim_watch(u, 2, t0, u->on_s * 1000);
    usim_locks(u->locks_on);
    esp_pm_lock_handle_t pm = NULL;
    for (int step = 1; step <= 4 && u->conn_ms[step - 1] < 0; step++) {
        if (step == 2 && esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "usbsim", &pm) == ESP_OK) {
            esp_pm_lock_acquire(pm);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (step == 3) {
            usb_serial_jtag_ll_phy_disable_pull_override();
            USB_SERIAL_JTAG.conf0.dp_pullup = 1;
            USB_SERIAL_JTAG.conf0.usb_pad_enable = 1;
        }
        if (step == 4) {
            PERIPH_RCC_ATOMIC() {
                usb_serial_jtag_ll_reset_register();
            }
        }
        usim_sample(u, 2 + step, t0);
        if (step != 4) usim_pulse();
        u->conn_ms[step] = usim_watch(u, 2 + step, t0, 8000);
    }
    if (pm) {
        esp_pm_lock_release(pm);
        esp_pm_lock_delete(pm);
    }
    u->running = 0;
    u->done = 1;
    s_usim_alive = false;
    bool ok = false;
    for (int i = 0; i < 5; i++) ok |= u->conn_ms[i] >= 0;
    if (!ok) {
        u->wdt_reset = 1;
        usim_wdt_reset(); /* R5 */
    }
    vTaskDelete(NULL);
}

static int cmd_usbsim(int argc, char **argv)
{
    usim_t *u = &s_usim;
    const bool valid = u->magic == USIM_MAGIC && u->n >= 0 && u->n <= USIM_ROWS;
    if (argc >= 3) {
        if (s_usim_alive) {
            printf("USBSIM busy\n");
            return 1;
        }
        memset(u, 0, sizeof *u);
        u->magic = USIM_MAGIC;
        u->off_s = (int16_t)atoi(argv[1]);
        u->on_s = (int16_t)atoi(argv[2]);
        u->mode = argc >= 4 ? (int16_t)atoi(argv[3]) : 0;
        u->running = 1;
        s_usim_alive = true;
        if (xTaskCreatePinnedToCore(usim_task, "usbsim", 4096, u, 5, NULL, 0) != pdPASS) {
            u->running = 0;
            s_usim_alive = false;
            printf("USBSIM no task\n");
            return 1;
        }
        printf("USBSIM start off %d s, on %d s, mode %d: once asleep the console drops\n", u->off_s, u->on_s, u->mode);
        return 0;
    }
    if (!valid) {
        printf("USBSIM none\n");
        return 0;
    }
    const char *how = u->running ? (s_usim_alive ? "(running)" : "(cut short by a reset)")
                      : u->done    ? "done"
                                   : "aborted (never asleep?)";
    printf("USBSIM off %d s on %d s mode %d %s; reset reason now %d; connected ms after replug %ld R1 %ld R2 %ld "
           "R3 %ld "
           "R4 %ld%s\n",
           u->off_s, u->on_s, u->mode, how,
           (int)esp_reset_reason(), (long)u->conn_ms[0], (long)u->conn_ms[1], (long)u->conn_ms[2],
           (long)u->conn_ms[3], (long)u->conn_ms[4], u->wdt_reset ? "; R5 RTC-WDT SYSTEM RESET" : "");
    printf("ph t_ms asleep_ms sleeps rejects conn sof mhz state conf0 busreset ls vbus_edges\n");
    for (int i = 0; i < u->n; i++) {
        const usim_row_t *r = &u->row[i];
        printf("%u %lu %lu %lu %lu %u %u %u %s %08lx %u %u %u\n", r->phase, (unsigned long)r->t_ms,
               (unsigned long)r->asleep_ms, (unsigned long)r->sleeps, (unsigned long)r->rejects, r->conn, r->sof, r->mhz,
               pwr_state_name((omni_pwr_state_t)r->state), (unsigned long)r->conf0,
               r->flags & 1, (r->flags >> 1) & 1, (r->flags >> 2) & 3);
        if (i % 16 == 15) vTaskDelay(pdMS_TO_TICKS(10));
    }
    u->locks_off[USIM_LOCKS - 1] = u->locks_on[USIM_LOCKS - 1] = 0;
    printf("-- locks at the end of 'unplugged' --\n%s", u->locks_off);
    vTaskDelay(pdMS_TO_TICKS(10));
    printf("-- locks at the end of 'replugged' --\n%s", u->locks_on);
    return 0;
}
#endif

#if CONFIG_PM_PROFILING || CONFIG_OMNI_DEBUG_CONSOLE
/* pm: the esp_pm locks (name, type, active count; times with CONFIG_PM_PROFILING). */
static int cmd_pm(int argc, char **argv)
{
#if CONFIG_OMNI_DEBUG_CONSOLE
    /* pm vbus 0|1|auto: power's view of VBUS only (light sleep configured as on battery), D+ left alone: with a USB
     * host attached CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION still holds the chip out of light sleep, so the console stays
     * up while power's light-sleep configuration (and its retention RAM) can be watched (M2). -1 is auto too (it read
     * as "on" once: Task 9); anything else prints the usage and changes nothing. */
    if (argc == 3 && strcmp(argv[1], "vbus") == 0) {
        const char *a = argv[2];
        const int v = strcmp(a, "auto") == 0 || strcmp(a, "-1") == 0 ? -1
                      : strcmp(a, "0") == 0                          ? 0
                      : strcmp(a, "1") == 0                          ? 1
                                                                     : -2;
        if (v == -2) {
            printf("usage: pm vbus 0|1|auto (-1 = auto)\n");
            return 1;
        }
        power_debug_vbus_lapsing(v);   /* M6: back to auto after 30 min (R66), and on any reboot */
        printf("PM vbus %s for power%s\n", v < 0 ? "auto" : a, v >= 0 ? " (back to auto in 30 min)" : "");
        return 0;
    }
#endif
    (void)argc;
    (void)argv;
    esp_pm_dump_locks(stdout);
#if CONFIG_OMNI_DEBUG_CONSOLE
    uint32_t sleeps, rejects;
    power_debug_sleeps(&sleeps, &rejects);
    printf("light sleeps %lu (skipped %lu), asleep %llu ms; VBUS edges acted on %u, VBUS read failures %u\n",
           (unsigned long)sleeps, (unsigned long)rejects, (unsigned long long)(power_asleep_us() / 1000),
           power_debug_vbus_edges(), board_vbus_read_failures());
#endif
    return 0;
}
#endif

#if CONFIG_OMNI_DEBUG_CONSOLE
/* assets: one line per file in the assets partition (name, bytes, w x h, colour format; each image is also copied to
 * PSRAM and compared, as assets_image_psram would for a face; an OFA1 font its glyph count and line height), then the payload checksum read back from flash: "ASSETS <n> checksum ok|BAD", or "ASSETS not mapped". */
static int cmd_assets(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!assets_ready()) {
        printf("ASSETS not mapped\n");
        return 1;
    }
    int n = assets_count();
    for (int i = 0; i < n; i++) {
        size_t size;
        const char *name = assets_entry(i, NULL, &size);
        lv_image_dsc_t d;
        const char *ext = strrchr(name, '.');
        const void *data;
        assets_entry(i, &data, NULL);
        omni_strip_t st;
        unsigned glyphs, line_h;
        if (ext && strcmp(ext, ".bin") == 0 && omni_strip_map(&st, data, size)) {   /* the ring strips (Task 2) */
            printf("%-16s %8u %u(%u)x%u strip L%u\n", name, (unsigned)size, st.n_theta, st.stride, st.n_r, st.level);
        } else if (watch_theme()->asset_line && watch_theme()->asset_line(name, data, size)) {
            /* one of the theme's own formats (the Omni: its prebuilt dial map), printed by the theme */
        } else if (ext && strcmp(ext, ".bin") == 0 && fontasset_info(data, size, &glyphs, &line_h)) {
            printf("%-16s %8u %u glyphs, line %u font\n", name, (unsigned)size, glyphs, line_h); /* OFA1 (Task 8) */
        } else if (ext && strcmp(ext, ".bin") == 0 && size >= 4 && memcmp(data, "OKL1", 4) == 0) {
            printf("%-16s %8u - layout\n", name, (unsigned)size); /* kb_lay.bin, not an image */
        } else if (ext && strcmp(ext, ".bin") == 0 && assets_image(name, &d)) {
            const char *cf = d.header.cf == LV_COLOR_FORMAT_ARGB8888   ? "ARGB8888"
                             : d.header.cf == LV_COLOR_FORMAT_RGB565A8 ? "RGB565A8"
                             : d.header.cf == LV_COLOR_FORMAT_RGB565   ? "RGB565"
                             : d.header.cf == LV_COLOR_FORMAT_A8       ? "A8"
                                                                       : "?";
            /* and the PSRAM copy a face would take: same bytes, A8 plane included */
            lv_draw_buf_t *b = assets_image_psram(name);
            bool copy_ok = b && memcmp(b->data, d.data, d.data_size) == 0;
            theme_buf_destroy(b);
            printf("%-16s %8u %ux%u %s%s\n", name, (unsigned)size, (unsigned)d.header.w, (unsigned)d.header.h, cf,
                   copy_ok ? "" : " (PSRAM copy FAILED)");
        } else if (ext && strcmp(ext, ".tex") == 0) { /* w x w/2 texels of 4 bytes: size = 2 w^2 */
            unsigned w = 1;
            while (2u * w * w < size) w <<= 1;
            printf("%-16s %8u %ux%u raw\n", name, (unsigned)size, w, w / 2);
        } else {
            printf("%-16s %8u - raw\n", name, (unsigned)size);
        }
    }
    int64_t t0 = esp_timer_get_time();
    bool ok = assets_checksum_ok();
    printf("ASSETS %d checksum %s (%lld ms)\n", n, ok ? "ok" : "BAD", (long long)(esp_timer_get_time() - t0) / 1000);
    return ok ? 0 : 1;
}
#endif

#if CONFIG_OMNI_DEBUG_CONSOLE
/* pack probe: the reserved `pack` partition (data, subtype 0x40, 16 MB at 0x1000000; partitions.csv, frozen): its size
 * and the CRC-32 of its first and last 4 KB, read only ("PACK <size> first <crc32> last <crc32>"). "PACK absent" on a
 * table without it. Nothing here ever writes to it (Stage 4b: no write to `pack`). */
static int cmd_pack(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "probe") != 0) {
        printf("usage: pack probe\n");
        return 1;
    }
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "pack");
    if (p == NULL) {
        printf("PACK absent\n");
        return 0;
    }
    enum { CHUNK = 4096 };
    uint8_t *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    if (buf == NULL || p->size < CHUNK) {
        printf("PACK %lu: %s\n", (unsigned long)p->size, buf ? "smaller than 4 KB" : "no memory");
        heap_caps_free(buf);
        return 1;
    }
    uint32_t crc[2] = {0, 0};
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 2 && err == ESP_OK; i++) {
        err = esp_partition_read(p, i == 0 ? 0 : p->size - CHUNK, buf, CHUNK);
        if (err == ESP_OK) crc[i] = esp_rom_crc32_le(0, buf, CHUNK);
    }
    heap_caps_free(buf);
    if (err != ESP_OK) {
        printf("PACK %lu read failed: %s\n", (unsigned long)p->size, esp_err_to_name(err));
        return 1;
    }
    printf("PACK %lu first %08lx last %08lx\n", (unsigned long)p->size, (unsigned long)crc[0], (unsigned long)crc[1]);
    return 0;
}
#endif

#if CONFIG_OMNI_DEBUG_CONSOLE
/* ui_stack_note (debug_console.h): the LVGL task's stack low, by the draw that reached it (spec 11). The log line is
 * deferred to an LVGL async call, which runs from lv_timer_handler, shallower than any draw callback, so printing
 * (vprintf takes about 1.3 KB) never makes a low of its own inside a draw. LVGL task only, except `stacks`' racy read. */
#define UI_STACK_TAGS 24   /* 14 Setup tags and the provisioning screen's 5 (R230), with room */
static struct { const char *tag; unsigned low; } s_ui_stack[UI_STACK_TAGS];   /* each tag's lowest */
static unsigned s_ui_low = UINT32_MAX;
static const char *s_ui_low_tag;
static bool s_ui_log_pending;
/* `stacks`' task handles, each taken on its own task (xTaskGetHandle would link ~300 B of FreeRTOS code into IRAM) */
static TaskHandle_t s_lvgl_task, s_timer_task;

static void grab_lvgl_task(void *arg)
{
    (void)arg;
    s_lvgl_task = xTaskGetCurrentTaskHandle();
}

static void grab_timer_task(void *arg)
{
    (void)arg;
    s_timer_task = xTaskGetCurrentTaskHandle();
}

/* debug_console_start: one async call on the LVGL task and one esp_timer shot (kept; never deleted) take them. */
static void stacks_start(void)
{
    bsp_display_lock(-1);
    lv_async_call(grab_lvgl_task, NULL);
    bsp_display_unlock();
    static esp_timer_handle_t shot;
    const esp_timer_create_args_t args = {.callback = grab_timer_task, .name = "stacks"};
    if (esp_timer_create(&args, &shot) == ESP_OK) esp_timer_start_once(shot, 1000);
}

static void ui_stack_log(void *arg)
{
    (void)arg;
    s_ui_log_pending = false;
    ESP_LOGI(TAG, "lvgl stack: %u B free after %s", s_ui_low, s_ui_low_tag);
}

void ui_stack_note(const char *tag)
{
    /* the LVGL task only: `shot`/`refr` (the console task) and the wake refresh (the event loop) draw screens too, and
     * their stacks are not the LVGL task's; before stacks_start's async call has run, nothing is recorded */
    if (s_lvgl_task == NULL || xTaskGetCurrentTaskHandle() != s_lvgl_task) return;
    const unsigned free_b = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    int i = 0;
    while (i < UI_STACK_TAGS && s_ui_stack[i].tag != NULL && strcmp(s_ui_stack[i].tag, tag) != 0) i++;
    if (i < UI_STACK_TAGS) {
        if (s_ui_stack[i].tag == NULL || free_b < s_ui_stack[i].low) s_ui_stack[i].low = free_b;
        s_ui_stack[i].tag = tag;
    }
    if (free_b >= s_ui_low) return;
    s_ui_low = free_b;
    s_ui_low_tag = tag;
    if (!s_ui_log_pending && lv_async_call(ui_stack_log, NULL) == LV_RESULT_OK) s_ui_log_pending = true;
}

/* stacks: the least each task's stack has had free, and the LVGL task's lowest after each ui_stack_note tag */
static int cmd_stacks(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("stacks free (least ever): lvgl %u B, esp_timer %u B, events %u B, net %u B, console %u B\n",
           s_lvgl_task ? (unsigned)uxTaskGetStackHighWaterMark(s_lvgl_task) : 0,
           s_timer_task ? (unsigned)uxTaskGetStackHighWaterMark(s_timer_task) : 0, app_events_stack_hwm(), net_stack_hwm(),
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
    for (int i = 0; i < UI_STACK_TAGS && s_ui_stack[i].tag != NULL; i++)
        printf("lvgl after %s: %u B\n", s_ui_stack[i].tag, s_ui_stack[i].low);
    return 0;
}
#endif

/* m8: a debug aid. A command that cannot be registered (internal RAM short at boot) is logged and skipped, and the
 * watch runs on without the console if the REPL cannot start: nothing here aborts. */
void debug_console_reg(const char *command, const char *help, int (*func)(int argc, char **argv))
{
    const esp_console_cmd_t c = {.command = command, .help = help, .func = func};
    esp_err_t err = esp_console_cmd_register(&c);
    if (err != ESP_OK) ESP_LOGE(TAG, "command %s not registered: %s", command, esp_err_to_name(err));
}

void debug_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    static char prompt[OMNI_BOOT_ID_MAX + 2];   /* "<id>> ": the descriptor's id, set before this runs (watch_main) */
    repl_cfg.prompt = omni_boot_prompt(watch_theme()->id, prompt, sizeof prompt);
    repl_cfg.task_stack_size = 8192; /* `shot` renders the screen (lv_snapshot_take) on this stack */
    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console not started: %s", esp_err_to_name(err));
        return;
    }

    debug_console_reg("shot", "Dump the active screen as base64 RGB565 rows", &cmd_shot);
    debug_console_reg("state", "Power, battery, settings, net, sync, RAM, failure counters and the last 5 events", &cmd_state);
    debug_console_reg("batsession",
        "batsession: the last unplug..replug battery session (start/end %, mV, duration, wakes, hours per 100 wakes)",
        &cmd_batsession);
    debug_console_reg("city", "city [name]: show or set the home city", &cmd_city);
    debug_console_reg("clock", "clock [log]: timekeeping internals; log = SNTP corrections and calibration", &cmd_clock);
    debug_console_reg("reboot", "restart the chip", &cmd_reboot);
#if CONFIG_OMNI_DEBUG_CONSOLE
    debug_console_reg("net", "net [sync|prov|forget|forget-lock on|off|fake <state> [prov] [forget]|boot-unprov]: Wi-Fi "
        "provisioning and time sync",
        &cmd_net);
    debug_console_reg("fps", "fps start | fps: frames rendered since start", &cmd_fps);
    debug_console_reg("refr", "time a full-screen redraw", &cmd_refr);
    debug_console_reg("trace",
        "trace on|off: stream T,<ms>,<ax>,<ay>,<az> accel lines (g, 25 Hz); "
        "trace rec <s> [delay_s]: record to RAM; trace dump: print it",
        &cmd_trace);
    debug_console_reg("inject",
        "inject lift|pwr|touch|boot [double|long] [delay_s]: post that event (touch = G_TAP at the centre, boot = "
        "BOOT short unless double|long); "
        "inject open picker|quick|settings|netlist|faces; inject gesture <kind> [x y [x0 y0]]; inject drag <x0> <y0> <x1> <y1> <ms> "
        "and inject arc <r> <a0> <a1> <ms>: a synthetic finger through the touch classifier",
        &cmd_inject);
    debug_console_reg("face", "face <key>: show that face (`face` lists the keys)", &cmd_face);
    debug_console_reg("carousel", "carousel [set <id>...]: the swipe order", &cmd_carousel);
    debug_console_reg("kb", "kb password|ssid|city: open the rim keyboard in that mode (password: the saved network's)", &cmd_kb);
    debug_console_reg("netlist", "netlist: scan, then print the list's rows (name with bytes outside ASCII as \\xNN, RSSI, "
        "class, auth, channel, saved)", &cmd_netlist);
    debug_console_reg("setting", "setting reveal flip|iris|none | setting motion on|off | setting sound on|off | setting volume 0..100: "
        "Settings -> Reveal / Reduced motion / Sound / Volume",
        &cmd_setting);
    debug_console_reg("sfx", "sfx play <name>|stop|stat|race stop|play: play a sound asset (surge = sfx_surge.pcm), stop, the "
        "player's state; race: a provisioning start against a playing surge and a racing stop or play (T13 I1)",
        &cmd_sfx);
    debug_console_reg("awake", "awake hold|release: suspend the glance/sleep timeout (until release, reboot or 30 min)", &cmd_awake);
    debug_console_reg("settime", "settime <unix>: set the system clock (UTC seconds)", &cmd_settime);
    debug_console_reg("nvs", "nvs stats | nvs migrate force: NVS entries and namespace bytes; re-import omni/settings over the "
        "watch and theme records (omni is never written)", &cmd_nvs);
    debug_console_reg("disp", "disp | disp bench full|box|off | disp bench full band | disp bench direct [overlay|inval|stall] | "
        "disp direct auto|off|reserve <bytes>|shadow on|off|selftest | disp flush early|late: display pipeline "
        "counters, the "
        "synthetic bench view (image, band or direct path), the direct path's mode, the flush release",
        &cmd_disp);
    debug_console_reg("assets", "list the assets partition (name, bytes, w x h, format) and check its checksum", &cmd_assets);
    debug_console_reg("theme", "theme dump <0|1|2>: a brass background's RGB565 pixels as base64 rows (bezel, picker, settings)",
        &cmd_theme);
    debug_console_reg("stacks", "stacks: the least stack each task has had free (lvgl, esp_timer, events, net, console), and "
        "the LVGL task's lowest after each screen draw tag (ui_stack_note); 0: no task", &cmd_stacks);
    stacks_start();
    debug_console_reg("pack", "pack probe: the reserved pack partition's size and the CRC-32 of its first and last 4 KB "
        "(read only; PACK absent on a table without it)", &cmd_pack);
#else
    debug_console_reg("net", "net [sync|prov]: Wi-Fi status, time sync, provisioning", &cmd_net);
#endif
#if CONFIG_PM_PROFILING || CONFIG_OMNI_DEBUG_CONSOLE
    debug_console_reg("pm", "esp_pm locks (name, type, active count); pm vbus 0|1|auto (-1 = auto): power's VBUS view",
                      &cmd_pm);
#endif
#if CONFIG_OMNI_DEBUG_CONSOLE
    debug_console_reg("usbsim", "usbsim <off_s> <on_s> [mode]: simulated USB unplug (D+ off; mode 0 power sees battery, 1 VBUS kept, 2 battery + "
        "BBPLL held, 3 all see battery + a PWR wake every 90 s; "
        "mode 9 only watches off_s s for a real unplug by hand) then replug and recovery steps; usbsim: results",
        &cmd_usbsim);
#endif
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (watch_theme()->console) watch_theme()->console();   /* the theme's own commands (the Omni: globe, dial, ring) */
#endif

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "console REPL not started: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "console ready");
}
