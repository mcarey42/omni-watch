#pragma once
/* The Omni dial view (Stage 3 Task 7): the whole 466 x 466 dial, composed by omni_dial_render (Task 6) from the state
 * omni_model steps (Task 5), on DISP_PATH_FULLSCREEN (disp_stats.h). dial_view.c; its console jobs are dial_console.c.
 *
 * Two tasks per view, stacks in PSRAM (16 KB each: the compositor's deepest path is about 5.3 KB, R85, and the bench
 * job used 8.7 KB; `dial` prints the marks): a worker on core 1 and a helper on core 0, priority 2 (4 while building),
 * as globe_view. The worker builds everything at create's request, off the LVGL task (the face switch never waits for
 * it: create takes about 4 ms, the build about 500 ms): the Omni globe's LUT, packed into the compact LUT, and its
 * shade table; the dial map (loaded from the prebuilt omni_dialmap.bin, R80; variant B only, T2 m13) and the
 * magnifier's map and zoomed base; the loupe at 4 sizes (map and zoomed base each: a build is about 90 ms, so none
 * happens per frame; the loupe's animated value is drawn at the nearest size, the fidelity rule's motion case, and the
 * open loupe is exact); the overlays as compact spans at their assets_meta.h origins; the L0 ring strip copied into
 * PSRAM (R79; freed on destroy). The texture, the L1/L2 strips, omni_base and the blooms are read in place from the
 * flash mapping (R70). Each frame is prepared once (omni_dial_prepare: the hands, marks and globe terms, T6 M4) and
 * rendered from it by both cores. A frame where only the globe drifted (and the frame before it) is a drift step: its
 * dirty rects are the disc's 16-row bands and the two marker boxes instead of the globe box (and not the open
 * loupe's content), and it renders by the drift cache (R88): the under stage over the disc's 8-row chord bands,
 * cached until the rings, hands or marks change, then the over stage and the markers. The layers' overlay rule runs
 * on the PIE unit (globe_over_pie).
 *
 * The paths (Task 1's provisional choice, decided here with the compositor's real cost: IMAGE; dial_view_path()):
 *  IMAGE: two 466 x 466 PSRAM frames shown by one opaque lv_image (disp_image_cover: LVGL never draws beneath it). The
 *    worker renders each submitted frame into the back buffer, in row chunks on both cores, re-rendering only the
 *    union of its last two frames' dirty rects (omni_dial_dirty); poll swaps it in and invalidates only the new
 *    frame's dirty rects.
 *  BAND: one screen-covering object whose draw event renders each LVGL band's rows from the prepared frame, the
 *    band's chunks shared by the LVGL task and the helper (priority 6 then); poll takes the latest state and
 *    invalidates its dirty rects. Since R108 the LVGL task is on core 0 with the helper, so the BAND path (`dial path`,
 *    measurement only) shares one core; the IMAGE path's LVGL task only swaps and blits.
 * Neither task touches LVGL or the panel.
 *
 * THREADING: every call needs the LVGL lock (LVGL context), as the faces' hooks have it. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "sdkconfig.h"
#include "omni/omni_model.h"
#include "disp_stats.h"

typedef struct dial_view dial_view_t;
dial_view_t *dial_view_create(lv_obj_t *parent);           /* NULL: no PSRAM / assets; builds on its worker */
void dial_view_destroy(dial_view_t *v);                    /* stops and waits for the workers, frees all */
bool dial_view_ready(const dial_view_t *v);
bool dial_view_failed(const dial_view_t *v);               /* terminal, logged */
/* LVGL ctx; latest wins (a frame not yet taken is replaced). No back-pressure (Task 1: measured and rejected): a
 * rendered frame is swapped in at the next poll. */
void dial_view_submit(dial_view_t *v, const omni_dial_state_t *st);
/* LVGL ctx, after the face's step has set the caption and submitted the frame: publishes the overlays (Task 6 m2) so
 * the next direct frame blends a step's caption, not the poll's older one. A no-op unless the path is DIRECT, and once
 * per direct frame: only when the worker took the last set (the first step after a take; the take is at most a step
 * behind), so each overlay is snapshotted at most once a direct frame. A client that calls it takes over poll's own
 * per-frame publish. */
void dial_view_overlays_publish(dial_view_t *v);
bool dial_view_poll(dial_view_t *v);                       /* LVGL ctx: swap/invalidate only the dirty rects; true if a
                                                            * new frame went in */
/* LVGL ctx: hides the view (the glance draws over a black screen) or shows it again. Only a view that has shown a frame
 * is affected (until then it is hidden anyway, and its first frame shows it), and not while a blank waits. Touches
 * nothing else: the face stops submitting and polling while asleep, so the workers idle and the frame counter stands
 * still. */
void dial_view_show(dial_view_t *v, bool on);
/* LVGL ctx (T8 m1): hides the view (black: the screen) until a frame submitted after this call is the one on screen (or
 * one found the same as the shown frame). The face blanks on every sleep; a wake that plays a reveal (the lid, the
 * iris or the Instant entry, all but the first wake's: R103) keeps the blank, so its first light is the fresh frame,
 * never the frame from before the sleep; a wake at rest ends it (dial_view_unblank, R102). poll ends the blank; `dial`
 * prints how long they took. */
void dial_view_blank(dial_view_t *v);
/* LVGL ctx (R102): ends a blank at once: the view shows its last frame again until the fresh one replaces it (a wake
 * at rest, a re-lift or a screen closing back to the face, keeps the previous content rather than going black). */
void dial_view_unblank(dial_view_t *v);
/* LVGL ctx: a blank is waiting for its fresh frame (false for NULL or a failed view, which never gets one) */
bool dial_view_blanked(const dial_view_t *v);
typedef struct { uint32_t frames, render_us_avg, render_us_max, dirty_px_avg, build_ms, bytes; } dial_view_stats_t;
/* frames: rendered (IMAGE) or taken for drawing (BAND); render_us: the worker's render of a frame (IMAGE) or the
 * band draws of a refresh (BAND); dirty_px: the pixels a frame re-rendered; bytes: PSRAM the view holds. */
void dial_view_stats(dial_view_t *v, dial_view_stats_t *s, bool reset);
dial_view_t *dial_view_current(void);
/* NAN = off: overrides the state's globe longitude (screenshots against the reference frames); a held globe is still,
 * so it is drawn with the linear-in-u kernel (R76) */
void dial_view_hold(dial_view_t *v, float lon_deg);

/* The path the next view is created on: DISP_PATH_FULLSCREEN, or what `dial path` set (a measurement switch for one
 * view: the next one created takes it, so swipe away and back, or `face digital`, `face omni`, and the one after is
 * back on DISP_PATH_FULLSCREEN). */
disp_path_t dial_view_path(void);
void dial_view_set_path(disp_path_t p);

#if CONFIG_OMNI_DEBUG_CONSOLE
/* Registers the `dial` and `ring` console commands (dial_console.c; the Omni's console hook calls it). */
void dial_console_register(void);
/* `dial` (the caller holds the LVGL lock, which guards the blank's fields): stats, worker and helper stack high-water
 * marks, loupe-map rebuilds, PSRAM free; and the lid frames since the last `dial` (Task 9): their count, render avg /
 * max, the PSRAM free low among them (the flip peak, R70), and the first 24 frames' angle and render time. */
void dial_view_print(dial_view_t *v);
/* `dial bench`: on the worker (the caller holds the LVGL lock, so nothing submits): the full frame, a rest frame (the
 * globe drifting one 33 ms step), a slow ring turn (every ring 0.05 slot) and a whirl (3 slots, L2, fast, the globe
 * 20 deg) from the last submitted state, each on one core and on both, plus the hands' per-band cost (Task 6 M4) and
 * a loupe-map rebuild, and the lid's full frames at 0, 30 and 60 deg (Task 9: the prepare alone, the dial drawn under
 * the opaque lid too, the lid from a PSRAM copy, no globe). Prints "DIAL BENCH ..." lines. */
void dial_view_bench(dial_view_t *v);
/* `dial selftest` (ring_only false) / `ring selftest` (true): the ring kernel against the plain LUT on every span of
 * the dial map at 16 offsets on each strip level (omni_ring_kernel_bitexact, through a transient A+B build of this
 * view's map, whose digest must equal the loaded one's), the ring synthetic digest (OMNI_RING_SYNTH_DIGEST); then the
 * globe's PIE kernel against cspan_ref on this view's compact LUT at 5 longitudes (omni_globe_ckernel_bitexact), the
 * compact synthetic digest, and globe_pie_shade_ok on the Omni shade table. Prints "DIAL SELFTEST PASS" (or "RING
 * SELFTEST PASS"), or FAIL with the reason. */
bool dial_view_selftest(dial_view_t *v, bool ring_only);
/* `dial scen slow|whirl|flash|off`: a measurement overlay on the submitted states: slow turns every ring 0.05 slot
 * a frame, whirl 3 slots a frame (L2, fast) with the globe 20 deg a frame, flash adds a 0.3 flash to the whirl. */
typedef enum { DIAL_SCEN_OFF, DIAL_SCEN_SLOW, DIAL_SCEN_WHIRL, DIAL_SCEN_FLASH } dial_scen_t;
void dial_view_scenario(dial_view_t *v, dial_scen_t s);
/* `dial perf tex|drift|lossy|render|base on|off` (R88): switches one kept performance candidate off for a before/after
 * measurement (bits 0..4, all on by default: PERF_DEFAULT). render off (bit 3, Task 10) swaps frames and invalidates
 * their dirty rects without rendering them: the display path's own ceiling for whatever the face animates (R97); the
 * skip is compiled only with this console (R101). tex and base (R114: omni_base copied to PSRAM) apply to the next
 * view created (and only to it); drift, lossy and render to v, and die with it (v NULL: tex and base only). Nothing
 * outlives a view (T7 M3). bit -1: just read. Returns the mask. */
unsigned dial_view_perf(dial_view_t *v, int bit, int on);
/* `dial verify` ("DIAL VERIFY MATCH"): each valid buffer against the frame it holds, rendered whole on one core: an
 * oracle for the dirty rects, the drift cache and the two cores' composition. A third line (Task 5) checks lever B:
 * "DIAL VERIFY DIRECT MATCH" or "MISMATCH at (x, y)": the current frame's whole screen through the direct path's
 * chunks, fill and pack (no send) against a whole render; and, with `disp direct shadow on` through the last motion,
 * panel truth (D3): the shadow of what the sender put on the panel, outside what the hand-back redrew, against the exit
 * frame (the line then says "(panel shadow)"; "DIAL VERIFY DIRECT SKIP (aborted ...)" when the last motion ended in an
 * abort, which redraws the whole screen and leaves nothing to compare). Task 6: the pipeline part blends the visible
 * registered overlays (the caption, the pips) into the bands, and its reference applies omni_band_overlay over the
 * whole render; the line counts them. The buffer lines cannot see:
 *  - the panel: a pixel right in the buffer but never invalidated stays stale on screen (T7 M1 was such a case);
 *  - errors common to both sides: the same prepare and kernels render the reference (`dial selftest` checks those);
 *  - the past: one moment, with the worker idle; a wrong frame shown and since overwritten is gone;
 *  - a buffer with half rows (a flash frame with lossy on): excused, as it differs by design. */
bool dial_view_verify(dial_view_t *v);
/* `dial direct [reset]` (Task 5): lever B's counters for this view (claims, direct frames and their render, exits by
 * cause and the exit frame's render, aborts, refusals at the claim and at the per-frame check, display waits and
 * back-offs), and the transport's since boot (lost bands, orphan releases, drain time-outs); reset zeroes the
 * view's. */
void dial_view_direct(dial_view_t *v, bool reset);
void dial_view_direct_lose(dial_view_t *v);   /* `dial direct lose`: the next direct chunk is lost (I1's check) */
/* `dial overlay check` (Task 6, C5), at rest on IMAGE: each visible registered overlay snapshotted and blended by
 * omni_band_overlay over the exact frame on screen, against LVGL's own render of the screen (`shot`'s) inside the
 * overlay's box: "DIAL OVERLAY max diff <n> in <px> px" (n: the largest channel difference, in 8-bit levels). A small
 * difference is expected: LVGL's blend rule is not omni_blend565's (D5). */
bool dial_view_overlay_check(dial_view_t *v);
/* `dial lid <deg>|off` (Task 9, for shots): the lid held at that angle over whatever the face submits (fade 0), until
 * off (NAN); the face's own flip shows through again then. */
void dial_view_lid(dial_view_t *v, float theta_rad);
/* `dial iris <ms>|off` (Task 10, for shots): the iris held at that time of its timeline (omni_iris_at, fade 0) over
 * whatever the face submits, until off (-1). */
void dial_view_iris(dial_view_t *v, int32_t ms);
/* `dial bench iris` (Task 10): the iris's full-disc frames (the box omni_dial_dirty gives, OMNI_IRIS_BOX) at ap 0, 50
 * and 150 with the globe spinning, on one core and both; the iris's prepare alone; and each without the globe and with
 * the dial drawn under the opaque blades too. Prints "DIAL BENCH iris ..." lines. */
void dial_view_bench_iris(dial_view_t *v);
#endif
