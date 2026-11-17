#pragma once
/* Display pipeline measurements (Task 1): counters fed by the LVGL display's refresh, render and flush events and by
 * the views that render frames on workers (the Omni's globe views, the `disp bench` view), plus the display path a full-screen
 * view uses and the cover check the opaque views share.
 *
 * A frame is "rendered" when a worker has filled a buffer with it, and "shown" when it was swapped in and an LVGL
 * refresh that drew it has finished (LV_EVENT_REFR_READY after the swap: its last flush is queued, the panel gets it).
 * A frame swapped out again before such a refresh was rendered and never shown. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "sdkconfig.h"

typedef struct {
    uint32_t refr, flushes, flush_px, flush_us_max, flush_wait_us;  /* per window */
    uint32_t lv_render_us_avg, lv_render_us_max;                    /* LV_EVENT_RENDER_START..READY */
    uint32_t frames_rendered, frames_shown;                         /* views: rendered vs swapped-and-flushed */
} disp_stats_t;
/* refr: refreshes that rendered something; flushes and flush_px: flush_cb calls and their pixels; flush_us_max: the
 * longest flush_cb (the adapter's byte swap and board.c's staging hook); flush_wait_us: LVGL's waits for the previous
 * flush (LV_EVENT_FLUSH_WAIT_START..FINISH, a busy loop in LVGL 9.5 without a flush_wait_cb). Any task. */
void disp_stats_get(disp_stats_t *s, bool reset);
void disp_stats_frame_rendered(void);   /* worker side (atomic) */
void disp_stats_frame_shown(void);      /* LVGL side: a refresh that drew the swapped frame finished (its last band is
                                         * queued for the panel, not yet sent, with board.c's early release) */
/* LVGL context, once, after the display starts: hooks the display's events. */
void disp_stats_init(lv_display_t *disp);
/* LVGL context: refreshes that rendered something, finished, since boot (wraps). A view that swapped a frame in at
 * count c knows it was drawn once this differs from c. */
uint32_t disp_stats_refr_done(void);

/* The swap bookkeeping a double-buffered view shares (LVGL context): */
typedef struct {
    bool await;        /* the last swapped frame has not been drawn yet */
    uint32_t refr;     /* disp_stats_refr_done at that swap */
    uint32_t unseen;   /* frames swapped out before they were drawn */
} disp_swap_t;
/* At a swap. Task 1 step 2 measured back-pressure (swap only once the last swap was drawn) and rejected it: with
 * Task 11's pipeline every rendered frame is already shown (shown/rendered 0.99-1.01 on all three faces), and holding
 * the swap cost analog's flip 18.3 -> 13.9 fps. */
void disp_swap_did(disp_swap_t *s);
/* At every poll: counts the last swapped frame shown once a refresh drew it. */
void disp_swap_poll(disp_swap_t *s);

/* The full-screen path the Omni dial's view draws on: a full-frame PSRAM image that a worker renders and LVGL
 * blits (IMAGE), or a full-screen view whose draw event renders each LVGL band straight into the band buffer (BAND).
 * Task 1 chose BAND provisionally on `disp bench` (IMAGE 27.9 fps, BAND 34.4, with a row callback that costs nothing).
 * Stage 3 Task 7 decided IMAGE with the dial compositor (displayed fps, LVGL pinned to core 1):
 * rest 24.4 / 23.6 (BAND), slow ring turn 10.9 / 9.4, whirl 9.9 / 8.7, whirl with the flash 6.3 / 6.1; LVGL's own time a
 * refresh at rest 10.6 ms against 40 ms (BAND renders on the LVGL task, so touch and the other timers wait for it).
 * IMAGE costs two 466 x 466 frames in PSRAM (868 KB). The globe faces use IMAGE (their box) as well. */
typedef enum { DISP_PATH_IMAGE, DISP_PATH_BAND } disp_path_t;
extern const disp_path_t DISP_PATH_FULLSCREEN;

/* An opaque RGB565 image (no alpha, no transform, full opacity) covers its area: register this LV_EVENT_COVER_CHECK
 * handler on it so LVGL starts each redraw at the image instead of drawing what lies beneath (the Omni globe views' box). */
void disp_image_cover(lv_obj_t *img);
void disp_image_uncover(lv_obj_t *img);

#if CONFIG_OMNI_DEBUG_CONSOLE
/* `disp bench`: a synthetic opaque RGB565 view on the active screen, refilled with a moving pattern every frame as fast as
 * the pipeline takes it, as the active screen's last child (LVGL always draws the top layer over the screen, so a
 * cover there would not spare the face beneath). full: 466 x 466 at (0, 0); box: 350 x 350 at (58, 46); band: the full-screen BAND path (no
 * frame buffer: the pattern is drawn into each LVGL band). LVGL context (the console holds the lock).
 * direct (Stage 4a lever B, disp_direct.h): the full-screen object claims the direct path, and a worker on core 1 sends
 * the moving pattern as DISP_DIRECT_ROWS-row chunks (disp_direct_send_chunk), frame after frame, until `disp bench off`
 * (which releases) or the path ends: after an abort the bench idles with LVGL drawing its object; after a refusal
 * (claim or check) it claims again every DISP_DIRECT_BACKOFF_MS, as a client in motion would.
 * direct overlay: after 30 direct frames a 40 x 40 object goes on lv_layer_top() (the next check refuses OVERLAY);
 * direct inval: a 200 x 20 box of the object is invalidated every 33 ms (LVGL renders it; the flush is dropped).
 * direct stall: after 30 frames the worker holds one band 600 ms inside its fill (the orphan rule, a stuck release and
 * the stuck bands' free, D1/I1). */
typedef enum { DISP_BENCH_OFF, DISP_BENCH_FULL, DISP_BENCH_BOX, DISP_BENCH_BAND, DISP_BENCH_DIRECT,
               DISP_BENCH_DIRECT_OVERLAY, DISP_BENCH_DIRECT_INVAL, DISP_BENCH_DIRECT_STALL } disp_bench_t;
bool disp_bench_start(disp_bench_t mode);   /* stops a running one first; false: no PSRAM */
void disp_bench_stop(void);
disp_bench_t disp_bench_mode(void);
const char *disp_bench_name(disp_bench_t mode);
#endif
