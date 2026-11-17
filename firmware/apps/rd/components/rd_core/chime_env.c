/* The Mining Ship chime's picture envelope and plan (rd/chime_env.h; rd wave 1 Task 8, spec section 11; the mockup's
 * chimeEnv). The one rd_core file allowed libm (expf, RW8): the faces call it only while a chime plays, so no still
 * frame's bytes depend on it. */
#include "rd/chime_env.h"
#include <math.h>
#include <stddef.h>

void rd_chime_env(uint32_t ms, rd_chime_env_t *e)
{
    if (e == NULL) return;
    e->thrum = e->bip = e->bop = e->click = e->beep = 0.0f;
    if (ms >= RD_CHIME_MS) return;   /* uint32: UINT32_MAX and every wrapped value land here too */
    if (ms < 550) {
        const float u = (float)ms / 550.0f;
        e->thrum = u * u;
    } else if (ms < 1500) {
        e->thrum = 1.0f;
    } else if (ms < 2350) {
        const float u = 1.0f - (float)(ms - 1500) / 850.0f;
        e->thrum = u * u;
    }
    if (ms >= 750 && ms < 860) e->bip = expf(-(float)(ms - 750) / 45.0f);
    if (ms >= 950 && ms < 1100) e->bop = expf(-(float)(ms - 950) / 60.0f);
    if (ms >= 2200 && ms < 2260) e->click = 1.0f - (float)(ms - 2200) / 60.0f;
    e->beep = e->bip > e->bop ? e->bip : e->bop;
}

bool rd_chime_plan(const fk_chime_verdict_t *v, uint8_t vol_pct, const char **asset, uint8_t *gain)
{
    if (v == NULL || asset == NULL || gain == NULL || v->why != FK_CH_PLAY) return false;
    *asset = RD_CHIME_PCM;   /* the recipe has no hour pings: v->pings is ignored */
    *gain = vol_pct;
    return true;
}
