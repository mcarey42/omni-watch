#pragma once
/* Time asleep on the device (rd_asleep.c; rd wave 1 Task 9, spec section 9.6, R154, PR17): the record in RTC memory
 * (RTC_NOINIT, never NVS), the boot's token, and face_kit's slept and on_sleep hooks. The rules are rd_core's
 * (rd/asleep.h). */
#include <stdbool.h>
#include <stdint.h>
#include "fk/face.h"
#include "omni/reveal.h"
#include "sdkconfig.h"

/* The boot task, once (rd_theme.c's after_store; a second call does nothing): this boot's token (esp_random) and the
 * cold-boot clear (a power-on or brownout reset, or an invalid record, clears the record). */
void rd_asleep_boot(void);
/* face_kit's on_sleep hook (every rd face's sleep: the glance or off): the record of this sleep, sealed, used 0;
 * sleep_unix 0 when the clock is unset. It discards a held soft-reset record (rd_asleep_new_sleep). */
void rd_asleep_on_sleep(void);
/* The boot task, once the faces are registered (rd_theme.c's register_faces): whether the boot's face, the one the
 * boot's first wake shows, is the Stasis Clock. If not, the record is discarded (rd_asleep_boot_face) and no later wake
 * is the boot's first. */
void rd_asleep_boot_on(bool on_stasis);
/* face_kit's woke hook (every rd face's wake, before its first frame): when rd_asleep_wake_counts says this wake counts
 * (a full wake after a sleep, a Stasis lift, the boot's first wake), the duration of the last sleep is frozen from the
 * record (rd_asleep_dur) and the record consumed; otherwise the value shown stays. The held slot (R162): a soft
 * reset's record, moved there at boot (rd_asleep_boot) while the clock is unset. It is read instead of the live record
 * only at a counting wake on the Stasis Clock (on_stasis) with the clock set and no sleep since the boot (on_sleep
 * discards it: a new sleep supersedes it), and then consumed with the live one; a counting wake on another face, a
 * boot onto another face, the 400-day cap or a clock behind the sleep discards it (rd_asleep_wake_held). */
void rd_asleep_woke(omni_wake_cause_t cause, bool reveal, bool lift, bool on_stasis);
/* face_kit's slept hook (every frame's input): in->slept_s is the frozen value, -1 with none since boot. */
void rd_asleep_slept(fk_face_in_t *in);
/* The event loop, at a Stasis lift (rd_theme.c's lift_face) whose sleep was on a face that writes no record (the shared
 * Digital face, a placeholder): the record left is from leaving an rd face earlier (a swipe away also runs its sleep
 * hook), not from this sleep, so it is consumed and the wake shows none (PR17), never the time since that swipe. */
void rd_asleep_discard(void);
/* The event loop, at a full wake that stays on a face these hooks never see (the shared Digital face, a placeholder): a
 * full wake elsewhere first, so a held soft-reset record is discarded (R162). */
void rd_asleep_discard_held(void);
#if CONFIG_OMNI_DEBUG_CONSOLE
void rd_asleep_print(void);    /* `rd asleep`: the record, the token match, the frozen value, the readout */
void rd_asleep_forget(void);   /* `rd asleep forget`: invalidate the record and the frozen value (a cold boot's) */
#endif
