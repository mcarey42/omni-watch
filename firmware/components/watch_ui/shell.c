#include "shell.h"
#include <math.h>
#include <time.h>
#include "theme.h"
#include "disp_direct.h"
#include "store.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "board.h"
#include "omni_events.h"
#include "omni/timefmt.h"
#include "watch_theme.h"

#define TICK_MS 100              /* face refresh check; the face updates when the second changes */
#define PIPS_MS 1000             /* spec: pip dots for 1 s */
#define PIP_Y (THEME_CY + 180)   /* 6 o'clock, inside the seconds rail */
#define PIP_STEP 10              /* faces-setup.js position pips */
#define LENS_R 202               /* faces-setup.js: lens ring radius, 11 o'clock */
#define LENS_A (-M_PI / 2 - 0.52)
#define LENS_BLINK_MS 1200       /* half period of sin(T / 380) */
#define LENS_STALE_LIT 350       /* sync stale: steady at lit 0.35 (R48/C16) */

static const omni_nav_t *s_nav;
static const omni_face_vt_t *s_faces[SHELL_FACE_MAX];
static const omni_face_vt_t *s_shown;
static const omni_screen_vt_t *s_screens[SCR__COUNT];   /* every id, SCR_THEME too (B7) */
static const omni_screen_vt_t *s_screen; /* shown over the face, NULL = the face */
static lv_obj_t *s_face_root, *s_screen_root, *s_pips, *s_lens, *s_jewel;
/* The theme's glance_override container (watch_theme.h): above the face, below the lens and pips; hidden unless the
 * override took the glance frame (s_glance_over). */
static lv_obj_t *s_glance_root;
static bool s_glance_over;
static lv_timer_t *s_pips_timer;
static bool s_glance;
static bool s_awake;             /* between shell_wake and shell_sleep / shell_set_glance(true) */
static bool s_running;           /* the face's wake hook ran and its sleep has not */
static bool s_revealed;          /* the shown face has had a wake since it was created */
static omni_lens_t s_lens_mode = OMNI_LENS_OFF;
/* The face whose claim may take a touch (the shown one, awake, no screen on top), else NULL. Read by the INPUT
 * task in face_claim without the LVGL lock: a single aligned pointer store. */
static const omni_face_vt_t *volatile s_claim_face;
static const char *TAG = "shell";
static int64_t s_last_sec = -1;
static shell_status_t s_status = {.battery_pct = -1};

static int64_t now_s(void) { return (int64_t)time(NULL); }

/* ------------------------------------------------------------ red lens (clock not set / sync stale) */
static void lens_create(lv_obj_t *parent)
{
    int32_t x = THEME_CX + (int32_t)lroundf(LENS_R * cosf(LENS_A)), y = THEME_CY + (int32_t)lroundf(LENS_R * sinf(LENS_A));
    s_jewel = theme_lens(parent, x, y, false);
    s_lens = lv_obj_get_parent(s_jewel);
    lv_obj_add_flag(s_lens, LV_OBJ_FLAG_HIDDEN);
}

/* the mockup's lit 0.25..1, sin(T / 380) */
static void lens_blink(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_jewel);
    lv_anim_set_exec_cb(&a, theme_lens_lit);
    lv_anim_set_values(&a, 250, 1000);
    lv_anim_set_duration(&a, LENS_BLINK_MS);
    lv_anim_set_reverse_duration(&a, LENS_BLINK_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* omni_sync_lens on the awake face, on every face: blinking while the clock is unset, steady and dim while the
 * time sync is stale, hidden otherwise. Hidden in the glance (it shows dashes), under screens (they draw their
 * own lenses) and over a face that shows its own (own_lens: the Omni dial's red lens). */
static void lens_update(int64_t now)
{
    omni_lens_t m = s_glance || s_screen != NULL || (s_shown && s_shown->own_lens)
                        ? OMNI_LENS_OFF : omni_sync_lens(now, &s_status.sync);
    if (m == s_lens_mode) return;
    s_lens_mode = m;
    lv_anim_delete(s_jewel, theme_lens_lit);
    if (m == OMNI_LENS_OFF) {
        lv_obj_add_flag(s_lens, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (m == OMNI_LENS_UNSET) lens_blink();
    else theme_lens_lit(s_jewel, LENS_STALE_LIT);
    lv_obj_remove_flag(s_lens, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------ face hosting */
/* Shows the theme's glance root in place of the face root (on) or the face root again (off). */
static void glance_over_set(bool on)
{
    if (on == s_glance_over) return;
    s_glance_over = on;
    if (on) {
        lv_obj_add_flag(s_face_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_glance_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_glance_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_face_root, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(int64_t now)
{
    s_last_sec = now;
    if (s_screen) {
        if (s_screen->update) s_screen->update(now);
    } else {
        if (s_shown && s_shown->update) s_shown->update(now, s_glance);
        /* after the face's update: in the glance the theme may take the frame (the Omni's countdown) */
        bool (*over)(lv_obj_t *, int64_t, bool) = watch_theme()->glance_override;
        glance_over_set(s_glance && over != NULL && over(s_glance_root, now, settings()->h24));
    }
    lens_update(now);
}

/* ------------------------------------------------------------ face hooks */
/* INPUT task: the shown face's claim, only while it is awake with no screen on top (a stale claim never takes a
 * drag). */
static bool face_claim(int16_t x0, int16_t y0, void *ctx)
{
    const omni_face_vt_t *vt = s_claim_face;
    return vt != NULL && ctx == (const void *)vt && vt->claim != NULL && vt->claim(x0, y0);
}

/* The touch claim follows the face while it is on top (a screen sets and clears its own). */
static void claim_sync(void)
{
    if (s_screen != NULL) return;
    const omni_face_vt_t *vt = s_awake && !s_glance ? s_shown : NULL;
    s_claim_face = vt;
    board_set_touch_claim(vt && vt->claim ? face_claim : NULL, (void *)vt);
}

static void face_wake(bool reveal)
{
    if (s_shown == NULL || s_screen != NULL || !s_awake || s_glance) return;
    s_running = true;
    s_revealed = true;
    ESP_LOGI(TAG, "face wake reveal=%d", reveal);
    if (s_shown->wake) s_shown->wake(reveal);
}

static void face_sleep(void)
{
    if (!s_running) return;
    s_running = false;
    ESP_LOGI(TAG, "face sleep");
    if (s_shown && s_shown->sleep) s_shown->sleep();
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    int64_t now = now_s();
    if (now != s_last_sec) refresh(now);
}

void shell_init(const omni_nav_t *nav)
{
    s_nav = nav;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    s_face_root = theme_box(scr, 0, 0, THEME_W, THEME_W);
    s_glance_root = theme_box(scr, 0, 0, THEME_W, THEME_W);
    lv_obj_add_flag(s_glance_root, LV_OBJ_FLAG_HIDDEN);
    lens_create(scr);
    s_screen_root = theme_box(scr, 0, 0, THEME_W, THEME_W);
    lv_obj_add_flag(s_screen_root, LV_OBJ_FLAG_HIDDEN);
    s_pips = theme_box(scr, 0, PIP_Y - 4, THEME_W, 8);
    lv_obj_add_flag(s_pips, LV_OBJ_FLAG_HIDDEN);
    disp_direct_overlay_add(s_pips);   /* drawn over a direct view's settle: blended into its bands (Task 6) */
    lv_timer_create(tick_cb, TICK_MS, NULL);
}

void shell_register_face(uint8_t id, const omni_face_vt_t *vt)
{
    if (vt && id < SHELL_FACE_MAX) s_faces[id] = vt;
}

void shell_show_current(void)
{
    const omni_face_vt_t *vt = NULL;
    if (s_nav) {
        uint8_t f = omni_nav_current_face(s_nav, settings());
        if (f < SHELL_FACE_MAX) vt = s_faces[f];
    }
    if (vt == NULL && watch_theme()->fallback_face < SHELL_FACE_MAX) vt = s_faces[watch_theme()->fallback_face];
    if (vt != s_shown) {
        face_sleep();
        if (s_screen == NULL) { /* the old face's claim goes before its destroy */
            s_claim_face = NULL;
            board_set_touch_claim(NULL, NULL);
        }
        if (s_shown && s_shown->destroy) s_shown->destroy();
        lv_obj_clean(s_face_root);
        s_shown = vt;
        s_revealed = false;
        const int64_t t0 = esp_timer_get_time();
        if (vt && vt->create) vt->create(s_face_root);
        ESP_LOGI(TAG, "face %s created in %u ms", vt && vt->name ? vt->name : "?",
                 (unsigned)((esp_timer_get_time() - t0) / 1000));   /* LVGL context: the stall a swipe sees */
        refresh(now_s());
        face_wake(true);
    } else {
        refresh(now_s());
    }
    claim_sync();
}

/* ------------------------------------------------------------ screens over the face */
void shell_register_screen(const omni_screen_vt_t *vt)
{
    if (vt && (unsigned)vt->id < sizeof s_screens / sizeof s_screens[0]) s_screens[vt->id] = vt;
}

/* Closes the screen on top. to_face: back to the face, which wakes without its reveal (unless it was created under
 * the screen, a pick from the picker, and has never been shown awake) and gets its claim back; false when another
 * screen replaces it at once. */
static void screen_close(bool to_face)
{
    if (s_screen == NULL) return;
    if (s_screen->destroy) s_screen->destroy();
    lv_obj_clean(s_screen_root);
    lv_obj_add_flag(s_screen_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_face_root, LV_OBJ_FLAG_HIDDEN);
    s_screen = NULL;
    if (!to_face) return;
    refresh(now_s()); /* the face's layout is current before it animates */
    face_wake(!s_revealed);
    claim_sync();
}

bool shell_show_top(void)
{
    omni_screen_t top = s_nav ? omni_nav_top(s_nav) : SCR_FACE;
    const omni_screen_vt_t *vt = (unsigned)top < sizeof s_screens / sizeof s_screens[0] ? s_screens[top] : NULL;
    if (top == SCR_FACE || vt == NULL) {
        screen_close(true);
        if (top == SCR_FACE) scr_settings_home();   /* Settings left the stack (spec 8.5's restore row) */
        refresh(now_s());
        return top == SCR_FACE;
    }
    if (vt != s_screen) {
        if (s_screen == NULL) { /* the face goes under: it stops, and its claim goes before the screen sets one */
            glance_over_set(false);   /* the face root shows again when the screen closes */
            face_sleep();
            s_claim_face = NULL;
            board_set_touch_claim(NULL, NULL);
        }
        screen_close(false);
        s_screen = vt;
        lv_obj_add_flag(s_face_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pips, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_screen_root, LV_OBJ_FLAG_HIDDEN);
        if (vt->create) vt->create(s_screen_root);
        if (s_glance && vt->dim) vt->dim(true);
    }
    refresh(now_s());
    return true;
}

void shell_refresh(void) { refresh(now_s()); }

const omni_nav_t *shell_nav(void) { return s_nav; }

bool shell_face_ready(uint8_t id) { return id < SHELL_FACE_MAX && s_faces[id] != NULL; }

bool shell_post(int code, int arg)
{
    omni_event_t e = {.kind = EV_UI, .t_ms = (uint32_t)(esp_timer_get_time() / 1000), .ui = UI_EV(code, arg)};
    return omni_events_post(&e);
}

bool shell_face_holds(int16_t x, int16_t y)
{
    return s_screen == NULL && s_running && s_shown != NULL && s_shown->holds != NULL && s_shown->holds(x, y);
}

void shell_gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    if (s_screen) {
        if (s_screen->gesture) s_screen->gesture(g, t_ms);
    } else if (s_running && s_shown && s_shown->gesture) {
        s_shown->gesture(g, t_ms);
    }
}

void shell_dim(bool on)
{
    if (s_screen && s_screen->dim) s_screen->dim(on);
}

void shell_set_glance(bool on)
{
    if (on) {
        face_sleep();
        s_awake = false;
    }
    s_glance = on;
    if (!on) glance_over_set(false);   /* the glance ended: the face again */
    claim_sync();
    shell_dim(on);
    if (on) lv_obj_add_flag(s_pips, LV_OBJ_FLAG_HIDDEN);
    refresh(now_s());
}

void shell_wake(bool reveal)
{
    s_awake = true;
    face_wake(reveal);
    claim_sync();
}

void shell_sleep(void)
{
    face_sleep();
    s_awake = false;
    claim_sync();
}

static void pips_hide(lv_timer_t *t)
{
    (void)t;
    lv_obj_add_flag(s_pips, LV_OBJ_FLAG_HIDDEN);
    disp_direct_overlay_changed(s_pips);
    s_pips_timer = NULL;
}

void shell_show_pips(uint8_t idx, uint8_t count)
{
    lv_obj_clean(s_pips);
    int32_t x0 = THEME_CX - (int32_t)(count - 1) * PIP_STEP / 2;
    for (uint8_t i = 0; i < count; i++) {
        int32_t r = i == idx ? 3 : 2;
        lv_obj_t *d = theme_box(s_pips, x0 + i * PIP_STEP - r, 4 - r, r * 2, r * 2);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(i == idx ? THEME_BRASS : THEME_PIP_OFF), 0);
    }
    lv_obj_remove_flag(s_pips, LV_OBJ_FLAG_HIDDEN);
    disp_direct_overlay_changed(s_pips);   /* new dots */
    if (s_pips_timer) lv_timer_delete(s_pips_timer);
    s_pips_timer = lv_timer_create(pips_hide, PIPS_MS, NULL);
    lv_timer_set_repeat_count(s_pips_timer, 1);
}

void shell_set_status(const shell_status_t *st) { s_status = *st; }

const shell_status_t *shell_status(void) { return &s_status; }
