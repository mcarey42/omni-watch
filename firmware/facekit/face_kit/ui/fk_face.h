#pragma once
/* The face runtime (fk_face.c; from the first theme's face_generic.c, face_kit since rd wave 1 Task 2):
 * omni_face_vt_t's callbacks take no context, so each face gets static trampolines bound to its ops (A18/B6).
 * FK_FACE_VT(id, ops) emits them, `const omni_face_vt_t id##_VT`, whose designated initialiser is the zero
 * initialisation (every member it does not name is 0: claim and Stage 5's holds stay NULL, E2; own_lens is true: the
 * faces draw their own stale marker), and id##_bind(rt), which points the trampolines at rt and sets rt->ops. A theme
 * keeps its fk_face_rt_t blocks in PSRAM (R147 PR9): its register_faces allocates them and binds each face before it
 * registers it, so a trampoline never runs unbound. The theme's own work goes through its hooks (fk_face_set_hooks),
 * set once before the faces register. */
#include <stdbool.h>
#include <stdint.h>
#include "fk_view.h"

/* One face's state on the shell: the trampolines' only context. */
typedef struct fk_face_rt {
    const fk_face_ops_t *ops;
    fk_view_t *view;
    lv_timer_t *timer;   /* motion at rest (ops->cadence_ms), awake only */
    bool alt;            /* FK_TAP_ALT toggles it */
    omni_wake_track_t wt;   /* why a wake came (omni/reveal.h) */
    int64_t reveal_t0;   /* esp_timer us at the reveal's start: wake_ms while it runs */
    int64_t sun_min;     /* the minute the sun was last looked up for, and what it found */
    uint16_t sun_city;
    int64_t sun_next_s;
    uint8_t sun_kind;
    uint8_t sun_h, sun_m;   /* the event's local time */
    int16_t sun_rise_m, sun_set_m;   /* today's local sunrise and sunset in minutes, -1 none (the sun_today hook) */
    /* a face's own wake (ops->wake_len_ms, no reveal) and a FK_TAP_PULSE (ops->pulse_len_ms): running, since
     * esp_timer us */
    bool own_wake, pulse;
    int64_t own_t0, pulse_t0;
    int64_t alt_t0;      /* esp_timer us at the FK_TAP_ALT that set alt (ops->alt_len_ms) */
    lv_timer_t *alt_timer;   /* the one shot that ends alt_len_ms */
    bool fresh;          /* a revealing wake at fresh_t0 (esp_timer us): in->fresh_ms */
    int64_t fresh_t0;
    /* the face's variant (in->variant). A FK_TAP_CYCLE moves it on (fk_variant_next), a tap_var may set it. It lives
     * here, not in the view, so it survives the view and the sleep; RAM only: 0 again after a reboot (the blocks are
     * allocated zeroed once at boot), never written to NVS. uint16_t (rd wave 1 Task 2; the first theme's was
     * uint8_t), wrapped modulo 255 by fk_variant_next as before */
    uint16_t variant;
} fk_face_rt_t;

/* The theme's hooks (every one optional; NULL: none). No chime hook: fk_face fills the chime channels from
 * fk_chime_svc_fill (fk_chime_svc.h). */
typedef struct {
    /* today's local sunrise and sunset (minutes) for the theme's home city, -1 none; called once a minute for a face
     * with ops->uses_sun while the clock is set and a city is chosen */
    bool (*sun_today)(int64_t now, int16_t *rise_m, int16_t *set_m);
    bool (*take_lift)(void);                  /* a lift's wake: the reveal whatever the style (true once) */
    void (*chime_preview)(void);              /* FK_TAP_CHIME */
    void (*slept)(fk_face_in_t *in);          /* fill in->slept_s (0 without the hook) */
    void (*on_sleep)(void);                   /* every sleep, whichever face */
    /* appended (rd wave 1 Task 9 fix round): every wake of a face, before its first frame, with why it came (the
     * face's own tracker: FIRST for a create's first wake, a swipe, the picker, the boot; AFTER_SLEEP after a sleep),
     * the shell's reveal flag (false for a re-lift within 10 s or a screen closing) and whether a lift's take_lift
     * gave the reveal. The first theme sets none. */
    void (*woke)(omni_wake_cause_t cause, bool reveal, bool lift);
} fk_face_hooks_t;
void fk_face_set_hooks(const fk_face_hooks_t *h);   /* once, before the faces register; h is kept */

void fk_face_create(fk_face_rt_t *rt, lv_obj_t *parent);
void fk_face_update(fk_face_rt_t *rt, int64_t now, bool glance);
void fk_face_destroy(fk_face_rt_t *rt);
void fk_face_wake(fk_face_rt_t *rt, bool reveal);
void fk_face_sleep(fk_face_rt_t *rt);
void fk_face_gesture(fk_face_rt_t *rt, const omni_gesture_t *g, uint32_t t_ms);

#define FK_FACE_VT(id_, ops_)                                                                                         \
    static fk_face_rt_t *id_##_rt;                                                                                    \
    static void id_##_bind(fk_face_rt_t *rt)                                                                          \
    {                                                                                                                 \
        rt->ops = &(ops_);                                                                                            \
        id_##_rt = rt;                                                                                                \
    }                                                                                                                 \
    static void id_##_create(lv_obj_t *p) { fk_face_create(id_##_rt, p); }                                           \
    static void id_##_update(int64_t now, bool glance) { fk_face_update(id_##_rt, now, glance); }                    \
    static void id_##_destroy(void) { fk_face_destroy(id_##_rt); }                                                   \
    static void id_##_wake(bool reveal) { fk_face_wake(id_##_rt, reveal); }                                          \
    static void id_##_sleep(void) { fk_face_sleep(id_##_rt); }                                                       \
    static void id_##_gesture(const omni_gesture_t *g, uint32_t t) { fk_face_gesture(id_##_rt, g, t); }              \
    const omni_face_vt_t id_##_VT = {.name = #id_, .create = id_##_create, .update = id_##_update,                 \
                                      .destroy = id_##_destroy, .wake = id_##_wake, .sleep = id_##_sleep,           \
                                      .gesture = id_##_gesture, .own_lens = true}
