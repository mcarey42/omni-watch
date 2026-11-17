/* A theme's offer and boot rules (fk/offer.h). */
#include "fk/offer.h"
#include <stddef.h>

#define IDS 16          /* an offered mask has a bit for ids 0 .. 15 */
#define CAROUSEL_MAX 16 /* OMNI_CAROUSEL_MAX */

static int find(const uint8_t *carousel, uint8_t len, uint8_t id)
{
    for (uint8_t i = 0; carousel && i < len; i++)
        if (carousel[i] == id) return i;
    return -1;
}

bool fk_offer_list(uint16_t *offered, uint8_t carousel[16], uint8_t *len, const uint8_t *list, uint8_t n)
{
    bool changed = false;
    for (uint8_t i = 0; list && i < n; i++) {
        const uint8_t id = list[i];
        if (id >= IDS) continue;   /* no bit to keep it by */
        const uint16_t bit = (uint16_t)(1u << id);
        if (*offered & bit) continue;
        if (*len < CAROUSEL_MAX && find(carousel, *len, id) < 0) carousel[(*len)++] = id;
        *offered |= bit;   /* offered once, appended or not (present, or no room) */
        changed = true;
    }
    return changed;
}

bool fk_offer_reset_list(uint16_t *offered, const uint8_t *carousel, uint8_t len, const uint8_t *list, uint8_t n)
{
    const uint16_t was = *offered;
    for (uint8_t i = 0; list && i < n; i++)
        if (list[i] < IDS && find(carousel, len, list[i]) < 0) *offered &= (uint16_t)~(1u << list[i]);
    return *offered != was;
}

bool fk_prefs_boot_offer(bool writable, fk_boot_t b)
{
    return writable && (b == FK_BOOT_KEEP || b == FK_BOOT_RESET_COMMIT);
}

fk_boot_t fk_prefs_boot(bool writable, fk_load_t r)
{
    if (r == FK_LOAD_READ_ONLY) return FK_BOOT_RAM_ONLY;
    if (r == FK_LOAD_ABSENT) return writable ? FK_BOOT_RESET_COMMIT : FK_BOOT_RAM_ONLY;
    return FK_BOOT_KEEP;
}
