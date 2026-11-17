#pragma once
/* A full-screen reveal's life on the device (the view; from the first theme's wave 1 Task 12): every decision the view
 * takes about a reveal's mode, its buffers, the latch and the worker, as pure functions the host tests drive
 * (test_fk_reveal_sm). The view does the side effects (the claim, the abort, the release, the frees, the draws) in the
 * order these say. The console's bench (R101: console builds only) stays in the view, on top of them.
 *
 * A reveal: fk_rs_gate (may it start?) -> fk_rs_started (direct or through the canvas) -> fk_rs_tick every POLL_MS ->
 * the end: fk_rs_end_releases (abort and release the direct path first?) then fk_rs_end (free or leave the buffers,
 * the still frame) -> ... -> fk_rs_destroy_frees at the view's destroy.
 *
 * The buffers (the interior, the leaf copy) are read by the worker while a direct reveal runs. A worker that does not
 * come back in time (never seen) keeps them: they are left (stuck) rather than freed under it, and freed at the next
 * gate or destroy once it is idle. At destroy, a worker that does not stop keeps the whole view (N1): it is leaked. */
#include <stdbool.h>
#include <stdint.h>
#include "fk/cols.h"
#include "omni/band.h"

/* WAIT: the panel was not on yet at the claim (a lift's wake starts the doors before the panel comes back on; the
 * claim's DISPLAY is a wait, not a refusal, B16): the claim is tried again each tick; the doors' time starts when it
 * holds (the view falls back to the canvas if the panel does not come on in time). */
typedef enum { FK_RS_OFF, FK_RS_DIRECT, FK_RS_CANVAS, FK_RS_WAIT } fk_rs_mode_t;
typedef struct {
    fk_rs_mode_t mode;
    bool stuck;   /* the last direct reveal's buffers are left to a worker that had not come back */
    bool latch;   /* the latch sound is still to play at the pop (the Doors swing only, WD15) */
} fk_rs_t;

/* May a reveal start? Not while one runs; not while a stuck worker is still busy (REFUSE); GO_FREE first frees the
 * stuck buffers (the worker is idle now) and clears stuck. */
typedef enum { FK_RS_GO, FK_RS_GO_FREE, FK_RS_REFUSE } fk_rs_gate_t;
fk_rs_gate_t fk_rs_gate(fk_rs_t *s, bool worker_busy);
/* It started: through the direct path (the claim held) or the canvas; latch: the Doors swing (not the cross-fade, not
 * the bench). */
void fk_rs_started(fk_rs_t *s, bool direct, bool latch);
/* The claim found the panel off: wait for it (the latch is kept for when the doors start). */
void fk_rs_waiting(fk_rs_t *s, bool latch);

/* Every POLL_MS while a reveal runs. *play_latch: play the latch now (once, at or past the pop). Then: */
typedef enum {
    FK_RT_IDLE,          /* no reveal */
    FK_RT_CANVAS_FRAME,  /* the fallback: draw the next frame through the canvas */
    FK_RT_END,           /* the worker is done: end the reveal (cut as the worker says) */
    FK_RT_END_CUT,       /* the path went inactive (an abort, the orphan rule, provisioning, the display off) */
    FK_RT_CHECK,         /* direct and running: admit the frames shown since the last tick (disp_direct_check) */
    FK_RT_CLAIM          /* waiting for the panel: claim again (fk_rs_started on the result) */
} fk_rt_t;
fk_rt_t fk_rs_tick(fk_rs_t *s, uint32_t elapsed_ms, uint32_t pop_ms, bool worker_done, bool path_active,
                   bool *play_latch);

/* The end, first half: true if the direct path must be aborted (when the worker is busy) and released. */
bool fk_rs_end_releases(const fk_rs_t *s);
/* The end, second half, after the release: the mode goes OFF and the latch with it. Returns FK_RE_* bits: FREE (the
 * buffers; otherwise they are left and stuck is set: a direct reveal whose worker is still busy) and STILL (the
 * settled frame is the first exact frame after motion: not after a cut). */
#define FK_RE_FREE 1u
#define FK_RE_STILL 2u
unsigned fk_rs_end(fk_rs_t *s, bool cut, bool worker_busy);

/* The view's destroy, after the reveal was stopped and the worker asked to stop: true frees the view, its context
 * and any stuck buffers; false (the worker did not stop) leaks them all to it (N1). */
bool fk_rs_destroy_frees(fk_rs_t *s, bool worker_stopped);

/* A frame's interior (the view's frame jobs, Task 12): what the face draws into the interior before the bands read it.
 * The settled frame whole, once (the cross-fade shows it, and the end is it), then nothing; during the swing, the
 * columns between the leaves at the doors' time (a column a leaf uncovers is drawn in the frame that shows it), or
 * nothing while the leaves meet (the columns a leaf does not cover whole: an inward leaf's interior shows above and
 * below it, so its swing draws every column; wave 2 Task 3). *settled carries "the settled frame is drawn" from frame
 * to frame. Done with keep is still the settled frame once: the band adds the kept leaves over it. */
typedef enum { FK_RP_NONE, FK_RP_SETTLED, FK_RP_SWING } fk_rp_kind_t;
typedef struct { fk_rp_kind_t kind; int16_t xa, xb; } fk_rp_t;
void fk_rs_plan(const fk_cols_state_t *st, const fk_col_t cols[466], bool reduced, bool *settled, fk_rp_t *out);
/* The interior's input for a plan (the view's frame jobs; wave 2 Task 11, the Task 3 review's ask, so the host tests
 * drive the device's choice): *wake_ms is UINT32_MAX for the settled frame, else the doors' time t (the tail);
 * *reveal_live (fk_face_in_t's) is true unless reduced: the band draws the leaves through the swing, its settled end and
 * a tap, so a keep face leaves its kept leaves out of the interior; under the reduced and Fade cross-fade it is false,
 * so the face draws its own kept leaves and they fade in with it (R147 PR11). */
void fk_rs_job_in(const fk_rp_t *p, uint32_t t, bool reduced, uint32_t *wake_ms, bool *reveal_live);
/* A job's rows [y0, y1) of the plan's columns [xa, xb): those on the round panel (the widest of its rows'; the bands
 * read nothing else), in [*a, *b) (empty: a >= b). */
void fk_rs_job_cols(int y0, int y1, int xa, int xb, int *a, int *b);
/* The same for any need rect (rd wave 1 Task 3: a row renderer's need is rows, the doors' columns): a job's rows
 * [y0, y1) of need, those on the round panel, into *r (need's rows in the job, and of need's columns those of the
 * widest panel row among them; empty: r->x0 >= r->x1 or r->y0 >= r->y1). For a full-height need its columns are
 * fk_rs_job_cols's. */
void fk_rs_job_rows(int y0, int y1, omni_rect_t need, omni_rect_t *r);

/* A frame job to take (the view's run_jobs, under its lock; Task 13 review I1): the next of `jobs`, or -1 when none is
 * left, the frame has failed (*fail), or the direct claim is no longer this reveal's (mine != now: a later view has
 * claimed the path, so disp_direct_active() reads true again for it). A moved claim sets *fail, so no job is taken on
 * either core after it. */
int fk_rs_job_take(int *next, int jobs, bool *fail, uint32_t mine, uint32_t now);
/* The same test before each band is sent: false sets *fail. */
bool fk_rs_claim_ours(bool *fail, uint32_t mine, uint32_t now);

/* ---- One holder of the direct path, and the rest motion's claims (wave 2 Task 14) */
/* disp_direct_claim hands an active claim to a second claimer (it returns OK while the path is claimed and active: the
 * Task 13 review), so the view keeps the holder itself (a view's doors, a view's rest motion, the console's bench) and
 * refuses any other claim while one holds: a lift's doors and #12's rest motion never share a claim. May `me` claim,
 * given the holder (NULL: none) and the bench's bracket (bench: on; me_is_bench: the bench asks)? The bracket refuses
 * every claim but the bench's own. */
bool fk_dm_may_claim(const void *holder, const void *me, bool bench, bool me_is_bench);
/* The rest motion's next claim (the view's timer, every poll): only when wanted (an awake .direct face), not the
 * glance, not reduced (the tunnel is still: nothing full-screen moves), not under the bench's bracket, and once now_us
 * reaches retry_us. */
bool fk_dm_want_claim(bool wanted, bool glance, bool reduced, bool bench, int64_t now_us, int64_t retry_us);
/* When to try again after a claim's answer `why` at now_us (also after a cut, with OMNI_DD_OK): at once for DISPLAY (the
 * panel coming on is a wait, B16); FK_DM_POLL_RETRY_MS for BACKOFF (the transport's own back-off, or another holder,
 * waited out without lengthening it); FK_DM_RETRY_MS for any other refusal and after a cut or release (OVERLAY, HIDDEN,
 * RAM, PROV, OFF: each refusal starts the transport's 500 ms back-off anyway). */
#define FK_DM_RETRY_MS 500
#define FK_DM_POLL_RETRY_MS 100
int64_t fk_dm_retry_at(omni_dd_why_t why, int64_t now_us);
