#pragma once
/* Which reveal a face's wake plays (spec §8, Stage 3 Task 11, plan D2), theme-neutral so a later theme reuses the cause
 * and skip rules (D6), as Stage 2's reveal curves (globe_motion.h) are shared by its faces. Pure C. */
#include <stdbool.h>
#include <stdint.h>

typedef enum { OMNI_RV_NONE, OMNI_RV_DIAL, OMNI_RV_FLIP, OMNI_RV_IRIS } omni_rv_t;
/* The shell's wake(reveal) hook does not say why it came, so the face tracks it: FIRST = the first wake since create
 * (swipe, picker, boot, a screen closing over a new face, R54); AFTER_SLEEP = a sleep() came before (lift, PWR, the
 * glance ending); REPLAY = awake with no sleep since the last wake (BOOT double). */
typedef enum { OMNI_WAKE_FIRST, OMNI_WAKE_AFTER_SLEEP, OMNI_WAKE_REPLAY } omni_wake_cause_t;
/* reveal false -> NONE (at rest); FIRST -> DIAL; AFTER_SLEEP and REPLAY -> the setting (FLIP, IRIS, or DIAL for NONE).
 * reveal_style is enum omni_reveal (settings.h); a value out of range is the default, FLIP. */
omni_rv_t omni_reveal_for(omni_wake_cause_t cause, bool reveal, uint8_t reveal_style);

/* The cause tracker: zeroed at the face's create; the face calls _sleep from its sleep hook and _wake from its wake hook.
 * A sleep before the first wake leaves that wake FIRST; two sleeps in a row are one. */
typedef struct { bool woke, slept; } omni_wake_track_t;
void omni_wake_track_sleep(omni_wake_track_t *w);
omni_wake_cause_t omni_wake_track_wake(omni_wake_track_t *w);
