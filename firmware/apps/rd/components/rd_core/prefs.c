/* t_rd/local's extension (rd/prefs.h): t_dw's codec rules (dw/prefs.h) over rd's own fields and defaults. */
#include "rd/prefs.h"
#include <string.h>
#include "rd/faces.h"

#define VER_OFF 0
#define LEN_OFF 1
#define MODE_OFF 2
#define CHIME_OFF 3
#define FROM_OFF 4
#define TO_OFF 5
#define OFFERED_OFF 6   /* uint16 LE */
_Static_assert(OFFERED_OFF + 2 == RD_EXT_V1_SIZE, "rd extension v1 layout");
_Static_assert(RD_EXT_V1_SIZE + RD_EXT_TAIL_MAX <= 255, "ext_len is one byte");
_Static_assert(RD_OFFERED_DEF == (1u << RD_F__COUNT) - 1u, "a fresh record has offered every built face");

#define DEF_QUIET_FROM 22
#define DEF_QUIET_TO 7

void rd_prefs_defaults(rd_prefs_t *p)
{
    memset(p, 0, sizeof *p);
    p->mode = RD_MODE_ALWAYS_ON;
    p->chime_on = 0;
    p->quiet_from_h = DEF_QUIET_FROM;
    p->quiet_to_h = DEF_QUIET_TO;
    p->offered = RD_OFFERED_DEF;
}

/* PR16: t_dw's rules exactly (an out-of-range field resets to its default, never a clamp) */
bool rd_prefs_validate(rd_prefs_t *p)
{
    bool ok = true;
    if (p->mode > RD_MODE_STASIS) { p->mode = RD_MODE_ALWAYS_ON; ok = false; }
    if (p->quiet_from_h >= 24) { p->quiet_from_h = DEF_QUIET_FROM; ok = false; }
    if (p->quiet_to_h >= 24) { p->quiet_to_h = DEF_QUIET_TO; ok = false; }
    return ok;
}

fk_load_t rd_prefs_decode(rd_prefs_t *p, const uint8_t *ext, size_t n)
{
    rd_prefs_defaults(p);
    if (n == 0) return FK_LOAD_ABSENT;
    /* the header first: nothing past it is read unless the length adds up (8 and up; any bytes past 8 a newer
     * build's tail) */
    if (ext == NULL || n < RD_EXT_V1_SIZE || ext[VER_OFF] != RD_EXT_VERSION || ext[LEN_OFF] != n ||
        n - RD_EXT_V1_SIZE > RD_EXT_TAIL_MAX) {
        p->read_only = true;   /* never written: encode refuses */
        return FK_LOAD_READ_ONLY;
    }
    p->mode = ext[MODE_OFF];
    p->chime_on = ext[CHIME_OFF] != 0;
    p->quiet_from_h = ext[FROM_OFF];
    p->quiet_to_h = ext[TO_OFF];
    p->offered = (uint16_t)(ext[OFFERED_OFF] | ext[OFFERED_OFF + 1] << 8);
    p->tail_len = (uint8_t)(n - RD_EXT_V1_SIZE);
    if (p->tail_len) memcpy(p->tail, &ext[RD_EXT_V1_SIZE], p->tail_len);
    rd_prefs_validate(p);
    return p->tail_len ? FK_LOAD_TAIL_KEPT : FK_LOAD_LOADED;
}

size_t rd_prefs_encode(const rd_prefs_t *p, uint8_t *buf, size_t cap)
{
    if (p->read_only || p->tail_len > RD_EXT_TAIL_MAX) return 0;
    const size_t n = RD_EXT_V1_SIZE + p->tail_len;
    if (buf == NULL || cap < n) return 0;
    rd_prefs_t v = *p;   /* only valid values are ever written */
    rd_prefs_validate(&v);
    buf[VER_OFF] = RD_EXT_VERSION;
    buf[LEN_OFF] = (uint8_t)n;
    buf[MODE_OFF] = v.mode;
    buf[CHIME_OFF] = v.chime_on ? 1 : 0;
    buf[FROM_OFF] = v.quiet_from_h;
    buf[TO_OFF] = v.quiet_to_h;
    buf[OFFERED_OFF] = (uint8_t)(v.offered & 0xFF);
    buf[OFFERED_OFF + 1] = (uint8_t)(v.offered >> 8);
    if (v.tail_len) memcpy(&buf[RD_EXT_V1_SIZE], v.tail, v.tail_len);
    return n;
}

fk_load_t rd_prefs_load(rd_prefs_t *p, const uint8_t *ext, size_t n, size_t cap)
{
    if (n <= cap) return rd_prefs_decode(p, ext, n);
    rd_prefs_defaults(p);
    p->read_only = true;   /* the bytes were never seen: never written over */
    return FK_LOAD_READ_ONLY;
}

static const uint8_t FIELD_LIMIT[RD_PF__COUNT] = {[RD_PF_MODE] = RD_MODE_STASIS + 1, [RD_PF_CHIME] = 2,
                                                 [RD_PF_QUIET_FROM] = 24, [RD_PF_QUIET_TO] = 24};

uint8_t rd_prefs_get(const rd_prefs_t *p, rd_pref_field_t f)
{
    switch (f) {
    case RD_PF_MODE: return p->mode;
    case RD_PF_CHIME: return p->chime_on ? 1 : 0;
    case RD_PF_QUIET_FROM: return p->quiet_from_h;
    case RD_PF_QUIET_TO: return p->quiet_to_h;
    default: return 0;
    }
}

bool rd_prefs_set(rd_prefs_t *p, rd_pref_field_t f, uint8_t v)
{
    if ((unsigned)f >= RD_PF__COUNT || v >= FIELD_LIMIT[f]) return false;
    switch (f) {
    case RD_PF_MODE: p->mode = v; break;
    case RD_PF_CHIME: p->chime_on = v != 0; break;
    case RD_PF_QUIET_FROM: p->quiet_from_h = v; break;
    default: p->quiet_to_h = v; break;
    }
    return true;
}

rd_offer_verdict_t rd_prefs_offer_reset_verdict(rd_prefs_t *p, bool writable, const uint8_t *carousel, uint8_t len)
{
    if (!writable || p->read_only) return RD_OFFER_V_READ_ONLY;   /* nothing could be saved: nothing changes */
    return fk_offer_reset_list(&p->offered, carousel, len, RD_DEF_CAROUSEL, RD_DEF_CAROUSEL_LEN) ? RD_OFFER_V_RESET
                                                                                                  : RD_OFFER_V_NOTHING;
}

rd_after_store_t rd_prefs_after_store_decide(rd_prefs_t *p, fk_load_t r, bool writable, uint8_t carousel[16],
                                             uint8_t *len)
{
    rd_after_store_t a = { .d = fk_prefs_boot(writable, r), .reset = false, .offered = false, .save = false };
    if (a.d == FK_BOOT_RESET_COMMIT) {   /* the first rd boot on a watch (spec section 5) */
        memcpy(carousel, RD_DEF_CAROUSEL, RD_DEF_CAROUSEL_LEN);
        *len = RD_DEF_CAROUSEL_LEN;
        a.reset = true;
    }
    a.offered = fk_prefs_boot_offer(writable, a.d) &&
                fk_offer_list(&p->offered, carousel, len, RD_DEF_CAROUSEL, RD_DEF_CAROUSEL_LEN);
    a.save = a.reset || a.offered;
    return a;
}
