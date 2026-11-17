#pragma once
/* Clock self-calibration (R40), the safety net under the timekeeping fix. Pure state machine; the app feeds it
 * each SNTP correction (the step the sync applied), the uptime and the time spent in light sleep, and asks it for
 * the correction due between syncs and for the next sync interval.
 *
 * Model: the board has no 32 kHz crystal. Awake, time runs on the 40 MHz crystal (esp_timer); in light sleep
 * esp_timer is advanced by the sleep length measured on the internal RC slow clock, so the error builds up per
 * asleep second. The drift since the previous sync is the SNTP step plus the corrections already applied; divided
 * by the asleep seconds it gives a rate (ppm), folded into the running estimate. Between syncs the estimate times
 * the asleep seconds is applied (in steps of at least OMNI_CLK_APPLY_MIN_US).
 * The sync interval starts dense (OMNI_CLK_INTERVAL_MIN_S), doubles after each small residual up to
 * OMNI_CLK_INTERVAL_MAX_S, and drops back to the minimum after a large one.
 *
 * The persisted part (rate, interval, correction log) is omni_clk_saved_t; the reference to the previous sync is
 * per boot (uptime and sleep counters restart). */
#include <stdbool.h>
#include <stdint.h>

#define OMNI_CLK_LOG_N 8
#define OMNI_CLK_INTERVAL_MIN_S 900      /* learning: 15 min, 30 min, 1 h, 2 h, 4 h, 6 h */
#define OMNI_CLK_INTERVAL_MAX_S 21600
#define OMNI_CLK_SMALL_US 1000000        /* |residual| at most this: the interval doubles */
#define OMNI_CLK_LARGE_US 4000000        /* |residual| above this: back to the minimum */
#define OMNI_CLK_MIN_ASLEEP_S 600        /* less light sleep than this between syncs: no rate estimate */
#define OMNI_CLK_MAX_RATE_PPM 20000      /* a larger rate is not drift (a manual settime, a reboot): ignored */
#define OMNI_CLK_APPLY_MIN_US 100000
/* Once learned, an estimate further than this from the rate is held back (taken only if the next one agrees with
 * it to half the band). Bound: on the board the RC's rate against its calibration moved +1338..+5096 ppm over
 * 33 min awake (task 14 clock report); in light sleep the calibration is fresh at every entry, so between two
 * syncs a real rate moves less than that. A larger jump is a non-drift step (a lost reference) or a regime change,
 * which the agreeing second estimate then admits. */
#define OMNI_CLK_BAND_PPM 5000

typedef struct {
    int64_t unix_s;      /* clock right after the sync */
    int32_t step_ms;     /* the sync's correction, after - before (+ = the watch was behind) */
    int32_t applied_ms;  /* self-corrections applied since the previous sync */
    uint32_t uptime_s;
    uint32_t since_s;    /* uptime since the previous sync this boot; 0 = first sync since boot */
    uint32_t asleep_s;   /* light sleep within since_s */
    int32_t rate_ppm;    /* the estimate after this sync */
} omni_clk_entry_t;

typedef struct {         /* persisted as one blob */
    int32_t rate_ppm;    /* correction in us per asleep second (+ = the clock loses time asleep: add) */
    uint32_t interval_s;
    uint8_t n_est;       /* estimates folded in (saturating) */
    uint8_t log_n;       /* entries in log */
    uint8_t log_head;    /* next slot written */
    omni_clk_entry_t log[OMNI_CLK_LOG_N];
} omni_clk_saved_t;

/* Persisted blob (NVS key "clockcal", store.c; copied verbatim by the migration): uint16 version, then the raw
 * omni_clk_saved_t (274 B on the device and the host). Any layout change must bump the version. */
#define OMNI_CLK_BLOB_VERSION 1
#define OMNI_CLK_BLOB_SIZE (sizeof(uint16_t) + sizeof(omni_clk_saved_t))

typedef struct {
    omni_clk_saved_t saved;
    bool have_ref;               /* a sync this boot: ref_* are valid */
    uint64_t ref_uptime_us, ref_asleep_us;
    int64_t applied_us;          /* applied since the reference sync */
    bool on_battery;             /* omni_clk_on_battery .. omni_clk_on_usb */
    bool far_pending;            /* a held-back estimate (far_ppm) waits for an agreeing one */
    int32_t far_ppm;
} omni_clockcal_t;

void omni_clk_init(omni_clockcal_t *c);
/* Takes a persisted state; anything out of range gives the defaults. Returns false if it did. */
bool omni_clk_restore(omni_clockcal_t *c, const omni_clk_saved_t *s);
/* A successful sync that stepped the clock by step_us (after - before). */
void omni_clk_on_sync(omni_clockcal_t *c, int64_t step_us, int64_t unix_s, uint64_t uptime_us, uint64_t asleep_us);
/* The correction to apply now: 0 before the first sync this boot or while it is under OMNI_CLK_APPLY_MIN_US. */
int64_t omni_clk_due_us(const omni_clockcal_t *c, uint64_t asleep_us);
/* The caller moved the clock by us (what omni_clk_due_us returned). */
void omni_clk_applied(omni_clockcal_t *c, int64_t us);
uint32_t omni_clk_interval_s(const omni_clockcal_t *c);
/* Unplugged (light sleep starts): before the rate is learned (OMNI_CLK_LEARNED estimates) the interval goes back to
 * the minimum, so the battery period that teaches it is sampled densely. */
#define OMNI_CLK_LEARNED 2
void omni_clk_on_battery(omni_clockcal_t *c);
/* Plugged in: no light sleep, so a small residual backs the interval off without an estimate. */
void omni_clk_on_usb(omni_clockcal_t *c);
/* The clock was set by hand (settime): the next sync only re-establishes the reference and learns nothing. */
void omni_clk_invalidate_ref(omni_clockcal_t *c);
/* Holds a backward correction at floor_us, the start of the latest minute shown: the clock never goes back
 * across a minute already on screen. Returns the part of due_us to apply now (the rest stays due). */
int64_t omni_clk_hold_back(int64_t now_us, int64_t due_us, int64_t floor_us);
/* clock_correct's decision: the correction to apply now (us, 0 = none). Nothing while a sync runs (sync_busy: the
 * sync measures its step as the change of the clock, so a correction then would be counted as drift), else
 * omni_clk_due_us held back at shown_floor_us (omni_clk_hold_back). */
int64_t omni_clk_correction(const omni_clockcal_t *c, bool sync_busy, int64_t now_us, uint64_t asleep_us,
                            int64_t shown_floor_us);

/* SNTP steps on their way from the net task to the event loop (which feeds them to omni_clk_on_sync on NET_OK).
 * Steps add up until taken, so a sync whose EV_NET was dropped is folded into the next one (the calibration's
 * reference is still the earlier sync, so the sum is the right drift). Saturating. The caller serialises access. */
typedef struct {
    int64_t us;
    bool have;
} omni_clk_steps_t;
void omni_clk_steps_add(omni_clk_steps_t *s, int64_t step_us);
/* The sum since the last take (and clears it); false (and 0) if there was none. */
bool omni_clk_steps_take(omni_clk_steps_t *s, int64_t *step_us);

/* Up to max log entries, oldest first; returns the count. */
int omni_clk_log(const omni_clockcal_t *c, omni_clk_entry_t *out, int max);
