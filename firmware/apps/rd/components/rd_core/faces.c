/* The Mining Ship face registry, the default carousel, the lift rule, the pinned still frames, the glow tables and the
 * shared stale marker (rd/faces.h): what rd_shot, the host harness and the device's `rd verify` share, so all three
 * draw the same inputs. Pure, no allocation. */
#include "rd/faces.h"
#include <string.h>
#include "fk/shapes.h"

/* the family order (spec section 6): Signature (Stasis, Bay, Drift), Bridge (Console, Scope, Deck, Terminal, Hull),
 * Quarters (Vending, Curry, Bowl, Bunk, Mop Bot) */
const uint8_t RD_DEF_CAROUSEL[RD_DEF_CAROUSEL_LEN] = {
    RD_F_STASIS, RD_F_BAY,     RD_F_DRIFT, RD_F_CONSOLE, RD_F_SCOPE, RD_F_DECK,  RD_F_TERMINAL,
    RD_F_HULL,   RD_F_VENDING, RD_F_CURRY, RD_F_BOWL,    RD_F_BUNK,  RD_F_MOPBOT};

/* Each face task appends its ops (wave 2: and clears its RD_PENDING bit in rd/faces.h). */
const fk_face_ops_t *const RD_FACES[] = {
    &RD_FACE_STASIS,     /* Task 9 */
    &RD_FACE_TERMINAL,   /* Task 11 */
    &RD_FACE_DECK,       /* Task 12 */
    &RD_FACE_HULL,       /* Task 13 */
    &RD_FACE_BUNK,       /* Task 14 */
    &RD_FACE_CURRY,      /* Task 15: wave 1's six faces built */
    &RD_FACE_BAY,        /* wave 2 Task 3 */
    &RD_FACE_DRIFT,      /* wave 2 Task 5 */
    &RD_FACE_CONSOLE,    /* wave 2 Task 6 */
    &RD_FACE_SCOPE,      /* wave 2 Task 7 */
    &RD_FACE_VENDING,    /* wave 2 Task 8 */
    &RD_FACE_BOWL,       /* wave 2 Task 9 */
    &RD_FACE_MOPBOT,     /* wave 2 Task 10: wave 2's seven faces built */
};

/* every id's key (rd/faces.h's RD_FACE_TABLE, which rd_theme.c's descriptor table expands too) */
#define RD_KEY_OF(id, key, title, group) [id] = key,
const char *const RD_FACE_KEYS[RD_F__COUNT] = {RD_FACE_TABLE(RD_KEY_OF)};
#undef RD_KEY_OF
const unsigned RD_FACE_COUNT = sizeof RD_FACES / sizeof RD_FACES[0];

const fk_face_ops_t *rd_face_by_key(const char *key)
{
    for (unsigned i = 0; key && i < RD_FACE_COUNT; i++)
        if (RD_FACES[i]->key && strcmp(RD_FACES[i]->key, key) == 0) return RD_FACES[i];
    return NULL;
}

int rd_lift_index(const rd_prefs_t *p, const uint8_t *carousel, uint8_t len)
{
    if (p == NULL || p->mode != RD_MODE_STASIS || carousel == NULL) return -1;
    for (uint8_t i = 0; i < len && i < RD_CAROUSEL_MAX; i++)
        if (carousel[i] == RD_F_STASIS) return i;
    return -1;
}

/* ---- the stills (RW12): the year 2026, yday 1..366 (localtime_r's tm_yday + 1) */
static const struct {
    uint8_t h, m, s; uint16_t ms; uint8_t mday, mon, wday; uint16_t yday;
    bool set, stale, glance, h24; int8_t odx, ody; int32_t slept; const char *what;
} STILLS[RD_STILL_N] = {
    {10, 9, 30, 500, 1, 10, 0, 305, true, false, false, true, 0, 0, 12307, "10:09:30.500 Sun 1 Nov, 24 h"},
    {3, 47, 12, 900, 15, 6, 3, 196, true, false, false, true, 0, 0, -1, "03:47:12.900 Wed 15 Jul, 24 h"},
    {23, 59, 59, 0, 31, 9, 6, 304, true, false, false, true, 0, 0, -1, "23:59:59.000 Sat 31 Oct, 24 h"},
    {10, 9, 30, 500, 1, 10, 0, 305, true, false, false, false, 0, 0, -1, "10:09:30.500 Sun 1 Nov, 12 h"},
    {13, 5, 7, 250, 1, 10, 0, 305, true, false, false, false, 0, 0, -1, "13:05:07.250 Sun 1 Nov, 12 h"},
    {10, 9, 30, 500, 1, 10, 0, 305, true, false, true, true, 0, 0, -1, "glance 10:09, 24 h"},
    {3, 47, 12, 900, 15, 6, 3, 196, true, false, true, true, 4, -4, -1, "glance 03:47, orbit (+4, -4)"},
    {0, 0, 0, 0, 0, 0, 0, 0, false, false, false, true, 0, 0, -1, "unset"},
    {10, 9, 30, 500, 1, 10, 0, 305, true, true, false, true, 0, 0, -1, "stale 10:09:30.500"},
    {0, 0, 0, 0, 0, 0, 0, 0, false, false, true, true, -4, 4, -1, "unset glance, orbit (-4, +4)"},
    {13, 5, 0, 0, 1, 10, 0, 305, true, false, true, false, 0, 0, -1, "glance 13:05, 12 h"},
};

void rd_still(unsigned i, rd_still_t *out)
{
    memset(out, 0, sizeof *out);
    if (i >= RD_STILL_N) i = 0;
    fk_face_in_t *in = &out->in;
    in->clk.set = STILLS[i].set, in->clk.stale = STILLS[i].stale;
    in->clk.h = STILLS[i].h, in->clk.m = STILLS[i].m, in->clk.s = STILLS[i].s, in->clk.ms = STILLS[i].ms;
    in->clk.mday = STILLS[i].mday, in->clk.mon = STILLS[i].mon, in->clk.wday = STILLS[i].wday;
    in->clk.yday = STILLS[i].yday;
    in->h24 = STILLS[i].h24;
    in->wake_ms = UINT32_MAX;
    in->odx = STILLS[i].odx, in->ody = STILLS[i].ody;
    in->battery = -1;
    in->sun_rise_m = in->sun_set_m = -1;
    in->slept_s = STILLS[i].slept;
    /* wave 2 Task 6: row 0 "SYNC 12 MIN AGO"; the stale row "SYNC 25 H AGO" (M9); every other row 0 */
    in->sync_age_s = i == 0 ? 754 : i == 8 ? 90000 : 0;
    out->glance = in->glance = STILLS[i].glance;
    out->what = STILLS[i].what;
}

unsigned rd_still_count(const fk_face_ops_t *ops) { return RD_STILL_N + (ops ? ops->stills_extra : 0u); }

/* each face task with a tapped state adds its line: if (ops == &RD_FACE_<KEY>) return RD_<KEY>_TAPPED_VARIANT; */
uint16_t rd_still_variant(const fk_face_ops_t *ops)
{
    if (ops == &RD_FACE_TERMINAL) return RD_TM_TAPPED_VARIANT;
    if (ops == &RD_FACE_DECK) return RD_DK_TAPPED_VARIANT;
    if (ops == &RD_FACE_HULL) return RD_HP_TAPPED_VARIANT;
    if (ops == &RD_FACE_BUNK) return RD_BK_TAPPED_VARIANT;
    if (ops == &RD_FACE_CURRY) return RD_CU_TAPPED_VARIANT;
    if (ops == &RD_FACE_CONSOLE) return RD_DC_TAPPED_VARIANT;
    if (ops == &RD_FACE_SCOPE) return RD_SC_TAPPED_VARIANT;
    if (ops == &RD_FACE_VENDING) return RD_VM_TAPPED_VARIANT;
    return 0;
}

/* wave 2 Task 9: each face whose tapped state shows only while its pulse plays adds its line */
uint16_t rd_still_pulse_ms(const fk_face_ops_t *ops)
{
    if (ops == &RD_FACE_VENDING) return RD_VM_PULSE_STILL_MS;
    if (ops == &RD_FACE_BOWL) return RD_CB_PAW_STILL_MS;
    if (ops == &RD_FACE_MOPBOT) return RD_MB_HOP_STILL_MS;
    return 0;
}

void rd_still_frame(const fk_face_ops_t *ops, const void *ctx, unsigned i, uint16_t *fb)
{
    rd_still_t st;
    rd_still(i < RD_STILL_N ? i : 0, &st);
    if (i >= RD_STILL_N) {
        st.in.variant = rd_still_variant(ops);
        const uint16_t p = rd_still_pulse_ms(ops);
        if (p) st.in.pulsing = true, st.in.pulse_ms = p;
    }
    fk_still_render(ops, ctx, &st.in, st.glance, fb);
}

/* ---- the last sync's age and the reserve as text (wave 2 Task 6; moved here from console.c by Task 7: Drive
 * Console's strip and Nav Scope's page 4 share them) */
void rd_sync_str(int32_t age_s, char out[24])
{
    if (age_s < 0) {
        strcpy(out, "SYNC NEVER");
        return;
    }
    long n;
    const char *unit;
    if (age_s < 6000) n = age_s / 60, unit = " MIN AGO";
    else if (age_s < 360000) n = age_s / 3600, unit = " H AGO";
    else n = age_s / 86400, unit = " D AGO";
    char d[12];
    int k = 0;
    do d[k++] = (char)('0' + n % 10), n /= 10;
    while (n);
    strcpy(out, "SYNC ");
    int o = 5;
    while (k) out[o++] = d[--k];
    out[o] = 0;
    strcat(out, unit);
}

void rd_reserve_str(int8_t battery, char out[16])
{
    if (battery < 0) {
        strcpy(out, "RESERVE --%");
        return;
    }
    const int b = battery > 100 ? 100 : battery;
    strcpy(out, "RESERVE ");
    int o = 8;
    if (b >= 100) out[o++] = '1';
    if (b >= 10) out[o++] = (char)('0' + b / 10 % 10);
    out[o++] = (char)('0' + b % 10);
    out[o++] = '%';
    out[o] = 0;
}

/* ---- the glow tables as const data (lesson 3) */
#include "glow_lut.h"

const uint8_t *rd_glow_lut(int r)
{
    switch (r) {
    case 16: return RD_GLOW_LUT_16;
    case 14: return RD_GLOW_LUT_14;
    case 190: return RD_GLOW_LUT_190;
    case 9: return RD_GLOW_LUT_9;
    case 40: return RD_GLOW_LUT_40;
    case 48: return RD_GLOW_LUT_48;
    case 56: return RD_GLOW_LUT_56;
    case 64: return RD_GLOW_LUT_64;
    case 72: return RD_GLOW_LUT_72;
    case 80: return RD_GLOW_LUT_80;
    case 88: return RD_GLOW_LUT_88;
    case 120: return RD_GLOW_LUT_120;
    case 18: return RD_GLOW_LUT_18;     /* wave 2 Task 3: Bay Mouth's pip glow (soft) */
    case 255: return RD_GLOW_LUT_255;   /* and its chime glow (soft, coarse) */
    case 10: return RD_GLOW_LUT_10;     /* wave 2 Task 5: Drift Plate's newest window (soft) */
    case 180: return RD_GLOW_LUT_180;   /* its chime glow over the time (soft, coarse) */
    case 200: return RD_GLOW_LUT_200;   /* and its flash on the windows (soft, coarse) */
    case 46: return RD_LIN_LUT_46;   /* Task 12: the linear glows (RD_LIN_LUT_RADII) */
    case 8: return RD_LIN_LUT_8;     /* Task 13 */
    case 17: return RD_LIN_LUT_17;   /* wave 2 Task 6: Drive Console's lamps (linear) */
    case 26: return RD_QTR_LUT_26;   /* Task 14: the quarters glows (RD_QTR_LUT_RADII) */
    case 24: return RD_GLOW_LUT_24;  /* wave 2 Task 4: the airlock's dust (soft) */
    case 32: return RD_GLOW_LUT_32;
    default: return NULL;
    }
}

const uint8_t *rd_soft_lut(int r)
{
    static const int SR[] = RD_SOFT_LUT_RADII;
    /* wave 2 Task 4: the airlock's dust and lamp, beside the other lists' 16 and 26 (another profile each) */
    if (r == 16) return RD_SOFT_LUT_16;
    if (r == 26) return RD_SOFT_LUT_26;
    for (unsigned i = 0; i < sizeof SR / sizeof SR[0]; i++)
        if (SR[i] == r) return rd_glow_lut(r);
    return NULL;
}

/* ---- the shared stale marker (faces-reveal.js staleMark(): the lamp ring at (x - 30, y - 4.5), lab('STALE', x - 20,
 * y, 13, col, 'left'), whose canvas letterSpacing is 0.14 em) */
void rd_stale_mark(uint16_t *fb, int stride, const omni_rect_t *clip, const fk_atlas_t *ch13, int cx, int cy,
                   bool glance)
{
    const uint16_t col = glance ? RD_G_AMBER : RD_C_AMBER;
    const omni_rect_t c = clip ? *clip : (omni_rect_t){0, 0, 466, 466};
    fk_ring(fb, stride, &c, (float)cx - 30.f, (float)cy - 4.5f, 4.f, 1.6f, col, 255);
    const float track = 13.f * 0.14f, w = fk_atlas_width_tracked(ch13, "STALE", track);
    fk_atlas_text_tracked(fb, stride, c, ch13, "STALE", (float)cx - 20.f + w / 2.f, cy, track, true, col, 255);
}
