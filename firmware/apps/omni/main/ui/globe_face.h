#pragma once
/* What every globe face shares (R48/D6): the view and its animation loop (an lv_timer that polls the view, ticks the
 * face for a frame and submits it at omni_globe_period_ms's cadence), the wake reveal, the overlays that follow the
 * frame on screen, the reset box a reveal starts from (I1), the rebuild for a new home city, the clock-step frame,
 * the sun/unset-clock choice and the home marker. LVGL context throughout (the faces' hooks have the lock).
 *
 * A face describes itself once, in a static const globe_face_ops_t, and calls:
 *   create/destroy from its own; wake/sleep from its wake/sleep hooks; update once per face update (every second);
 *   kick for "a frame now" (a drag starting at rest); skip on a tap (true if it ended a reveal); touch on activity.
 * Every call but create takes NULL (a face whose view could not be made) and then does nothing. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "omni/cities.h"
#include "omni/globe_motion.h"
#include "omni/input.h"
#include "omni/sun.h"
#include "ui/globe_view.h"

typedef struct globe_face globe_face_t;

/* The "world" view's look.gain (globe_face.c's desc). A frame's gain replaces the desc's (globe_view shade_for), so
 * the world face submits this too: one constant for both. */
#define GLOBE_WORLD_GAIN 0.92f

typedef struct {
    uint32_t now_ms;   /* the frame's slot (esp_timer ms, R43) */
    float dt;          /* s since the previous tick; 0 for the first after a wake and for the first after a tick at
                        * rest (an animation starting then starts from here, not from up to 30 s ago) */
    omni_reveal_t rv;  /* the reveal at now_ms; its end values outside one (and for a face without a reveal) */
} globe_tick_t;

typedef struct {
    const char *view;                /* the view's desc (globe_face.c's table): "live", "spin", "analog" or "world" */
    omni_cad_face_t cad;
    int32_t z;                       /* the view's index among its parent's children (kept through a rebuild) */
    /* The wake reveal (omni_reveal_live, _analog or _spin; NULL: none): armed by a revealing wake, started by the
     * first tick (the first ready frame), ended by a tap (globe_face_skip). */
    void (*reveal)(uint32_t t_ms, bool reduced, omni_reveal_t *r);
    globe_view_blank_t blank;        /* the box from a revealing wake until the reveal's first frame: its t = 0 look */
    /* Fills f (preset: lon0 0, gain 1, fade 255, no halo; sun from the previous frame). *animating (preset: a reveal
     * is running) = full rate: a drag, inertia, return home, a view flip. */
    void (*tick)(globe_face_t *gf, const globe_tick_t *t, globe_view_frame_t *f, bool *animating);
    /* The frame the last tick filled is on screen now: the overlays computed with it follow (NULL: none). Called in
     * the same timer run as the swap, so an overlay never leads or lags its globe by a redraw. */
    void (*shown)(globe_face_t *gf);
    /* The box was just reset (a revealing wake, a rebuild): hide the overlays until the next shown (NULL: none). */
    void (*blanked)(globe_face_t *gf);
    /* The view's desc does not follow the home city (world: lat0 0 whatever the city; A12/B16): a city change never
     * rebuilds it (globe_face_update), and the face refreshes its own overlays and text. Appended (the Omni's own
     * struct); false for live and analog. */
    bool fixed_view;
} globe_face_ops_t;

/* The view (ops->view's desc in globe_face.c) at its box origin made even (the panel rounds redraws to even bounds: an odd
 * box would redraw the face beneath it on every frame, Task 11), at index ops->z in parent. NULL: no assets or
 * PSRAM. ops must outlive the face (static const). user: the face's own state, for its hooks (globe_face_user), so
 * two globe faces can share one ops table (N5). Stopped until a wake. */
globe_face_t *globe_face_create(lv_obj_t *parent, const globe_face_ops_t *ops, void *user);
void globe_face_destroy(globe_face_t *gf);
void *globe_face_user(const globe_face_t *gf);
globe_view_t *globe_face_view(globe_face_t *gf);              /* NULL after a rebuild that failed */
void globe_face_box(const globe_face_t *gf, int32_t *x, int32_t *y);   /* the box origin on screen */

/* Runs. reveal: arms it and resets the box to ops->blank, if the view exists and its build has not failed (a reveal
 * that never ticks would leave a face's labels faded out or its hands at 12: N1). */
void globe_face_wake(globe_face_t *gf, bool reveal);
void globe_face_sleep(globe_face_t *gf);               /* stops: nothing is ticked or submitted */
void globe_face_kick(globe_face_t *gf);                /* a frame now (at rest the next is up to 30 s away); awake only */
bool globe_face_skip(globe_face_t *gf);                /* a tap: ends an armed or running reveal (true) with a frame now */
bool globe_face_revealing(const globe_face_t *gf);     /* a reveal is armed or running */
void globe_face_touch(globe_face_t *gf);               /* resets since_active (spin's 20 s rule); wake does it too */
/* Once per face update (every second, awake or glancing): a new home city (settings()->city) or lat0 rebuilds the
 * view (true: ops->blanked was called; logged at INFO as "globe face <view>: rebuilt for city <n>") unless
 * ops->fixed_view, and a clock set (the first SNTP sync after boot), stepped or synced by more than a few seconds
 * gets a frame now, since a globe at rest keeps the old sun for up to 30 s. Not for a face that does not follow the
 * home city or the real sun (spin). */
bool globe_face_update(globe_face_t *gf, int64_t now);

uint32_t globe_face_now_ms(void);          /* animation time (R43: esp_timer, ms) */
const omni_city_t *globe_face_home(void);  /* the home city now (NULL: none) */
/* sun for "now": the real sun, or the view-centre sun when the clock is unset (omni_globe_sun_now); false = unset (no halo, bead, glow) */
bool globe_face_sun(int64_t now, double lat0, double lon0, omni_globe_sun_t *sun, omni_sun_t *raw);
/* home marker (mockup 423-431 / 699-701): black 0.7 ring + brassHi ring of radius r, and if pulse >= 0 the pulse ring */
void globe_face_home_ring(lv_layer_t *layer, int32_t x, int32_t y, float r, float ring_w, float pulse);
/* G_DRAG_START: the longitude change (deg) that follows at once the px between where the finger went down and where
 * it became a drag (12 px), without the velocity a 0 ms move would give; a drag right turns the globe by
 * -deg_per_px a px. */
float globe_face_drag_lead(const omni_gesture_t *g, float deg_per_px);

