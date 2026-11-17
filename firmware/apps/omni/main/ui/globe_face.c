#include "ui/globe_face.h"
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "store.h"
#include "omni/cities.h"
#include "omni/timefmt.h"
#include "ui/assets_meta.h"
#include "theme.h"

#define POLL_MS 4          /* while a submitted frame renders: how often the timer looks for it */
#define REDRAW_LEAD_MS 25  /* a frame due within this of a swap is submitted at the swap (Task 11, below) */
#define BUILD_POLL_MS 20   /* while the worker builds the LUT, texture and base */
#define STUCK_MS 3000      /* a frame not back by then is given up on (never expected) */
#define REST_POLL_MS 250   /* at rest: a frame the view rendered on its own (globe_view_hold) shows within this */

static const char *TAG = "globe_face";

struct globe_face {
    const globe_face_ops_t *ops;
    void *user;
    lv_obj_t *parent;
    int32_t x, y;               /* the box origin (even) */
    globe_view_t *view;
    lv_timer_t *timer;
    bool running;               /* between wake and sleep */
    bool awaiting;              /* a submitted frame is not swapped in yet */
    uint32_t next_ms, sub_ms, active_ms;
    omni_globe_sun_t sun;       /* the previous frame's, preset for the next tick */
    /* the reveal */
    bool rv_pending, rv_running;
    uint32_t rv_t0;
    /* the tick's dt */
    uint32_t last_tick;
    bool last_animating;
    /* the overlays of the last tick's frame wait for it (ops->shown) */
    bool pend;
    uint32_t pend_seq;
    /* globe_face_update */
    uint16_t city;
    int8_t lat0;
    int64_t last_now;           /* the previous update's wall time (0: none) */
    bool last_set;
};

uint32_t globe_face_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

const omni_city_t *globe_face_home(void) { return omni_city_at(settings()->city); }

static int8_t home_lat0(uint16_t city)
{
    const omni_city_t *c = omni_city_at(city);
    return omni_globe_lat0_for(c ? c->lat : 0);
}

/* The reveal now: started at its first tick; outside one, the curve's end values (it clamps). */
static void reveal_step(globe_face_t *gf, uint32_t now, omni_reveal_t *rv)
{
    if (!gf->ops->reveal) {
        *rv = (omni_reveal_t){ .lon_off_deg = 0, .gain = 1, .fade = 1, .settle = 1, .done = true };
        return;
    }
    if (gf->rv_pending) {
        gf->rv_pending = false;
        gf->rv_running = true;
        gf->rv_t0 = now;
    }
    const bool reduced = settings()->reduced_motion;
    if (gf->rv_running) {
        gf->ops->reveal(now - gf->rv_t0, reduced, rv);
        if (rv->done) gf->rv_running = false;
        return;
    }
    gf->ops->reveal(1u << 30, reduced, rv);
}

static void submit(globe_face_t *gf, uint32_t at)
{
    globe_tick_t t = { .now_ms = at };
    reveal_step(gf, at, &t.rv);
    t.dt = gf->last_tick && gf->last_animating ? (float)(int32_t)(at - gf->last_tick) / 1000.0f : 0;
    globe_view_frame_t f = { .lon0_deg = 0, .sun = gf->sun, .gain = 1, .fade = 255 };
    bool animating = gf->rv_running;
    gf->ops->tick(gf, &t, &f, &animating);
    f.still = !animating;   /* R76: at rest (and the settled frame after motion) the linear kernel */
    gf->last_tick = at ? at : 1;   /* 0 is "none" */
    gf->last_animating = animating;
    gf->sun = f.sun;
    const uint32_t seq = globe_view_submit(gf->view, &f);
    gf->pend = gf->ops->shown != NULL;
    gf->pend_seq = seq;
    gf->awaiting = true;
    gf->next_ms = at + omni_globe_period_ms(gf->ops->cad, animating, at - gf->active_ms);
}

static void anim_cb(lv_timer_t *t)
{
    globe_face_t *gf = lv_timer_get_user_data(t);
    if (!gf->view) {   /* a rebuild failed: nothing to show */
        lv_timer_pause(t);
        return;
    }
    const uint32_t now = globe_face_now_ms();
    const bool swapped = globe_view_poll(gf->view);
    if (swapped && gf->pend && (int32_t)(globe_view_shown(gf->view) - gf->pend_seq) >= 0) {
        gf->pend = false;
        gf->ops->shown(gf);
    }
    if (swapped || (gf->awaiting && (int32_t)(now - gf->sub_ms) > STUCK_MS)) gf->awaiting = false;
    if (!globe_view_ready(gf->view)) {
        if (globe_view_failed(gf->view)) {   /* terminal (the worker logged why): stop polling until a restart */
            lv_timer_pause(t);
            return;
        }
        lv_timer_set_period(t, BUILD_POLL_MS);
        return;
    }
    /* A swap makes LVGL redraw the box (about 20-28 ms) before this timer runs again, so a frame due by then is
     * submitted now and renders on the workers during that redraw; it is ticked for its slot and the slots stay a
     * period apart (Task 11: waiting for the slot serialised render and redraw, about 45 ms a frame for 24 + 19). */
    const int32_t early = (int32_t)(gf->next_ms - now);
    if (!gf->awaiting && (early <= 0 || (swapped && early <= REDRAW_LEAD_MS))) {
        submit(gf, early > 0 ? gf->next_ms : now);
        gf->sub_ms = now;
    }
    const int32_t left = (int32_t)(gf->next_ms - now);
    const int32_t period = gf->awaiting ? POLL_MS : left > REST_POLL_MS ? REST_POLL_MS : left;
    lv_timer_set_period(t, period < 1 ? 1 : (uint32_t)period);
}

static bool view_desc(const char *name, globe_view_desc_t *d, int32_t *x, int32_t *y);

static globe_view_t *view_make(globe_face_t *gf)
{
    globe_view_desc_t d;
    int32_t x, y;
    if (!view_desc(gf->ops->view, &d, &x, &y)) return NULL;
    gf->x = x & ~1;
    gf->y = y & ~1;
    globe_view_t *v = globe_view_create(gf->parent, gf->x, gf->y, &d);
    if (v && globe_view_obj(v)) lv_obj_move_to_index(globe_view_obj(v), gf->ops->z);
    return v;
}

globe_face_t *globe_face_create(lv_obj_t *parent, const globe_face_ops_t *ops, void *user)
{
    globe_face_t *gf = heap_caps_calloc(1, sizeof *gf, MALLOC_CAP_SPIRAM);
    if (!gf) return NULL;
    gf->ops = ops;
    gf->user = user;
    gf->parent = parent;
    gf->view = view_make(gf);
    if (gf->view) gf->timer = lv_timer_create(anim_cb, BUILD_POLL_MS, gf);
    if (!gf->view || !gf->timer) {
        globe_view_destroy(gf->view);
        heap_caps_free(gf);
        return NULL;
    }
    lv_timer_pause(gf->timer);
    gf->active_ms = globe_face_now_ms();
    omni_globe_sun_studio(&gf->sun);
    gf->city = settings()->city;
    gf->lat0 = home_lat0(gf->city);
    return gf;
}

void globe_face_destroy(globe_face_t *gf)
{
    if (!gf) return;
    lv_timer_delete(gf->timer);
    globe_view_destroy(gf->view);
    heap_caps_free(gf);
}

globe_view_t *globe_face_view(globe_face_t *gf) { return gf ? gf->view : NULL; }
void *globe_face_user(const globe_face_t *gf) { return gf ? gf->user : NULL; }

void globe_face_box(const globe_face_t *gf, int32_t *x, int32_t *y)
{
    *x = gf->x;
    *y = gf->y;
}

/* Cheap both ways (a glance closing a pushed screen wakes the face and puts it straight back to sleep): the worker
 * stays up for the view's life and idles on its notification; only the timer runs or pauses. */
static void start(globe_face_t *gf)
{
    if (!gf->view) return;
    gf->next_ms = globe_face_now_ms();   /* a frame at once */
    lv_timer_set_period(gf->timer, 1);
    lv_timer_resume(gf->timer);
    lv_timer_reset(gf->timer);
}

/* The box to its blank look; frames from before are dropped, so nothing from before shows (I1). */
static void blank(globe_face_t *gf)
{
    if (gf->view) globe_view_reset(gf->view, gf->ops->blank);
    gf->awaiting = false;   /* a frame in flight is dropped: submit the next at once */
    gf->pend = false;
    if (gf->ops->blanked) gf->ops->blanked(gf);
}

void globe_face_wake(globe_face_t *gf, bool reveal)
{
    if (!gf) return;
    gf->running = true;
    gf->rv_pending = reveal && gf->ops->reveal && gf->view && !globe_view_failed(gf->view);   /* N1 */
    gf->rv_running = false;
    gf->last_tick = 0;
    gf->active_ms = globe_face_now_ms();   /* spin's 20 s to 15 fps count from this wake */
    /* Without this the wake's first redraw (power.c display_on, before DISPON) showed the frame from before the
     * sleep at full brightness, then the reveal's black t = 0 frame (I1). A wake without a reveal keeps it: it is
     * the right globe, one frame old. */
    if (gf->rv_pending) blank(gf);
    start(gf);
}

void globe_face_sleep(globe_face_t *gf)
{
    if (!gf) return;
    gf->running = false;
    lv_timer_pause(gf->timer);
}

void globe_face_kick(globe_face_t *gf)
{
    if (gf && gf->running) start(gf);
}

bool globe_face_skip(globe_face_t *gf)
{
    if (!gf || !globe_face_revealing(gf)) return false;
    gf->rv_pending = gf->rv_running = false;
    globe_face_kick(gf);   /* the end frame now */
    return true;
}

bool globe_face_revealing(const globe_face_t *gf) { return gf && (gf->rv_pending || gf->rv_running); }

void globe_face_touch(globe_face_t *gf)
{
    if (gf) gf->active_ms = globe_face_now_ms();
}

/* Once per update: true when the clock was set, stepped or synced by more than a few seconds since the last one. */
static bool clock_stepped(globe_face_t *gf, int64_t now)
{
    const bool set = omni_time_is_set(now);
    const bool stepped = gf->last_now != 0 && (set != gf->last_set || now < gf->last_now || now - gf->last_now > 5);
    gf->last_now = now;
    gf->last_set = set;
    return stepped;
}

bool globe_face_update(globe_face_t *gf, int64_t now)
{
    if (!gf) return false;
    const uint16_t city = settings()->city;
    const int8_t lat0 = home_lat0(city);
    /* A12/B16: a fixed view (world) is the same for every city: the face refreshes its band and cartouche itself */
    const bool rebuild = !gf->ops->fixed_view && (city != gf->city || lat0 != gf->lat0);
    if (rebuild) {   /* destroy before create: two views never coexist (PSRAM) */
        ESP_LOGI(TAG, "globe face %s: rebuilt for city %u", gf->ops->view, (unsigned)city);   /* Task 9 checks this */
        gf->city = city;
        gf->lat0 = lat0;
        globe_view_destroy(gf->view);
        gf->view = view_make(gf);
        blank(gf);
        if (gf->running) start(gf);
    }
    if (clock_stepped(gf, now)) globe_face_kick(gf);
    return rebuild;
}

bool globe_face_sun(int64_t now, double lat0, double lon0, omni_globe_sun_t *sun, omni_sun_t *raw)
{
    omni_sun_t s;
    const bool set = omni_sun_light(now, &s);
    if (set && raw) *raw = s;
    return omni_globe_sun_now(sun, set ? &s : NULL, lat0, lon0);
}

float globe_face_drag_lead(const omni_gesture_t *g, float deg_per_px) { return -(float)(g->x - g->x0) * deg_per_px; }

static void ring(lv_layer_t *layer, int32_t x, int32_t y, float r, float w, uint32_t rgb, float a)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.center.x = x;
    d.center.y = y;
    d.radius = (uint16_t)lroundf(r + w / 2);   /* a canvas stroke of width w at r: outer edge r + w/2 */
    d.width = (int32_t)lroundf(w) < 1 ? 1 : (int32_t)lroundf(w);
    d.start_angle = 0;
    d.end_angle = 360;
    d.color = lv_color_hex(rgb);
    d.opa = THEME_OPA(a);
    lv_draw_arc(layer, &d);
}

void globe_face_home_ring(lv_layer_t *layer, int32_t x, int32_t y, float r, float ring_w, float pulse)
{
    if (pulse >= 0) ring(layer, x, y, 6 + pulse * 10, 1.2f, THEME_BRASS_HI, 0.55f * (1 - pulse));
    ring(layer, x, y, r, ring_w * 1.95f, 0x000000, 0.7f);   /* mockup: 3.5 under 1.8 (live), 3 under 1.5 (analog) */
    ring(layer, x, y, r, ring_w, THEME_BRASS_HI, 1);
    if (pulse >= 0) ring(layer, x, y, 0.8f, 1.6f, THEME_BRASS_HI, 1);   /* live's centre dot, r 1.6 */
}

/* The face's view desc (plan Tasks 8-10, Stage 5 Task 8) and its box origin (assets_meta.h, as the mockup places it):
 * "live", "spin", "analog" (lat0 from the home city now) or "world" (fixed: polar, lat0 0). False for another name.
 * The one place the descs live: a new globe consumer (Stage 3's Omni globe) adds a row here. */
static bool view_desc(const char *name, globe_view_desc_t *d, int32_t *x, int32_t *y)
{
    const double lat0 = home_lat0(settings()->city);
    if (strcmp(name, "live") == 0) {
        *d = (globe_view_desc_t){ .name = "live",
                                  .cfg = { .size = 286, .diam = 262, .lat0_deg = (float)lat0, .proj = OMNI_PROJ_ORTHO },
                                  .base_name = ASSET_LIVE_RING, .over_name = ASSET_LIVE_OVER,
                                  .look = { .gain = 1, .night = 1.25f, .atmo = 1 } };
        *x = ASSET_LIVE_RING_X;
        *y = ASSET_LIVE_RING_Y;
        return true;
    }
    if (strcmp(name, "spin") == 0) {
        const float l[3] = { -0.52f, 0.48f, 0.70f }, n = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
        *d = (globe_view_desc_t){ .name = "spin",
                                  .cfg = { .size = 350, .diam = 316, .lat0_deg = 14, .roll_deg = 23.44f,
                                           .proj = OMNI_PROJ_ORTHO, .studio = true,
                                           .light = { l[0] / n, l[1] / n, l[2] / n } },
                                  .over_name = ASSET_SPIN_OVER, .look = { .gain = 1, .night = 1.1f, .atmo = 0.8f } };
        *x = ASSET_SPIN_OVER_X;
        *y = ASSET_SPIN_OVER_Y;
        return true;
    }
    if (strcmp(name, "analog") == 0) {
        /* The plan's box is 316 px at ana_over's (75, 75). An odd origin loses the box's cover (Task 11: about 60 ms
         * more a frame) and moving it 1 px would put the disc off the dial's centre, so the box is 2 px wider at
         * (74, 74): the disc stays centred at (233, 233) and ana_over sits at (1, 1) in it. Outside the disc the
         * box shows ana_dial itself, so it meets the dial beside it exactly (Task 7's faint square). */
        *d = (globe_view_desc_t){ .name = "analog",
                                  .cfg = { .size = ASSET_ANA_OVER_W + 2, .diam = 300, .lat0_deg = (float)lat0,
                                           .proj = OMNI_PROJ_ORTHO },
                                  .over_name = ASSET_ANA_OVER, .over_x = 1, .over_y = 1, .under_name = ASSET_ANA_DIAL,
                                  .look = { .gain = 1, .night = 1.25f, .atmo = 0.7f } };
        *x = ASSET_ANA_OVER_X - 1;
        *y = ASSET_ANA_OVER_Y - 1;
        return true;
    }
    if (strcmp(name, "world") == 0) {   /* D7: polar, planar path (polar is not compact-eligible) */
        *d = (globe_view_desc_t){ .name = "world",
                                  .cfg = { .size = 338, .diam = 332, .lat0_deg = 0, .proj = OMNI_PROJ_POLAR,
                                           .max_colat_deg = 145 },
                                  .over_name = ASSET_WT_OVER, .under_name = ASSET_WT_FRAME,
                                  .look = { .gain = GLOBE_WORLD_GAIN, .night = 1.35f, .atmo = 1 } };
        *x = ASSET_WT_OVER_X;   /* box origin (64, 64): disc centre (233, 233) */
        *y = ASSET_WT_OVER_Y;
        return true;
    }
    return false;
}
