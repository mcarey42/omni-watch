/* The Omni dial: design/mockups/faces-omni.js makeDial() (spec §7.3), at home. The whole screen is one dial_view
 * (rings, V marks, annular hands, the Omni globe drifting under its dome, the lenses, the magnifier and the loupe) that
 * omni_model steps and omni_dial_render composes; this face only drives them.
 *
 * Awake: the anim timer steps the model every 32 ms (R90) (phase-locked: a late run does not push the next one back) and
 * submits the state; the globe drifts at 2.2 deg/s at rest, so the dial keeps its 30 fps (spec §7.3) whenever it is
 * awake. Between steps the timer polls the view, which swaps a finished frame in and redraws only its dirty rects.
 * Wake (Task 11, plan D2, reveal.h): the face tracks why the wake came (omni_wake_track: the first since create, after a
 * sleep, or a replay) and omni_reveal_for picks the entry: the setting's reveal (Settings -> Reveal: the lid flip, the
 * brass iris, or Instant, the plain dial entry) on a wake from sleep with reveal or a BOOT double; the plain dial
 * entry (spin-up, ring settle, green) on the first wake (a swipe, the picker, boot); at rest on a wake without reveal
 * (a re-lift within 10 s). omni_model_wake starts it once the view's build is done, with the reduced-motion setting
 * (the mockup's: the lid and iris at t x 2.2, no spin, no drift, no settle), as read at the wake.
 * The stale frame (T8 m1, R102): the view is blanked on every sleep (dial_view_blank: hidden until a fresh frame), as
 * the glance ends (update) in a lock section of its own before the wake comes. A wake that plays the lid or iris
 * keeps it blanked (black: the reveal starts from black) and hides the glance outline; any other wake keeps the
 * previous content until the fresh frame replaces it: the glance outline if it is up (tick hides it then), else the
 * last frame at once (dial_view_unblank: a re-lift, a screen closing back to the face).
 * Taps: a tap while the entry visibly moves (omni_model_revealing: lid or iris, spin-up, settle) skips it to its end
 * (omni_model_skip: lid or iris gone, spin-up, settle and green done); otherwise the magnifier opens the loupe and a tap on the open loupe (or the magnifier again)
 * closes it (omni_hit, omni_model_loupe); the red lens is ready 400 ms after the reveal is done (omni_model_red_ready,
 * the T5 carry: omni_hit does not gate) and travels (travel.h, Task 12): a red tap before then, during the entry, is the
 * skip and queues nothing; every tap during a travel is ignored; red or green while away comes home.
 * The face shows its own red lens (own_lens: the shell's is hidden): blinking
 * while the clock is unset, steady and dim while the sync is stale (omni_model_step from omni_sync_lens).
 * The caption (the travel's destination, Task 12: omni_travel_caption, its alpha the label's opacity) is hidden at home,
 * in the glance and from the sleep, which also brings a travel home (omni_travel_abort_home).
 * R103: every wake that plays a reveal but the first (the lid, the iris, or the Instant entry after a sleep or a
 * replay) keeps the view blanked until its first fresh frame.
 * Sound (Task 13, sfx.h; the triggers are an owner check, preflight S8): the latch as a flip starts (its pop, t 0), the
 * whoosh as the wake's spin-up starts (flip, iris and the plain entry; none with reduced motion or once skipped), the
 * surge as a travel starts, the chime as green reaches 1 (the entry's green, a skip's, or a travel's arrival). The steps
 * run only while awake, and the glance never plays; face_sleep (the sleep, and the glance's start) and the glance's
 * first update stop a playing effect under the LVGL lock the steps run under (T13 M1), and so does destroying the
 * face.
 *
 * Glance (spec §13; faces-omni.js omniGlance, designed there first): the dial view is hidden (dial_view_show) and
 * nothing submits, so the workers idle and the frame counter stands still; a full-screen object's draw callback draws
 * the outline in LVGL: the circles at r 126, 197 and 225 and the bezel's hour dots, the two annular hands as outlines,
 * the magnifier's frame and the date column (century, year, day, month; BC in dim red), everything offset by
 * omni_orbit_offset. Colours only (R28). Redrawn when the minute changes (the orbit, the hands and the date move then).
 * Unset clock: no date column, hands at 12. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "shell.h"
#include "theme.h"
#include "omni_fonts.h"
#include "omni_faces.h"
#include "store.h"
#include "dial_view.h"
#include "disp_direct.h"
#include "omni/cities.h"
#include "omni/dial.h"
#include "omni/omni_model.h"
#include "omni/power.h"
#include "omni/reveal.h"
#include "omni/settings.h"
#include "omni/timefmt.h"
#include "omni/travel.h"
#include "omni_sfx.h"

#define POLL_MS 4         /* the timer: polls the view between steps */
#define CAPTION_RGB 0xe9d7ae
#define CAPTION_CY 309.5f /* drawDial 531: the pill rrect(C - tw/2 - 10, C + 66, tw + 20, 20, 10), text middle C + 76.5 */

/* The glance's inputs: it redraws when any of them changes. */
typedef struct {
    int8_t dx, dy;
    bool set;
    int16_t year;
    int8_t mon, day, hour, min;
} glance_key_t;

typedef struct {
    dial_view_t *view;
    lv_obj_t *caption, *glance;
    glance_key_t gk;        /* what the glance object last drew */
    bool gk_valid;
    lv_timer_t *timer;
    omni_model_t model;
    struct tm tm;           /* local time, from the last update */
    bool set;               /* the clock is set */
    omni_lens_t lens;
    bool armed;             /* a wake waits for the view's build */
    omni_rv_t rv;           /* that wake's entry (omni_reveal_for) */
    bool rm;                /* that wake's reduced motion (settings, read at the wake: m4) */
    bool in_glance;         /* the last update was the glance's */
    omni_wake_track_t wt;   /* why the wake came (zeroed at create) */
    bool running;
    uint32_t due;           /* the next step (esp_timer ms) */
    int city;               /* settings()->city the model's home is from */
    const char *cap_text;   /* what the caption label shows (NULL: hidden) */
    lv_opa_t cap_opa;
    /* the sounds' edges (Task 13), reset by each wake */
    bool snd_spin;          /* this wake's spin-up start has passed (its whoosh played, or was not due) */
    bool snd_travel;        /* the last step had a travel active */
    bool snd_have_green;    /* snd_green is the last step's green (not before the wake's first step) */
    float snd_green;
#if CONFIG_OMNI_DEBUG_CONSOLE
    /* a travel's displayed dial frames (dial_view_poll swapped one in), whirl (to 2040 ms) and flash (to the landing),
     * logged as it lands: the measurement of Task 12 (LVGL's `fps` also counts the caption's own redraws) */
    bool tr_on;
    uint32_t tr_t0, tr_n[2];
#endif
} omni_face_t;

static omni_face_t *S;
static const char *TAG = "omni";

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void home_from_settings(void)
{
    const omni_city_t *c = omni_city_at(settings()->city);
    S->city = settings()->city;
    omni_model_set_home(&S->model, c ? c->lat : 0, c ? c->lon : 0);   /* N2: a new city re-centres the globe */
}

/* ------------------------------------------------------------ glance */
#define GL_BRASS_RING_OPA 0.30f
#define GL_DOT_R 218
#define GL_INK_OPA 0.36f
#define GL_BC_OPA 0.40f
/* the engraved month names (faces-omni.js MONTHS) */
static const char *const MONTHS[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUNE", "JULY", "AUG", "SEPT", "OCT", "NOV", "DEC"};

static void glance_circle(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r, uint32_t rgb, lv_opa_t opa)
{
    theme_draw_arc(layer, cx, cy, r, 1, 0, 360, rgb, opa, false);
}

/* hand(): the annular hand polygon, from r 118 to r1, w wide, tip length tip (hour: the narrower root). */
static void glance_hand(lv_layer_t *layer, float a, int32_t cx, int32_t cy, float r1, float w, bool hour)
{
    const float tip = hour ? 16 : 12, r0 = 118;
    const float lx[5] = {hour ? -w * .32f : -w / 2, hour ? -w / 2 : -w * .38f, 0, hour ? w / 2 : w * .38f, hour ? w * .32f : w / 2};
    const float ly[5] = {-r0, -(r1 - tip), -r1, -(r1 - tip), -r0};
    int32_t x[5], y[5];
    for (int i = 0; i < 5; i++) omni_dial_rot(a, lx[i], ly[i], cx, cy, &x[i], &y[i]);
    /* 1.5 px ink 0.38 outline: 2 px at 0.75 of that (the weight of the stroke, as the analog glance) */
    for (int i = 0; i < 5; i++)
        theme_draw_line(layer, x[i], y[i], x[(i + 1) % 5], y[(i + 1) % 5], 2, THEME_INK, THEME_OPA(0.38f * 0.75f), true);
}

static void glance_draw(lv_event_t *e)
{
    if (S == NULL) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    const glance_key_t *k = &S->gk;
    const int32_t cx = THEME_CX + k->dx, cy = THEME_CY + k->dy;
    static const int32_t RINGS_R[3] = {126, 197, 225};
    for (int i = 0; i < 3; i++) glance_circle(layer, cx, cy, RINGS_R[i], THEME_BRASS, THEME_OPA(GL_BRASS_RING_OPA));
    for (int i = 0; i < 12; i++) {   /* the bezel's hour dots, brass 0.36: r 2.2, and 3.2 at 3, 6 and 9 */
        int32_t x, y;
        omni_dial_rot((float)i / 12 * 2 * (float)M_PI, 0, -GL_DOT_R, cx, cy, &x, &y);
        const int32_t r = (i % 3 == 0 && i != 0) ? 3 : 2;
        theme_draw_arc(layer, x, y, r, r, 0, 360, THEME_BRASS, THEME_OPA(0.36f), false);
    }
    float ha = 0, ma = 0;
    if (k->set) omni_hand_angles(k->hour, k->min, 0, &ha, &ma);
    glance_hand(layer, ha, cx, cy, 178, 11, true);
    glance_hand(layer, ma, cx, cy, 214, 5.2f, false);
    /* the magnifier: rrect(207, 18, 52 x 104, r 3), 1 px brass 0.34 */
    theme_draw_box(layer, 207 + k->dx, 18 + k->dy, 258 + k->dx, 121 + k->dy, 3, THEME_BRASS, LV_OPA_TRANSP, 1,
                   THEME_OPA(0.34f));
    if (!k->set) return;
    float v[OMNI_RINGS];
    omni_dial_vals(k->year, k->mon, k->day, v);
    const int cent = (int)v[3];
    const bool bc = cent < 20;
    char buf[24];
    const uint32_t rgb = bc ? THEME_RED : THEME_INK;
    const lv_opa_t opa = THEME_OPA(bc ? GL_BC_OPA : GL_INK_OPA);
    snprintf(buf, sizeof buf, "%d %s", bc ? 19 - cent : cent - 20, bc ? "BC" : "AD");
    /* rows at the magnified ring rows (canvas middle baseline: baseline = y + 3 at 13 px, y + 4 at 15 px, as measured against the mockup shot) */
    theme_draw_text(layer, buf, &font_cormorant_700_13, rgb, opa, 0, THEME_CENTER, cx, 32 + k->dy + 3);
    snprintf(buf, sizeof buf, "%02d", (int)v[2]);
    theme_draw_text(layer, buf, &font_cormorant_700_13, THEME_INK, THEME_OPA(GL_INK_OPA), 0, THEME_CENTER, cx, 61 + k->dy + 3);
    snprintf(buf, sizeof buf, "%d", k->day);
    theme_draw_text(layer, buf, &font_cormorant_700_15, THEME_INK, THEME_OPA(GL_INK_OPA), 0, THEME_CENTER, cx, 86 + k->dy + 4);
    theme_draw_text(layer, MONTHS[(k->mon - 1) % 12], &font_cormorant_700_15, THEME_INK, THEME_OPA(GL_INK_OPA), 0,
                    THEME_CENTER, cx, 110 + k->dy + 4);
}

/* The glance's state for `now`; redraws the object (whole: it changes once a minute) when it differs. */
static void glance_update(int64_t now)
{
    glance_key_t k = {0};
    omni_orbit_offset(now, &k.dx, &k.dy);
    k.set = S->set;
    if (S->set) {
        k.year = (int16_t)(S->tm.tm_year + 1900);
        k.mon = (int8_t)(S->tm.tm_mon + 1);
        k.day = (int8_t)S->tm.tm_mday;
        k.hour = (int8_t)S->tm.tm_hour;
        k.min = (int8_t)S->tm.tm_min;
    }
    if (S->gk_valid && memcmp(&k, &S->gk, sizeof k) == 0) return;
    S->gk = k;
    S->gk_valid = true;
    lv_obj_invalidate(S->glance);
}

static const omni_entry_t *entry_for(omni_rv_t rv)
{
    switch (rv) {
    case OMNI_RV_DIAL: return &OMNI_ENTRY_DIAL;
    case OMNI_RV_FLIP: return &OMNI_ENTRY_FLIP;
    case OMNI_RV_IRIS: return &OMNI_ENTRY_IRIS;
    default: return NULL;   /* at rest */
    }
}

/* The caption (a travel's destination): its text, and its alpha as the label's opacity (the pill and the text together;
 * R28 is the glance's rule, and the caption is hidden there). Touched only when they change. */
static void caption_set(const char *text, float alpha)
{
    const lv_opa_t opa = text ? (lv_opa_t)lroundf(fminf(fmaxf(alpha, 0), 1) * 255) : 0;
    if (opa == 0) text = NULL;
    if (text != S->cap_text) {
        if (text) {
            lv_label_set_text_static(S->caption, text);
            lv_obj_remove_flag(S->caption, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(S->caption, LV_OBJ_FLAG_HIDDEN);
        }
        S->cap_text = text;
        disp_direct_overlay_changed(S->caption);   /* the direct path's snapshot of it (Task 6) */
    }
    if (text && opa != S->cap_opa) {
        lv_obj_set_style_opa(S->caption, opa, 0);
        disp_direct_overlay_changed(S->caption);
    }
    S->cap_opa = opa;
}

/* The wake's sounds: the latch as a flip starts (t 0); the edges below start from this wake. */
static void sounds_wake(void)
{
    S->snd_spin = false;
    S->snd_travel = false;
    S->snd_have_green = false;
    if (S->rv == OMNI_RV_FLIP && !S->in_glance) sfx_play(SFX_LATCH);
}

/* Each step's sound edges: the whoosh at the spin-up's start, the surge at a travel's start, the chime as green reaches
 * 1. In the glance (the steps stop there anyway) the edges are only tracked. */
static void sounds_step(uint32_t t, const omni_dial_state_t *st)
{
    const omni_model_t *m = &S->model;
    const omni_entry_t *e = m->entry;
    const bool quiet = S->in_glance;
    if (!S->snd_spin && e && (int32_t)(t - (m->wake_t + e->spin_t0)) >= 0) {
        S->snd_spin = true;
        if (!quiet && !m->reduced && !m->skipped) sfx_play(SFX_WHOOSH);   /* no spin-up then */
    }
    const bool travel = omni_travel_active(m);
    if (travel && !S->snd_travel && !quiet) sfx_play(SFX_SURGE);
    S->snd_travel = travel;
    if (S->snd_have_green && S->snd_green < 1 && st->green >= 1 && !quiet) sfx_play(SFX_CHIME);
    S->snd_green = st->green;
    S->snd_have_green = true;
}

static void step(uint32_t t)
{
    omni_dial_state_t st;
    omni_model_step(&S->model, t, S->set, S->set ? &S->tm : NULL, S->lens, &st);
    dial_view_submit(S->view, &st);
    sounds_step(t, &st);
#if CONFIG_OMNI_DEBUG_CONSOLE
    const bool on = omni_travel_active(&S->model);
    if (on && !S->tr_on) {   /* started (a tap's, or the auto-return in this step) */
        S->tr_t0 = t;
        S->tr_n[0] = S->tr_n[1] = 0;
    } else if (!on && S->tr_on) {
        const uint32_t ms = t - S->tr_t0;
        if (ms > 2040)   /* landed (a travel ends earlier only by a replay wake or the clock going unset: T12 M4) */
            ESP_LOGI(TAG, "travel landed after %u ms: whirl %u frames in 2040 ms (%.1f fps), flash %u in %u ms (%.1f fps)",
                     (unsigned)ms, (unsigned)S->tr_n[0], S->tr_n[0] / 2.04, (unsigned)S->tr_n[1], (unsigned)(ms - 2040),
                     S->tr_n[1] * 1000.0 / (ms - 2040));
        else
            ESP_LOGI(TAG, "travel ended after %u ms: %u frames", (unsigned)ms, (unsigned)S->tr_n[0]);
    }
    S->tr_on = on;
#endif
    float a;
    const char *cap = omni_travel_caption(&S->model, t, &a);
    caption_set(cap, a);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    if (S == NULL || S->view == NULL || !S->running) return;
    if (dial_view_failed(S->view)) {
        lv_timer_pause(S->timer);
        return;
    }
    const bool fresh = dial_view_poll(S->view);
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (S->tr_on && fresh) S->tr_n[(int32_t)(now_ms() - S->tr_t0) >= 2040]++;
#else
    (void)fresh;
#endif
    /* the glance outline kept up over a blank (a wake without lid or iris) goes once the fresh frame is in */
    if (!S->in_glance && !lv_obj_has_flag(S->glance, LV_OBJ_FLAG_HIDDEN) && !dial_view_blanked(S->view))
        lv_obj_add_flag(S->glance, LV_OBJ_FLAG_HIDDEN);
    if (!dial_view_ready(S->view)) return;
    const uint32_t t = now_ms();
    if (S->armed) {   /* the build is done: the wake (and its reveal) starts now, with its first frame */
        S->armed = false;
        omni_model_wake(&S->model, t, entry_for(S->rv), S->rm);
        S->due = t;
        sounds_wake();
    }
    if ((int32_t)(t - S->due) < 0) return;
    S->due += OMNI_MODEL_STEP_MS;
    if ((int32_t)(t - S->due) >= 0) S->due = t + OMNI_MODEL_STEP_MS;   /* fell behind by a step: resynchronise */
    step(t);
    dial_view_overlays_publish(S->view);   /* m2: with this step's caption, for the frame the step just submitted */
}

static void face_create(lv_obj_t *parent)
{
    S = heap_caps_calloc(1, sizeof *S, MALLOC_CAP_SPIRAM);
    if (S == NULL) return;
    omni_model_init(&S->model, 0, 0);
    home_from_settings();
    S->view = dial_view_create(parent);   /* NULL: no PSRAM or assets (logged); the face stays black */
    /* the caption pill (hidden at home; Task 12 sets its text and shows it): black .55, radius 10, 20 high, 10 px each
     * side of the text, the text's middle at y 309.5 */
    S->caption = theme_label(parent, &font_cormorant_700_13, CAPTION_RGB, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(S->caption, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(S->caption, THEME_OPA(0.55), 0);
    lv_obj_set_style_radius(S->caption, 10, 0);
    lv_obj_set_style_pad_hor(S->caption, 10, 0);
    lv_obj_set_height(S->caption, 20);
    lv_obj_set_style_pad_top(S->caption, (20 - lv_font_get_line_height(&font_cormorant_700_13)) / 2, 0);
    lv_label_set_text(S->caption, "");
    lv_obj_align(S->caption, LV_ALIGN_TOP_MID, 0, (int32_t)lroundf(CAPTION_CY) - 10);
    lv_obj_add_flag(S->caption, LV_OBJ_FLAG_HIDDEN);
    disp_direct_overlay_add(S->caption);   /* drawn over the dial: blended into the direct bands (Task 6) */
    S->glance = theme_box(parent, 0, 0, THEME_W, THEME_W);
    lv_obj_add_flag(S->glance, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(S->glance, glance_draw, LV_EVENT_DRAW_MAIN, NULL);
    S->timer = lv_timer_create(tick, POLL_MS, NULL);
    lv_timer_pause(S->timer);
}

static void face_destroy(void)
{
    if (S == NULL) return;
    sfx_stop();   /* a swipe away mid-whoosh: the next face is quiet */
    if (S->timer) lv_timer_delete(S->timer);
    dial_view_destroy(S->view);
    disp_direct_overlay_remove(S->caption);   /* after the view, which reads the sprites (Task 6) */
    heap_caps_free(S);
    S = NULL;
}

static void face_wake(bool reveal)
{
    if (S == NULL) return;
    static const char *const CAUSE[] = { "first", "after sleep", "replay" }, *const RV[] = { "none", "dial", "flip", "iris" };
    const omni_wake_cause_t cause = omni_wake_track_wake(&S->wt);
    S->rv = omni_reveal_for(cause, reveal, settings()->reveal_style);
    S->rm = settings()->reduced_motion;   /* the value omni_model_wake gets, even if the build is still running */
    ESP_LOGI(TAG, "wake (%s, reveal %d): %s%s", CAUSE[cause], reveal, RV[S->rv], S->rm ? ", reduced motion" : "");
    S->running = true;
    S->armed = true;   /* omni_model_wake once the view is built (at once if it is) */
    if (S->rv == OMNI_RV_FLIP || S->rv == OMNI_RV_IRIS) {
        /* the lid or iris starts from black: nothing from before (T8 m1), no glance outline */
        dial_view_blank(S->view);
        lv_obj_add_flag(S->glance, LV_OBJ_FLAG_HIDDEN);
    } else if (S->rv == OMNI_RV_DIAL && cause != OMNI_WAKE_FIRST) {
        /* R103: the Instant entry after a sleep or a replay: black (or the outline) until its first frame, not the
         * pre-sleep dial popping to the entry's */
        dial_view_blank(S->view);
    } else if (lv_obj_has_flag(S->glance, LV_OBJ_FLAG_HIDDEN)) {
        dial_view_unblank(S->view);   /* R102: the last frame until the fresh one replaces it */
    } else {
        dial_view_blank(S->view);   /* re-armed: still since the sleep, timed from the wake; the outline stays up
                                     * over it until the fresh frame (tick) */
    }
    lv_timer_resume(S->timer);
    lv_timer_ready(S->timer);
}

static void face_sleep(void)
{
    if (S == NULL) return;
    sfx_stop();   /* T13 M1: under the lock the steps play under, so no step's edge can start a sound after it */
    omni_wake_track_sleep(&S->wt);
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (S->tr_on) ESP_LOGI(TAG, "travel aborted by the sleep");
    S->tr_on = false;
#endif
    omni_travel_abort_home(&S->model);   /* a sleep comes home: the next wake is at home, today (T8 m2) */
    caption_set(NULL, 0);
    dial_view_blank(S->view);   /* until the wake decides (R102): the glance's end comes before it, in its own lock */
    S->running = false;
    lv_timer_pause(S->timer);
}

/* A tap during the wake's entry skips it to its end (a red tap too: it is consumed by the skip and does not queue a
 * travel; the red lens is ready 400 ms after the skip, T11 ruling); every tap during a travel is ignored (the mockup's);
 * otherwise the magnifier opens the loupe and a tap on the open loupe closes it, the red lens travels once ready
 * (omni_model_red_ready, gated here as omni_hit does not: T5 carry; to the next destination, or home when away), and
 * green comes home when away. */
static void face_gesture(const omni_gesture_t *g, uint32_t t_ms)
{
    (void)t_ms;
    if (S == NULL || !S->running || g->kind != G_TAP) return;
    const uint32_t t = now_ms();
    const omni_model_t *m = &S->model;
    if (!S->armed && omni_model_revealing(m, t)) {
        omni_model_skip(&S->model, t);
        ESP_LOGI(TAG, "tap %d,%d: reveal skipped at %d ms", g->x, g->y, (int)(int32_t)(t - m->wake_t));
        return;
    }
    if (omni_travel_active(m)) {
        ESP_LOGI(TAG, "tap %d,%d: ignored (travelling)", g->x, g->y);
        return;
    }
    const omni_hit_t h = omni_hit(g->x, g->y, S->model.loupe_on);
    ESP_LOGI(TAG, "tap %d,%d: hit %d", g->x, g->y, (int)h);
    switch (h) {
    case OMNI_HIT_MAG:
    case OMNI_HIT_LOUPE_OFF: omni_model_loupe(&S->model, t); break;
    case OMNI_HIT_RED:
        if (S->armed || !omni_model_red_ready(m, t)) {
            ESP_LOGI(TAG, "red lens: not ready");
            break;
        }
        omni_travel_start(&S->model, t, omni_travel_away(m));
        ESP_LOGI(TAG, "red lens: %s", !omni_travel_active(m) ? "no travel (clock unset)"
                                      : omni_travel_to_home(m) ? "travel home" : omni_travel_dest_name(m));
        break;
    case OMNI_HIT_GREEN:
        if (omni_travel_away(m)) {
            omni_travel_start(&S->model, t, true);
            ESP_LOGI(TAG, "green lens: travel home");
        }
        break;
    default: break;
    }
}

static void face_update(int64_t now, bool glance)
{
    if (S == NULL) return;
    S->set = omni_time_is_set(now);
    if (S->set) {
        const time_t tt = (time_t)now;
        localtime_r(&tt, &S->tm);
    }
    S->lens = omni_sync_lens(now, &shell_status()->sync);
    if (settings()->city != S->city) home_from_settings();
    if (glance && !S->in_glance) sfx_stop();   /* the glance starts: quiet (T13 M1) */
    S->in_glance = glance;
    if (glance) {
        caption_set(NULL, 0);   /* T8 m2: the glance has no caption (step shows it again after) */
        dial_view_show(S->view, false);
        lv_obj_remove_flag(S->glance, LV_OBJ_FLAG_HIDDEN);
        glance_update(now);
        return;
    }
    /* the glance's end: the outline stays while the view is blanked (since the sleep): the wake decides (R102) */
    if (!dial_view_blanked(S->view)) lv_obj_add_flag(S->glance, LV_OBJ_FLAG_HIDDEN);
    S->gk_valid = false;
    dial_view_show(S->view, true);
}

const omni_face_vt_t FACE_OMNI_VT = {
    .name = "omni-dial",
    .create = face_create,
    .update = face_update,
    .destroy = face_destroy,
    .wake = face_wake,
    .sleep = face_sleep,
    .gesture = face_gesture,
    .own_lens = true,
};
