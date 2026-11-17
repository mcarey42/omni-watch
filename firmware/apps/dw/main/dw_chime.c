/* dw_chime (dw_chime.h): the dw theme's chime plan for face_kit's chime service (fk_chime_svc.h; rd wave 1
 * Task 2, the plan the service called itself until then, dw wave 1 Task 16, spec section 13).
 *
 * ctx: the prefs snapshot (dw_prefs_now: the Chime row and the quiet window), never settings() (R27). play (the
 * esp_timer task, under the service's lock; it logs nothing and keeps its arrays in statics): on PLAY,
 * dw_chime_play_plan's steps (the chime at 0, then the hour's pings, dw/chime.h) through sfx_play_seq with keep at
 * the verdict's gain; the chime's length comes from the dw_chime.pcm asset (never a constant), read at the first
 * play. env (the faces, LVGL task): dw_chime_env (bound to the asset at the first play) at the verdict's pings and
 * the length that played (published for it, s_env_ms). */
#include "dw_chime.h"
#include <stdio.h>
#include "assets.h"
#include "sfx.h"
#include "dw/chime.h"
#include "dw_prefs_svc.h"

#define CHIME_PCM "dw_chime.pcm"
#define PING_PCM "dw_ping.pcm"
#define PCM_MS(bytes) ((uint32_t)((bytes) / 2 / 16))   /* s16le mono 16 kHz */

static uint16_t s_chime_ms;          /* the service's lock (play): dw_chime.pcm's length */
static uint16_t s_env_ms;            /* atomic: the length the last chime played with (env's) */
static uint32_t s_no_pcm, s_refused; /* the service's lock: plays that found no dw_chime.pcm, or sfx_play_seq refused */
static dw_chime_step_t s_plan[13];   /* the service's lock: off the esp_timer task's stack */
static sfx_step_t s_steps[13];

/* dw_chime.pcm's length, from the asset (never a constant); 0 if it is absent (the assets image is not mapped yet) */
static uint16_t chime_ms(void)
{
    size_t size = 0;
    const uint32_t ms = assets_raw(CHIME_PCM, &size) ? PCM_MS(size) : 0;
    return (uint16_t)(ms > DW_CHIME_CHIME_MAX_MS ? DW_CHIME_CHIME_MAX_MS : ms);
}

static void ctx(fk_chime_ctx_t *c)
{
    c->chime_on = dw_prefs_now(DW_PF_CHIME) != 0;
    c->quiet_from_h = dw_prefs_now(DW_PF_QUIET_FROM);
    c->quiet_to_h = dw_prefs_now(DW_PF_QUIET_TO);
}

static bool play(const fk_chime_verdict_t *v, uint8_t vol_pct)
{
    if (s_chime_ms == 0) {
        s_chime_ms = chime_ms();
        size_t size = 0;
        const uint8_t *pcm = assets_raw(CHIME_PCM, &size);   /* the envelope follows the sound that plays (once) */
        dw_chime_env_bind(pcm, pcm ? size : 0);
    }
    if (s_chime_ms == 0) {   /* no dw_chime.pcm: silent (counted; `dw chime` shows it) */
        s_no_pcm++;
        return false;
    }
    const unsigned n = dw_chime_play_plan(v, vol_pct, s_chime_ms, s_plan, 13);
    for (unsigned i = 0; i < n; i++) s_steps[i] = (sfx_step_t){s_plan[i].what ? PING_PCM : CHIME_PCM, s_plan[i].at_ms};
    if (!sfx_play_seq(s_steps, (uint8_t)n, vol_pct, true)) {   /* a sound missing (counted) */
        s_refused++;
        return false;
    }
    __atomic_store_n(&s_env_ms, s_chime_ms, __ATOMIC_RELEASE);   /* before the service publishes the start */
    return true;
}

static void env(uint32_t ms, const fk_chime_verdict_t *at_play, fk_face_in_t *in)
{
    in->chime = dw_chime_env(ms, at_play->pings, __atomic_load_n(&s_env_ms, __ATOMIC_ACQUIRE));
}

const fk_chime_theme_t DW_CHIME_THEME = {.ctx = ctx, .play = play, .env = env, .len_ms = DW_CHIME_MAX_MS};

#if CONFIG_OMNI_DEBUG_CONSOLE
void dw_chime_print(void)
{
    fk_chime_svc_print("DW CHIME");
    /* before the first play s_chime_ms is still 0: the length from the asset here (rd wave 1 Task 2 review M2) */
    const uint16_t ms = s_chime_ms ? s_chime_ms : chime_ms();
    printf("DW CHIME chime %u ms; no chime pcm %lu, refused %lu\n", (unsigned)ms, (unsigned long)s_no_pcm,
           (unsigned long)s_refused);
}
#endif
