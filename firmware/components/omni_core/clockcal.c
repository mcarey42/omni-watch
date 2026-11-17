#include "omni/clockcal.h"
#include <string.h>

void omni_clk_init(omni_clockcal_t *c)
{
    memset(c, 0, sizeof *c);
    c->saved.interval_s = OMNI_CLK_INTERVAL_MIN_S;
}

bool omni_clk_restore(omni_clockcal_t *c, const omni_clk_saved_t *s)
{
    omni_clk_init(c);
    if (s->interval_s < OMNI_CLK_INTERVAL_MIN_S || s->interval_s > OMNI_CLK_INTERVAL_MAX_S ||
        s->rate_ppm > OMNI_CLK_MAX_RATE_PPM || s->rate_ppm < -OMNI_CLK_MAX_RATE_PPM || s->log_n > OMNI_CLK_LOG_N ||
        s->log_head >= OMNI_CLK_LOG_N)
        return false;
    c->saved = *s;
    return true;
}

static int32_t clamp_i32(int64_t v)
{
    return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v;
}

static void log_entry(omni_clockcal_t *c, const omni_clk_entry_t *e)
{
    omni_clk_saved_t *s = &c->saved;
    s->log[s->log_head] = *e;
    s->log_head = (uint8_t)((s->log_head + 1) % OMNI_CLK_LOG_N);
    if (s->log_n < OMNI_CLK_LOG_N) s->log_n++;
}

static void adjust_interval(omni_clk_saved_t *s, int64_t residual_us, bool may_back_off)
{
    int64_t r = residual_us < 0 ? -residual_us : residual_us;
    if (r > OMNI_CLK_LARGE_US) {
        s->interval_s = OMNI_CLK_INTERVAL_MIN_S;
    } else if (r <= OMNI_CLK_SMALL_US && may_back_off) {
        uint32_t next = s->interval_s * 2;
        s->interval_s = next > OMNI_CLK_INTERVAL_MAX_S ? OMNI_CLK_INTERVAL_MAX_S : next;
    }
}

/* Folds one rate estimate k (ppm) in. Before learning anything inside the cap counts; once learned, an estimate
 * more than OMNI_CLK_BAND_PPM from the rate is held back, and replaces the rate only if the next one agrees with it
 * (a real change of regime, not a one-off non-drift step). */
static void fold(omni_clockcal_t *c, int64_t k)
{
    omni_clk_saved_t *s = &c->saved;
    if (s->n_est >= OMNI_CLK_LEARNED && (k > s->rate_ppm + OMNI_CLK_BAND_PPM || k < s->rate_ppm - OMNI_CLK_BAND_PPM)) {
        if (c->far_pending && k <= c->far_ppm + OMNI_CLK_BAND_PPM / 2 && k >= c->far_ppm - OMNI_CLK_BAND_PPM / 2) {
            s->rate_ppm = (int32_t)k;
            s->n_est = 1;
            c->far_pending = false;
        } else {
            c->far_pending = true;
            c->far_ppm = (int32_t)k;
        }
        return;
    }
    c->far_pending = false;
    s->rate_ppm = s->n_est == 0 ? (int32_t)k : (int32_t)(s->rate_ppm + (k - s->rate_ppm) / 2);
    if (s->n_est < UINT8_MAX) s->n_est++;
}

/* A drift above this cannot be a rate under the cap for any realistic sleep (it is about 106 days): no multiply. */
#define DRIFT_MAX_US (INT64_MAX / 1000000)

void omni_clk_on_sync(omni_clockcal_t *c, int64_t step_us, int64_t unix_s, uint64_t uptime_us, uint64_t asleep_us)
{
    omni_clk_saved_t *s = &c->saved;
    omni_clk_entry_t e = {.unix_s = unix_s, .step_ms = clamp_i32(step_us / 1000), .uptime_s = (uint32_t)(uptime_us / 1000000)};
    if (c->have_ref) {
        e.since_s = (uint32_t)((uptime_us - c->ref_uptime_us) / 1000000);
        e.asleep_s = (uint32_t)((asleep_us - c->ref_asleep_us) / 1000000);
        e.applied_ms = clamp_i32(c->applied_us / 1000);
        uint64_t asleep = asleep_us - c->ref_asleep_us;
        bool estimated = false;
        /* the drift of the uncorrected clock (the step is bounded first so the sum cannot overflow) */
        int64_t drift_us = (step_us > DRIFT_MAX_US || step_us < -DRIFT_MAX_US) ? step_us : step_us + c->applied_us;
        if (asleep >= (uint64_t)OMNI_CLK_MIN_ASLEEP_S * 1000000 && drift_us <= DRIFT_MAX_US && drift_us >= -DRIFT_MAX_US) {
            int64_t k = drift_us * 1000000 / (int64_t)asleep; /* per asleep second: us/s = ppm */
            if (k <= OMNI_CLK_MAX_RATE_PPM && k >= -OMNI_CLK_MAX_RATE_PPM) {
                fold(c, k);
                estimated = true;
            }
        }
        /* Back off only on evidence: an estimate, or on USB (no light sleep, the crystal keeps the time). On
         * battery without an estimate (e.g. the sync right after an unplug) the learning schedule stays put; a
         * large residual tightens it either way. */
        adjust_interval(s, step_us, estimated || !c->on_battery);
    }
    e.rate_ppm = s->rate_ppm;
    log_entry(c, &e);
    c->have_ref = true;
    c->ref_uptime_us = uptime_us;
    c->ref_asleep_us = asleep_us;
    c->applied_us = 0;
}

int64_t omni_clk_due_us(const omni_clockcal_t *c, uint64_t asleep_us)
{
    if (!c->have_ref || asleep_us < c->ref_asleep_us) return 0;
    int64_t target = (int64_t)(asleep_us - c->ref_asleep_us) / 1000000 * c->saved.rate_ppm +
                     (int64_t)((asleep_us - c->ref_asleep_us) % 1000000) * c->saved.rate_ppm / 1000000;
    int64_t due = target - c->applied_us;
    return (due < OMNI_CLK_APPLY_MIN_US && due > -OMNI_CLK_APPLY_MIN_US) ? 0 : due;
}

void omni_clk_applied(omni_clockcal_t *c, int64_t us) { c->applied_us += us; }

uint32_t omni_clk_interval_s(const omni_clockcal_t *c) { return c->saved.interval_s; }

void omni_clk_on_battery(omni_clockcal_t *c)
{
    c->on_battery = true;
    if (c->saved.n_est < OMNI_CLK_LEARNED) c->saved.interval_s = OMNI_CLK_INTERVAL_MIN_S;
}

int omni_clk_log(const omni_clockcal_t *c, omni_clk_entry_t *out, int max)
{
    const omni_clk_saved_t *s = &c->saved;
    int n = s->log_n < max ? s->log_n : max;
    int first = (s->log_head + OMNI_CLK_LOG_N - s->log_n) % OMNI_CLK_LOG_N; /* oldest */
    for (int i = 0; i < n; i++) out[i] = s->log[(first + i) % OMNI_CLK_LOG_N];
    return n;
}

void omni_clk_on_usb(omni_clockcal_t *c) { c->on_battery = false; }

void omni_clk_invalidate_ref(omni_clockcal_t *c)
{
    c->have_ref = false;
    c->applied_us = 0;
}

int64_t omni_clk_hold_back(int64_t now_us, int64_t due_us, int64_t floor_us)
{
    if (due_us >= 0) return due_us;
    if (now_us + due_us >= floor_us) return due_us;
    return now_us > floor_us ? floor_us - now_us : 0;
}

int64_t omni_clk_correction(const omni_clockcal_t *c, bool sync_busy, int64_t now_us, uint64_t asleep_us,
                            int64_t shown_floor_us)
{
    if (sync_busy) return 0;
    return omni_clk_hold_back(now_us, omni_clk_due_us(c, asleep_us), shown_floor_us);
}

void omni_clk_steps_add(omni_clk_steps_t *s, int64_t step_us)
{
    if (step_us > 0 && s->us > INT64_MAX - step_us) s->us = INT64_MAX;
    else if (step_us < 0 && s->us < INT64_MIN - step_us) s->us = INT64_MIN;
    else s->us += step_us;
    s->have = true;
}

bool omni_clk_steps_take(omni_clk_steps_t *s, int64_t *step_us)
{
    bool have = s->have;
    *step_us = have ? s->us : 0;
    s->us = 0;
    s->have = false;
    return have;
}
