#pragma once
/* The globe engine all three globe faces render through: an S x S RGB565 lv_image whose two PSRAM buffers a render
 * worker fills. The worker (core 1) and its helper (core 0) - two tasks per view, stacks in PSRAM (each TCB is in
 * internal RAM: xTaskCreatePinnedToCoreWithCaps keeps it there), priority 2 (4 while building) - build the LUT
 * (packed into the compact LUT for ortho globes), the PSRAM texture, the base and the overlay spans, then render
 * each submitted frame into the back buffer in row chunks on both cores. Neither
 * touches LVGL or the panel: the face swaps the buffers in LVGL context (globe_view_poll). The opaque box covers its
 * area (LVGL never redraws beneath it) when its origin is even (the panel rounds redraws to even bounds).
 *
 * THREADING: every call needs the LVGL lock (LVGL context), as faces have it. The console's bench and selftest
 * (below) are called with the lock held too: they run on the worker and wait for it, so nothing submits meanwhile. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "sdkconfig.h"
#include "omni/globe.h"
#include "omni/globe_compact.h"
#include "ui/assets_meta.h"

#define GLOBE_TEX ASSET_GLOBE_TEX           /* "earth512.tex": the value Task 5 step 4 recorded (R53) */

typedef struct {
    const char *name;                      /* "live" | "spin" | "analog": logs and stats */
    omni_globe_cfg_t cfg;                  /* tex_w is filled from GLOBE_TEX */
    const char *base_name;                 /* box-sized ARGB asset composited over black; NULL = black */
    const char *over_name;                 /* ARGB overlay asset, at (over_x, over_y) in the box; NULL = none */
    int16_t over_x, over_y;
    /* Screen-sized RGB565 asset (the screen's background) shown as it is outside the disc: the box's crop of it,
     * at the box origin, with the overlay dropped there. The box then matches the background beside it pixel for
     * pixel (the overlay over black packs differently from the asset's own RGB565). NULL = none. */
    const char *under_name;
    omni_globe_look_t look;
} globe_view_desc_t;

typedef struct {
    double lon0_deg; omni_globe_sun_t sun; float gain; uint8_t fade;
    bool halo; float halo_dir[2]; float halo_strength;      /* rebuilt into the base only when these change */
    bool still;   /* R76: a frame at rest (the rest cadence, or the settled frame after motion or a reveal): rendered
                   * with the linear-in-u kernel (omni_globe_cspan_lin_fast, softer coastlines, about 2.2x the time);
                   * frames in motion keep the PIE nearest kernel (bit-exact with cspan_ref, `globe selftest`) */
} globe_view_frame_t;
/* halo: draw omni_globe_halo round the disc (strength halo_strength). halo_dir: the sun-side lobe's direction in
 * box pixels (x right, y down); {0, 0} = no lobe. */

typedef struct globe_view globe_view_t;
globe_view_t *globe_view_create(lv_obj_t *parent, int32_t x, int32_t y, const globe_view_desc_t *d); /* NULL: no PSRAM / assets */
void globe_view_destroy(globe_view_t *v);  /* stops the worker and waits for it (the worker never takes the LVGL lock), frees all */
bool globe_view_ready(const globe_view_t *v);           /* LUT, texture, base and overlay built (the worker does it) */
bool globe_view_failed(const globe_view_t *v);          /* the build failed (logged): terminal, the view stays blank */
/* LVGL ctx; the latest pending wins. Returns the frame's sequence number (1, 2, ...: never 0), which
 * globe_view_shown reports once the frame is on screen. */
uint32_t globe_view_submit(globe_view_t *v, const globe_view_frame_t *f);
/* LVGL ctx: a finished frame is swapped in and the box invalidated (true); one submitted before the last
 * globe_view_reset is dropped instead (false). */
bool globe_view_poll(globe_view_t *v);
uint32_t globe_view_shown(const globe_view_t *v);   /* LVGL ctx: the sequence number of the frame on screen (0: none) */
/* What a reset box shows: black (a reveal from fade 0), or the box without its globe (the base, or under_name
 * outside the disc, and the overlay, over a black disc: a reveal at fade 255 that brightens the globe in). */
typedef enum { GLOBE_BLANK_BLACK, GLOBE_BLANK_EMPTY } globe_view_blank_t;
/* LVGL ctx (I1): the shown buffer becomes the blank look at once, and every frame submitted or rendered before this
 * call is dropped, never shown: a wake's first redraw then shows the reveal's start, not the frame from before the
 * sleep. Before the view is built the box still shows its initial look, so this only drops. */
void globe_view_reset(globe_view_t *v, globe_view_blank_t look);
const omni_globe_cfg_t *globe_view_cfg(const globe_view_t *v);
lv_obj_t *globe_view_obj(globe_view_t *v);
typedef struct { uint32_t frames, render_us_avg, render_us_max, base_us_max, build_ms, bytes, resets, dropped, unseen; } globe_view_stats_t;
/* frames: rendered by the worker; resets: globe_view_reset calls; dropped: frames a reset kept off the screen; unseen:
 * frames swapped in and out again before a refresh drew them */
void globe_view_stats(globe_view_t *v, globe_view_stats_t *s);
globe_view_t *globe_view_current(void);    /* the one live view, or NULL */
void globe_view_hold(globe_view_t *v, double lon_deg); /* NAN = off; overrides lon0 (screenshots) */
extern omni_globe_span_fn globe_view_kernel;           /* planar LUTs: omni_globe_span_fast, bit-exact with span_ref */
extern omni_globe_cspan_fn globe_view_ckernel;         /* compact LUTs (ortho): globe_cspan_pie, = cspan_ref */
extern void (*globe_view_over_run)(const uint32_t *argb, uint16_t *dst, int n);   /* compact overlay: globe_over_pie */

const char *globe_view_name(const globe_view_t *v);

/* ---- Console (the gated `globe` command; from the console task, holding the LVGL lock) */
#if CONFIG_OMNI_DEBUG_CONSOLE
/* `globe kernel none`: v's frames are swapped without being rendered (LVGL's own ceiling). This view only: the next
 * view (a face switch, a wake that rebuilds) renders again (R100/R101, as `dial path`/`dial perf`). */
void globe_view_null_render(globe_view_t *v, bool on);
/* Pauses submissions and renders n frames of the last submitted frame (turning 1.5 deg a frame): the reference and
 * the active kernel on one core, the frame on both cores (full, and kept: same base and overlay), kept without the
 * overlay; then the base rebuild (halo lobe), the shade build, PSRAM rates and cold/hot ns per disc pixel. Prints
 * "BENCH <name> ..." lines. A halo base the view's own frames never asked for (analog) is freed again after. */
void globe_view_bench(globe_view_t *v, int n);
/* The 5 frames (lon0 -179.9, -60, 0, 60, 179.9, the last frame's sun): the reference whole (C kernel and overlay) vs
 * the active kernel in two row halves, vs the dual-core frame, and (compact) vs the dual-core frame kept over a
 * buffer drawn for another lon0, memcmp; each frame's spans also through omni_globe_(c)kernel_bitexact; then the
 * libm-free synthetic digests (planar and compact) of the reference and active kernels. Prints
 * "GLOBE SELFTEST PASS 5/5" (or FAIL k/5 with the reason). True on a pass. */
bool globe_view_selftest(globe_view_t *v);
#endif
unsigned globe_view_worker_stack_free(const globe_view_t *v);   /* the worker's stack high-water mark, bytes */
unsigned globe_view_helper_stack_free(const globe_view_t *v);   /* the helper's */
