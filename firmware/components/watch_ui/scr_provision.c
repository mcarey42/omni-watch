/* SCR_PROVISION: design/mockups/faces-setup.js `wifi-qr` (frame(), drawQRPlate(), the glyphs) for the QR and
 * the steps; the error states have no mockup and follow the same frame (spec 13): bezel, lenses, ring, a glyph,
 * a Cormorant heading, Plex lines and one 46 px pill.
 *
 *   PROV_WAITING  QR plate (brass frame, warm-white plate, 7 px modules + 3-module quiet zone), "Scan to set up",
 *                 the app/BLE name line. Dims to 40 % when idle 60 s (app_main); orbits +-1 px every 30 s.
 *   PROV_PHONE    step 2: phone -> watch link, "Phone linked".
 *   JOINING       step 3: wifi glyph, "Joining", the network name. SYNCING adds "Setting the time".
 *   OK            green ring, check, the time, "Time set ✓"; posts UI_POP after 2 s (tap continues at once).
 *   ERR_*         red lens fast-blinks. In the QR flow the ring retracts to step 2 and "Scan again" returns
 *                 to the QR (the phone can also retry); after a background sync: the network name, the clock
 *                 marked stale (or "Time not set"), when it retries, and "OK". Only after the saved password
 *                 was refused twice in a row (AUTH_FAIL) is "Forget and re-scan" offered instead; nothing on
 *                 this screen erases the saved network otherwise. That pill acts only on a 1.5 s hold (I4, C2:
 *                 omni_forget_hold_step, the claim asking for G_DOWN / G_HOLD / G_LIFT_STILL only while it shows),
 *                 its brass ring filling meanwhile; a tap does nothing.
 *   ERR_NO_MEM    the Wi-Fi driver did not start (internal RAM): "Wi-Fi did not start".
 * "Type it instead" (the pill on the QR, spec 7.1, B1) posts UI_PROV_TO_LIST: the event loop ends the session and puts
 * the network list in this screen's place. It shows only when the keyboard's assets load and the session can
 * stop now (omni_prov_type_instead_ok: never while joining, I5). A phone session's failure also says whether the
 * saved network was kept (spec 7.6, stepart_copy). */
#include "scr_provision.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "qrcode.h"
#include "esp_log.h"
#include "theme.h"
#include "stepart.h"
#include "store.h"
#include "omni_events.h"
#include "omni/cities.h"
#include "omni/timefmt.h"
#include "esp_timer.h"
#include "assets.h"
#include "scr_keyboard.h"
#include "board.h"
#include "omni/join.h"
#include "debug_console.h"

#define CX THEME_CX
#define CY THEME_CY
#define TAU (2 * (float)M_PI)
#define LENS_R 202           /* lenses and step ticks */
#define RING_R STEPART_RING_R /* progress ring */
#define QR_MOD 7             /* px per module */
#define QR_QUIET 3           /* modules */
#define QR_PLATE ((33 + 2 * QR_QUIET) * QR_MOD) /* 273: version 4 at 7 px; larger versions get smaller modules */
#define ANIM_MS 50
#define DONE_MS 2000         /* "Time set" shows this long, then back to the face */
#define ORBIT_S 30
#define DIM_OPA LV_OPA_40
#define PILL_X 149           /* faces-setup.js typeBtn */
#define PILL_Y 396
#define PILL_W 168
#define PILL_H 46
#define RING_BOX PILL_X - 4, PILL_Y - 4, PILL_X + PILL_W + 4, PILL_Y + PILL_H + 4
#define HOLD_WANT (OMNI_GW_DOWN | OMNI_GW_HOLD | OMNI_GW_LIFT_STILL)

static scr_prov_info_t s_info = {.state = NET_PROV_WAITING, .prov = true};

static struct {
    lv_obj_t *root, *draw, *red, *green;
    lv_obj_t *qr, *canvas, *title, *caption;
    lv_obj_t *heading, *line1, *line2, *faint, *time, *date, *step_lbl, *pill, *pill_text;
    lv_draw_buf_t *qr_buf;
    lv_timer_t *anim, *done;
    omni_net_state_t shown;  /* state the layout was built for */
    bool shown_prov, shown_forget, shown_type;
    uint32_t step_t;         /* lv_tick when the step changed */
    int step, prev_step;     /* 0..3, -1 = no step ticks */
    int pill_action;         /* UI_* for the pill, 0 = hidden */
    int orbit;
    omni_forget_hold_t hold; /* "Forget and re-scan" (I4): the hold, the only path to UI_FORGET_RESCAN */
    uint32_t hold_t;         /* lv_tick when it armed (the ring's fill) */
    bool hold_on;            /* the pill shows it: the claim asks for the hold's gestures */
} S;

static bool is_err(omni_net_state_t st)
{
    return st == NET_ERR_PASSWORD || st == NET_ERR_NO_AP || st == NET_ERR_SYNC || st == NET_ERR_NO_MEM;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float ease_out(float t) { t = clampf(t, 0, 1); return 1 - powf(1 - t, 3); }
/* Canvas radians -> LVGL arc degrees (both clockwise from 3 o'clock); LVGL wants 0..359. */
static int32_t deg(float rad)
{
    int32_t d = (int32_t)lroundf(rad * 180 / (float)M_PI) % 360;
    return d < 0 ? d + 360 : d;
}

static bool post_ui(int code)
{
    omni_event_t e = {.kind = EV_UI, .t_ms = (uint32_t)(esp_timer_get_time() / 1000), .ui = code};
    return omni_events_post(&e);
}

static void put(lv_obj_t *l, const char *text, int32_t baseline)
{
    if (text == NULL || text[0] == '\0') {
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(l, text);
    theme_place(l, THEME_CENTER, CX, baseline);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------ QR plate */
static void qr_paint(esp_qrcode_handle_t q)
{
    int n = esp_qrcode_get_size(q), mod = QR_MOD;
    if ((n + 2 * QR_QUIET) * mod > QR_PLATE) mod = QR_PLATE / (n + 2 * QR_QUIET); /* longer payloads: smaller modules */
    int side = n * mod;
    S.qr_buf = lv_draw_buf_create(side, side, LV_COLOR_FORMAT_RGB565, 0);
    if (S.qr_buf == NULL) return;
    const uint16_t white = lv_color_to_u16(lv_color_hex(THEME_PLATE));
    for (int y = 0; y < side; y++) {
        uint16_t *row = (uint16_t *)(S.qr_buf->data + y * S.qr_buf->header.stride);
        for (int x = 0; x < side; x++) row[x] = esp_qrcode_get_module(q, x / mod, y / mod) ? 0x0000 : white;
    }
}

static void qr_canvas(void);

static void qr_create(lv_obj_t *parent)
{
    const int32_t x0 = CX - QR_PLATE / 2 - 1; /* -floor(full / 2) around the centre */
    S.qr = theme_box(parent, 0, 0, THEME_W, THEME_W);

    lv_obj_t *frame = theme_box(S.qr, x0 - 5, x0 - 5, QR_PLATE + 10, QR_PLATE + 10);
    lv_obj_set_style_radius(frame, 14, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    /* fr: #e6c886 -> #8d6c34 -> #4a3518 diagonally; LVGL here has 2-stop simple gradients: vertical, ends only */
    lv_obj_set_style_bg_color(frame, lv_color_hex(0xe6c886), 0);
    lv_obj_set_style_bg_grad_color(frame, lv_color_hex(0x4a3518), 0);
    lv_obj_set_style_bg_grad_dir(frame, LV_GRAD_DIR_VER, 0);

    lv_obj_t *plate = theme_box(S.qr, x0, x0, QR_PLATE, QR_PLATE);
    lv_obj_set_style_radius(plate, 10, 0);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(plate, lv_color_hex(THEME_PLATE), 0);

    qr_canvas();
    S.title = theme_label(S.qr, &font_cormorant_700_26, THEME_SETUP_HEAD, LV_OPA_COVER, 0); /* 700 26px FD */
    put(S.title, "Scan to set up", 76);
    S.caption = theme_label(S.qr, &font_plex_mono_400_11, THEME_INK_DIM, LV_OPA_COVER, 0); /* 400 11px FM */
    char cap[64];
    snprintf(cap, sizeof cap, "ESP BLE Provisioning app · %s", s_info.name);
    put(S.caption, cap, 386);
}

/* ------------------------------------------------------------ custom drawing: step ticks, ring, glyphs */
static void dashed(lv_layer_t *layer, int32_t x0, int32_t x1, int32_t y, uint32_t t)
{
    /* setLineDash([3, 5]) with lineDashOffset -T/60 */
    int off = (int)(t / 60) % 8;
    for (int x = x0 - 8 + off; x < x1; x += 8) {
        int a = x < x0 ? x0 : x, b = x + 3 > x1 ? x1 : x + 3;
        if (b > a) theme_draw_line(layer, a, y, b, y, 2, THEME_BRASS, LV_OPA_COVER, false);
    }
}

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t)
{
    int r = (int)lroundf(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
    int g = (int)lroundf(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
    int bl = (int)lroundf((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
    return (uint32_t)(r << 16 | g << 8 | bl);
}

static void ring(lv_layer_t *layer, uint32_t t)
{
    omni_net_state_t st = s_info.state;
    theme_draw_arc(layer, CX, CY, RING_R + 1, 3, 0, 360, THEME_SETUP_TRACK, LV_OPA_COVER, false);
    if (S.step < 0) return; /* a background sync's error: the track only */
    bool done = S.step == 3;
    float from = (float)(S.prev_step < 0 ? 0 : S.prev_step) / 3, to = (float)S.step / 3;
    float p = from + (to - from) * ease_out((float)(t - S.step_t) / 500);
    float a0 = -(float)M_PI / 2, a1 = a0 + TAU * p;
    /* progress: linear gradient #f0d595 -> #8d6c34 across the ring's box, in 4-degree pieces; green when done */
    for (float a = a0; a < a1 - 0.001f; a += 4 * (float)M_PI / 180) {
        float b = fminf(a + 4 * (float)M_PI / 180, a1), m = (a + b) / 2;
        float gt = clampf((cosf(m) + sinf(m) + 2) / 4, 0, 1);
        uint32_t c = done ? THEME_GREEN : lerp_rgb(THEME_SETUP_BRASS_HI, 0x8d6c34, gt);
        theme_draw_arc(layer, CX, CY, RING_R + 1, 3, deg(a), deg(b + 0.02f), c, LV_OPA_COVER, false);
    }
    for (int i = 1; i <= 3; i++) { /* station marks (the first has radius 0 in the mockup) */
        float a = a0 + TAU * i / 3 * (i == 3 ? 0.999f : 1);
        int32_t x = CX + (int32_t)lroundf(cosf(a) * RING_R), y = CY + (int32_t)lroundf(sinf(a) * RING_R);
        uint32_t c = i <= S.step ? (done ? THEME_GREEN : THEME_SETUP_BRASS_HI) : THEME_PIP_OFF;
        theme_draw_box(layer, x - 4, y - 4, x + 4, y + 4, LV_RADIUS_CIRCLE, c, LV_OPA_COVER, 0, 0);
    }
    if (st == NET_JOINING || st == NET_SYNCING) stepart_ring(layer, t, THEME_SETUP_BRASS_HI); /* the working comet */
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    uint32_t t = lv_tick_get();
    omni_net_state_t st = s_info.state;
    if (S.step >= 0) { /* four step ticks between the lenses */
        for (int i = 0; i < 4; i++) {
            float a = -(float)M_PI / 2 + (i - 1.5f) * 0.19f;
            int32_t x = CX + (int32_t)lroundf(cosf(a) * LENS_R), y = CY + (int32_t)lroundf(sinf(a) * LENS_R);
            bool done = i < S.step || S.step == 3, cur = i == S.step && S.step < 3;
            int32_t r = cur ? 4 : 3;
            uint32_t c = done ? THEME_BRASS : cur ? (is_err(st) ? THEME_RED : THEME_SETUP_BRASS_HI) : THEME_SETUP_STEP_OFF;
            lv_opa_t o = cur ? (lv_opa_t)((0.5f + 0.5f * sinf((float)t / 200)) * 255) : LV_OPA_COVER;
            theme_draw_box(layer, x - r, y - r, x + r, y + r, LV_RADIUS_CIRCLE, c, o, 0, 0);
        }
    }
    if (st == NET_PROV_WAITING) {
        ui_stack_note("prov/qr");   /* console builds: the LVGL task's stack after this draw (R230) */
        return;
    }
    ring(layer, t);
    switch (st) {
    case NET_PROV_PHONE:
        stepart_phone(layer, CX - 30, 166);
        dashed(layer, CX - 12, CX + 18, 166, t);
        theme_draw_arc(layer, CX + 36, 166, 15, 2, 0, 360, THEME_SETUP_BRASS_HI, LV_OPA_COVER, false);
        break;
    case NET_JOINING:
    case NET_SYNCING: stepart_wifi(layer, CX, 160, THEME_SETUP_BRASS_HI, true, t); break;
    case NET_OK: stepart_check(layer, CX, 136, clampf((float)(t - S.step_t) / 900, 0, 1)); break;
    case NET_ERR_SYNC: stepart_wifi(layer, CX, 160, THEME_BRASS, false, 0); break; /* joined; the time server failed */
    case NET_ERR_PASSWORD:
    case NET_ERR_NO_MEM:
    case NET_ERR_NO_AP: /* not joined: a faint wifi mark struck through in red */
        stepart_wifi(layer, CX, 160, THEME_SETUP_FAINT, false, 0);
        theme_draw_line(layer, CX - 22, 150, CX + 22, 190, 3, THEME_RED, LV_OPA_COVER, true);
        break;
    default: break;
    }
    if (S.hold_on) {   /* the hold's ring round the pill: a faint track, brassHi filling over 1.5 s while held */
        theme_draw_pill_ring(layer, RING_BOX, 1, 2, THEME_BRASS, THEME_OPA(0.16));
        const int32_t age = (int32_t)(t - S.hold_t);
        if (S.hold.armed && age > 0)
            theme_draw_pill_ring(layer, RING_BOX, age >= OMNI_FORGET_HOLD_MS ? 1 : (float)age / OMNI_FORGET_HOLD_MS, 3,
                                 THEME_SETUP_BRASS_HI, LV_OPA_COVER);
    }
    /* console builds: the LVGL task's stack after this draw (R230). The draw runs on every refresh (S.draw is the whole
     * screen), so a low reached in the screen's other objects (the lenses, the QR plate) is filed by the next frame */
    ui_stack_note(st == NET_PROV_PHONE ? "prov/phone" : st == NET_JOINING || st == NET_SYNCING ? "prov/join"
                  : st == NET_OK ? "prov/ok" : "prov/error");
}

static void inval(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_area_t a = {x1, y1, x2, y2};
    lv_obj_invalidate_area(S.draw, &a);
}

/* Redraw only what moves: a full-screen redraw at 20 fps (bezel image included) starves the other tasks. */
static void anim_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t t = lv_tick_get(), since = t - S.step_t;
    omni_net_state_t st = s_info.state;
    if (since < 600) { /* the ring is moving to the new step */
        lv_obj_invalidate(S.draw);
        return;
    }
    if (S.step >= 0 && S.step < 3) inval(CX - 64, 22, CX + 64, 46); /* the blinking step tick */
    if (S.hold.armed) inval(PILL_X - 8, PILL_Y - 8, PILL_X + PILL_W + 8, PILL_Y + PILL_H + 8); /* the hold's ring */
    switch (st) {
    case NET_PROV_PHONE: inval(CX - 14, 162, CX + 20, 170); break;
    case NET_JOINING:
    case NET_SYNCING: {
        inval(CX - 36, 136, CX + 36, 180); /* wifi arcs */
        float ca = fmodf((float)t / 900, 1) * TAU; /* comet: this frame's and the last frame's span */
        int32_t x1 = THEME_W, y1 = THEME_W, x2 = 0, y2 = 0;
        for (float a = ca - 1.0f; a <= ca + 0.01f; a += 0.1f) {
            int32_t x = CX + (int32_t)lroundf(cosf(a) * RING_R), y = CY + (int32_t)lroundf(sinf(a) * RING_R);
            x1 = x < x1 ? x : x1, y1 = y < y1 ? y : y1, x2 = x > x2 ? x : x2, y2 = y > y2 ? y : y2;
        }
        inval(x1 - 6, y1 - 6, x2 + 6, y2 + 6);
        break;
    }
    case NET_OK:
        if (since < 1300) inval(CX - 34, 104, CX + 34, 168); /* the check drawing itself */
        break;
    default: break;
    }
}

static void done_cb(lv_timer_t *t)
{
    (void)t;
    S.done = NULL;
    post_ui(UI_POP);
}

/* ------------------------------------------------------------ lenses */
static void lens_anim(lv_obj_t *jewel, int32_t from, int32_t to, uint32_t ms, bool repeat)
{
    lv_anim_delete(jewel, theme_lens_lit);
    if (ms == 0) {
        theme_lens_lit(jewel, to);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, jewel);
    lv_anim_set_exec_cb(&a, theme_lens_lit);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    if (repeat) {
        lv_anim_set_reverse_duration(&a, ms);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    }
    lv_anim_start(&a);
}

/* ------------------------------------------------------------ layout per state */
static int step_of(omni_net_state_t st, bool prov)
{
    switch (st) {
    case NET_PROV_WAITING: return 0;
    case NET_PROV_PHONE: return 1;
    case NET_JOINING:
    case NET_SYNCING: return 2;
    case NET_OK: return 3;
    case NET_ERR_NO_MEM: return -1; /* the session never started */
    default: return prov ? 1 : -1; /* a QR-flow error retracts to "phone linked" */
    }
}

static void retry_text(char *out, size_t n, int64_t now)
{
    char age[8];
    omni_fmt_age(s_info.next_try - now, age);
    if (strcmp(age, "now") == 0) snprintf(out, n, "retrying now");
    else snprintf(out, n, "retrying in %s", age);
}

/* The clock as the watch still has it, marked stale, or "Time not set". */
static void stale_text(char *out, size_t n, int64_t now)
{
    if (!omni_time_is_set(now) || s_info.last_ok == 0) {
        snprintf(out, n, "Time not set");
        return;
    }
    char hhmm[6], ampm[3], age[8];
    omni_fmt_clock(now, settings()->h24, hhmm, ampm);
    omni_fmt_age(now - s_info.last_ok, age);
    snprintf(out, n, "%s%s%s · stale, set %s%s", hhmm, ampm[0] ? " " : "", ampm, strcmp(age, "now") ? age : "just now",
             strcmp(age, "now") ? " ago" : "");
}

static void time_text(int64_t now)
{
    char hhmm[6], ampm[3], buf[48];
    omni_fmt_clock(now, settings()->h24, hhmm, ampm);
    put(S.time, hhmm, 245);
    time_t tt = (time_t)now;
    struct tm tm;
    localtime_r(&tt, &tm);
    snprintf(buf, sizeof buf, "%s %d %s%s%s", omni_day_name(tm.tm_wday), tm.tm_mday, omni_month_name(tm.tm_mon), ampm[0] ? " · " : "", ampm);
    put(S.date, buf, 274);
}

/* Texts that change with the clock: the time on OK; on an error the retry countdown, and after a background
 * sync the clock marked stale. */
static void live_text(int64_t now)
{
    char buf[64], retry[24];
    omni_net_state_t st = s_info.state;
    if (st == NET_OK) {
        time_text(now);
        return;
    }
    if (!is_err(st)) return;
    retry_text(retry, sizeof retry, now);
    if (s_info.prov) {
        if (st == NET_ERR_SYNC) put(S.faint, retry, 318); /* provisioned now: it retries on its own */
        return;
    }
    snprintf(buf, sizeof buf, "%s · %s",
             st == NET_ERR_PASSWORD ? "password refused"
             : st == NET_ERR_NO_AP  ? "not in range"
             : st == NET_ERR_NO_MEM ? "low memory"
                                    : "no answer from pool.ntp.org",
             retry);
    put(S.line2, buf, 290);
    stale_text(buf, sizeof buf, now);
    put(S.faint, buf, 318);
}

/* "Type it instead" needs the keyboard's assets (the layout, the fonts, the icons: scr_keyboard_ready, loaded once). */
static bool type_instead(omni_net_state_t st)
{
    return st == NET_PROV_WAITING && scr_keyboard_ready() && omni_prov_type_instead_ok(net_prov_phase());
}

/* Input task, no LVGL lock: while the hold is offered every slide is the screen's (G_DRAG_START), so a press that slides
 * off disarms the hold at once and its ring clears (review I1); fixed state only. */
static bool hold_slide(int16_t x0, int16_t y0, void *ctx)
{
    (void)x0, (void)y0, (void)ctx;
    return true;
}

/* The claim asks for the hold's gestures (and claims slides) only while "Forget and re-scan" shows, and nothing
 * otherwise (C2). */
static void hold_claim(bool on)
{
    omni_forget_hold_reset(&S.hold);
    if (on != S.hold_on) {
        if (on) board_set_touch_claim_w(hold_slide, NULL, HOLD_WANT);
        else board_set_touch_claim(NULL, NULL);
    }
    S.hold_on = on;
}

static void pill_update(omni_net_state_t st)
{
    hold_claim(S.pill_action == UI_FORGET_RESCAN);
    if (S.pill_action) {
        lv_label_set_text(S.pill_text, S.pill_action == UI_RESCAN          ? (st == NET_ERR_NO_MEM ? "Try again" : "Scan again")
                                       : S.pill_action == UI_FORGET_RESCAN ? "Forget and re-scan"
                                       : S.pill_action == UI_PROV_TO_LIST  ? "Type it instead"
                                                                           : "OK");
        /* typeBtn: the label's centre 104 px in, right of the keyboard glyph */
        lv_obj_align(S.pill_text, LV_ALIGN_CENTER, S.pill_action == UI_PROV_TO_LIST ? 104 - PILL_W / 2 : 0, 0);
        lv_obj_remove_flag(S.pill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(S.pill, LV_OBJ_FLAG_HIDDEN);
    }
}

static void layout(void)
{
    omni_net_state_t st = s_info.state;
    bool prov = s_info.prov, changed = st != S.shown || prov != S.shown_prov || s_info.offer_forget != S.shown_forget;
    const bool type = type_instead(st);
    if (!changed) {   /* the session's phase can change under the same state (the QR, then the session's start) */
        if (type != S.shown_type) {
            S.shown_type = type;
            S.pill_action = type ? UI_PROV_TO_LIST : 0;
            pill_update(st);
        }
        return;
    }
    S.shown_type = type;
    int64_t now = (int64_t)time(NULL);
    S.shown = st;
    S.shown_prov = prov;
    S.shown_forget = s_info.offer_forget;
    S.prev_step = S.step;
    S.step = step_of(st, prov);
    S.step_t = lv_tick_get();

    if (st == NET_PROV_WAITING) lv_obj_remove_flag(S.qr, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(S.qr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *const texts[] = {S.heading, S.line1, S.line2, S.faint, S.time, S.date, S.step_lbl};
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++) lv_obj_add_flag(texts[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(S.line1, &font_plex_sans_400_14, 0);
    lv_obj_set_style_text_color(S.line1, lv_color_hex(THEME_INK_DIM), 0);
    lv_obj_set_style_text_color(S.faint, lv_color_hex(THEME_SETUP_FAINT), 0);
    S.pill_action = 0;

    char buf[64], quoted[48];
    snprintf(quoted, sizeof quoted, "“%s”", s_info.ssid);
    const char *ssid = s_info.ssid[0] ? quoted : NULL;
    stepart_copy_t c; /* the phone path's heading and lines (stepart.c) */
    stepart_copy(st, true, s_info.had_saved, &c);   /* the snapshot restore keeps it (spec 7.4) */
    switch (st) {
    case NET_PROV_WAITING: S.pill_action = type ? UI_PROV_TO_LIST : 0; break;
    case NET_PROV_PHONE:
        put(S.heading, c.head, 238);
        put(S.line1, c.line1, 266);
        put(S.line2, c.line2, 285);
        snprintf(buf, sizeof buf, "BLE · %s", s_info.name);
        put(S.faint, buf, 318);
        break;
    case NET_JOINING:
    case NET_SYNCING:
        put(S.heading, c.head, 238);
        lv_obj_set_style_text_font(S.line1, &font_plex_sans_500_16, 0); /* 500 16px FS, ink */
        lv_obj_set_style_text_color(S.line1, lv_color_hex(THEME_INK), 0);
        put(S.line1, ssid, 266);
        put(S.line2, st == NET_SYNCING || prov ? c.line1 : "saved network", 290);
        break;
    case NET_OK: {
        const omni_city_t *c = omni_city_at(settings()->city);
        snprintf(buf, sizeof buf, "Time set ✓ · pool.ntp.org · %s", c ? c->name : "");
        put(S.faint, buf, 300);
        break;
    }
    case NET_ERR_PASSWORD:
    case NET_ERR_NO_AP:
    case NET_ERR_SYNC:
    case NET_ERR_NO_MEM:
        put(S.heading, prov || st != NET_ERR_NO_AP ? c.head : "No network", 238);
        lv_obj_set_style_text_font(S.line1, &font_plex_sans_500_16, 0);
        lv_obj_set_style_text_color(S.line1, lv_color_hex(THEME_INK), 0);
        put(S.line1, ssid, 266);
        if (prov) put(S.line2, c.line1, 290);
        if (prov && c.line2) { /* spec 7.6: whether the saved network was kept */
            lv_obj_set_style_text_color(S.faint, lv_color_hex(THEME_INK_DIM), 0);
            put(S.faint, c.line2, 318);
        }
        if (!prov) lv_obj_set_style_text_color(S.faint, lv_color_hex(THEME_RED), 0); /* the stale clock */
        S.pill_action = prov ? (st == NET_ERR_SYNC ? UI_POP : UI_RESCAN)
                             : (st == NET_ERR_PASSWORD && s_info.offer_forget ? UI_FORGET_RESCAN : UI_POP);
        if (S.pill_action == UI_FORGET_RESCAN) {   /* no step line here (step -1): the hold's hint takes its place */
            lv_obj_set_style_text_color(S.step_lbl, lv_color_hex(THEME_SETUP_FAINT), 0);
            put(S.step_lbl, "HOLD TO FORGET", 370);
        }
        break;
    default: break;
    }
    if (S.step >= 0 && st != NET_PROV_WAITING) {
        snprintf(buf, sizeof buf, st == NET_OK ? "TAP TO CONTINUE" : "STEP %d OF 4", S.step + 1);
        lv_obj_set_style_text_color(S.step_lbl, lv_color_hex(st == NET_OK ? THEME_BRASS : THEME_SETUP_FAINT), 0);
        put(S.step_lbl, buf, 370);
    }
    live_text(now);

    pill_update(st);

    /* lenses: red blinks while not fixed (fast on an error), green lights when the time is set */
    if (st == NET_OK) {
        lens_anim(S.red, 0, 0, 0, false);
        lens_anim(S.green, 0, 1000, 420, false);
    } else {
        lens_anim(S.red, 250, 1000, is_err(st) ? 300 : 1200, true);
        lens_anim(S.green, 0, 0, 0, false);
    }

    if (S.done) {
        lv_timer_delete(S.done);
        S.done = NULL;
    }
    if (st == NET_OK) {
        S.done = lv_timer_create(done_cb, DONE_MS, NULL);
        lv_timer_set_repeat_count(S.done, 1);
    }
    lv_obj_invalidate(S.draw);
}

/* typeBtn's little keyboard: a 22 x 15 outline, two rows of four keys, a space bar. */
static void pill_glyph(lv_event_t *e)
{
    if (S.pill_action != UI_PROV_TO_LIST) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    d.bg_opa = LV_OPA_TRANSP;
    d.border_width = 1;
    d.border_color = lv_color_hex(THEME_BRASS);
    d.border_opa = LV_OPA_COVER;
    lv_area_t a = {PILL_X + 18, PILL_Y + 15, PILL_X + 39, PILL_Y + 29};
    lv_draw_rect(layer, &d, &a);
    for (int k = 0; k < 8; k++) {
        int32_t x = PILL_X + 21 + (int32_t)lroundf((k & 3) * 4.6f), y = PILL_Y + 18 + (k >> 2) * 4;
        theme_draw_box(layer, x, y, x + 1, y + 1, 0, THEME_BRASS, LV_OPA_COVER, 0, 0);
    }
    theme_draw_box(layer, PILL_X + 23, PILL_Y + 26, PILL_X + 34, PILL_Y + 27, 0, THEME_BRASS, LV_OPA_COVER, 0, 0);
}

/* ------------------------------------------------------------ screen interface */
static lv_obj_t *text(const lv_font_t *f, uint32_t rgb, int32_t ls)
{
    lv_obj_t *l = theme_label(S.root, f, rgb, LV_OPA_COVER, ls);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

static void create(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    S.shown = (omni_net_state_t)-1;
    S.step = -1;
    S.root = theme_box(parent, 0, 0, THEME_W, THEME_W);
    theme_bezel(S.root);
    const float ar = -(float)M_PI / 2 - 0.52f, ag = -(float)M_PI / 2 + 0.52f;
    S.red = theme_lens(S.root, CX + (int32_t)lroundf(cosf(ar) * LENS_R), CY + (int32_t)lroundf(sinf(ar) * LENS_R), false);
    S.green = theme_lens(S.root, CX + (int32_t)lroundf(cosf(ag) * LENS_R), CY + (int32_t)lroundf(sinf(ag) * LENS_R), true);
    S.draw = theme_box(S.root, 0, 0, THEME_W, THEME_W);
    lv_obj_add_event_cb(S.draw, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    qr_create(S.root);

    S.heading = text(&font_cormorant_700_30, THEME_SETUP_HEAD, 0);   /* 700 30px FD */
    S.line1 = text(&font_plex_sans_400_14, THEME_INK_DIM, 0);        /* 400 14px FS */
    S.line2 = text(&font_plex_sans_400_13, THEME_INK_DIM, 0);        /* 400 13px FS */
    S.faint = text(&font_plex_mono_400_11, THEME_SETUP_FAINT, 0);    /* 400 11px FM */
    S.time = text(&font_cormorant_700_70, THEME_SETUP_TIME, 0);      /* 700 70px FD */
    S.date = text(&font_plex_sans_400_14, THEME_INK_DIM, 0);
    S.step_lbl = text(&font_plex_sans_600_10, THEME_SETUP_FAINT, 2);     /* 600 10px FS, tracked 2.4 */

    S.pill = theme_box(S.root, PILL_X, PILL_Y, PILL_W, PILL_H);      /* key(): dark pill, brass hairline */
    lv_obj_set_style_radius(S.pill, PILL_H / 2, 0);
    lv_obj_set_style_bg_opa(S.pill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(S.pill, lv_color_hex(0x12100c), 0);
    lv_obj_set_style_border_width(S.pill, 1, 0);
    lv_obj_set_style_border_color(S.pill, lv_color_hex(THEME_BRASS), 0);
    lv_obj_set_style_border_opa(S.pill, THEME_OPA(0.38), 0);
    S.pill_text = theme_label(S.pill, &font_plex_sans_500_14, THEME_INK, LV_OPA_COVER, 0); /* 500 14px FS */
    lv_obj_add_event_cb(S.pill, pill_glyph, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_flag(S.pill, LV_OBJ_FLAG_HIDDEN);

    S.anim = lv_timer_create(anim_cb, ANIM_MS, NULL);
    layout();
}

static void update(int64_t now)
{
    if (s_info.state == NET_PROV_WAITING) { /* burn-in: orbit the plate by 1 px every 30 s */
        static const int8_t O[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        int o = (int)((now / ORBIT_S) % 4);
        if (o != S.orbit) {
            S.orbit = o;
            lv_obj_set_pos(S.qr, O[o][0], O[o][1]);
        }
    }
    live_text(now);
}

static void destroy(void)
{
    if (S.hold_on) board_set_touch_claim(NULL, NULL);
    if (S.anim) lv_timer_delete(S.anim);
    if (S.done) lv_timer_delete(S.done);
    lv_anim_delete(S.red, theme_lens_lit);
    lv_anim_delete(S.green, theme_lens_lit);
    if (S.canvas) lv_obj_delete(S.canvas); /* before its buffer */
    if (S.qr_buf) lv_draw_buf_destroy(S.qr_buf);
    memset(&S, 0, sizeof S);
}

static void gesture(const omni_gesture_t *g, uint32_t t)
{
    (void)t;
    const bool on = S.pill_action && g->x >= PILL_X && g->x <= PILL_X + PILL_W && g->y >= PILL_Y &&
                    g->y <= PILL_Y + PILL_H; /* inclusive at the edge */
    if (S.hold_on) {   /* "Forget and re-scan": every gesture feeds the hold; no tap path (I4, C2) */
        const bool was = S.hold.armed;
        if (omni_forget_hold_step(&S.hold, g->kind, g->hold_ms, on)) {
            if (!post_ui(UI_FORGET_RESCAN)) {   /* dropped (queue full): a fresh hold may retry (review M4) */
                omni_forget_hold_reset(&S.hold);
                ESP_LOGW("prov", "forget-rescan dropped (queue full): hold again");
            }
        } else if (S.hold.armed && !was) {
            S.hold_t = lv_tick_get();
        }
        if (S.hold.armed != was) inval(PILL_X - 8, PILL_Y - 8, PILL_X + PILL_W + 8, PILL_Y + PILL_H + 8);
        return;
    }
    if (g->kind != G_TAP) return;
    if (s_info.state == NET_OK) {
        post_ui(UI_POP);
        return;
    }
    if (on && S.pill_action != UI_FORGET_RESCAN) post_ui(S.pill_action);   /* that one only by the hold, above */
}

static void dim(bool on) { lv_obj_set_style_opa(S.root, on ? DIM_OPA : LV_OPA_COVER, 0); }

static void qr_canvas(void)
{
    if (S.canvas) lv_obj_delete(S.canvas);
    if (S.qr_buf) lv_draw_buf_destroy(S.qr_buf);
    S.canvas = NULL;
    S.qr_buf = NULL;
    if (s_info.qr[0] == '\0') return; /* a new session's code is on its way: blank plate */
    esp_qrcode_config_t cfg = {
        .display_func = qr_paint, .max_qrcode_version = 10, .qrcode_ecc_level = ESP_QRCODE_ECC_LOW};
    /* R151: the PoP is never printed. espressif/qrcode logs the payload it encodes at INFO under "QRCODE" (the JSON
     * with the PoP); WARN keeps that tag's warnings and errors. Set once, before the first code is made
     * (CONFIG_LOG_DYNAMIC_LEVEL_CONTROL is on in both apps). */
    static bool quiet;
    if (!quiet) esp_log_level_set("QRCODE", ESP_LOG_WARN), quiet = true;
    esp_qrcode_generate(&cfg, s_info.qr);
    if (S.qr_buf == NULL) return;
    const int32_t x0 = CX - QR_PLATE / 2 - 1, side = S.qr_buf->header.w;
    S.canvas = lv_canvas_create(S.qr);
    lv_canvas_set_draw_buf(S.canvas, S.qr_buf);
    lv_obj_set_pos(S.canvas, x0 + (QR_PLATE - side) / 2, x0 + (QR_PLATE - side) / 2);
}

void scr_provision_set(const scr_prov_info_t *info)
{
    bool new_qr = strcmp(info->qr, s_info.qr) != 0;
    s_info = *info;
    if (S.root == NULL) return;
    if (new_qr) qr_canvas(); /* a new session's PoP */
    layout();
    live_text((int64_t)time(NULL)); /* e.g. the retry time, booked after the state arrived */
}

const omni_screen_vt_t SCR_PROVISION_VT = {
    .id = SCR_PROVISION, .create = create, .update = update, .destroy = destroy, .gesture = gesture, .dim = dim,
};
