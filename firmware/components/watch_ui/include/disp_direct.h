#pragma once
/* The direct path (Stage 4a lever B, R118, R120): full-screen motion rendered into internal-RAM bands and sent
 * straight to the panel by a sender task, while LVGL's flushes are dropped (board.h, direct mode). Theme-neutral: every
 * view that sends full-screen motion uses it, the doors included.
 *
 * Bands: the 14 912 B staging buffer, lent by the board between begin and end, plus (DISP_DIRECT_BANDS - lent) bands
 * allocated in internal DMA RAM at the claim and freed at the release. They exist only while the path owns the panel,
 * and never under provisioning (disp_direct_set_blocked). Never from PSRAM: PSRAM DMA to the panel underflows and
 * wedges the SPI driver (inv. section 4e).
 *
 * A client's life: claim (LVGL ctx) -> per frame: render tasks take bands (disp_direct_band or disp_direct_send_chunk),
 * put them, and the client calls disp_direct_frame_end(seq) -> disp_direct_check every direct frame -> release (LVGL
 * ctx), which hands the panel back and invalidates what LVGL dropped. Whenever disp_direct_active() turns false (an
 * abort, the orphan rule, provisioning, the display going off) the client treats it as an abort: the panel content is
 * unknown (D3).
 *
 * Bands reach the panel in the order they were put; the two render cores may finish chunks in any order and there is
 * no TE, so the tear pattern is not LVGL's top-to-bottom one (D11). */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "sdkconfig.h"
#include "omni/band.h"

#define DISP_DIRECT_ROWS 8         /* rows per band (R118: 8 x 4; 16 x 3 only if step 8 measures >= 2 ms faster) */
#define DISP_DIRECT_BANDS 4
#define DISP_DIRECT_RESERVE 32768   /* internal bytes free after the allocation (R120) */
#define DISP_DIRECT_MIN_PX 100000   /* a frame this dirty is full-screen motion (R120, D7) */
#define DISP_DIRECT_BACKOFF_MS 500  /* no claim after a refusal or an exit, until this passes or a still frame (D7) */
#define DISP_DIRECT_DRAIN_MS 300    /* release/blocked: wait this long for every band back before freeing (D1) */
#define DISP_DIRECT_LOCKED_WAIT_MS 20   /* release in LVGL ctx: the longest wait inside LVGL's lock (M6) */

/* The modes: AUTO admits by omni_direct_admit; OFF refuses every claim (OMNI_DD_OFF). The default is a production
 * constant (R101, C1): console builds can change it (`disp direct`), console-off builds cannot. */
typedef enum { DISP_DIRECT_MODE_AUTO, DISP_DIRECT_MODE_OFF } disp_direct_mode_t;
#define DISP_DIRECT_DEFAULT_MODE DISP_DIRECT_MODE_AUTO
#if CONFIG_OMNI_DISP_DIRECT
#define DISP_DIRECT_BUILT 1         /* CONFIG_OMNI_DISP_DIRECT as a value (an n option is not defined at all) */
#else
#define DISP_DIRECT_BUILT 0
#endif

omni_dd_why_t disp_direct_claim(lv_obj_t *view);   /* LVGL ctx: full admission (need = the new bands), allocate,
                                                    * begin; a claim while !display_on returns DISPLAY and counts as a
                                                    * wait, not a refusal (B16). Within DISP_DIRECT_BACKOFF_MS of a
                                                    * refusal or of any exit (until disp_direct_still), or while the
                                                    * last claim's bands drain back, it returns OMNI_DD_BACKOFF,
                                                    * counted as a back-off (D7, N1, N2) */
omni_dd_why_t disp_direct_check(lv_obj_t *view, bool ram);   /* LVGL ctx: does admission still hold? need 0 (A7);
                                                    * ram false skips the heap walk (it runs once per direct frame) */
/* LVGL ctx: a still frame was shown; ends the back-off (D7) unconditionally. R2-1's contract: call it only on the
 * first exact frame after motion stops (settled, not lossy, no motion-dirty pixels), never every frame and never on a
 * calm frame with no motion before it, or a client that flaps at the threshold claims and exits without back-off. */
void disp_direct_still(void);
uint16_t *disp_direct_band(void);                  /* render tasks: a free band (blocks); NULL once aborted */
void disp_direct_put(uint16_t *band, omni_rect_t c);   /* band holds c packed (omni_band_pack) */
void disp_direct_skip(void);                       /* render tasks: a chunk not sent (band() gave NULL): lost */
/* Render tasks (C6): takes a band, calls fill(ctx, band, c) to render c into it at stride OMNI_DIAL_W, packs, puts;
 * false (and counts the chunk lost) if the path was aborted. The bench and the dial view both use it. c has at most
 * DISP_DIRECT_ROWS rows. */
bool disp_direct_send_chunk(omni_rect_t c, void (*fill)(void *ctx, uint16_t *band, omni_rect_t c), void *ctx);
void disp_direct_frame_end(uint32_t seq);          /* every band of frame seq is put */
uint32_t disp_direct_shown(void);                  /* the last seq whose bands are all on the wire */
uint32_t disp_direct_lost(void);                   /* bands skipped or discarded since the claim (D3) */
void disp_direct_abort(void);                      /* any task: wakes every blocked band() with NULL; queued bands
                                                    * are discarded (lost) */
/* LVGL ctx, idempotent: abort if needed, wait until taken - put == 0 and the sender is idle after board_direct_wait,
 * board_direct_end, invalidate each returned area, free the bands. The wait here is at most DISP_DIRECT_LOCKED_WAIT_MS
 * (M6); a longer one finishes on the orphan timer, the path inactive meanwhile with LVGL's flushes still dropped. Once
 * DISP_DIRECT_DRAIN_MS has passed and the sender has drained (no band in flight), the panel is handed back even if a
 * render task still holds a band: the bands stay allocated until every one is back. A sender that has not drained
 * keeps LVGL's flushes dropped (it may be wedged on the bus): that is warned once and counted. Either time-out is
 * counted once per release (D1, I1, R1-1). A client must check disp_direct_active() before each write into a band
 * it took (disp_direct_send_chunk does): once false, the band may be the stage LVGL uses again (M1). An exit starts a
 * back-off (D7): a claim within DISP_DIRECT_BACKOFF_MS returns OMNI_DD_BACKOFF unless a still frame came (N1). */
void disp_direct_release(void);
bool disp_direct_active(void);                     /* false after any abort, orphan, block or force-end: the client
                                                    * treats it as an abort (D3, D6) */
void disp_direct_set_blocked(bool on);             /* event loop, under bsp_display_lock (D2): on sets the flag,
                                                    * then abort + release with D1's wait, before provisioning starts */
/* Event loop, after power_apply(PA_TO_SLEEP) (the board force-ended the path at display_off): release with the lock
 * dropped while waiting, so no band stays allocated through the sleep (I2). LVGL is paused then; bsp_display_lock is
 * still free. */
void disp_direct_display_off(void);

/* LVGL overlays over a direct view (Task 6): known objects drawn after the view (the travel caption, the face pips) are
 * snapshotted (an ARGB8888 lv_snapshot, the object's opacity applied) into a PSRAM draw buffer grown to fit, never
 * DMA'd, and blended into the bands by the client (omni_band_overlay), so motion stays direct while they show. Any
 * other object over the view still refuses as OVERLAY, and so does a registered one whose snapshot failed. */
#define DISP_DIRECT_OVERLAYS 4
bool disp_direct_overlay_add(lv_obj_t *obj);       /* LVGL ctx; false if full. Removed by itself when obj is deleted
                                                    * (a delete waits out a client's copy, below) */
void disp_direct_overlay_remove(lv_obj_t *obj);    /* LVGL ctx; frees its sprite (after the client that reads the
                                                    * sprites stopped: the dial view's destroy) */
void disp_direct_overlay_changed(lv_obj_t *obj);   /* LVGL ctx: text, opacity, visibility or position changed */
/* LVGL ctx, once per direct frame: refreshes changed sprites (an ARGB8888 snapshot of the object, its opacity
 * included, into a PSRAM draw buf grown to fit; omni_band_overlay_t.stride = header.stride / 4, A9), fills out[] with
 * the visible ones, and adds to `areas` the old and new rects of any that changed (even-rounded). `owed` gains the
 * rects of visible objects LVGL shows but whose snapshot failed (and the rect they were sent at): the client must
 * redraw them at the hand-back (m1). Returns the count. Opacity, visibility and position are also found changed by
 * themselves; text needs disp_direct_overlay_changed. The sprites stay valid until the next call or a remove: a client
 * reading them on other tasks serialises its reads with its own calls (the dial view copies them under its overlay
 * flags). A delete or remove frees a sprite only after the clients that hold the registry (hold, below) let go, and
 * moves the generation first: a client that published before it skips its copy (m3). */
int disp_direct_overlays(omni_band_overlay_t *out, omni_rects_t *areas, omni_rects_t *owed);
uint32_t disp_direct_overlay_gen(void);            /* any task: moves at every delete or remove */
void disp_direct_overlay_hold(bool on);            /* any task: bracket a read of the published sprites (counted) */
bool disp_direct_overlay_registered(const lv_obj_t *obj);   /* registered, and its last snapshot did not fail (A11) */

#if CONFIG_OMNI_DEBUG_CONSOLE
void disp_direct_shadow(bool on);                  /* the sender copies every band it sends into a PSRAM frame (D3);
                                                    * off by default; floors are measured with it off */
const uint16_t *disp_direct_shadow_frame(void);    /* NULL when off */
void disp_direct_set_mode(disp_direct_mode_t m);   /* `disp direct auto|off` (off mid-claim: the next check says OFF) */
void disp_direct_set_reserve(uint32_t bytes);      /* `disp direct reserve <bytes>` */
typedef struct {
    disp_direct_mode_t mode;
    uint32_t reserve;
    bool claimed, active, shadow, releasing, stuck;
    uint32_t claims, refusals[OMNI_DD__COUNT], display_waits, backoffs;
    omni_dd_why_t backoff_cause;   /* what started the last back-off (OK: a plain exit) */
    uint32_t frames, bands, lost_total, orphans, drain_timeouts;   /* since boot (frames: whole frames on the wire) */
    uint32_t lost;                 /* since the last claim */
    uint32_t frame_us_avg, frame_us_max;   /* whole frame to whole frame on the wire, since the last claim */
    uint32_t held_bytes;           /* the new bands allocated now (stuck ones included) */
    int32_t perm_bytes;            /* internal bytes the sender, queues and timer took at the first claim (-1: none) */
    uint32_t stack_hwm;            /* the sender's stack high-water mark, bytes (0: no sender yet) */
    struct { bool used, shown, failed; omni_rect_t r; uint32_t snaps, us_avg, us_max; } ov[DISP_DIRECT_OVERLAYS];
} disp_direct_stats_t;
void disp_direct_stats(disp_direct_stats_t *s);
const char *disp_direct_why_name(omni_dd_why_t why);
#endif
