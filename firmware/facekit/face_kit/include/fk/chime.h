#pragma once
/* The hourly chime's scheduler: when a top of hour is due, what it plays, and when to look again. Pure: the clock is
 * passed in, the local time comes from localtime_r under the caller's TZ (as omni/timefmt.c), and nothing here reads
 * settings or starts a timer. A theme's service owns the sound/volume mirror, the one-shot timer and the state; it
 * calls fk_chime_due at each wake and re-arms with fk_chime_arm_ms. */
#include <stdbool.h>
#include <stdint.h>

#define FK_CHIME_GRACE_S 30          /* a boundary is honoured in [b, b + 30 s) only ("on the hour only") */
#define FK_CHIME_QUIET_PCT 40        /* volume 30 plays at 12 */
#define FK_CHIME_ARM_CAP_MS 60000
#define FK_CHIME_STEP_BACK_S 7200    /* a mark further ahead than this is a clock corrected backward: it resets */

/* sound_pending (appended, the final review M2): the sound mirror is not configured yet (sfx_config has not run since
 * boot), so sound_on and volume are its defaults, not the wearer's */
typedef struct {
    bool chime_on, sound_on, time_set, provisioning;
    uint8_t volume, quiet_from_h, quiet_to_h;
    bool sound_pending;
} fk_chime_ctx_t;
typedef struct { int64_t last_b; } fk_chime_state_t;   /* the last boundary handled (UTC s); 0 none */
/* FK_CH_BOOT (appended, M2): the sound mirror is pending; no decision, nothing marked, a look again in a second */
typedef enum {
    FK_CH_WAIT, FK_CH_PLAY, FK_CH_LATE, FK_CH_UNSET, FK_CH_PROV, FK_CH_SILENT, FK_CH_OFF, FK_CH_ERROR, FK_CH_BOOT
} fk_chime_why_t;
typedef struct { fk_chime_why_t why; uint8_t pings, gain_pct; int64_t next_s; } fk_chime_verdict_t;

/* the latest local top of hour <= now (DST-correct); 0 if the time cannot be broken down or no boundary is found. */
int64_t fk_chime_boundary(int64_t now_s);
int64_t fk_chime_next(int64_t now_s);           /* the earliest local top of hour > now */
/* PLAY, LATE, PROV and SILENT mark st (that hour is handled); UNSET, OFF and BOOT do not (BOOT: c->sound_pending,
 * next_s one second on). next_s: when to look again (0:
 * no timer for OFF; 0 for UNSET, where fk_chime_arm_ms polls). A boundary at or before st->last_b (a clock stepped
 * back by up to FK_CHIME_STEP_BACK_S) is WAIT and leaves st alone, so an hour already played is not played again;
 * a mark more than that ahead is a clock corrected backward, and resets (the real hour then plays). FK_CH_ERROR (a
 * NULL argument, or a time localtime_r rejects) marks nothing. PLAY carries pings and gain_pct (40 in the quiet
 * window, else 100; the caller scales its volume by it).
 * H7 (a theme's `chime at +N` command): the service shifts now_s so the next top of hour is N s ahead
 * (fk_chime_sched_at). A change or clear of the offset is a discontinuity of up to 59 min, which looks like a step back and is WAIT, so the service
 * zeroes *st whenever the offset changes.
 * A change of TZ or city between calls needs no reset: boundaries are always local top-of-hour UTC seconds compared
 * with last_b, so a zone whose hour starts later plays, one whose boundary is already marked does not. */
fk_chime_verdict_t fk_chime_due(fk_chime_state_t *st, int64_t now_s, const fk_chime_ctx_t *c);
uint8_t fk_chime_pings(int hour24);             /* 0 -> 12, 1..12, 13 -> 1, 23 -> 11 */
bool fk_chime_quiet(int hour24, uint8_t from_h, uint8_t to_h);   /* [from, to), wrapping; from == to: never */

/* The one-shot arm (R136 item 8, B4): ms until the verdict's next_s, capped at FK_CHIME_ARM_CAP_MS, so a clock first
 * set or stepped is noticed within a minute; the cap when next_s is 0 but the chime is on (clock unset); 0 when the
 * chime is off (no timer). A next_s already reached arms 1 ms. */
uint32_t fk_chime_arm_ms(int64_t now_s, const fk_chime_verdict_t *v, bool chime_on);

/* ---- The service's pure part (Task 16): the state, the `chime at +N` offset (H7) and the last verdict. A theme's
 * device service holds one under its lock; nothing here reads a clock or a setting. */
#define FK_CHIME_AT_MAX_S 3540       /* 59 min: the hour before stays >= 60 s back (LATE), never played by the command */
typedef struct {
    fk_chime_state_t st;   /* kept across sleeps and wakes; zeroed only when the offset changes */
    int64_t off_s;         /* added to the real clock (0: none) */
    fk_chime_verdict_t last;
} fk_chime_sched_t;
/* `chime at +N`: the offset that puts the first local top of hour at or after real_now_s + n_s exactly n_s seconds
 * ahead; n_s 0 clears it. s->st is zeroed whenever the offset changes, and only then (H7: a change looks like a step
 * back). False, and nothing changed, for a NULL s, n_s above FK_CHIME_AT_MAX_S, or a time with no boundary. */
bool fk_chime_sched_at(fk_chime_sched_t *s, int64_t real_now_s, uint32_t n_s);
/* A wake: fk_chime_due at real_now_s + the offset, kept in s->last; returns the ms to arm (fk_chime_arm_ms on the
 * shifted clock; 0: no timer). FK_CH_ERROR does nothing and re-arms at FK_CHIME_ARM_CAP_MS. 0 for a NULL s. */
uint32_t fk_chime_sched_tick(fk_chime_sched_t *s, int64_t real_now_s, const fk_chime_ctx_t *c);
