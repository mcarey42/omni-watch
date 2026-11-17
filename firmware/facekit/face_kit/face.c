/* The face runtime's small rules, the still frames' CRC and its table lookup (fk/face.h). Pure, no allocation. */
#include "fk/face.h"
#include <string.h>

bool fk_pulse_retap(const fk_face_ops_t *ops, bool pulsing, uint32_t pulse_ms)
{
    return !pulsing || pulse_ms >= ops->pulse_guard_ms;
}

uint16_t fk_face_timer_ms(const fk_face_ops_t *ops, bool running)
{
    if (ops->cadence_ms) return ops->cadence_ms;
    return running && (ops->wake_len_ms || ops->pulse_len_ms) ? FK_FACE_TIMER_WAKE_MS : 0;
}

void fk_face_in_advance(const fk_face_ops_t *ops, fk_face_in_t *in, uint32_t dt_ms)
{
    in->mono_ms += dt_ms;   /* wraps, as esp_timer's ms count does */
    if (in->wake_ms != UINT32_MAX) {
        const uint64_t w = (uint64_t)in->wake_ms + dt_ms;
        in->wake_ms = w < ops->wake_len_ms ? (uint32_t)w : UINT32_MAX;
    }
    if (in->pulsing) {
        const uint64_t p = (uint64_t)in->pulse_ms + dt_ms;
        if (p < ops->pulse_len_ms) {
            in->pulse_ms = (uint16_t)p;
        } else {
            in->pulsing = false;
            in->pulse_ms = 0;
        }
    }
}

int32_t fk_sync_age(int64_t now_s, int64_t last_ok_s)
{
    if (last_ok_s == 0) return -1;
    if (now_s <= last_ok_s) return 0;
    const int64_t d = now_s - last_ok_s;
    return d > INT32_MAX ? INT32_MAX : (int32_t)d;
}

uint16_t fk_variant_next(uint16_t v) { return (uint16_t)((v + 1u) % 255u); }

uint32_t fk_crc32(uint32_t crc, const void *p, size_t n)
{
    static const uint32_t T[16] = {0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu, 0x76dc4190u, 0x6b6b51f4u,
                                   0x4db26158u, 0x5005713cu, 0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
                                   0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu};
    const uint8_t *b = p;
    crc = ~crc;
    while (n--) {
        crc ^= *b++;
        crc = (crc >> 4) ^ T[crc & 15];
        crc = (crc >> 4) ^ T[crc & 15];
    }
    return ~crc;
}

const fk_verify_t *fk_verify_lookup(const fk_verify_tab_t *tabs, unsigned ntabs, const char *face, unsigned still,
                                    unsigned *dups)
{
    if (dups) *dups = 0;
    for (unsigned t = 0; t < ntabs; t++) {
        const fk_verify_t *hit = NULL;
        unsigned n = 0;
        for (unsigned r = 0; r < tabs[t].n; r++)
            if (tabs[t].rows[r].still == still && strcmp(tabs[t].rows[r].face, face) == 0) {
                if (!hit) hit = &tabs[t].rows[r];
                n++;
            }
        if (hit) {
            if (dups) *dups = n;
            return hit;
        }
    }
    return NULL;
}

void fk_still_render(const fk_face_ops_t *ops, const void *ctx, const fk_face_in_t *in, bool glance, uint16_t *fb)
{
    (glance ? ops->glance : ops->draw)(ctx, in, (omni_rect_t){0, 0, 466, 466}, fb, 466);
}
