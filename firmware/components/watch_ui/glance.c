#include "glance.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "theme.h"
#include "omni/power.h"
#include "omni/timefmt.h"

#define CX THEME_CX
#define CY THEME_CY
#define G_SIZE 76 /* glanceTime: 76 px, cells 0.58 / 0.28 of the size */

struct glance {
    lv_obj_t *root, *cell[GLANCE_MAX_CELLS], *date, *sub;
};

float glance_place_cells(lv_obj_t *const *cells, const char *s, float cx, int32_t y, float cell, float colon)
{
    float w = 0;
    for (const char *p = s; *p; p++) w += *p == ':' ? colon : cell;
    float x = cx - w / 2;
    int i = 0;
    for (; i < GLANCE_MAX_CELLS && s[i]; i++) {
        char ch[2] = {s[i], 0};
        float cw = s[i] == ':' ? colon : cell;
        theme_set_text(cells[i], ch);
        theme_place(cells[i], THEME_CENTER, lroundf(x + cw / 2), y);
        x += cw;
    }
    for (; i < GLANCE_MAX_CELLS; i++) theme_set_text(cells[i], "");
    return w;
}

glance_t *glance_create(lv_obj_t *parent)
{
    glance_t *g = lv_malloc_zeroed(sizeof *g);
    if (g == NULL) return NULL;
    g->root = theme_box(parent, 0, 0, THEME_W, THEME_W);
    for (int i = 0; i < GLANCE_MAX_CELLS; i++)
        g->cell[i] = theme_label(g->root, &font_plex_sans_500_76, THEME_INK, THEME_OPA(0.30), 0);
    g->date = theme_label(g->root, &font_cormorant_700_15, THEME_BRASS, THEME_OPA(0.38), 2); /* 600 15px FD, 2 */
    g->sub = theme_label(g->root, &font_plex_mono_500_13, THEME_INK_DIM, THEME_OPA(0.38), 0); /* 500 13px FM */
    lv_obj_add_flag(g->root, LV_OBJ_FLAG_HIDDEN);
    return g;
}

void glance_update(glance_t *g, int64_t now, bool h24, const char *sub)
{
    if (g == NULL) return;
    bool set = omni_time_is_set(now);
    char hhmm[6] = "--:--", ap[3] = "", buf[40] = "";
    if (set) {
        omni_fmt_clock(now, h24, hhmm, ap);
        struct tm tm;
        time_t t = (time_t)now;
        localtime_r(&t, &tm);
        snprintf(buf, sizeof buf, "%.3s %d %.3s%s%s", omni_day_name(tm.tm_wday), tm.tm_mday, omni_month_name(tm.tm_mon),
                 ap[0] ? "  \xc2\xb7  " : "", ap);
    }
    int8_t dx = 0, dy = 0;
    omni_orbit_offset(now, &dx, &dy);
    lv_obj_set_pos(g->root, dx, dy);
    glance_place_cells(g->cell, hhmm, CX, CY + 22, G_SIZE * 0.58f, G_SIZE * 0.28f);
    theme_set_text(g->date, buf);
    theme_place(g->date, THEME_CENTER, CX, CY - 52);
    theme_set_text(g->sub, sub ? sub : "");
    theme_place(g->sub, THEME_CENTER, CX, CY + 58);
}

void glance_show(glance_t *g, bool on)
{
    if (g == NULL) return;
    if (on) lv_obj_remove_flag(g->root, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g->root, LV_OBJ_FLAG_HIDDEN);
}

void glance_free(glance_t *g) { lv_free(g); }
