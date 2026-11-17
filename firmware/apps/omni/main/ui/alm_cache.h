#pragma once
/* The almanac's two bitmaps off the LVGL lock (R140): the sky ring (omni_alm_ring_render, 438 px RGB565) and the moon
 * (omni_alm_moon_render, 88 px RGB565A8) are rendered by a low-priority worker into PSRAM and kept here, in a cache
 * that outlives the face: one ring (by omni_alm_ring_key_t) and one moon (by its key, round(f x 400)). A face switch
 * back on the same day shows the cached ring at once; a new day, city or zone renders once, off the lock, while the
 * face shows the ring it has (or its plain ring before the first one lands).
 *
 * The worker (core 1, priority 1, stack in PSRAM; its TCB is internal, like the globe views' workers) serves the face:
 * alm_cache_start at create, alm_cache_stop at destroy. It never touches LVGL: the face dispatches jobs and swaps
 * finished buffers in LVGL context (alm_cache_poll), the only place a shown buffer changes, and the old one is freed
 * only after the images were re-pointed. At most one buffer of each kind is rendering at a time: a request made
 * meanwhile waits (only its latest parameters) and is dispatched when the one in flight lands, unless its key moved on.
 * alm_cache_stop never waits (review I2): it detaches the worker, which reads quit before each job, frees the buffers
 * of any job it still holds itself and parks; an lv_timer (or the next start) deletes it then, or the next start takes
 * it back if it is still finishing a render.
 *
 * THREADING: every call is LVGL context (the face's hooks and timers have the lock). */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "omni/alm_face.h"

typedef struct {
    omni_alm_ring_key_t key;
    int64_t now;                 /* any instant of the day (omni_alm_ring_render reads its local date) */
    double lat, lon;
    float rise_h, set_h;         /* local clock hours of the ticks, < 0: none */
    bool set;
} alm_ring_job_t;

bool alm_cache_start(void);      /* the worker; false: none (no memory), the face then shows its plain ring */
void alm_cache_stop(void);       /* keeps a finished render, detaches the worker (no wait); the cache stays */

lv_draw_buf_t *alm_cache_ring(const omni_alm_ring_key_t *k);   /* the cached ring if it is k's, else NULL */
lv_draw_buf_t *alm_cache_moon(long *key);                      /* the cached moon (any key, *key set) or NULL */
void alm_cache_ring_want(const alm_ring_job_t *j);             /* render j unless cached or in flight */
void alm_cache_ring_keep(const omni_alm_ring_key_t *k);        /* k (shown) is wanted again: a render for another
                                                                * key still in flight is dropped when it lands */
void alm_cache_moon_want(long key, double f);                  /* the moon for illuminated phase f (key round(f*400)) */
bool alm_cache_busy(void);                                     /* a render is in flight or waiting */
/* A finished render is taken: a ring for the latest wanted key becomes the cached ring and ring_img (if not NULL)
 * shows it, likewise the moon and moon_img; the old buffers are freed after. Bit 0: the ring changed, bit 1: the moon.
 * Each render's time and the worker's stack high-water mark are logged at INFO ("almanac: ring <us> us (worker, stack
 * free <n> B)", likewise "moon"; ", stale" when dropped). */
unsigned alm_cache_poll(lv_obj_t *ring_img, lv_obj_t *moon_img);
