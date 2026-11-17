/* rd_chime (rd_chime.h): the Mining Ship theme's chime plan for face_kit's chime service (fk_chime_svc.h; rd wave 1
 * Task 8, spec section 11, "Ship Clears Its Throat").
 *
 * ctx: the prefs snapshot (rd_prefs_now: the Chime row and the quiet window), never settings() (R27). play (the
 * esp_timer task under the service's lock, or the console task through fk_chime_svc_play_now; it never blocks or logs,
 * and returns whether it sounded): rd_chime_plan's one asset at its gain (the service's 100, or 40 in the quiet window),
 * once, through sfx_play_seq(&step, 1, gain, true) (PR14: sfx_play_asset has no gain and no keep), so the chime keeps
 * its exemption from the sleep and glance stop; the verdict's pings are ignored (the recipe has none). env (the faces,
 * any task): rd_chime_env's thrum, beep (max(bip, bop)) and click at ms after the start (PR15: at_play is not needed);
 * in->chime stays 0, the first theme's channel. */
#include "rd_chime.h"
#include <stdio.h>
#include "sfx.h"
#include "rd/chime_env.h"
#include "rd/prefs.h"
#include "rd_prefs_svc.h"

static bool s_recipe;                /* atomic: the next play is the recipe voicing (the console's, taken by play) */
static uint32_t s_refused;           /* the service's lock: plays sfx_play_seq refused (an asset missing) */
static sfx_step_t s_step;            /* the service's lock: off the esp_timer task's stack */

static void ctx(fk_chime_ctx_t *c)
{
    c->chime_on = rd_prefs_now(RD_PF_CHIME) != 0;
    c->quiet_from_h = rd_prefs_now(RD_PF_QUIET_FROM);
    c->quiet_to_h = rd_prefs_now(RD_PF_QUIET_TO);
}

static bool play(const fk_chime_verdict_t *v, uint8_t vol_pct)
{
    const bool recipe = __atomic_exchange_n(&s_recipe, false, __ATOMIC_ACQ_REL);   /* once: the hour plays the default */
    const char *asset = NULL;
    uint8_t gain = 0;
    if (!rd_chime_plan(v, vol_pct, &asset, &gain)) return false;
    s_step = (sfx_step_t){recipe ? RD_CHIME_R_PCM : asset, 0};
    if (!sfx_play_seq(&s_step, 1, gain, true)) {   /* the asset missing (counted; `rd chime` shows it) */
        s_refused++;
        return false;
    }
    return true;
}

static void env(uint32_t ms, const fk_chime_verdict_t *at_play, fk_face_in_t *in)
{
    (void)at_play;
    rd_chime_env_t e;
    rd_chime_env(ms, &e);
    in->thrum = e.thrum;
    in->beep = e.beep;
    in->click = e.click;
}

const fk_chime_theme_t RD_CHIME = {.ctx = ctx, .play = play, .env = env, .len_ms = RD_CHIME_MS};

bool rd_chime_play_now(bool recipe)
{
    /* play takes the flag (an atomic exchange) under the service's lock. A top of hour that fires between this store
     * and play_now's lock plays the recipe instead, once, and this call the default: the console's check only, never
     * a stored state */
    __atomic_store_n(&s_recipe, recipe, __ATOMIC_RELEASE);
    const bool ok = fk_chime_svc_play_now();
    __atomic_store_n(&s_recipe, false, __ATOMIC_RELEASE);
    return ok;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
void rd_chime_print(void)
{
    fk_chime_svc_print("RD CHIME");
    printf("RD CHIME sound %s (%u ms), recipe %s; refused %lu\n", RD_CHIME_PCM, (unsigned)RD_CHIME_MS, RD_CHIME_R_PCM,
           (unsigned long)s_refused);
}
#endif
