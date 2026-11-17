/* The stock digital face and its glance: design/mockups/faces-practical.js, `id: 'digital'` (frame) and its
 * sleepFrame (glanceTime). Positions, sizes and colours are the mockup's; comments name the mockup values.
 *
 * Awake: weekday + date (Cormorant SC), the time in fixed-width cells (IBM Plex Sans 118 px), AM/PM and the
 * seconds, a brass hairline, three complications (battery, time-sync age, next sun event; IBM Plex Mono
 * 13 px at 64 %) and the home city, inside a brass seconds rail. Unset clock: "--:--", no rail progress.
 * Glance: the shared glanceTime layout (glance.c) with no sub-line. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "shell.h"
#include "glance.h"
#include "theme.h"
#include "store.h"
#include "omni/cities.h"
#include "omni/sun.h"
#include "omni/sync.h"
#include "omni/timefmt.h"

#define CX THEME_CX
#define CY THEME_CY
#define RAIL_R 222            /* seconds rail radius, 2 px */
#define TICK_R0 214           /* 5-second ticks, 214..218 */
#define TICK_R1 218
#define TIME_CELL 68          /* drawCells(..., 68, 30) at 118 px */
#define TIME_COLON 30
#define TIME_BASE (CY + 48)
#define COMP_Y (CY + 104)     /* complications baseline */
#define COMP_OPA THEME_OPA(0.64)
#define MAX_CELLS GLANCE_MAX_CELLS

static struct {
    lv_obj_t *awake;
    glance_t *glance;
    lv_obj_t *rail, *icons;
    lv_obj_t *day, *date, *cell[MAX_CELLS], *ampm, *secs, *home;
    lv_obj_t *batt, *sync, *sun;
    int sec;             /* rail position, -1 = none */
    int batt_pct;        /* icon state, as drawn */
    bool charging, sync_bad, sync_never, icon_sun_ok, icon_sun_rise;
    bool sun_ok, sun_rise; /* next sun event, refreshed each minute */
    int64_t sun_at, sun_checked;
    uint16_t sun_city;
} D;

/* Seconds rail: a faint full ring, 12 ticks, and the elapsed arc from 12 o'clock fading in (the mockup's
 * conic gradient, transparent -> brassHi 0.9) one 6-degree segment per second, with a brassHi head dot. */
static void rail_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    theme_draw_arc(layer, CX, CY, RAIL_R + 1, 2, 0, 360, THEME_BRASS, THEME_OPA(0.18), false);
    for (int i = 0; i < 12; i++) {
        float a = (float)i / 12.0f * 2.0f * (float)M_PI - (float)M_PI / 2.0f;
        theme_draw_line(layer, CX + lroundf(cosf(a) * TICK_R0), CY + lroundf(sinf(a) * TICK_R0), CX + lroundf(cosf(a) * TICK_R1),
                  CY + lroundf(sinf(a) * TICK_R1), 2, THEME_BRASS, THEME_OPA(0.32), false);
    }
    if (D.sec < 0) return;
    for (int i = 0; i < D.sec; i++)
        theme_draw_arc(layer, CX, CY, RAIL_R + 1, 2, 270 + i * 6, 270 + (i + 1) * 6, THEME_BRASS_HI,
                 (lv_opa_t)(0.9f * 255 * (float)(i + 1) / (float)D.sec), false);
    float a = (float)(-M_PI / 2 + D.sec / 60.0 * 2 * M_PI);
    int32_t hx = CX + lroundf(cosf(a) * RAIL_R), hy = CY + lroundf(sinf(a) * RAIL_R);
    theme_draw_box(layer, hx - 3, hy - 3, hx + 3, hy + 3, LV_RADIUS_CIRCLE, THEME_BRASS_HI, LV_OPA_COVER, 0, 0);
}

/* Complication glyphs, drawn as strokes (faces-practical.js battery(), wifi() and the sun-event mark). */
static void icons_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const int32_t y = COMP_Y;
    /* battery(ctx, cx - 140, y - 4): 20x11 rounded outline, 2x4 nub, fill 16*pct x 7 at 85 % */
    const int32_t bx = CX - 140, by = y - 4;
    theme_draw_box(layer, bx, by - 6, bx + 20, by + 5, 3, THEME_INK, LV_OPA_TRANSP, 1, COMP_OPA);
    theme_draw_box(layer, bx + 21, by - 2, bx + 22, by + 1, 0, THEME_INK, COMP_OPA, 0, 0);
    if (D.batt_pct > 0) {
        int32_t w = (16 * D.batt_pct + 50) / 100;
        if (w > 0)
            theme_draw_box(layer, bx + 2, by - 4, bx + 2 + w - 1, by + 2, 0, D.charging ? THEME_BRASS : THEME_INK,
                     (lv_opa_t)(COMP_OPA * 0.85f), 0, 0);
    }
    /* wifi(ctx, cx - 30, y - 9): three arcs r 4.2/8.4/12.6 over -135..-45 deg, dot r 1.4; red when stale */
    const uint32_t wc = D.sync_bad ? THEME_RED : THEME_INK;
    const lv_opa_t wo = D.sync_bad ? LV_OPA_COVER : COMP_OPA;
    const int32_t wx = CX - 30, wy = y - 9 + 4;
    static const int32_t WR[] = {5, 9, 13};
    for (int i = 0; i < 3; i++)
        theme_draw_arc(layer, wx, wy, WR[i], 2, 225, 315, wc, (lv_opa_t)(wo * (D.sync_never && i > 0 ? 0.3f : 0.85f)), true);
    theme_draw_box(layer, wx - 1, wy - 1, wx + 1, wy + 1, LV_RADIUS_CIRCLE, wc, wo, 0, 0);
    /* sun: half disc r 6 on a 16 px horizon, chevron above (down = sunset, up = sunrise) */
    const int32_t sx = CX + 40;
    theme_draw_arc(layer, sx + 7, y - 1, 7, 1, 180, 360, THEME_INK, COMP_OPA, false);
    theme_draw_line(layer, sx - 1, y - 1, sx + 15, y - 1, 1, THEME_INK, COMP_OPA, false);
    if (D.icon_sun_ok) {
        int32_t tip = D.icon_sun_rise ? y - 13 : y - 10, wing = D.icon_sun_rise ? y - 10 : y - 13;
        theme_draw_line(layer, sx + 4, wing, sx + 7, tip, 1, THEME_INK, COMP_OPA, true);
        theme_draw_line(layer, sx + 7, tip, sx + 10, wing, 1, THEME_INK, COMP_OPA, true);
    }
}

/* ------------------------------------------------------------ build */
static void face_create(lv_obj_t *parent)
{
    memset(&D, 0, sizeof D);
    D.sec = -1;
    D.batt_pct = -2;
    D.sun_checked = -1;

    /* awake */
    D.awake = theme_box(parent, 0, 0, THEME_W, THEME_W);
    D.rail = theme_box(D.awake, 0, 0, THEME_W, THEME_W);
    lv_obj_add_event_cb(D.rail, rail_draw, LV_EVENT_DRAW_MAIN, NULL);
    D.day = theme_label(D.awake, &font_cormorant_700_25, THEME_BRASS, LV_OPA_COVER, 2);    /* 600 25px FD, 2.5 */
    D.date = theme_label(D.awake, &font_cormorant_500_17, THEME_INK_DIM, LV_OPA_COVER, 2); /* 500 17px FD, 2 */
    for (int i = 0; i < MAX_CELLS; i++) D.cell[i] = theme_label(D.awake, &font_plex_sans_500_118, THEME_INK, LV_OPA_COVER, 0);
    D.ampm = theme_label(D.awake, &font_plex_sans_600_15, THEME_BRASS, LV_OPA_COVER, 2);   /* 600 15px FS, 1.5 */
    D.secs = theme_label(D.awake, &font_plex_mono_500_15, THEME_INK_FAINT, LV_OPA_COVER, 0);
    /* 1 px brass 0.35 line on y = cy + 76: the canvas spreads it over two rows at half strength */
    lv_obj_t *hair = theme_box(D.awake, CX - 120, CY + 75, 240, 2);
    lv_obj_set_style_bg_color(hair, lv_color_hex(THEME_BRASS), 0);
    lv_obj_set_style_bg_opa(hair, THEME_OPA(0.35 / 2), 0);
    D.icons = theme_box(D.awake, CX - 145, COMP_Y - 18, 290, 26);
    lv_obj_add_flag(D.icons, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(D.icons, icons_draw, LV_EVENT_DRAW_MAIN, NULL);
    D.batt = theme_label(D.awake, &font_plex_mono_500_13, THEME_INK, COMP_OPA, 0);
    D.sync = theme_label(D.awake, &font_plex_mono_500_13, THEME_INK, COMP_OPA, 0);
    D.sun = theme_label(D.awake, &font_plex_mono_500_13, THEME_INK, COMP_OPA, 0);
    D.home = theme_label(D.awake, &font_plex_sans_500_10, THEME_INK_FAINT, LV_OPA_COVER, 2); /* 500 10px FS, 2 */

    D.glance = glance_create(parent);
}

static void face_destroy(void)
{
    glance_free(D.glance);
    memset(&D, 0, sizeof D);
}

/* ------------------------------------------------------------ update */
static void update_sun(int64_t now)
{
    uint16_t city = settings()->city;
    if (D.sun_checked >= 0 && now / 60 == D.sun_checked / 60 && city == D.sun_city && now < D.sun_at) return;
    D.sun_checked = now;
    D.sun_city = city;
    const omni_city_t *c = omni_city_at(city);
    D.sun_ok = c && omni_time_is_set(now) && omni_sun_next_event(now, c->lat, c->lon, &D.sun_at, &D.sun_rise);
    if (!D.sun_ok) D.sun_at = INT64_MAX;
}

static void update_awake(int64_t now, bool set, const struct tm *tm)
{
    const bool h24 = settings()->h24;
    char hhmm[6] = "--:--", ap[3] = "", buf[48];
    if (set) omni_fmt_clock(now, h24, hhmm, ap);

    /* weekday + date */
    theme_set_text(D.day, set ? omni_day_name(tm->tm_wday) : "");
    theme_place(D.day, THEME_CENTER, CX, 128);
    if (set) snprintf(buf, sizeof buf, "%d %s %d", tm->tm_mday, omni_month_name(tm->tm_mon), tm->tm_year + 1900);
    else strcpy(buf, "Time not set");
    theme_set_text(D.date, buf);
    theme_place(D.date, THEME_CENTER, CX, 154);

    /* time: cells centred 10 px left when AM/PM follows; AM/PM up top, seconds on the baseline */
    float cx = (float)CX - (ap[0] ? 10 : 0);
    float w = glance_place_cells(D.cell, hhmm, cx, TIME_BASE, TIME_CELL, TIME_COLON);
    for (int i = 0; i < MAX_CELLS; i++) /* the unset dashes are a placeholder, not a time: dimmer */
        lv_obj_set_style_text_color(D.cell[i], lv_color_hex(set ? THEME_INK : THEME_INK_DIM), 0);
    theme_set_text(D.ampm, ap);
    theme_place(D.ampm, THEME_LEFT, lroundf(cx + w / 2 + 6), CY - 22);
    if (ap[0] && set) snprintf(buf, sizeof buf, "%02d", tm->tm_sec);
    else buf[0] = '\0';
    theme_set_text(D.secs, buf);
    theme_place(D.secs, THEME_LEFT, lroundf(cx + w / 2 + 7), TIME_BASE);

    /* rail */
    int sec = set ? tm->tm_sec : -1;
    if (sec != D.sec) {
        D.sec = sec;
        lv_obj_invalidate(D.rail);
    }

    /* complications */
    const shell_status_t *st = shell_status();
    bool sync_bad = omni_sync_stale(&st->sync, now);
    if (st->battery_pct >= 0) snprintf(buf, sizeof buf, "%d%%", st->battery_pct);
    else strcpy(buf, "\xe2\x80\x94"); /* em dash */
    theme_set_text(D.batt, buf);
    theme_place(D.batt, THEME_LEFT, CX - 113, COMP_Y);

    if (st->sync.last_ok == 0) strcpy(buf, "never");
    else omni_fmt_age(now - st->sync.last_ok, buf);
    theme_set_text(D.sync, buf);
    lv_obj_set_style_text_color(D.sync, lv_color_hex(sync_bad ? THEME_RED : THEME_INK), 0);
    lv_obj_set_style_text_opa(D.sync, sync_bad ? LV_OPA_COVER : COMP_OPA, 0);
    theme_place(D.sync, THEME_LEFT, CX - 16, COMP_Y);

    update_sun(now);
    if (D.sun_ok) omni_fmt_short_time(D.sun_at, h24, buf);
    else strcpy(buf, "\xe2\x80\x94");
    theme_set_text(D.sun, buf);
    theme_place(D.sun, THEME_LEFT, CX + 61, COMP_Y);

    bool never = st->sync.last_ok == 0;
    if (st->battery_pct != D.batt_pct || st->charging != D.charging || sync_bad != D.sync_bad ||
        never != D.sync_never || D.sun_ok != D.icon_sun_ok || D.sun_rise != D.icon_sun_rise) {
        D.batt_pct = st->battery_pct;
        D.charging = st->charging;
        D.sync_bad = sync_bad;
        D.sync_never = never;
        D.icon_sun_ok = D.sun_ok;
        D.icon_sun_rise = D.sun_rise;
        lv_obj_invalidate(D.icons);
    }

    /* home city, very quiet */
    const omni_city_t *c = omni_city_at(settings()->city);
    snprintf(buf, sizeof buf, "%s, %s", c ? c->name : "", c ? c->country : "");
    omni_ascii_upper(buf);
    theme_set_text(D.home, c ? buf : "");
    theme_place(D.home, THEME_CENTER, CX, CY + 142);
}

static void face_update(int64_t now, bool glance)
{
    if (D.awake == NULL) return;
    if (glance) {
        lv_obj_add_flag(D.awake, LV_OBJ_FLAG_HIDDEN);
        glance_show(D.glance, true);
        glance_update(D.glance, now, settings()->h24, NULL);
        return;
    }
    bool set = omni_time_is_set(now);
    struct tm tm = {0};
    time_t t = (time_t)now;
    if (set) localtime_r(&t, &tm);
    glance_show(D.glance, false);
    lv_obj_remove_flag(D.awake, LV_OBJ_FLAG_HIDDEN);
    update_awake(now, set, &tm);
}

const omni_face_vt_t FACE_DIGITAL_VT = {
    .name = "digital",
    .create = face_create,
    .update = face_update,
    .destroy = face_destroy,
};
