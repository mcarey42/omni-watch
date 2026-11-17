#pragma once
/* The timer's alarm and bell decisions (Omni Stage 5 Task 6, fix round 1 m5), pure so the host can pin them; the
 * device's timer_svc.c carries them out (esp_timer, shell_post, sfx). uint32 ms times, compared by signed difference. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/timer.h"

#define OMNI_TMR_RETRY_FAST_US 100000u   /* F5: a failed post re-arms the one-shot this much later ... */
#define OMNI_TMR_RETRY_FAST_N 50u        /* ... this many times, ... */
#define OMNI_TMR_RETRY_SLOW_US 1000000u  /* ... then every 1 s for as long as it fails: never given up (m3) */
#define OMNI_TMR_RING_EVERY_MS 2000u     /* owner default: the bell again every 2 s ... */
#define OMNI_TMR_RING_FOR_MS 30000u      /* ... for at most 30 s from the countdown's end */
#define OMNI_TMR_WAKE_GAP_MS 1000u       /* a poll this late followed a pause of LVGL: the watch was asleep */

/* The one-shot's next arming after its post: 0 when it posted (stop), else the delay in us. fails counts the failed
 * posts in a row, this one included (1 = the first). */
uint32_t omni_tmr_retry_us(bool posted, uint32_t fails);

/* True when the poll at now, the one before at last (valid: there was one), followed a sleep. */
bool omni_tmr_woke(bool valid, uint32_t last, uint32_t now);

typedef struct {
    bool handled;        /* the timer face has been shown awake in this DONE */
    bool ringing;        /* a bell of this DONE may still be sounding */
    uint32_t last_ring;
} omni_tmr_bell_t;

typedef struct {
    bool ring;           /* play the bell now */
    bool keep;           /* post UI_ALARM_KEEP (activity; the watch stays awake while it rings) */
    bool post_alarm;     /* post UI_ALARM: a DONE the face has not been seen in, on a wake */
    bool handled_now;    /* the face was just seen awake in DONE: the one-shot's retries can stop */
    bool silence;        /* stop the bell */
} omni_tmr_bell_out_t;

/* One poll, after omni_tmr_tick (stepped: it returned true). mode and done_ms are the machine's after the tick.
 *  - The step to DONE rings once (handled cleared), unless the DONE is already past the 30 s cap: then it is silent.
 *  - While DONE with the face awake (handled set), the bell again every 2 s (with keep) until 30 s after done_ms; the
 *    cap silences it.
 *  - A DONE not handled, the face not awake, on a wake: post_alarm.
 *  - Any other mode silences a ringing bell. */
void omni_tmr_bell_poll(omni_tmr_bell_t *b, omni_tmr_mode_t mode, uint32_t done_ms, bool stepped, bool face_awake,
                        bool woke, uint32_t now, omni_tmr_bell_out_t *o);
/* Leaving the face (also the glance and sleep), or a change that left DONE: true = stop the bell. */
bool omni_tmr_bell_silence(omni_tmr_bell_t *b);

/* The console's `tmr count <s>` (m6): decimal digits only, at most 9, into *s. False (s untouched) for anything else,
 * so a typo is refused, never read as 0. */
bool omni_tmr_parse_seconds(const char *txt, uint32_t *s);
