#pragma once
/* dial_view's insides, shared by dial_view.c (the view: build, frames, paths) and dial_console.c (its console jobs:
 * print, bench, verify, selftest). Nothing else includes this. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "omni/omni_dial.h"
#include "omni/rects.h"
#include "ui/dial_view.h"
#include "disp_direct.h"

#define SCR OMNI_DIAL_W
#define STACK 16384         /* each task's, in PSRAM: the compositor's deepest path is about 5.3 KB (R85); the bench and
                             * selftest jobs and their printf of doubles add to it (8 KB overflowed in `dial bench`; the
                             * jobs leave about 8 KB of 16 free); `dial` prints the marks */
#define CHUNK_ROWS 16       /* IMAGE: rows a core claims at a time (one omni_dial_render band) */
#define MAX_CHUNKS 256
#define LOUPE_STEPS 4       /* the loupe's sizes (R85 measured: a map rebuild is 73 ms, so none happens per frame) */
#define FAST_LON 20.0f      /* deg/frame: the whirl scenario's globe */
#define DV_SCREEN ((omni_rect_t){ 0, 0, SCR, SCR })

/* `dial perf` (R88): the performance round's kept candidates, each switchable for a before/after measurement. TEX is
 * the next view's (the texture copy is made at its build); DRIFT and LOSSY are the current view's, and die with it. */
enum { PERF_TEX = 1, PERF_DRIFT = 2, PERF_LOSSY = 4, PERF_RENDER = 8, PERF_BASE = 16 };
#define PERF_NEXT_VIEW (PERF_TEX | PERF_BASE)   /* taken at the view's build (R114 for BASE) */
#define PERF_DEFAULT (PERF_TEX | PERF_DRIFT | PERF_LOSSY | PERF_RENDER | PERF_BASE)
/* R100/R101: a view's mask derives from PERF_DEFAULT, and the default renders (the Task 10 incident: a mask without
 * PERF_RENDER showed stale PSRAM; the skip itself is compiled only with the debug console) */
_Static_assert((PERF_DEFAULT & PERF_RENDER) != 0, "PERF_DEFAULT must render");

/* Everything the tasks and LVGL context share is guarded by this spinlock (a few word copies at a time). */
extern portMUX_TYPE dial_mux;

typedef omni_rects_t rects_t;
/* still: the linear-in-u globe kernel (R76); mbox: the marker's pixels (omni_dial_marker_box) */
typedef struct { omni_dial_frame_t f; bool still; omni_rect_t mbox; } vframe_t;
/* What the under stage (base, rings, V marks, hands, top) depends on: a cached under is this frame's while it is equal */
typedef struct { omni_rings_pose_t pose; float hour, min, sel_old[OMNI_RINGS], sel_new[OMNI_RINGS], mark_old, mark_new;
                 bool fast; } ukey_t;
typedef enum { JOB_NONE, JOB_BENCH, JOB_BENCH_IRIS, JOB_SELFTEST, JOB_RINGTEST, JOB_VERIFY } job_t;
typedef void (*help_fn)(dial_view_t *v, int who, int a, int b);
/* Lever B (Stage 4a Task 5, D7, D8, B15-B19): IMAGE at rest and for everything else; DIRECT while full-screen motion
 * goes straight to the panel through disp_direct (internal-RAM bands, the PSRAM buffers untouched); EXIT once the exit
 * frame is in the back buffer, until poll releases the path and swaps it in. */
typedef enum { DV_IMAGE, DV_DIRECT, DV_EXIT } dv_mode_t;
enum { DV_EX_STILL, DV_EX_BELOW, DV_EX_REQ, DV_EX_EPOCH, DV_EX__COUNT };   /* what made a frame the exit frame */

struct dial_view {
    disp_path_t path;
    lv_obj_t *obj;                /* IMAGE: the lv_image; BAND: the drawing object */
    bool shown;                   /* the object has been unhidden (a first frame went in) */
    bool want_on;                 /* dial_view_show's (true from create) */
    /* the blank's fields (blanked .. blank_max_us) are LVGL context only: the LVGL lock guards them (`dial` holds it) */
    bool blanked;                 /* dial_view_blank: hidden until a frame submitted after it is on screen (T8 m1) */
    uint32_t sub_seq, fresh_seq;  /* LVGL ctx: submits so far (the pending frame's number); the blank's first fresh one */
    int64_t blank_t0;             /* the blank's start (esp_timer us), for `dial` */
    uint32_t blank_n, blank_last_us, blank_max_us;
    lv_draw_buf_t *buf[2];        /* IMAGE: buf[0] is the image's source; its data is px[front] */
    uint16_t *px[2];
    int front;
    bool buf_valid[2];            /* worker: px[i] holds a whole frame (else the next render there is the screen) */
    /* resources: built by the worker, read-only once `built` (but the loupe map pointers, which the frame's renderer sets
     * before both cores start: the worker in IMAGE, poll in BAND) */
    omni_dial_res_t res;
    omni_strip_t strip[3];
    void *l0_copy;                /* R79: the L0 strip's planes in PSRAM */
    size_t l0_bytes, strip_flash;
    void *map_mem;
    size_t map_bytes;
    bool map_loaded;
    void *mag_mem;
    void *ov_mem[8];
    void *lut_mem;
    size_t lut_bytes;
    omni_globe_lut_t lut;
    void *clut_mem;
    omni_globe_clut_t clut;
    omni_globe_shade_t *shade;
    omni_globe_tex_t tex;
    omni_globe_cspan_fn kmove;    /* the motion kernel: PIE (cspan_ref bit for bit) when the shade table allows */
    omni_rect_t gbox;             /* the globe's box */
    rects_t disc;                 /* the disc as 16-row bands (its spans' extent + DISC_MARGIN): what a drift step changes */
    rects_t chord;                /* 8-row bands at the spans' exact extent: the drift path's pieces */
    uint16_t *ucache;             /* the under stage over the chord bands, in the globe box's layout (stride = its side) */
    ukey_t ukey;
    bool u_ok;
    omni_rect_t mbox_pp;          /* the marker box of the frame before prev (the back buffer's frame) */
    bool ukey_rings;              /* the chord bands reach the rings' radii: the cache's key includes them */
    float chord_rmax;

    vframe_t bufframe[2];         /* the frame each buffer holds (`dial verify` checks both) */
    struct { uint8_t path, back, nr, dd, lv[4], fast; int16_t y0, y1; float min, hour; } trace[16];   /* the last renders */
    uint32_t ntrace;
    unsigned perf;                /* under dial_mux: PERF_* (TEX as this view was built) */
    uint32_t perf_epoch;          /* under dial_mux: bumped by `dial perf`, the view re-renders both buffers whole */
    uint32_t perf_seen;           /* worker: the epoch its buffers were drawn under */
    uint32_t drift_frames, u_builds;
    uint32_t minus_full;          /* M7: lists less the loupe that overflowed OMNI_RECTS_MAX, rendered whole (atomic) */
    int disc_pieces, chord_pieces;   /* the bands less the open loupe, counted at the build (-1: they overflow) */
    /* worker, read and cleared by `dial` under dial_mux (Task 12): each ring's fidelity level per rendered frame (the
     * fidelity rule, L0-L2) and the flash's frames with their render time, since the last `dial` */
    uint32_t lvl_n[OMNI_RINGS][3], flash_frames, flash_us_max;
    uint64_t flash_us_sum;
    omni_rect_t lossy[2];         /* R88 candidate 5: the box of each buffer's pixels rendered at half rows */
    void *base_copy;              /* R114 (lever A): omni_base in PSRAM while the Omni is live (434 312 B) */
    void *tex_copy;               /* R88 candidate 3: the texture in PSRAM (512 KB) */
    int16_t *lid_run;             /* Task 9: omni_dial_lid_runs of the flash-mapped lid (res.lid), 4 per row, then
                                   * omni_dial_lid_spec (res.lid_spec, 3 per column): one PSRAM block */
    /* worker, read by `dial` under dial_mux: the reveal's frames (the lid or the iris on: Tasks 9, 10), their render
     * time and the PSRAM free after each (its low: the reveal's peak, R70), since the last `dial` */
    uint32_t reveal_frames, reveal_us_max, reveal_psram_low;
    uint64_t reveal_us_sum;
    /* the window's first reveal frames: v the lid's angle in degrees, or the iris's aperture in px; render time */
    struct { float v; uint32_t us; bool iris; } reveal_log[24];
    /* the loupe at its LOUPE_STEPS sizes, a = (i + 1) / LOUPE_STEPS: maps and zoomed bases, built with the view */
    void *loupe_mem[LOUPE_STEPS];
    omni_pmap_t loupe_map[LOUPE_STEPS];
    uint16_t *loupe_base[LOUPE_STEPS];
    bool loupe_ok[LOUPE_STEPS];
    uint32_t loupe_us_max, loupe_us_sum;
    bool loupe_miss_logged;
    uint16_t *band_scratch[2];    /* BAND: a 466 x BAND_CHUNK row block per claimer (0: the LVGL task, 1: the helper) */
    /* tasks */
    TaskHandle_t task, helper;
    SemaphoreHandle_t exited, h_exited, help_done, job_done;   /* exited: the worker's, h_exited: the helper's */
    esp_pm_lock_handle_t pm;
    /* the helper's job (written before the notify; under dial_mux: pending, started) */
    help_fn h_fn;
    int h_a, h_b;
    bool h_pending, h_started;
    /* a render job: chunks claimed by both cores, from one prepared frame (omni_dial_prepare: T6 M4) */
    omni_dial_prep_t *prep;       /* PSRAM; prepared from a frame that outlives the job (it points into it) */
    uint16_t *j_fb;               /* IMAGE: the whole frame (stride SCR) */
    int j_stage;                  /* 0: the whole render, 1: the under stage, 2: the over stage */
    bool j_lossy;                 /* every other row rendered, the next one its copy (candidate 5) */
    uint16_t *j_dst;              /* BAND: the layer's pixel at (j_x0, j_y0), stride j_stride */
    int j_stride, j_x0, j_y0;
    omni_rect_t j_chunk[MAX_CHUNKS];
    int j_n;
    volatile int j_next;          /* under dial_mux */
    /* the frame history: IMAGE worker / BAND LVGL context */
    vframe_t prev, cur;
    bool have_prev;
    rects_t d_prev;               /* IMAGE: the dirty rects of the last rendered frame against the one before */
    bool d_prev_drift;            /* d_prev was a drift step (the disc's bands) */
    /* shared under dial_mux */
    bool built, failed, quit, h_quit, ready, has_pending, has_last;   /* h_quit: the helper's, set after the worker's exit */
    omni_dial_state_t pending, last;
    uint32_t pending_seq;         /* pending's submit number */
    uint32_t prev_seq;            /* the submit number of the last frame rendered (or found the same as it): with !ready
                                   * (IMAGE) it is the state on screen (or to be, at the next refresh) */
    float hold;
    float lid_hold;               /* `dial lid <deg>` (radians; NAN off): the lid held at that angle, for shots */
    int32_t iris_hold;            /* `dial iris <ms>` (-1 off): the iris held at that time of its timeline, for shots */
    rects_t back_rects;           /* IMAGE: what the frame in the back buffer changed (poll invalidates it) */
    job_t job;
    bool job_ok;
    uint32_t build_ms, bytes;
    uint32_t st_frames, st_max, st_late, st_late_drift;   /* late: a render over 30 ms (of which drift-path frames) */
    uint64_t st_sum, st_px;
    /* LVGL context */
    disp_swap_t swap;
    uint32_t band_conflicts;
    int64_t band_t0;              /* BAND: the refresh's first band draw */
    uint32_t band_us;
    /* lever B: the direct path's mode machine (IMAGE only). Under dial_mux unless noted. LVGL context changes mode
     * (claim, exit, abort) and bumps mode_gen with it; a worker frame that finds mode_gen moved under it was claimed or
     * aborted meanwhile (B19). */
    dv_mode_t mode;
    uint32_t mode_gen;
    bool want_direct;             /* the worker's last IMAGE frame was full-screen motion: poll may claim (once) */
    bool exit_req;                /* a check refused OFF, OVERLAY, PROV or RAM: the next frame exits (B19) */
    bool still_req;               /* the worker's first still frame after motion: poll calls disp_direct_still (R2-1) */
    bool exit_still;              /* the exit frame was still and exact: its release ends the back-off (D7) */
    bool dd_dead;                 /* a direct frame was cut short, mode_gen unchanged: the worker waits for the abort */
    bool buf_old[2];              /* buffer i holds bufframe[i], older than prev: d_prev does not describe it (B17) */
    int64_t backoff_until;        /* esp_timer us: no claim before (a refusal; a still frame clears it, D7) */
    uint32_t lost_seen;           /* disp_direct_lost as the worker last accounted for it (D3) */
    uint32_t dd_last_seq;         /* the last direct frame's seq (frame_end) */
    uint32_t dd_ended;            /* direct frames ended: poll runs disp_direct_check once each (D7) */
    uint8_t below_n;              /* worker: consecutive direct frames under DISP_DIRECT_MIN_PX (D8) */
    bool w_moved;                 /* worker: a motion or direct frame since the last calm one (R2-1, M4) */
    uint32_t w_gen, dd_gen;       /* worker: the mode_gen its frame history follows; the claim it did the entry for */
    vframe_t panel;               /* worker: the frame last sent whole (what the panel shows, unless panel_unknown) */
    bool panel_unknown;           /* worker: a band was lost (D3): the exit invalidates the whole screen */
    omni_rect_t lossy_panel;      /* worker: the box of the panel's half rows (B18) */
    uint32_t dd_seq;              /* worker: the frame numbers it gave disp_direct_frame_end (from shown at entry) */
    int dd_front;                 /* the front buffer at the claim (poll, under dial_mux): the panel the entry models */
    uint32_t dd_checked, dd_shown;   /* LVGL ctx: dd_ended checked; disp_direct_shown at the last poll */
    int64_t exit_t0;              /* LVGL ctx: the exit frame first seen ready (the wait for the last direct frame) */
    /* the direct job (render tasks): send chunk j_chunk[i] through disp_direct_send_chunk (j_direct), its rows rendered
     * from prep, or copied from j_src (a frame already in a PSRAM buffer); j_dry (console): the same, packed into
     * j_dry_band[who] and un-swapped into j_dry_out instead of sent (`dial verify`) */
    bool j_direct, j_dry;
    volatile bool j_dd_fail;      /* a chunk was not sent (the path was aborted) */
    unsigned j_perf;
    const uint16_t *j_src;
    uint16_t *j_dry_out, *j_dry_band[2];
    /* Task 6: LVGL overlays (lov) blended into the direct bands. LVGL context publishes the registry's set (lov_pub,
     * its pixels the registry's sprites) at the claim and then once per direct frame, after the worker took the last
     * one; the worker copies it into its own buffer (lov, lov_mem) at each direct frame's start. lov_writing and
     * lov_copying, under dial_mux, keep the two apart: a publish skips a poll while the worker copies, the worker waits
     * out a publish (its snapshots, <= 2 ms each). lov_areas: the rects the published sets changed since the worker
     * last took them. */
    omni_band_overlay_t lov_pub[DISP_DIRECT_OVERLAYS];
    int lov_pub_n;
    uint32_t lov_pub_gen;         /* disp_direct_overlay_gen() at the publish (m3) */
    rects_t lov_owed_pub;         /* m1: rects of overlays LVGL shows but whose snapshot failed (a publish adds, take
                                   * moves) */
    rects_t lov_areas;
    bool lov_fresh, lov_writing, lov_copying;   /* under dial_mux */
    bool lov_self;                /* LVGL ctx: the client calls dial_view_overlays_publish after its steps */
    omni_band_overlay_t lov[DISP_DIRECT_OVERLAYS];   /* worker: the set its direct frames blend (pixels in lov_mem) */
    int lov_n;
    uint32_t *lov_mem;            /* worker: PSRAM, grown to fit, freed with the view */
    size_t lov_cap;
    bool lov_entry;               /* worker: the claim's first direct frame (every overlay's rect joins L) */
    rects_t lov_owed;             /* worker: rects the hand-back redraws because a copy or snapshot failed (m1) */
    rects_t lov_sent;             /* worker: the overlays' rects as the panel shows them from direct frames */
    const omni_band_overlay_t *j_lov;   /* the direct job's overlays, blended by dv_direct_fill before the pack */
    int j_lov_n;
    /* `dial direct` (under dial_mux) */
    uint32_t dd_claims, dd_frames, dd_refused[OMNI_DD__COUNT], dd_checks[OMNI_DD__COUNT], dd_waits, dd_backoffs;
    uint32_t dd_exits, dd_exit_why[DV_EX__COUNT], dd_aborts, dd_copies, dd_exit_us_max, dd_frame_us_max;
    uint64_t dd_exit_us_sum, dd_frame_us_sum;
#if CONFIG_OMNI_DEBUG_CONSOLE
    /* D3's panel truth for `dial verify` (LVGL ctx): the shadow seeded with the front buffer at the claim (sh_armed),
     * and at the exit the exit frame and what the hand-back redrew (sh_patch; sh_whole after an abort) */
    bool sh_armed, sh_ok, sh_whole;
    bool dd_lose;                 /* `dial direct lose`: the next direct chunk is skipped (counted lost, D3) */
    uint32_t dd_claim_swaps;      /* under dial_mux: IMAGE frames that became ready during a claim (swapped, I1) */
    vframe_t sh_frame;
    rects_t sh_patch;
#endif
#if CONFIG_OMNI_DEBUG_CONSOLE
    dial_scen_t scen;
    float scen_val[OMNI_RINGS], scen_lon;
#endif
};

static inline int dv_imin(int a, int b) { return a < b ? a : b; }
uint32_t dv_us_since(int64_t t0);
vframe_t dv_make_frame(dial_view_t *v, const omni_dial_state_t *st);
/* What changed from prev to cur, into L; *drift: a drift step (L holds the disc's bands and both marker boxes). */
int dv_frame_dirty(dial_view_t *v, const vframe_t *prev, const vframe_t *cur, rects_t *L, bool *drift);
/* Points the resources at f's loupe size and kernels and prepares f into v->prep (f must outlive the renders). */
void dv_prepare(dial_view_t *v, const vframe_t *f);
void dv_set_chunks(dial_view_t *v, const rects_t *L, int rows);   /* the rects made disjoint, in chunks of rows */
void dv_dispatch(dial_view_t *v, bool both);                       /* renders the chunks set up, on one core or both */
uint16_t *dv_ucell(const dial_view_t *v, int x, int y);            /* the cached under stage at screen (x, y) */
/* Render tasks: chunk c of the direct job into band (stride SCR, row 0 = c.y0), as disp_direct_send_chunk's fill. */
void dv_direct_fill(void *ctx, uint16_t *band, omni_rect_t c);
/* Task 6, the LVGL overlays. Worker: takes the last published set into its own copy (lov), when it is new and changed,
 * or always when all (the claim's first frame, verify); areas (NULL: left for the next direct frame) gains the rects
 * the sets changed since the last take. false: no PSRAM for the copy (lov empty). */
bool dv_lov_take(dial_view_t *v, rects_t *areas, bool all);
/* LVGL ctx: the registry's set (disp_direct_overlays) published for the worker; false (skipped) while it copies. */
/* claim: the claim's own publish, which first drops the owed rects and areas the last claim left (minor 10) */
bool dv_lov_publish(dial_view_t *v, bool claim);
/* whether the next view created copies the texture to PSRAM (true without the console) */
bool dv_next_tex(void);
bool dv_next_base(void);         /* ... and omni_base (R114; true without the console) */
#if CONFIG_OMNI_DEBUG_CONSOLE
void dv_set_next_tex(bool on);   /* `dial perf tex` (R121/C2: the setters exist only under the console) */
void dv_set_next_base(bool on);  /* `dial perf base` */
#endif
#if CONFIG_OMNI_DEBUG_CONSOLE
bool dv_job(dial_view_t *v, job_t job);   /* the worker runs a console job (dial_console.c): true if it passed */
#endif
