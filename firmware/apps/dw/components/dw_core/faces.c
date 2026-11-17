/* The face registry, the pinned still frames and the frame CRC (dw/faces.h; dw wave 1 Task 11): what dw_shot, the
 * host harness and the device's `dw verify` share, so all three draw the same inputs. Pure, no allocation. */
#include "dw/faces.h"
#include <string.h>

const fk_face_ops_t *const DW_FACES[] = {
    &DW_FACE_CRYSTAL,
    &DW_FACE_ROUNDEL,
    &DW_FACE_CALLBOX,
    &DW_FACE_ROTOR,
    &DW_FACE_CORAL,
    &DW_FACE_CIRCLES,
    &DW_FACE_LEARN,
    &DW_KEY,
    &DW_FACE_ROOM,   /* wave 2 Task 5 */
    &DW_FACE_AMBER,  /* wave 2 Task 6 */
    &DW_FACE_FOB,    /* wave 2 Task 7 */
    &DW_FACE_CONSOLE, /* wave 2 Task 9 */
    &DW_FACE_WALL,    /* wave 2 Task 10 */
    &DW_FACE_DOORWAY, /* wave 2 Task 11 */
    &DW_FACE_BROADCAST, /* wave 2 Task 12 */
    &DW_FACE_VORTEX,    /* wave 2 Task 14 */
};
const unsigned DW_FACE_COUNT = sizeof DW_FACES / sizeof DW_FACES[0];

const fk_face_ops_t *dw_face_by_key(const char *key)
{
    for (unsigned i = 0; key && i < DW_FACE_COUNT; i++)
        if (strcmp(DW_FACES[i]->key, key) == 0) return DW_FACES[i];
    return NULL;
}

/* h, m, s, ms, mday, mon (0-11), wday, set, stale, glance, odx, ody */
static const struct {
    uint8_t h, m, s; uint16_t ms; uint8_t mday, mon, wday; bool set, stale, glance; int8_t odx, ody; const char *what;
} STILLS[DW_STILL_N] = {
    {10, 9, 30, 500, 1, 10, 0, true, false, false, 0, 0, "10:09:30.500 Sun 1 Nov"},
    {3, 47, 12, 900, 15, 6, 3, true, false, false, 0, 0, "03:47:12.900 Wed 15 Jul"},
    {23, 59, 59, 0, 31, 9, 6, true, false, false, 0, 0, "23:59:59.000 Sat 31 Oct"},
    {10, 9, 30, 500, 1, 10, 0, true, false, true, 0, 0, "glance 10:09"},
    {3, 47, 12, 900, 15, 6, 3, true, false, true, 4, -4, "glance 03:47, orbit (+4, -4)"},
    {0, 0, 0, 0, 0, 0, 0, false, false, false, 0, 0, "unset"},
    {10, 9, 30, 500, 1, 10, 0, true, true, false, 0, 0, "stale 10:09:30.500"},
    {0, 0, 0, 0, 0, 0, 0, false, false, true, -4, 4, "unset glance, orbit (-4, +4)"},
};

void dw_still(unsigned i, dw_still_t *out)
{
    memset(out, 0, sizeof *out);
    if (i >= DW_STILL_N) i = 0;
    fk_face_in_t *in = &out->in;
    in->clk.set = STILLS[i].set, in->clk.stale = STILLS[i].stale;
    in->clk.h = STILLS[i].h, in->clk.m = STILLS[i].m, in->clk.s = STILLS[i].s, in->clk.ms = STILLS[i].ms;
    in->clk.mday = STILLS[i].mday, in->clk.mon = STILLS[i].mon, in->clk.wday = STILLS[i].wday;
    in->wake_ms = UINT32_MAX;
    in->odx = STILLS[i].odx, in->ody = STILLS[i].ody;
    in->battery = -1;
    /* wave 2 Task 9: today's sun from 07:00 to 19:00 on a set clock (#9's SOLAR meter; no wave-1 face reads them), none
     * unset */
    in->sun_rise_m = STILLS[i].set ? 420 : -1, in->sun_set_m = STILLS[i].set ? 1140 : -1;
    out->glance = in->glance = STILLS[i].glance;
    out->what = STILLS[i].what;
}

unsigned dw_still_count(const fk_face_ops_t *ops) { return DW_STILL_N * (ops && ops->pages > 1 ? ops->pages : 1u); }

void dw_still_frame(const fk_face_ops_t *ops, const void *ctx, unsigned i, uint16_t *fb)
{
    dw_still_t st;
    dw_still(i % DW_STILL_N, &st);
    st.in.page = (uint8_t)(i / DW_STILL_N);
    fk_still_render(ops, ctx, &st.in, st.glance, fb);
}
