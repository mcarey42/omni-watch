/* rd_asleep (rd_asleep.h): the time-asleep record on the device (rd wave 1 Task 9, spec section 9.6, R154, PR17).
 *
 * The record is RTC_NOINIT (RTC slow memory): the watch's ASLEEP state is light sleep between the IMU's polls (on
 * battery; panel off and 40 MHz on USB, board/power.h), which keeps all RAM, so the record survives every sleep; it
 * survives a soft reset, a panic and a watchdog reset too, which keep RTC memory, and its magic and crc catch the
 * garbage a power-on leaves. A power-on or brownout reset clears it outright (rd_asleep_keep_at_boot: a cold boot
 * shows "--"). Never NVS (R26/R27: only the event loop writes NVS; this record is written at every sleep).
 *
 * The boot token is esp_random() once a boot, in RAM: a record with this boot's token is the same boot (the monotonic
 * path: esp_timer counts through light sleep, so a clock step cannot bend it); another token is a soft reset (the unix
 * path, when both clocks were set). The on_sleep hook writes the record at every rd face's sleep (the glance or off,
 * and a swipe away, which the shell's face_sleep also runs). The woke hook (face_kit's, with the wake's cause, reveal
 * and lift; the Task 9 review's I1) decides by rd_asleep_wake_counts whether this wake refreshes the readout: a full
 * wake after a sleep, a Stasis lift, the boot's first wake when it is Stasis's (on any other boot face the record is
 * discarded at boot, rd_asleep_boot_on). Then the duration is frozen and the record consumed; a
 * swipe, the picker or a screen closing (a FIRST wake: nothing slept) and a re-lift within 10 s keep the value shown.
 * The slept hook only serves the frozen value.
 *
 * R162: with no RTC the clock is unset at every boot until SNTP, so a soft reset's record moves at boot to a held slot
 * (s_held, RTC_NOINIT too) and waits there: the first counting wake on the Stasis Clock with the clock set shows its
 * wall-clock span and consumes it; any new sleep (on_sleep) discards it, so it shows only at a counting wake with no
 * sleep since the boot; a counting wake on another face first discards it, as does a boot onto another face
 * and a full wake on a face these hooks never see (lift_face's rd_asleep_discard_held); past the 400-day cap it gives
 * none. The live record serves this boot's own sleeps meanwhile (rd_asleep_wake_held).
 *
 * A full wake on the shared Digital face never reaches these hooks (it is no face_kit face), and that is harmless: the
 * record then stays unconsumed, but only a wake that counts can read it, and an rd face's next sleep rewrites it first;
 * the one path that reaches an rd face's counting wake without an rd sleep in between is a Stasis lift from Digital
 * (or a placeholder), and lift_face discards the record there (rd_asleep_discard), so the lift shows none (PR17), never
 * the time since the swipe that wrote it. A swipe from Digital to an rd face is a FIRST wake: it keeps the value. The
 * hooks run on the LVGL task, the discard on the event loop; every access to the record takes the one lock. */
#include "rd_asleep.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "omni/timefmt.h"
#include "rd/asleep.h"

static RTC_NOINIT_ATTR rd_asleep_rec_t s_rec;
static RTC_NOINIT_ATTR rd_asleep_rec_t s_held;   /* R162: a soft reset's record, held until the clock is set */
static uint32_t s_token;        /* this boot's */
static bool s_booted;
static int32_t s_frozen = -1;   /* the last completed sleep's duration, -1 none since boot */
static bool s_woken;            /* a face has woken this boot (the boot's first wake counts, on Stasis only) */
static bool s_boot_stasis;      /* the boot's first wake is the Stasis Clock's (rd_asleep_boot_on) */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static rd_rst_t reset_kind(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return RD_RST_POWERON;
    case ESP_RST_BROWNOUT: return RD_RST_BROWNOUT;
    case ESP_RST_SW: return RD_RST_SOFT;
    case ESP_RST_PANIC: return RD_RST_PANIC;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return RD_RST_WDT;
    default: return RD_RST_OTHER;
    }
}

void rd_asleep_boot(void)
{
    if (s_booted) return;   /* after_store runs again after a migrate force: the token stays this boot's */
    s_booted = true;
    s_token = esp_random();
    const bool keep = rd_asleep_keep_at_boot(reset_kind(esp_reset_reason()));
    portENTER_CRITICAL(&s_mux);
    rd_asleep_boot_recs(&s_rec, &s_held, s_token, keep);   /* the cold-boot clear; a soft reset's record held (R162) */
    portEXIT_CRITICAL(&s_mux);
}

void rd_asleep_on_sleep(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    rd_asleep_rec_t r;
    memset(&r, 0, sizeof r);
    r.token = s_token;
    r.sleep_unix = omni_time_is_set((int64_t)tv.tv_sec) ? (int64_t)tv.tv_sec : 0;
    r.sleep_mono_us = esp_timer_get_time();
    r.used = 0;
    rd_asleep_seal(&r);
    portENTER_CRITICAL(&s_mux);
    rd_asleep_new_sleep(&s_rec, &s_held, &r);   /* and a held soft-reset record goes: this sleep supersedes it */
    portEXIT_CRITICAL(&s_mux);
}

void rd_asleep_woke(omni_wake_cause_t cause, bool reveal, bool lift, bool on_stasis)
{
    /* the boot's first wake on any face: on Stasis it reaches this hook first; on another face (which may be Digital,
     * whose wake never reaches it) rd_asleep_boot_on already discarded the record, and no wake here is the first */
    const bool counts = rd_asleep_wake_counts(cause, reveal, lift, !s_woken && s_boot_stasis);
    s_woken = true;
    if (!counts) return;
    const int64_t mono = esp_timer_get_time();
    const int64_t unix_s = (int64_t)time(NULL);
    const bool set = omni_time_is_set(unix_s);
    portENTER_CRITICAL(&s_mux);
    s_frozen = rd_asleep_wake_held(&s_rec, &s_held, s_frozen, true, on_stasis, s_token, mono, unix_s, set);
    portEXIT_CRITICAL(&s_mux);
}

void rd_asleep_boot_on(bool on_stasis)
{
    s_boot_stasis = on_stasis;
    portENTER_CRITICAL(&s_mux);
    rd_asleep_boot_face(&s_rec, on_stasis);
    rd_asleep_boot_face(&s_held, on_stasis);   /* the boot's first wake elsewhere: a full wake elsewhere first (R162) */
    portEXIT_CRITICAL(&s_mux);
}

void rd_asleep_slept(fk_face_in_t *in) { in->slept_s = s_frozen; }

void rd_asleep_discard(void)
{
    portENTER_CRITICAL(&s_mux);
    rd_asleep_consume(&s_rec);
    portEXIT_CRITICAL(&s_mux);
}

void rd_asleep_discard_held(void)
{
    portENTER_CRITICAL(&s_mux);
    rd_asleep_consume(&s_held);
    portEXIT_CRITICAL(&s_mux);
}

#if CONFIG_OMNI_DEBUG_CONSOLE
void rd_asleep_print(void)
{
    portENTER_CRITICAL(&s_mux);
    const rd_asleep_rec_t r = s_rec, h = s_held;
    const int32_t frozen = s_frozen;
    portEXIT_CRITICAL(&s_mux);
    const bool valid = rd_asleep_valid(&r);
    const int64_t mono = esp_timer_get_time(), unix_s = (int64_t)time(NULL);
    rd_asleep_path_t path = RD_AS_NONE;
    const int32_t would = rd_asleep_dur(&r, s_token, mono, unix_s, omni_time_is_set(unix_s), &path);
    printf("RD ASLEEP record %s%s; token %s; sleep_unix %" PRId64 ", sleep_mono %" PRId64 " us (now %" PRId64
           " us); a wake now: %" PRId32 " s (%s)\n",
           valid ? "valid" : "none", valid && r.used ? ", consumed" : "",
           !valid ? "-" : r.token == s_token ? "this boot" : "another boot", valid ? r.sleep_unix : (int64_t)0,
           valid ? r.sleep_mono_us : (int64_t)0, mono, would,
           path == RD_AS_SAME_BOOT ? "same boot" : path == RD_AS_SOFT_RESET ? "soft reset" : "none");
    if (rd_asleep_held_live(&h))   /* R162 */
        printf("RD ASLEEP held (a soft reset's): sleep_unix %" PRId64 "; a Stasis wake now: %" PRId32 " s%s\n",
               h.sleep_unix, rd_asleep_dur(&h, s_token, mono, unix_s, omni_time_is_set(unix_s), NULL),
               omni_time_is_set(unix_s) ? "" : " (the clock unset: held)");
    else
        printf("RD ASLEEP held none\n");
    char l1[RD_ASLEEP_LINE];
    const char *l2 = NULL;
    rd_asleep_lines(frozen, omni_time_is_set(unix_s), l1, &l2);
    printf("RD ASLEEP shown %" PRId32 " s: %s / %s; reset reason %d\n", frozen, l1, l2, (int)esp_reset_reason());
}

void rd_asleep_forget(void)
{
    portENTER_CRITICAL(&s_mux);
    memset(&s_rec, 0, sizeof s_rec);
    memset(&s_held, 0, sizeof s_held);
    s_frozen = -1;
    portEXIT_CRITICAL(&s_mux);
}
#endif
