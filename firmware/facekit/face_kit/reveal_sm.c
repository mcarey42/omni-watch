#include "fk/reveal_sm.h"

/* The doors' life on the device as pure decisions (doors_sm.h). */

fk_rs_gate_t fk_rs_gate(fk_rs_t *s, bool worker_busy)
{
    if (s->mode != FK_RS_OFF) return FK_RS_REFUSE;
    if (!s->stuck) return FK_RS_GO;
    if (worker_busy) return FK_RS_REFUSE;   /* its buffers are still read: no new reveal over them */
    s->stuck = false;
    return FK_RS_GO_FREE;
}

void fk_rs_started(fk_rs_t *s, bool direct, bool latch)
{
    s->mode = direct ? FK_RS_DIRECT : FK_RS_CANVAS;
    s->latch = latch;
}

void fk_rs_waiting(fk_rs_t *s, bool latch)
{
    s->mode = FK_RS_WAIT;
    s->latch = latch;
}

fk_rt_t fk_rs_tick(fk_rs_t *s, uint32_t elapsed_ms, uint32_t pop_ms, bool worker_done, bool path_active,
                   bool *play_latch)
{
    *play_latch = false;
    if (s->mode == FK_RS_OFF) return FK_RT_IDLE;
    if (s->mode == FK_RS_WAIT) return FK_RT_CLAIM;   /* the doors' time has not started: no latch yet */
    if (s->latch && elapsed_ms >= pop_ms) {   /* elapsed times, not timestamps */
        s->latch = false;
        *play_latch = true;
    }
    if (s->mode == FK_RS_CANVAS) return FK_RT_CANVAS_FRAME;
    if (worker_done) return FK_RT_END;
    if (!path_active) return FK_RT_END_CUT;
    return FK_RT_CHECK;
}

bool fk_rs_end_releases(const fk_rs_t *s) { return s->mode == FK_RS_DIRECT; }

unsigned fk_rs_end(fk_rs_t *s, bool cut, bool worker_busy)
{
    const bool direct = s->mode == FK_RS_DIRECT;
    s->mode = FK_RS_OFF;
    s->latch = false;
    unsigned out = cut ? 0u : FK_RE_STILL;
    if (direct && worker_busy) s->stuck = true;   /* M1: never freed under the worker */
    else out |= FK_RE_FREE;
    return out;
}

bool fk_rs_destroy_frees(fk_rs_t *s, bool worker_stopped)
{
    if (!worker_stopped) return false;   /* N1: the worker still reads the view, its context and the buffers */
    s->stuck = false;
    return true;
}

void fk_rs_plan(const fk_cols_state_t *st, const fk_col_t cols[466], bool reduced, bool *settled, fk_rp_t *out)
{
    *out = (fk_rp_t){ FK_RP_NONE, 0, 0 };
    if (st->done || reduced) {
        if (*settled) return;
        *settled = true;   /* once these jobs are done (the frame waits for all of them) */
        *out = (fk_rp_t){ FK_RP_SETTLED, 0, 466 };
        return;
    }
    /* the columns a leaf does not cover whole (every row on the leaf: u on the door, inv_s <= 1; the band reads no
     * interior there). An outward leaf covers each of its columns whole; an inward one (wave 2 Task 3) shrinks, so the
     * interior shows above and below it in every column. No leaf crosses the centre line: the gap is one run. */
    int xa = 0, xb = 466;
    while (xa < 466 && cols[xa].u >= 0 && cols[xa].inv_s_q16 <= 65536u) xa++;
    while (xb > xa && cols[xb - 1].u >= 0 && cols[xb - 1].inv_s_q16 <= 65536u) xb--;
    if (xa < xb) *out = (fk_rp_t){ FK_RP_SWING, (int16_t)xa, (int16_t)xb };
}

void fk_rs_job_in(const fk_rp_t *p, uint32_t t, bool reduced, uint32_t *wake_ms, bool *reveal_live)
{
    *wake_ms = p->kind == FK_RP_SETTLED ? UINT32_MAX : t;   /* settled, or the tail at the doors' time */
    *reveal_live = !reduced;   /* the swing's band draws the leaves; the cross-fade's face shows its own */
}

void fk_rs_job_cols(int y0, int y1, int xa, int xb, int *a, int *b)
{
    int p0, p1;
    fk_panel_row(y1 <= 233 ? y1 - 1 : y0 >= 233 ? y0 : 233, &p0, &p1);   /* the row nearest the centre */
    *a = xa > p0 ? xa : p0;
    *b = xb < p1 ? xb : p1;
}

void fk_rs_job_rows(int y0, int y1, omni_rect_t need, omni_rect_t *r)
{
    const int ya = y0 > need.y0 ? y0 : need.y0, yb = y1 < need.y1 ? y1 : need.y1;
    int a = 0, b = 0;
    if (ya < yb && need.x0 < need.x1) fk_rs_job_cols(ya, yb, need.x0, need.x1, &a, &b);
    *r = (omni_rect_t){ (int16_t)a, (int16_t)(ya < yb ? ya : 0), (int16_t)b, (int16_t)(ya < yb ? yb : 0) };
}

int fk_rs_job_take(int *next, int jobs, bool *fail, uint32_t mine, uint32_t now)
{
    if (mine != now) *fail = true;
    if (*fail || *next >= jobs) return -1;
    return (*next)++;
}

bool fk_rs_claim_ours(bool *fail, uint32_t mine, uint32_t now)
{
    if (mine != now) *fail = true;
    return !*fail;
}

bool fk_dm_may_claim(const void *holder, const void *me, bool bench, bool me_is_bench)
{
    if (holder && holder != me) return false;   /* another holds the path: never a shared claim */
    return !bench || me_is_bench;
}

bool fk_dm_want_claim(bool wanted, bool glance, bool reduced, bool bench, int64_t now_us, int64_t retry_us)
{
    return wanted && !glance && !reduced && !bench && now_us >= retry_us;
}

int64_t fk_dm_retry_at(omni_dd_why_t why, int64_t now_us)
{
    if (why == OMNI_DD_DISPLAY) return now_us;
    return now_us + (why == OMNI_DD_BACKOFF ? FK_DM_POLL_RETRY_MS : FK_DM_RETRY_MS) * 1000LL;
}
