/* The countdown glance: design/mockups/faces-practical.js, the timer's sleepFrame (lines 1287-1297). Colours only (R28).
 *
 * The objects live in the shell's glance root while the override holds and are deleted when it ends, so nothing stays
 * allocated between countdowns. The arc and the number change once a minute (the arc shows the minutes the number
 * shows, ceil), and the orbit offset at most once a minute: the glance renders nothing between. */
#include "timer_glance.h"
#include <stdio.h>
#include "glance.h"
#include "theme.h"
#include "omni_fonts.h"
#include "omni/power.h"
#include "timer_svc.h"

#define CX THEME_CX
#define CY THEME_CY
#define ARC_R 205          /* the arc's centre line */
#define ARC_W 2            /* the mockup's 1.5 px at brass 0.40: 2 px at 0.30, the same ink (whole px only; ruled) */

static struct {
    lv_obj_t *root, *box, *arc, *cell[GLANCE_MAX_CELLS], *label;
    int mins;              /* as shown, -1 none */
    int8_t dx, dy;
} G;

lv_obj_t *timer_arc_create(lv_obj_t *parent, int32_t r, int32_t w, uint32_t rgb, lv_opa_t opa, bool rounded,
                           int32_t range)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);    /* nothing but the indicator is drawn */
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(a, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(rgb), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(a, opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, rounded, LV_PART_INDICATOR);
    const int32_t outer = r + (w + 1) / 2;   /* the stroke lies inside the object's box */
    lv_obj_set_size(a, 2 * outer, 2 * outer);
    lv_obj_set_pos(a, CX - outer, CY - outer);
    lv_arc_set_rotation(a, 270);   /* from 12 o'clock, clockwise */
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_range(a, 0, range);
    lv_arc_set_value(a, 0);
    return a;
}

static void build(lv_obj_t *root)
{
    G.root = root;
    G.mins = -1;
    G.dx = G.dy = 0;
    G.box = theme_box(root, 0, 0, THEME_W, THEME_W);
    G.arc = timer_arc_create(G.box, ARC_R, ARC_W, THEME_BRASS, THEME_OPA(0.30), false, 60);
    for (int i = 0; i < GLANCE_MAX_CELLS; i++)
        G.cell[i] = theme_label(G.box, &font_plex_sans_500_76, THEME_INK, THEME_OPA(0.35), 0);   /* mockup 72 px (D11) */
    G.label = theme_label(G.box, &font_cormorant_700_13, THEME_BRASS, THEME_OPA(0.38), 2);       /* 600 13px FD, 2 */
    theme_set_text(G.label, "MIN LEFT");
    theme_place(G.label, THEME_CENTER, CX, CY + 50);
}

bool timer_glance_override(lv_obj_t *root, int64_t now, bool h24)
{
    (void)h24;
    const omni_tmr_t *t = tmr_svc();
    if (t->mode != OMNI_TMR_COUNT && t->mode != OMNI_TMR_PAUSED) {
        if (G.root) {
            lv_obj_delete(G.box);
            G.root = NULL;
        }
        return false;
    }
    if (G.root != root) build(root);
    const uint32_t rem = omni_tmr_remaining_ms(t, tmr_svc_now_ms());
    const int mins = (int)((rem + 59999u) / 60000u);
    if (mins != G.mins) {
        G.mins = mins;
        char buf[12];
        snprintf(buf, sizeof buf, "%d", mins);   /* 0..60 */
        glance_place_cells(G.cell, buf, CX, CY + 24, 42, 18);   /* drawCells(..., 42, 18) */
        lv_arc_set_value(G.arc, mins);
    }
    int8_t dx = 0, dy = 0;
    omni_orbit_offset(now, &dx, &dy);
    if (dx != G.dx || dy != G.dy) {   /* the box starts at (0, 0) */
        G.dx = dx;
        G.dy = dy;
        lv_obj_set_pos(G.box, dx, dy);
    }
    return true;
}
