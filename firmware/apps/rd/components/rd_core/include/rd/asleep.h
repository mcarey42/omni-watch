#pragma once
/* Time asleep: the Stasis Clock's readout (rd wave 1 Task 9; spec section 9.6, ruling R154, PR17). The pure rules: the
 * record's seal and check, the duration at a revealing wake (the same boot by the monotonic clock, a soft reset by the
 * unix clock, otherwise none), the consuming wake, the cold-boot clear and the readout's two lines. The record itself
 * lives in RTC memory on the device (main/rd_asleep.c: RTC_NOINIT, never NVS); a cold boot (power-on or brownout)
 * clears it, so the readout shows "--". Pure: no esp_*, freertos or LVGL; no allocation. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/reveal.h"

#define RD_ASLEEP_MAGIC 0x52445a5au   /* "ZZDR" little-endian: a record this firmware sealed */
#define RD_ASLEEP_MAX_S (400 * 86400) /* the unix path's cap: a soft reset longer ago than 400 days is none */

typedef struct {
    uint32_t magic, token;      /* RD_ASLEEP_MAGIC; the boot token of the boot that slept (esp_random once a boot) */
    int64_t sleep_unix;         /* the wall clock at the sleep in s, 0 when the clock was unset */
    int64_t sleep_mono_us;      /* esp_timer at the sleep in us (it counts through light sleep) */
    uint32_t crc;               /* fk_crc32 of magic .. sleep_mono_us, then of used */
    uint8_t used;               /* PR17: set when a wake consumed it; covered by the crc */
} rd_asleep_rec_t;
_Static_assert(sizeof(rd_asleep_rec_t) <= 32, "the record fits 32 B of RTC memory");

typedef enum { RD_AS_NONE, RD_AS_SAME_BOOT, RD_AS_SOFT_RESET } rd_asleep_path_t;

bool rd_asleep_valid(const rd_asleep_rec_t *r);   /* magic and crc (false for NULL) */
void rd_asleep_seal(rd_asleep_rec_t *r);          /* sets magic and crc over the other fields */
/* The duration in s at a revealing wake, or -1 none (also -1 when r->used, or r invalid); *path (if not NULL) says
 * which rule applied:
 *   the same boot (r->token == boot_token): (now_mono_us - sleep_mono_us) / 1e6, truncated, whatever the unix clocks
 *   say (a step cannot bend it); -1 if negative;
 *   a soft reset (another token): now_unix - sleep_unix, when clock_set and sleep_unix is not 0, if 0 <= d < 400 days;
 *   otherwise none. */
int32_t rd_asleep_dur(const rd_asleep_rec_t *r, uint32_t boot_token, int64_t now_mono_us, int64_t now_unix,
                      bool clock_set, rd_asleep_path_t *path);
/* The consuming wake: sets used and re-seals, so a later wake with no new rd sleep gives -1, not a stale span. A NULL or
 * invalid record is left as it is. */
void rd_asleep_consume(rd_asleep_rec_t *r);

/* Which wakes refresh the readout (the Task 9 review's I1: keyed on the wake's cause, never on in->fresh): a wake after a
 * sleep with its reveal (PA_WAKE_FULL: a lift or PWR, whatever the reveal style and under reduced motion), a Stasis
 * lift's jump (lift: a FIRST wake of the face the lift shows), and the boot's first wake (first_of_boot: the first wake of
 * the boot on any face, which the caller passes true only when that face is the Stasis Clock, so a soft reset's span
 * shows; on any other boot face the record is discarded at boot, rd_asleep_boot_face). Never a FIRST from a swipe, the picker or a screen closing over a
 * new face (nothing slept), and not a wake without its reveal (PA_WAKE_SKIP_REVEAL, a re-lift within 10 s; a screen
 * closing; REPLAY): those keep the value shown. */
bool rd_asleep_wake_counts(omni_wake_cause_t cause, bool reveal, bool lift, bool first_of_boot);
/* The boot's face (the controller's ruling on the Task 9 fix round): when the boot's first wake is not the Stasis
 * Clock's (Digital, another face, the picker), a soft reset's record is consumed at once, so Stasis shows none until a
 * real sleep and wake and no span ever includes time spent awake since the boot. On Stasis the record stays for its
 * first wake (rd_asleep_wake_counts' first_of_boot). */
void rd_asleep_boot_face(rd_asleep_rec_t *r, bool on_stasis);
/* A face's wake: when counts, the duration rd_asleep_dur gives now is the value shown from here on and the record is
 * consumed (rd_asleep_consume); otherwise the record is left and `shown` is returned as it was. */
int32_t rd_asleep_wake(rd_asleep_rec_t *r, int32_t shown, bool counts, uint32_t boot_token, int64_t now_mono_us,
                       int64_t now_unix, bool clock_set);

/* R162: a soft reset's record is HELD while the clock is unset. The watch keeps time in RAM only (no RTC), so every
 * boot's first wake runs before SNTP; a soft reset's record would otherwise be read (and consumed) as none there.
 *
 * At boot (rd_asleep_boot_recs, after the cold-boot rule) a usable record from another boot (valid, unconsumed, its
 * wall clock set at the sleep) moves to the held slot and the live record is cleared; the live record then serves this
 * boot's sleeps, and the first of them discards the held record (rd_asleep_new_sleep). A second soft reset with nothing usable keeps the held record; a usable newer one replaces it.
 * A power-on or brownout clears both (keep false), and an invalid slot is cleared.
 *
 * rd_asleep_wake_held is rd_asleep_wake with the held slot: when the wake counts and a held record is live,
 *   (live only while no sleep has come since the boot: a counting wake with no sleep before it, i.e. the boot's own
 *   wake or a lift, with the clock already set)
 *   on the Stasis Clock with the clock set: the value is the held record's wall-clock span (rd_asleep_dur's unix path,
 *   so -1 past the 400-day cap or behind the clock); both records are consumed;
 *   on the Stasis Clock with the clock unset: the held record stays (the readout says AWAITING TIME SIGNAL anyway); the
 *   live record is handled as rd_asleep_wake would;
 *   on any other face (a full wake elsewhere first): the held record is discarded, then rd_asleep_wake as before.
 * A wake that does not count (a swipe, the picker, a re-lift within 10 s, a screen closing) touches neither. */
/* A new sleep (every rd face's on_sleep): *now becomes the live record, and a held soft-reset record is discarded (the
 * Task 17 review's ruling: any new sleep supersedes it, so a later wake shows that sleep, never a span with this boot's
 * awake time in it). NULL held: the live record only. */
void rd_asleep_new_sleep(rd_asleep_rec_t *rec, rd_asleep_rec_t *held, const rd_asleep_rec_t *now);
void rd_asleep_boot_recs(rd_asleep_rec_t *rec, rd_asleep_rec_t *held, uint32_t boot_token, bool keep);
bool rd_asleep_held_live(const rd_asleep_rec_t *held);   /* valid and not consumed */
int32_t rd_asleep_wake_held(rd_asleep_rec_t *rec, rd_asleep_rec_t *held, int32_t shown, bool counts, bool on_stasis,
                            uint32_t boot_token, int64_t now_mono_us, int64_t now_unix, bool clock_set);

/* spec section 14's cold-boot clear, pure (PR17): false (clear the record) on a power-on or brownout reset */
typedef enum { RD_RST_POWERON, RD_RST_BROWNOUT, RD_RST_SOFT, RD_RST_PANIC, RD_RST_WDT, RD_RST_OTHER } rd_rst_t;
bool rd_asleep_keep_at_boot(rd_rst_t reason);

/* The readout (spec section 9.6's table). Line 1 into l1: "STASIS 3H 25M 07S" (d >= 1 h, h up to 999),
 * "STASIS 04M 05S" (d < 1 h), "STASIS 999H+" (1000 h or more), "STASIS --H --M" (none: slept_s < 0),
 * "AWAITING TIME SIGNAL" (the clock unset, whatever slept_s). *l2: "ROUNDED UP: 3 000 000 YRS", or
 * "NOTHING TO THAW YET" when the clock is unset. */
#define RD_ASLEEP_LINE 24
void rd_asleep_lines(int32_t slept_s, bool clock_set, char l1[RD_ASLEEP_LINE], const char **l2);
