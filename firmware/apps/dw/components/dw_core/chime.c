#include "dw/chime.h"

#include <math.h>

#define FADE_MS 500u
#define HOLD_LEVEL 0.2f
#define GLINT_LEVEL 0.5f
#define WIN_SAMPLES (DW_CHIME_WIN_MS * 16)   /* 16 kHz */

static uint8_t s_loud[DW_CHIME_WIN_MAX];   /* the bound chime's loudness per window */
static unsigned s_loud_n;

/* Walks the (flash-mapped) PCM once, a few ms, at the first chime. */
void dw_chime_env_bind(const uint8_t *pcm, size_t bytes)
{
    s_loud_n = 0;
    if (!pcm) return;
    size_t samples = bytes / 2;
    if (samples > (size_t)DW_CHIME_CHIME_MAX_MS * 16) samples = (size_t)DW_CHIME_CHIME_MAX_MS * 16;
    const unsigned n = (unsigned)((samples + WIN_SAMPLES - 1) / WIN_SAMPLES);
    /* static: off the esp_timer task's stack (692 B); bind runs under the service's lock */
    static float rms[DW_CHIME_WIN_MAX];
    float top = 0.0f;
    for (unsigned w = 0; w < n; w++) {
        uint64_t acc = 0;
        size_t from = (size_t)w * WIN_SAMPLES, to = from + WIN_SAMPLES < samples ? from + WIN_SAMPLES : samples;
        for (size_t i = from; i < to; i++) {
            const int16_t v = (int16_t)(uint16_t)(pcm[2 * i] | (pcm[2 * i + 1] << 8));
            acc += (uint64_t)((int32_t)v * v);
        }
        rms[w] = sqrtf((float)acc / (float)(to - from));
        if (rms[w] > top) top = rms[w];
    }
    for (unsigned w = 0; w < n; w++) s_loud[w] = top > 0.0f ? (uint8_t)(rms[w] * 255.0f / top + 0.5f) : 0;
    s_loud_n = n;
}

static float loudness(uint32_t t_ms)
{
    const uint32_t w = t_ms / DW_CHIME_WIN_MS;
    if (w >= s_loud_n) return 0.0f;
    const float a = s_loud[w], b = w + 1 < s_loud_n ? s_loud[w + 1] : 0.0f;
    const float f = (float)(t_ms % DW_CHIME_WIN_MS) / (float)DW_CHIME_WIN_MS;
    return (a + (b - a) * f) / 255.0f;
}

uint8_t dw_chime_plan(uint8_t pings, uint16_t chime_ms, dw_chime_step_t out[13])
{
    if (!out) return 0;
    if (pings > 12) pings = 12;
    if (chime_ms > DW_CHIME_CHIME_MAX_MS) chime_ms = DW_CHIME_CHIME_MAX_MS;
    out[0].what = 0;
    out[0].at_ms = 0;
    for (uint8_t i = 0; i < pings; i++) {
        out[1 + i].what = 1;
        out[1 + i].at_ms = (uint16_t)(chime_ms + i * DW_CHIME_PING_GAP_MS);
    }
    return (uint8_t)(1 + pings);
}

unsigned dw_chime_play_plan(const fk_chime_verdict_t *v, uint8_t vol_pct, uint16_t chime_ms, dw_chime_step_t *steps,
                            unsigned cap)
{
    (void)vol_pct;   /* the gain the steps are played at (sfx_play_seq's); the steps carry none */
    if (!v || !steps || v->why != FK_CH_PLAY) return 0;
    dw_chime_step_t all[13];
    const unsigned n = dw_chime_plan(v->pings, chime_ms, all);
    const unsigned k = n < cap ? n : cap;
    for (unsigned i = 0; i < k; i++) steps[i] = all[i];
    return k;
}

static float maxf(float a, float b) { return a > b ? a : b; }

float dw_chime_env(uint32_t t_ms, uint8_t pings, uint16_t chime_ms)
{
    if (pings > 12) pings = 12;
    if (chime_ms > DW_CHIME_CHIME_MAX_MS) chime_ms = DW_CHIME_CHIME_MAX_MS;
    uint32_t last = pings ? (uint32_t)chime_ms + (uint32_t)(pings - 1) * DW_CHIME_PING_GAP_MS + DW_CHIME_PING_MAX_MS
                          : (uint32_t)chime_ms;
    if (t_ms >= last + FADE_MS) return 0.0f;

    float e = 0.0f;
    if (t_ms < chime_ms) {
        e = loudness(t_ms);
    } else {
        e = HOLD_LEVEL;
        for (uint8_t i = 0; i < pings; i++) {
            uint32_t at = (uint32_t)chime_ms + (uint32_t)i * DW_CHIME_PING_GAP_MS;
            if (t_ms >= at && t_ms < at + DW_CHIME_PING_MAX_MS) {
                float g = 1.0f - (float)(t_ms - at) / (float)DW_CHIME_PING_MAX_MS;
                e = maxf(e, GLINT_LEVEL * g);
            }
        }
    }
    if (t_ms >= last) e *= 1.0f - (float)(t_ms - last) / (float)FADE_MS;
    return e < 0.0f ? 0.0f : (e > 1.0f ? 1.0f : e);
}
