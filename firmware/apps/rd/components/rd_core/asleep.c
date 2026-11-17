/* Time asleep's pure rules (rd/asleep.h; rd wave 1 Task 9, spec section 9.6, PR17). No libm, no allocation. */
#include "rd/asleep.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "fk/face.h"   /* fk_crc32 */

/* the crc over the covered fields only (never the padding: RTC memory's garbage stays garbage) */
static uint32_t rec_crc(const rd_asleep_rec_t *r)
{
    uint32_t c = fk_crc32(0, &r->magic, sizeof r->magic);
    c = fk_crc32(c, &r->token, sizeof r->token);
    c = fk_crc32(c, &r->sleep_unix, sizeof r->sleep_unix);
    c = fk_crc32(c, &r->sleep_mono_us, sizeof r->sleep_mono_us);
    return fk_crc32(c, &r->used, sizeof r->used);
}

bool rd_asleep_valid(const rd_asleep_rec_t *r) { return r && r->magic == RD_ASLEEP_MAGIC && r->crc == rec_crc(r); }

void rd_asleep_seal(rd_asleep_rec_t *r)
{
    if (!r) return;
    r->magic = RD_ASLEEP_MAGIC;
    r->crc = rec_crc(r);
}

int32_t rd_asleep_dur(const rd_asleep_rec_t *r, uint32_t boot_token, int64_t now_mono_us, int64_t now_unix,
                      bool clock_set, rd_asleep_path_t *path)
{
    rd_asleep_path_t p = RD_AS_NONE;
    int64_t d = -1;
    if (rd_asleep_valid(r) && !r->used) {
        if (r->token == boot_token) {   /* the same boot: the monotonic clock (esp_timer counts through light sleep) */
            p = RD_AS_SAME_BOOT;
            d = now_mono_us >= r->sleep_mono_us ? (now_mono_us - r->sleep_mono_us) / 1000000 : -1;
            if (d > INT32_MAX) d = INT32_MAX;
        } else if (clock_set && r->sleep_unix != 0) {   /* a soft reset: both wall clocks set */
            d = now_unix - r->sleep_unix;
            if (d >= 0 && d < RD_ASLEEP_MAX_S) p = RD_AS_SOFT_RESET;
            else d = -1;
        }
    }
    if (d < 0) p = RD_AS_NONE;
    if (path) *path = p;
    return (int32_t)d;
}

void rd_asleep_consume(rd_asleep_rec_t *r)
{
    if (!rd_asleep_valid(r)) return;
    r->used = 1;
    rd_asleep_seal(r);
}

bool rd_asleep_wake_counts(omni_wake_cause_t cause, bool reveal, bool lift, bool first_of_boot)
{
    if (lift) return true;                                   /* a Stasis lift, its jump included */
    if (cause == OMNI_WAKE_AFTER_SLEEP) return reveal;       /* a full wake; not a re-lift or a screen closing */
    if (cause == OMNI_WAKE_FIRST) return reveal && first_of_boot;   /* the boot's on Stasis; never a swipe or the picker */
    return false;                                            /* REPLAY: awake, nothing slept */
}

void rd_asleep_boot_face(rd_asleep_rec_t *r, bool on_stasis)
{
    if (!on_stasis) rd_asleep_consume(r);
}

int32_t rd_asleep_wake(rd_asleep_rec_t *r, int32_t shown, bool counts, uint32_t boot_token, int64_t now_mono_us,
                       int64_t now_unix, bool clock_set)
{
    if (!counts) return shown;
    const int32_t d = rd_asleep_dur(r, boot_token, now_mono_us, now_unix, clock_set, NULL);
    rd_asleep_consume(r);
    return d;
}

void rd_asleep_new_sleep(rd_asleep_rec_t *rec, rd_asleep_rec_t *held, const rd_asleep_rec_t *now)
{
    if (!rec || !now) return;
    *rec = *now;
    rd_asleep_consume(held);   /* a new sleep supersedes a held soft-reset record */
}

void rd_asleep_boot_recs(rd_asleep_rec_t *rec, rd_asleep_rec_t *held, uint32_t boot_token, bool keep)
{
    if (!rec || !held) return;
    if (!keep || !rd_asleep_valid(rec)) memset(rec, 0, sizeof *rec);   /* magic 0: invalid */
    if (!keep || !rd_asleep_valid(held)) memset(held, 0, sizeof *held);
    if (rd_asleep_valid(rec) && !rec->used && rec->token != boot_token && rec->sleep_unix != 0) {
        *held = *rec;   /* R162: held for the first counting Stasis wake with the clock set */
        memset(rec, 0, sizeof *rec);
    }
}

bool rd_asleep_held_live(const rd_asleep_rec_t *held) { return rd_asleep_valid(held) && !held->used; }

int32_t rd_asleep_wake_held(rd_asleep_rec_t *rec, rd_asleep_rec_t *held, int32_t shown, bool counts, bool on_stasis,
                            uint32_t boot_token, int64_t now_mono_us, int64_t now_unix, bool clock_set)
{
    if (!counts) return shown;
    if (rd_asleep_held_live(held)) {
        if (!on_stasis) {
            rd_asleep_consume(held);   /* a full wake elsewhere first: discarded */
        } else if (clock_set) {
            const int32_t d = rd_asleep_dur(held, boot_token, now_mono_us, now_unix, true, NULL);
            rd_asleep_consume(held);
            rd_asleep_consume(rec);
            return d;
        }
    }
    return rd_asleep_wake(rec, shown, true, boot_token, now_mono_us, now_unix, clock_set);
}

bool rd_asleep_keep_at_boot(rd_rst_t reason) { return reason != RD_RST_POWERON && reason != RD_RST_BROWNOUT; }

void rd_asleep_lines(int32_t slept_s, bool clock_set, char l1[RD_ASLEEP_LINE], const char **l2)
{
    static const char UP[] = "ROUNDED UP: 3 000 000 YRS";
    if (!clock_set) {
        snprintf(l1, RD_ASLEEP_LINE, "AWAITING TIME SIGNAL");
        if (l2) *l2 = "NOTHING TO THAW YET";
        return;
    }
    if (l2) *l2 = UP;
    if (slept_s < 0) {
        snprintf(l1, RD_ASLEEP_LINE, "STASIS --H --M");
        return;
    }
    const long h = (long)slept_s / 3600, m = (long)slept_s / 60 % 60, s = (long)slept_s % 60;
    if (h >= 1000) snprintf(l1, RD_ASLEEP_LINE, "STASIS 999H+");
    else if (h >= 1) snprintf(l1, RD_ASLEEP_LINE, "STASIS %ldH %02ldM %02ldS", h, m, s);
    else snprintf(l1, RD_ASLEEP_LINE, "STASIS %02ldM %02ldS", m, s);
}
