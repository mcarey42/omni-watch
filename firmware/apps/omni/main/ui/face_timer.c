/* The timer and stopwatch: design/mockups/faces-practical.js, `id: 'timer'` (frame, lines 1211-1285) and its sleepFrame
 * (timer_glance.c), with R135's departure: turning the rim only SETS the duration; a centre tap starts the countdown and
 * a second tap pauses it, so a pocket drag can never start an alarm. The state machine and the texts are omni_core's
 * (omni/timer.h), kept by timer_svc.c from boot, so a countdown runs and rings whichever face is shown.
 *
 * Awake: the 60-minute scale ring (omni_tmr_ring_draw into an opaque RGB565 box in PSRAM, redrawn only where
 * omni_tmr_ring_dirty says it changed), 12 numerals, the readout in fixed cells (with the stopwatch's hundredths), the
 * label, the hint, the wall time and, under a held thumb, the reset progress ring. R144: the rim is a relative jog dial
 * (omni/tmr_jog.h): a drag started at r >= 140 winds only once it has turned 20 degrees round the centre inside the rim
 * band, 30 min per turn from the current value, so the thumb may lift and land anywhere and never hides a value it must
 * point at; before that, a flick is handed back and posted as the shell's swipe, so left/right navigate from anywhere on
 * the face, rim included, and a swipe down from the top edge opens quick settings. Swipes from the centre change face as everywhere. Wake: a 300 ms fade through each object's
 * opacity, never on an alarm (DONE shows at once). Glance: the countdown override in COUNT and PAUSED (watch_theme.h),
 * else the shared glance. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board.h"
#include "shell.h"
#include "glance.h"
#include "theme.h"
#include "store.h"
#include "assets.h"
#include "assets_meta.h"
#include "omni_fonts.h"
#include "omni_faces.h"
#include "omni/timefmt.h"
#include "omni/timer.h"
#include "omni/tmr_ring.h"
#include "omni/tmr_jog.h"
#include "omni_events.h"
#include "timer_glance.h"
#include "face_fade.h"
#include "timer_svc.h"

#define CX THEME_CX
#define CY THEME_CY
#define NUM_R (176 + 11)           /* the numerals at R0 + 11, canvas baseline 'middle' */
#define NUM_MID 5                  /* 'middle' to alphabetic baseline at 13 px Plex Mono: (1.025 - 0.275) / 2 em */
#define BIG_BASE (CY + 26)
#define BIG_CELL 44                /* drawCells(..., 44, 20) */
#define BIG_COLON 20
#define BIG_CELLS 10               /* "9999:59:59", the stopwatch's longest */
#define SMALL_SHIFT 20             /* the cells sit 20 px left when the hundredths follow */
#define LABEL_BASE (CY - 46)
#define LABEL_LS 2                 /* 600 13px FD, spacing 2.5 */
#define DOT_ADV 3                  /* the middle dot's advance at 13 px (D10: a 2 px brass dot object) */
#define DOT_RISE 5                 /* the dot's top above the label baseline */
#define HINT_BASE (CY + 66)
#define WALL_BASE (CY + 112)
#define HOLD_R 128                 /* the reset progress: r 128, 3 px, brassHi, round caps */
#define HOLD_W 3
#define HOLD_DELAY_MS 150
#define HOLD_FILL_MS 450
#define FADE_MS 300
#define CAD_FAST 33                /* SET and SW (A3, D6) */
#define CAD_PULSE 66               /* the PAUSED pulse, 15 fps (66 ms: 67 read 14.9) */
#define CAD_DONE 55                /* the DONE pulse: a 66 ms tick slipped behind its 42 ms refresh to 14.4 fps */
#define CAD_SLOW 1000              /* otherwise; in a mode a hold can reset, the thumb is watched every PRESS_MS */
#define PRESS_MS 50
#define CAD_SLACK 5               /* a tick this early still renders (lv_timer jitter) */
#define LOST_UP_MS 500             /* a wind whose finger lifted this long ago with no G_DRAG_END: cancelled */
#define INK_LAST 0xff9c8c          /* the last minute */
#define MIDDOT "\xC2\xB7"

typedef struct {
    lv_obj_t *awake, *ring, *num[12], *cell[BIG_CELLS], *small, *label[2], *dot, *hint, *wall, *hold;
    glance_t *glance;
    lv_draw_buf_t *box;
    omni_tmr_lut_t lut;            /* points into the flash mapping (tmr_lut.bin, R139): nothing built, nothing copied */
    omni_tmr_ring_t drawn;
    bool drawn_ok;
    void *pulse_mem;               /* DONE only (PSRAM, ~0.29 MB): its two layers, blended per frame (final fix) */
    omni_tmr_pulse_t pulse;
    bool pulse_off;                /* no memory for them this DONE: whole draws, not a retry every frame */
    lv_timer_t *timer;
    uint32_t period;
    bool running;
    bool fading;
    uint32_t fade_t0;
    lv_opa_t opa;                  /* the fade's opacity, as applied */
    omni_tmr_drag_t drag;          /* the rim drag being classified (R144) */
    bool pressed, dragged, up_seen;
    uint32_t press_t0, gesture_ms, up_ms, last_render;
    int hold_val;                  /* as shown, -1 hidden */
    char big[12], small_txt[4], label_txt[28], hint_txt[40];
    uint8_t ink;
    bool label_red;
    int64_t wall_min;
    bool wall_h24;
} face_t;

static face_t *F;
static const char *TAG = "face_timer";

static bool is_set_or_sw(omni_tmr_mode_t m) { return m == OMNI_TMR_SET || m == OMNI_TMR_SW; }
static bool can_reset(omni_tmr_mode_t m) { return m != OMNI_TMR_IDLE && m != OMNI_TMR_DONE && m != OMNI_TMR_SET; }

static uint32_t cadence(omni_tmr_mode_t m)
{
    if (is_set_or_sw(m)) return CAD_FAST;
    if (m == OMNI_TMR_DONE) return CAD_DONE;
    if (m == OMNI_TMR_PAUSED) return CAD_PULSE;
    return CAD_SLOW;
}

static void period_sync(void)
{
    const omni_tmr_mode_t m = tmr_svc()->mode;
    uint32_t p = cadence(m);
    if (F->fading || F->hold_val >= 0) p = CAD_FAST;
    else if (p == CAD_SLOW && can_reset(m)) p = PRESS_MS;   /* 1 s modes only (I1): the hold ring starts at 150 ms */
    if (p == F->period || F->timer == NULL) return;
    F->period = p;
    lv_timer_set_period(F->timer, p);
}

/* ------------------------------------------------------------ drawing */
static float place_cells(const char *s, float cx, int32_t y)
{
    float w = 0;
    for (const char *p = s; *p; p++) w += *p == ':' ? BIG_COLON : BIG_CELL;
    float x = cx - w / 2;
    int i = 0;
    for (; i < BIG_CELLS && s[i]; i++) {
        const char ch[2] = {s[i], 0};
        const float cw = s[i] == ':' ? BIG_COLON : BIG_CELL;
        theme_set_text(F->cell[i], ch);
        theme_place(F->cell[i], THEME_CENTER, lroundf(x + cw / 2), y);
        x += cw;
    }
    for (; i < BIG_CELLS; i++) theme_set_text(F->cell[i], "");
    return w;
}

static void pulse_free(void)
{
    F->pulse_off = false;
    if (F->pulse_mem == NULL) return;
    heap_caps_free(F->pulse_mem);
    F->pulse_mem = NULL;
    F->drawn_ok = false;   /* the box holds a blend, not a draw: the next draw is whole */
}

/* DONE: only the sector's alpha moves, so its two layers are drawn once and each frame blends them (omni_tmr_pulse_*)
 * instead of a whole-ring draw (~120 ms a frame on the device: 4.3 fps). False: no memory, draw as before. */
static bool pulse_update(const omni_tmr_ring_t *s)
{
    if (F->pulse_off) return false;
    if (F->pulse_mem == NULL) {
        const size_t n = omni_tmr_pulse_bytes(&F->lut);
        const int64_t t0 = esp_timer_get_time();
        F->pulse_mem = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        if (F->pulse_mem == NULL || !omni_tmr_pulse_build(&F->pulse, &F->lut, s, (uint16_t *)F->box->data,
                                                          F->pulse_mem, n)) {
            ESP_LOGW(TAG, "no DONE pulse layers (%u B): whole draws", (unsigned)n);
            heap_caps_free(F->pulse_mem);
            F->pulse_mem = NULL;
            F->pulse_off = true;
            return false;
        }
        ESP_LOGI(TAG, "DONE pulse layers built in %u us (%u B)", (unsigned)(esp_timer_get_time() - t0), (unsigned)n);
    }
    omni_tmr_pulse_draw(&F->pulse, s->alpha, (uint16_t *)F->box->data);
    F->drawn_ok = false;
    const lv_area_t a = {OMNI_TMR_BOX_XY, OMNI_TMR_BOX_XY, OMNI_TMR_BOX_XY + OMNI_TMR_BOX - 1,
                         OMNI_TMR_BOX_XY + OMNI_TMR_BOX - 1};
    lv_obj_invalidate_area(F->ring, &a);
    return true;
}

static void ring_update(const omni_tmr_view_t *v)
{
    if (F->ring == NULL) return;
    const omni_tmr_ring_t s = {.min = v->sector_min, .red = v->sector_red, .alpha = v->alpha, .bead = v->bead};
    if (tmr_svc()->mode != OMNI_TMR_DONE) pulse_free();
    else if (pulse_update(&s)) return;
    int r[4] = {0, 0, OMNI_TMR_BOX - 1, OMNI_TMR_BOX - 1};
    if (F->drawn_ok) omni_tmr_ring_dirty(&F->drawn, &s, r);
    F->drawn = s;
    F->drawn_ok = true;
    if (r[2] < r[0]) return;
    omni_tmr_ring_draw(&F->lut, &s, (uint16_t *)F->box->data, r[1], r[3] + 1);
    const lv_area_t a = {r[0] + OMNI_TMR_BOX_XY, r[1] + OMNI_TMR_BOX_XY, r[2] + OMNI_TMR_BOX_XY, r[3] + OMNI_TMR_BOX_XY};
    lv_obj_invalidate_area(F->ring, &a);
}

/* The label; "SET · TAP TO START" as two labels round a brass dot (the label font has no middle dot, D10), spaced as
 * the canvas spaces the whole string. */
static void label_update(const char *txt, bool red)
{
    if (red != F->label_red) {   /* a style set invalidates: only on a change */
        F->label_red = red;
        for (int i = 0; i < 2; i++) lv_obj_set_style_text_color(F->label[i], lv_color_hex(red ? THEME_RED : THEME_BRASS), 0);
    }
    if (strcmp(txt, F->label_txt) == 0) return;
    snprintf(F->label_txt, sizeof F->label_txt, "%s", txt);
    const char *dot = strstr(txt, MIDDOT);
    if (dot == NULL) {
        theme_set_text(F->label[0], txt);
        theme_place(F->label[0], THEME_CENTER, CX, LABEL_BASE);
        theme_set_text(F->label[1], "");
        lv_obj_add_flag(F->dot, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char left[28];
    size_t n = (size_t)(dot - txt);
    while (n > 0 && txt[n - 1] == ' ') n--;
    snprintf(left, sizeof left, "%.*s", (int)n, txt);
    const char *right = dot + strlen(MIDDOT);
    while (*right == ' ') right++;
    const lv_font_t *f = &font_cormorant_700_13;
    const int32_t wl = theme_text_width(left, f, LABEL_LS) + LABEL_LS;
    const int32_t wsp = theme_text_width(" ", f, LABEL_LS) + LABEL_LS;
    const int32_t wdot = DOT_ADV + LABEL_LS;
    const int32_t wr = theme_text_width(right, f, LABEL_LS) + LABEL_LS;
    const int32_t x0 = CX - (wl + 2 * wsp + wdot + wr) / 2;
    theme_set_text(F->label[0], left);
    theme_place(F->label[0], THEME_LEFT, x0, LABEL_BASE);
    theme_set_text(F->label[1], right);
    theme_place(F->label[1], THEME_LEFT, x0 + wl + 2 * wsp + wdot, LABEL_BASE);
    lv_obj_set_pos(F->dot, x0 + wl + wsp + DOT_ADV / 2 - 1, LABEL_BASE - DOT_RISE);
    lv_obj_remove_flag(F->dot, LV_OBJ_FLAG_HIDDEN);
}

static void wall_update(void)
{
    const int64_t now = (int64_t)time(NULL);
    const bool h24 = settings()->h24;
    if (now / 60 == F->wall_min && h24 == F->wall_h24) return;
    F->wall_min = now / 60;
    F->wall_h24 = h24;
    char hhmm[6] = "--:--", ap[3] = "", buf[12];
    if (omni_time_is_set(now)) omni_fmt_clock(now, h24, hhmm, ap);
    snprintf(buf, sizeof buf, "%s%s%s", hhmm, ap[0] ? " " : "", ap);
    for (char *p = buf; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');   /* tm.ap.toLowerCase() */
    theme_set_text(F->wall, buf);
    theme_place(F->wall, THEME_CENTER, CX, WALL_BASE);
}

static void render(uint32_t now)
{
    omni_tmr_view_t v;
    omni_tmr_view(tmr_svc(), now, &v);
    ring_update(&v);
    if (strcmp(v.big, F->big) != 0 || strcmp(v.small, F->small_txt) != 0 || v.ink != F->ink) {
        snprintf(F->big, sizeof F->big, "%s", v.big);
        snprintf(F->small_txt, sizeof F->small_txt, "%s", v.small);
        F->ink = v.ink;
        static const uint32_t INK[] = {THEME_INK, THEME_INK_FAINT, INK_LAST, THEME_RED};
        const uint32_t col = INK[v.ink < 4 ? v.ink : 0];
        for (int i = 0; i < BIG_CELLS; i++) lv_obj_set_style_text_color(F->cell[i], lv_color_hex(col), 0);
        const float cx = (float)CX - (v.small[0] ? SMALL_SHIFT : 0);
        const float w = place_cells(v.big, cx, BIG_BASE);
        theme_set_text(F->small, v.small);
        theme_place(F->small, THEME_LEFT, lroundf(cx + w / 2 + 2), BIG_BASE);
    }
    label_update(v.label, tmr_svc()->mode == OMNI_TMR_DONE);
    if (strcmp(v.hint, F->hint_txt) != 0) {
        snprintf(F->hint_txt, sizeof F->hint_txt, "%s", v.hint);
        theme_set_text(F->hint, v.hint);
        theme_place(F->hint, THEME_CENTER, CX, HINT_BASE);
    }
    wall_update();
    F->last_render = now;
}

/* ------------------------------------------------------------ the hold ring and the fade */
static void hold_update(uint32_t now)
{
    const bool pressed = board_touch_pressed();
    if (pressed && !F->pressed) {
        F->press_t0 = now;
        F->dragged = false;
    }
    F->pressed = pressed;
    int val = -1;
    if (pressed && !F->dragged && can_reset(tmr_svc()->mode)) {
        const uint32_t from = (int32_t)(F->gesture_ms - F->press_t0) > 0 ? F->gesture_ms : F->press_t0;
        const int32_t held = (int32_t)(now - from);
        if (held >= HOLD_DELAY_MS) {
            const int32_t p = (held - HOLD_DELAY_MS) * 1000 / HOLD_FILL_MS;
            val = p > 1000 ? 1000 : p;
        }
    }
    if (val == F->hold_val) return;
    F->hold_val = val;
    if (val < 0) {
        lv_obj_add_flag(F->hold, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_arc_set_value(F->hold, (int32_t)val);
    lv_obj_remove_flag(F->hold, LV_OBJ_FLAG_HIDDEN);
}

static void opa_apply(lv_opa_t opa)
{
    face_fade_children(F->awake, &F->opa, opa);   /* R28 */
}

static void fade_update(uint32_t now)
{
    if (!F->fading) return;
    const int32_t e = (int32_t)(now - F->fade_t0);
    if (e >= FADE_MS) F->fading = false;
    opa_apply(e >= FADE_MS ? LV_OPA_COVER : (lv_opa_t)(e < 0 ? 0 : e * 255 / FADE_MS));
}

/* ------------------------------------------------------------ the frame */
static void tick(lv_timer_t *t)
{
    (void)t;
    if (F == NULL || !F->running) return;
    const uint32_t now = tmr_svc_now_ms();
    omni_tmr_t *tm = tmr_svc();
    /* a wind whose G_DRAG_END never came (the finger is up): back to before it (Task 1 m2) */
    if (tm->mode == OMNI_TMR_SET && !board_touch_pressed()) {
        if (!F->up_seen) {
            F->up_seen = true;
            F->up_ms = now;
        } else if ((int32_t)(now - F->up_ms) >= LOST_UP_MS) {
            omni_tmr_wind_cancel(tm);
            tmr_svc_changed();
            F->up_seen = false;
            F->drag.phase = OMNI_TMR_DRAG_OFF;
        }
    } else {
        F->up_seen = false;
    }
    hold_update(now);
    fade_update(now);
    /* 1 s modes render on the readout's own second (so it never lags), else at the cadence. The tick may run faster
     * than the cadence (the hold ring and the fade tick at 33 ms; neither needs a render): PAUSED and DONE still pulse
     * at 15 fps (I1). */
    const uint32_t cad = cadence(tm->mode);
    const int32_t since = (int32_t)(now - F->last_render);
    bool due = cad < CAD_SLOW ? since >= (int32_t)cad - CAD_SLACK : since >= CAD_SLOW;
    if (!due) {
        omni_tmr_view_t v;
        omni_tmr_view(tm, now, &v);
        due = strcmp(v.big, F->big) != 0;
    }
    if (due) render(now);
    period_sync();
}

/* ------------------------------------------------------------ build */
static void face_create(lv_obj_t *parent)
{
    F = heap_caps_calloc(1, sizeof *F, MALLOC_CAP_SPIRAM);
    if (F == NULL) return;
    F->opa = LV_OPA_COVER;
    F->hold_val = -1;
    F->wall_min = -1;
    F->ink = 0xff;
    F->awake = theme_box(parent, 0, 0, THEME_W, THEME_W);

    /* the ring box (opaque RGB565, cleared to black once: the draw writes only 160 <= r <= 220) and its LUT, the flash
     * asset read in place (R139: building it here took 2.97-4.09 s on the device) */
    size_t lut_size = 0;
    const int64_t t0 = esp_timer_get_time();
    const void *lut_file = assets_raw(ASSET_TMR_LUT, &lut_size);
    const bool lut_ok = omni_tmr_lut_load(&F->lut, lut_file, lut_size);
    ESP_LOGI(TAG, "ring LUT %s: %u px, checked in %u us", lut_ok ? "loaded" : "refused", (unsigned)F->lut.n,
             (unsigned)(esp_timer_get_time() - t0));
    F->box = lut_ok ? theme_buf_create(OMNI_TMR_BOX, OMNI_TMR_BOX, LV_COLOR_FORMAT_RGB565) : NULL;
    if (F->box && F->box->header.stride == OMNI_TMR_BOX * 2) {
        memset(F->box->data, 0, F->box->data_size);
        F->ring = lv_image_create(F->awake);
        lv_obj_remove_flag(F->ring, LV_OBJ_FLAG_CLICKABLE);
        lv_image_set_src(F->ring, F->box);
        lv_obj_set_pos(F->ring, OMNI_TMR_BOX_XY, OMNI_TMR_BOX_XY);
    } else {
        ESP_LOGE(TAG, "no ring (box %p, %s %s): the readout only", (void *)F->box, ASSET_TMR_LUT,
                 lut_ok ? "ok" : lut_file ? "invalid" : "missing");
        theme_buf_destroy(F->box);
        F->box = NULL;
    }

    for (int i = 0; i < 12; i++) {   /* 500 12px FM at 0.8 (the 13 px cut) */
        F->num[i] = theme_label(F->awake, &font_plex_mono_500_13, THEME_INK, THEME_OPA(0.8), 0);
        char s[4];
        snprintf(s, sizeof s, "%d", i * 5);
        theme_set_text(F->num[i], s);
        const float a = -(float)M_PI / 2 + (float)i / 12.0f * 2.0f * (float)M_PI;
        theme_place(F->num[i], THEME_CENTER, CX + lroundf(cosf(a) * NUM_R), CY + lroundf(sinf(a) * NUM_R) + NUM_MID);
    }
    for (int i = 0; i < BIG_CELLS; i++)   /* 500 76px FS */
        F->cell[i] = theme_label(F->awake, &font_plex_sans_500_76, THEME_INK, LV_OPA_COVER, 0);
    F->small = theme_label(F->awake, &font_plex_mono_500_26, THEME_INK_DIM, LV_OPA_COVER, 0);   /* 500 26px FM */
    for (int i = 0; i < 2; i++)
        F->label[i] = theme_label(F->awake, &font_cormorant_700_13, THEME_BRASS, LV_OPA_COVER, LABEL_LS);
    F->dot = theme_box(F->awake, 0, 0, 2, 2);
    lv_obj_set_style_radius(F->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(F->dot, lv_color_hex(THEME_BRASS), 0);
    lv_obj_set_style_bg_opa(F->dot, LV_OPA_COVER, 0);
    lv_obj_add_flag(F->dot, LV_OBJ_FLAG_HIDDEN);
    F->hint = theme_label(F->awake, &font_plex_sans_500_11, THEME_INK_FAINT, LV_OPA_COVER, 1);   /* 500 10.5px FS, 1.2 */
    F->wall = theme_label(F->awake, &font_plex_mono_500_13, THEME_INK_DIM, LV_OPA_COVER, 0);     /* 500 13px FM */
    F->hold = timer_arc_create(F->awake, HOLD_R, HOLD_W, THEME_BRASS_HI, LV_OPA_COVER, true, 1000);
    lv_obj_add_flag(F->hold, LV_OBJ_FLAG_HIDDEN);

    F->glance = glance_create(parent);
    F->timer = lv_timer_create(tick, CAD_SLOW, NULL);
    F->period = CAD_SLOW;
    if (F->timer) lv_timer_pause(F->timer);
    const int64_t t1 = esp_timer_get_time();
    render(tmr_svc_now_ms());   /* the whole ring once */
    ESP_LOGI(TAG, "first render %u us", (unsigned)(esp_timer_get_time() - t1));
}

static void face_destroy(void)
{
    if (F == NULL) return;
    if (F->timer) lv_timer_delete(F->timer);
    lv_obj_delete(F->awake);   /* before the box it shows */
    theme_buf_destroy(F->box);
    heap_caps_free(F->pulse_mem);
    glance_free(F->glance);
    heap_caps_free(F);
    F = NULL;
}

/* ------------------------------------------------------------ hooks */
static void face_wake(bool reveal)
{
    if (F == NULL) return;
    F->running = true;
    tmr_svc_face_awake(true);   /* may step to DONE (an alarm's wake), before the frame below */
    const uint32_t now = tmr_svc_now_ms();
    /* Task 4 m1: an alarm never plays a reveal, awake or asleep; the alarm face shows at once */
    F->fading = reveal && tmr_svc()->mode != OMNI_TMR_DONE;
    F->fade_t0 = now;
    opa_apply(F->fading ? LV_OPA_TRANSP : LV_OPA_COVER);
    F->pressed = false;
    F->hold_val = -2;   /* re-applied below */
    hold_update(now);
    render(now);
    if (F->timer) {
        lv_timer_resume(F->timer);
        F->period = 0;
        period_sync();
    }
}

static void face_sleep(void)
{
    if (F == NULL) return;
    F->running = false;
    if (F->timer) lv_timer_pause(F->timer);
    omni_tmr_t *t = tmr_svc();
    if (t->mode == OMNI_TMR_SET) {   /* a wind cut by sleep: back to before it (Task 1 m2) */
        omni_tmr_wind_cancel(t);
        tmr_svc_changed();
    }
    F->drag.phase = OMNI_TMR_DRAG_OFF;
    F->fading = false;
    opa_apply(LV_OPA_COVER);
    F->hold_val = -1;
    lv_obj_add_flag(F->hold, LV_OBJ_FLAG_HIDDEN);
    tmr_svc_face_awake(false);
}

/* INPUT task, no lock: the mode's copy and constant geometry only. */
static bool face_claim(int16_t x0, int16_t y0)
{
    const omni_tmr_t t = {.mode = tmr_svc_mode()};
    return omni_tmr_claims(&t, x0, y0);
}

/* Event loop, LVGL lock: a long press here resets (no picker), asked before the gesture hook sees it (F1). */
static bool face_holds(int16_t x, int16_t y) { return omni_tmr_holds(tmr_svc(), x, y); }

static bool centre(int16_t x, int16_t y)
{
    const int dx = x - CX, dy = y - CY;
    return dx * dx + dy * dy < OMNI_TMR_RIM_R * OMNI_TMR_RIM_R;
}

/* A rim drag that ended as the shell's swipe (left/right, or down from the top): posted through the event loop like a
 * touch (nav, pips, quick settings, R27). A full queue is logged once, by omni_events_post. */
static void swipe_post(omni_gesture_kind_t kind, const omni_gesture_t *g, uint32_t t_ms)
{
    omni_event_t e = {.kind = EV_GESTURE, .t_ms = t_ms, .g = *g};
    e.g.kind = kind;
    (void)omni_events_post(&e);
}

static void face_gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    if (F == NULL || !F->running) return;
    omni_tmr_t *t = tmr_svc();
    const omni_tmr_mode_t was = t->mode;
    F->gesture_ms = tmr_svc_now_ms();
    switch (g->kind) {
    case G_DRAG_START:   /* claimed: it started on the rim (the mode may have moved on since: the jog re-checks) */
    case G_DRAG_MOVE:
    case G_DRAG_END: {   /* a jog winds (ARMED or PAUSED at the lift, never COUNT: R135); a flick is the shell's */
        F->dragged = true;
        const omni_gesture_kind_t swipe = omni_tmr_drag(&F->drag, t, g);
        if (swipe != G_NONE) swipe_post(swipe, g, t_ms);
        break;
    }
    case G_TAP:   /* the centre taps; in DONE a tap anywhere dismisses; a rim tap does nothing (D5) */
        if (t->mode == OMNI_TMR_DONE || centre(g->x0, g->y0)) omni_tmr_tap(t, t_ms);
        break;
    case G_LONG_PRESS:
        if (omni_tmr_holds(t, g->x0, g->y0)) omni_tmr_reset(t);
        break;
    default: break;   /* swipes navigate */
    }
    if (g->kind != G_DRAG_MOVE || t->mode != was) tmr_svc_changed();
    const uint32_t now = tmr_svc_now_ms();
    hold_update(now);
    if (g->kind != G_DRAG_MOVE) render(now);   /* moves render at the 33 ms cadence */
    period_sync();
}

/* ------------------------------------------------------------ update */
static void face_update(int64_t now, bool glance)
{
    if (F == NULL) return;
    if (glance) {
        lv_obj_add_flag(F->awake, LV_OBJ_FLAG_HIDDEN);
        glance_show(F->glance, true);
        glance_update(F->glance, now, settings()->h24, NULL);
        return;
    }
    glance_show(F->glance, false);
    lv_obj_remove_flag(F->awake, LV_OBJ_FLAG_HIDDEN);
    render(tmr_svc_now_ms());
}

const omni_face_vt_t FACE_TIMER_VT = {
    .name = "timer",
    .create = face_create,
    .update = face_update,
    .destroy = face_destroy,
    .wake = face_wake,
    .sleep = face_sleep,
    .claim = face_claim,
    .gesture = face_gesture,
    .holds = face_holds,
};
