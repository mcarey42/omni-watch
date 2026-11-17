#pragma once
/* A theme's faces on the shell (from the first theme's wave 1 Task 10; face_kit since rd wave 1 Task 2).
 *
 * fk_view: one PSRAM RGB565 466 x 466 lv_canvas per live face, drawn by its fk_face_ops_t (fk/face.h). The view
 * also holds the face's context (ops->ctx_bytes, PSRAM), bound to the mapped assets at create. It starts black and
 * stays black until its first drawn frame (R102/R103). A revealing face (ops->reveal) opens with its reveal
 * (ops->reveal_ops, fk/reveal.h) through the Stage 4a direct path (fk_view.c). LVGL lock held for every call.
 *
 * The runtime (fk_face.h, fk_face.c) gives each face the shell's life cycle on top of a view. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "shell.h"
#include "fk/face.h"
#include "fk/reveal.h"
#include "omni/band.h"
#include "omni/reveal.h"
#include "sdkconfig.h"

typedef struct fk_view fk_view_t;
fk_view_t *fk_view_create(lv_obj_t *parent, const struct fk_face_ops *ops);  /* NULL: no PSRAM (logged; black) */
void fk_view_draw(fk_view_t *v, const struct fk_face_in *in, bool full);     /* full, or only ops->dirty's rects */
void fk_view_set_glance(fk_view_t *v, bool glance);   /* ops->glance from now on (or ops->draw); the next draw is full */
void fk_view_destroy(fk_view_t *v);
const void *fk_view_ctx(const fk_view_t *v);   /* the face's bound context (ops->tap reads it) */
/* The reveal (a revealing face): from in (its clock; wake_ms is the reveal's own), the reveal or, reduced, its
 * cross-fade. False (a plain wake then) without the reveal's assets or the interior's PSRAM, or for a face with reveal
 * set and no reveal_ops (a configuration error, logged once). While it runs, draws are kept for the end, which draws
 * the newest input settled. */
bool fk_view_reveal(fk_view_t *v, const struct fk_face_in *in, bool reduced);
void fk_view_reveal_skip(fk_view_t *v);          /* a tap: the end frame next */
void fk_view_reveal_stop(fk_view_t *v);          /* sleep, glance, destroy: cut at once, the settled frame drawn */
bool fk_view_reveal_active(const fk_view_t *v);
/* (the first theme's wave 2 Task 14) A .direct face's rest motion: on while the face is awake (fk_face's wake), off at
 * the sleep. While on and admitted, the view holds the direct path and its worker and helper (the reveal's two cores
 * and 8-row bands) draw whole frames at the panel's rate from the newest input (fk_face_in_advance); the face's draws
 * are then kept for the worker. Refused or cut (an overlay, a swipe, the glance, reduced motion, the bench, the panel
 * off), the frames go through the canvas at the face's cadence, and the claim is tried again (fk_dm_retry_at). One
 * holder of the direct path at a time (fk_dm_may_claim): never shared with a lift's reveal or the console's bench.
 * No-op for a face without .direct. */
void fk_view_motion(fk_view_t *v, bool on);

/* The console (a theme's reveal bench, its verify): console builds only (R101). */
#if CONFIG_OMNI_DEBUG_CONSOLE
typedef struct {
    uint32_t swings, frames, us_sum, us_max, lost, psram_min, int_min, refused, direct;
    uint32_t stack_hwm;                                   /* the worker's stack high-water mark, bytes */
    uint32_t prep_sum, prep_max;   /* us drawing the frames' interior (the face's columns), summed over both cores */
    bool done, cut;
} fk_reveal_stats_t;
fk_view_t *fk_view_current(void);                         /* the live revealing face's view, or NULL */
const fk_face_ops_t *fk_view_ops(const fk_view_t *v);
bool fk_view_reveal_bench(fk_view_t *v, int n, bool copy);  /* n reveals back to back through the real path */
bool fk_view_reveal_bench_result(const fk_view_t *v, fk_reveal_stats_t *out);   /* false until done */
/* copy: every asset the reveal's bind reads (its leaf) copied to PSRAM for real reveals (default off: read in place) */
void fk_view_set_copy(bool on);
/* A theme's own direct-path bench brackets its run with begin and end. Begin refuses (false) while the reveal runs on
 * the revealing face's view or a bench is on; while it is on, fk_view_reveal refuses (a plain wake, as during the
 * reveal bench), and no view claims the direct path (a .direct face's rest motion ends at its next poll and runs
 * through the canvas). The bench claims and releases through fk_view_bench_claim and _release, under the one holder
 * rule (OMNI_DD_BACKOFF while a view still holds the path). LVGL lock held. */
bool fk_view_bench_begin(void);
void fk_view_bench_end(void);
omni_dd_why_t fk_view_bench_claim(lv_obj_t *obj);
void fk_view_bench_release(void);
#endif
