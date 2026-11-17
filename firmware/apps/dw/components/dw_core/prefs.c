#include "dw/prefs.h"
#include <string.h>

#define VER_OFF 0
#define LEN_OFF 1
#define MODE_OFF 2
#define CHIME_OFF 3
#define FROM_OFF 4
#define TO_OFF 5
#define OFFERED_OFF 6   /* uint16 LE; appended in wave 2 (WD24) */
_Static_assert(TO_OFF + 1 == DW_EXT_W1_SIZE, "wave 1's extension layout");
_Static_assert(OFFERED_OFF == DW_EXT_W1_SIZE && OFFERED_OFF + 2 == DW_EXT_V1_SIZE, "dw extension v1 layout");
_Static_assert(DW_EXT_V1_SIZE + DW_EXT_TAIL_MAX <= 255, "ext_len is one byte");

#define DEF_QUIET_FROM 22
#define DEF_QUIET_TO 7

const uint8_t DW_DEF_CAROUSEL[DW_DEF_CAROUSEL_LEN] = {DW_F_CRYSTAL, DW_F_ROUNDEL, DW_F_CALLBOX, DW_F_ROTOR,
                                                      DW_F_CORAL,   DW_F_CIRCLES, DW_F_LEARN,   DW_F_ROOM,
                                                      DW_F_AMBER,   DW_F_FOB,     DW_F_CONSOLE, DW_F_WALL,
                                                      DW_F_DOORWAY, DW_F_BROADCAST, DW_F_VORTEX};

#define KEY_OF(id, key, NAME, GROUP, thumb) [id] = #key,
const char *const DW_FACE_KEYS[DW_F__COUNT] = {DW_FACE_TABLE(KEY_OF)};
#undef KEY_OF

void dw_prefs_defaults(dw_prefs_t *p)
{
    memset(p, 0, sizeof *p);
    p->mode = DW_MODE_ALWAYS_ON;
    p->chime_on = false;
    p->quiet_from_h = DEF_QUIET_FROM;
    p->quiet_to_h = DEF_QUIET_TO;
    p->offered = DW_OFFERED_DEF;
}

bool dw_prefs_validate(dw_prefs_t *p)
{
    bool ok = true;
    if (p->mode > DW_MODE_FLIP_OPEN) { p->mode = DW_MODE_ALWAYS_ON; ok = false; }
    if (p->quiet_from_h >= 24) { p->quiet_from_h = DEF_QUIET_FROM; ok = false; }
    if (p->quiet_to_h >= 24) { p->quiet_to_h = DEF_QUIET_TO; ok = false; }
    return ok;
}

dw_prefs_load_t dw_prefs_decode(dw_prefs_t *p, const uint8_t *ext, size_t n)
{
    dw_prefs_defaults(p);
    if (n == 0) return DW_PREFS_ABSENT;
    /* the header first: nothing past it is read unless the length adds up. 6 is wave 1's record; 7 is no record any
     * build wrote; 8 and up is this build's, any bytes past 8 a newer build's tail */
    const bool w1 = n == DW_EXT_W1_SIZE;
    if (n < 2 || ext[VER_OFF] != DW_EXT_VERSION || ext[LEN_OFF] != n || (!w1 && n < DW_EXT_V1_SIZE) ||
        (!w1 && n - DW_EXT_V1_SIZE > DW_EXT_TAIL_MAX)) {
        p->read_only = true;   /* never written: encode refuses */
        return DW_PREFS_READ_ONLY;
    }
    p->mode = ext[MODE_OFF];
    p->chime_on = ext[CHIME_OFF] != 0;
    p->quiet_from_h = ext[FROM_OFF];
    p->quiet_to_h = ext[TO_OFF];
    if (w1) {   /* wave 1 offered ids 0-7 (its every face) and had no tail */
        dw_prefs_validate(p);
        return DW_PREFS_LOADED;
    }
    p->offered = (uint16_t)(ext[OFFERED_OFF] | ext[OFFERED_OFF + 1] << 8);
    p->tail_len = (uint8_t)(n - DW_EXT_V1_SIZE);
    if (p->tail_len) memcpy(p->tail, &ext[DW_EXT_V1_SIZE], p->tail_len);
    dw_prefs_validate(p);
    return p->tail_len ? DW_PREFS_TAIL_KEPT : DW_PREFS_LOADED;
}

size_t dw_prefs_encode(const dw_prefs_t *p, uint8_t *buf, size_t cap)
{
    if (p->read_only || p->tail_len > DW_EXT_TAIL_MAX) return 0;
    const size_t n = DW_EXT_V1_SIZE + p->tail_len;
    if (cap < n) return 0;
    dw_prefs_t v = *p;   /* only valid values are ever written */
    dw_prefs_validate(&v);
    buf[VER_OFF] = DW_EXT_VERSION;
    buf[LEN_OFF] = (uint8_t)n;
    buf[MODE_OFF] = v.mode;
    buf[CHIME_OFF] = v.chime_on ? 1 : 0;
    buf[FROM_OFF] = v.quiet_from_h;
    buf[TO_OFF] = v.quiet_to_h;
    buf[OFFERED_OFF] = (uint8_t)(v.offered & 0xFF);
    buf[OFFERED_OFF + 1] = (uint8_t)(v.offered >> 8);
    if (v.tail_len) memcpy(&buf[DW_EXT_V1_SIZE], v.tail, v.tail_len);
    return n;
}

/* WD24: the Classic faces (their home is the Room, #2) */
#define CLASSIC_IDS                                                                                                    \
    ((1u << DW_F_ROUNDEL) | (1u << DW_F_CALLBOX) | (1u << DW_F_ROTOR) | (1u << DW_F_ROOM) | (1u << DW_F_CONSOLE) |      \
     (1u << DW_F_WALL) | (1u << DW_F_BROADCAST))

dw_era_t dw_face_era(uint8_t id)
{
    return id < DW_F__ALL && (CLASSIC_IDS >> id) & 1u ? DW_ERA_CLASSIC : DW_ERA_MODERN;
}

static int find(const uint8_t *carousel, uint8_t len, uint8_t id)
{
    for (uint8_t i = 0; carousel && i < len; i++)
        if (carousel[i] == id) return i;
    return -1;
}

int dw_lift_index(const dw_prefs_t *p, const uint8_t *carousel, uint8_t len, uint8_t cur_face)
{
    if (p->mode != DW_MODE_FLIP_OPEN || carousel == NULL) return -1;
    const bool classic = dw_face_era(cur_face) == DW_ERA_CLASSIC;
    const int home = find(carousel, len, classic ? DW_F_ROOM : DW_F_CRYSTAL);
    return home >= 0 ? home : find(carousel, len, classic ? DW_F_CRYSTAL : DW_F_ROOM);
}

bool dw_prefs_offer(dw_prefs_t *p, uint8_t carousel[DW_CAROUSEL_MAX], uint8_t *len)
{
    return fk_offer_list(&p->offered, carousel, len, DW_DEF_CAROUSEL, DW_DEF_CAROUSEL_LEN);
}

bool dw_prefs_offer_reset(dw_prefs_t *p, const uint8_t *carousel, uint8_t len)
{
    return fk_offer_reset_list(&p->offered, carousel, len, DW_DEF_CAROUSEL, DW_DEF_CAROUSEL_LEN);
}

dw_offer_verdict_t dw_prefs_offer_reset_verdict(dw_prefs_t *p, bool writable, const uint8_t *carousel, uint8_t len)
{
    if (!writable || p->read_only) return DW_OFFER_V_READ_ONLY;   /* nothing could be saved: nothing changes */
    return dw_prefs_offer_reset(p, carousel, len) ? DW_OFFER_V_RESET : DW_OFFER_V_NOTHING;
}

dw_prefs_load_t dw_prefs_load(dw_prefs_t *p, const uint8_t *ext, size_t n, size_t cap)
{
    if (n <= cap) return dw_prefs_decode(p, ext, n);
    dw_prefs_defaults(p);
    p->read_only = true;   /* the bytes were never seen: never written over */
    return DW_PREFS_READ_ONLY;
}

static const uint8_t FIELD_LIMIT[DW_PF__COUNT] = {[DW_PF_MODE] = DW_MODE_FLIP_OPEN + 1, [DW_PF_CHIME] = 2,
                                                 [DW_PF_QUIET_FROM] = 24, [DW_PF_QUIET_TO] = 24};

uint8_t dw_prefs_get(const dw_prefs_t *p, dw_pref_field_t f)
{
    switch (f) {
    case DW_PF_MODE: return p->mode;
    case DW_PF_CHIME: return p->chime_on ? 1 : 0;
    case DW_PF_QUIET_FROM: return p->quiet_from_h;
    case DW_PF_QUIET_TO: return p->quiet_to_h;
    default: return 0;
    }
}

bool dw_prefs_set(dw_prefs_t *p, dw_pref_field_t f, uint8_t v)
{
    if ((unsigned)f >= DW_PF__COUNT || v >= FIELD_LIMIT[f]) return false;
    switch (f) {
    case DW_PF_MODE: p->mode = v; break;
    case DW_PF_CHIME: p->chime_on = v != 0; break;
    case DW_PF_QUIET_FROM: p->quiet_from_h = v; break;
    default: p->quiet_to_h = v; break;
    }
    return true;
}

uint32_t dw_prefs_pack(const dw_prefs_t *p)
{
    uint32_t w = 0;
    for (int f = 0; f < DW_PF__COUNT; f++) w |= (uint32_t)dw_prefs_get(p, (dw_pref_field_t)f) << (8 * f);
    return w;
}

uint8_t dw_prefs_packed(uint32_t w, dw_pref_field_t f)
{
    return (unsigned)f < DW_PF__COUNT ? (uint8_t)(w >> (8 * f)) : 0;
}
