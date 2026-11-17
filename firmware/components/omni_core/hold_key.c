#include "omni/hold_key.h"

void omni_hold_key_down(omni_hold_key_t *h, uint32_t t)
{
    h->t_down = t;
    h->active = true;
}

omni_hold_key_rc_t omni_hold_key_gesture(omni_hold_key_t *h, const omni_gesture_t *g, uint16_t fire_ms)
{
    if (!h->active || g->kind == G_DOWN) return OMNI_HOLD_KEY_NONE;
    if (g->kind == G_HOLD && g->hold_ms < fire_ms) return OMNI_HOLD_KEY_NONE;
    h->active = false;
    return g->kind == G_HOLD ? OMNI_HOLD_KEY_FIRE : OMNI_HOLD_KEY_CANCEL;
}

uint16_t omni_hold_key_fill(const omni_hold_key_t *h, uint32_t now, uint16_t fire_ms)
{
    const int32_t age = (int32_t)(now - h->t_down);
    if (!h->active || age <= 0) return 0;
    return age >= fire_ms ? 1000 : (uint16_t)((uint32_t)age * 1000u / fire_ms);
}
