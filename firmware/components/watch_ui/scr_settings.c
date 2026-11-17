/* SCR_SETTINGS: design/mockups/faces-setup.js `settings` (frame(), pointer(), setRimAt()).
 *
 * The centre picks the row, the rim sets its value: drag the centre up/down (or tap a row above/below) and the
 * drum rolls until a row sits in the brass window; the rim (r >= 168) is then that row's dial - the 270-degree
 * scale for levels, engraved detents for choices (the red index marks the current one), and for the home city a
 * jog wheel through the city table (alphabetical, the neighbours along the top arc). Tapping the windowed row
 * steps a choice or opens the row's link (omni_rows_link). Every change applies at once: the screen posts UI_SET_* and
 * the event loop edits and commits the settings (R27); brightness reaches the panel the same way (R26).
 *
 * Rows (spec 8.1): home city (name, country, its local time; a tap on the windowed row opens Find city, the keyboard in
 * city mode), brightness, sleep after, time format, sound, volume, reveal (the face's wake from sleep, labelled by the
 * theme's reveal_names: the Omni's lid flip, brass iris or Instant, Stage 3 Task 11; hidden when the theme has one
 * reveal or none), reduced motion, the theme's rows, Faces ("8 of 8 >": the carousel editor), Wi-Fi (the sheet: Choose
 * network, Use your phone, Forget; Forget asks first and acts only on a 1.5 s hold, C2), Theme (the firmware's theme
 * name: information only; Stage 4b, owner Q2) and About ("v<version> . 82 %": the read-only About sheet, spec 8.4). The
 * mockup's "Default face" and "Time zone" rows are left out (spec 8.1). Sheets are states of this screen, not pushes
 * (spec 8.5); every push (the editor, the list, the keyboard, provisioning) records the windowed row first.
 *
 * The theme's own rows (watch_theme.h settings, dw wave 1 Task 10, B14) follow Reduced motion in the same style: a
 * choice row, or an hour dial over 0..23 on the level scale, shown "HH:00". They post UI_THEME_SET, and the event
 * loop calls the row's apply. The rows live in storage filled at create (s_rows, in PSRAM), ids R__COUNT + the theme's
 * row. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "shell.h"
#include "theme.h"
#include "store.h"
#include "board.h"
#include "net.h"
#include "omni/cities.h"
#include "omni/settings_rows.h"
#include "omni/timefmt.h"
#include "watch_theme.h"
#include "drum.h"
#include "debug_console.h"
#include "scr_keyboard.h"
#include "assets.h"
#include "omni/join.h"
#include "omni/kb.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define CX THEME_CX
#define CY THEME_CY
#define TAU (2 * (float)M_PI)
#define RIM_R 168           /* touches from here out are the rim */
#define WIN_X DRUM_WIN_X    /* the window (drum.h) */
#define WIN_Y DRUM_WIN_Y
#define WIN_W DRUM_WIN_W
#define WIN_H DRUM_WIN_H
#define ENUM_SPAN 2.0f      /* radians of top arc used by option labels */
#define JOG_STEP 0.5f       /* rim radians per city */
#define OPTS_MARGIN 0.3f    /* choice rows take rim input only on the labelled top arc, +-(ENUM_SPAN / 2 + this) */
#define LEVEL_MARGIN 0.15f  /* level rows: on the 270-degree dial (+ this), not the bottom gap */
#define ANIM_MS 16
#define FLASH_MS 220
#define DIM_OPA LV_OPA_40
#define PILL_H 46
#define FORGET_WANT (OMNI_GW_DOWN | OMNI_GW_HOLD | OMNI_GW_LIFT_STILL)   /* only while the Forget sheet is up (C2) */

/* The row ids, order and kinds are the core's (omni/settings_rows.h, host-tested); a theme row k is R__COUNT + k. */
enum row_id { R_CITY = OMNI_ROW_CITY, R_BRIGHT = OMNI_ROW_BRIGHT, R_SLEEP = OMNI_ROW_SLEEP, R_CLOCK = OMNI_ROW_CLOCK,
              R_SOUND = OMNI_ROW_SOUND, R_VOLUME = OMNI_ROW_VOLUME, R_REVEAL = OMNI_ROW_REVEAL, R_MOTION = OMNI_ROW_MOTION,
              R_FACES = OMNI_ROW_FACES, R_WIFI = OMNI_ROW_WIFI, R_THEME = OMNI_ROW_THEME, R_ABOUT = OMNI_ROW_ABOUT,
              R__COUNT = OMNI_ROW__COUNT };
enum row_kind { K_JOG = OMNI_ROWK_JOG, K_LEVEL = OMNI_ROWK_LEVEL, K_OPTS = OMNI_ROWK_OPTS, K_LINK = OMNI_ROWK_LINK,
                K_INFO = OMNI_ROWK_INFO };

typedef struct {              /* 24 B: twelve rows in rodata, and their copy in PSRAM */
    const char *label;
    const char *const *opts, *const *shorts;
    const int *values;        /* setting value of each option */
    uint8_t kind;             /* enum row_kind: omni_rows_kind (a theme's hour dial: K_LEVEL), set at create */
    uint8_t n;                /* options */
    uint8_t min, max;         /* levels (0..100) */
    uint8_t ui;               /* UI_SET_*; a theme row: its index in settings[] (posted as UI_THEME_SET) */
    bool hours;               /* a theme's hour dial: steps of 1, shown "HH:00" */
} row_t;

static const char *const SLEEP_O[] = {"5 s", "10 s", "15 s", "30 s", "1 min"}, *const SLEEP_S[] = {"5S", "10S", "15S", "30S", "1M"};
static const int SLEEP_V[] = {5, 10, 15, 30, 60};
static const char *const CLOCK_O[] = {"12 h", "24 h"}, *const CLOCK_S[] = {"12H", "24H"};
static const int BOOL_V[] = {0, 1}, BOOL_INV_V[] = {1, 0};
static const char *const ONOFF_O[] = {"On", "Off"}, *const ONOFF_S[] = {"ON", "OFF"};
static const char *const OFFON_O[] = {"Off", "On"}, *const OFFON_S[] = {"OFF", "ON"};
/* The reveal choices in their rim order (iris, flip, none: flip at the top) with their rim shorts; the window labels are
 * the theme's reveal_names, and only the ids below its reveal_count are offered (reveal_row). */
static const char *const REVEAL_S[] = {"IRIS", "FLIP", "NONE"};
static const int REVEAL_V[] = {OMNI_REVEAL_IRIS, OMNI_REVEAL_FLIP, OMNI_REVEAL_NONE};
#define REVEAL_MAX 3

static const row_t ROWS[R__COUNT] = {
    [R_CITY] = {"Home city", .ui = UI_SET_CITY},
    [R_BRIGHT] = {"Brightness", .min = 5, .max = 100, .ui = UI_SET_BRIGHTNESS},
    [R_SLEEP] = {"Sleep after", .n = 5, .opts = SLEEP_O, .shorts = SLEEP_S, .values = SLEEP_V, .ui = UI_SET_SLEEP},
    [R_CLOCK] = {"Time format", .n = 2, .opts = CLOCK_O, .shorts = CLOCK_S, .values = BOOL_V, .ui = UI_SET_H24},
    [R_SOUND] = {"Sound", .n = 2, .opts = ONOFF_O, .shorts = ONOFF_S, .values = BOOL_INV_V, .ui = UI_SET_SOUND},
    [R_VOLUME] = {"Volume", .min = 0, .max = 100, .ui = UI_SET_VOLUME},
    [R_REVEAL] = {"Reveal", .ui = UI_SET_REVEAL},   /* its options come from the theme: reveal_row() */
    [R_MOTION] = {"Reduced motion", .n = 2, .opts = OFFON_O, .shorts = OFFON_S, .values = BOOL_V, .ui = UI_SET_MOTION},
    [R_FACES] = {"Faces"},
    [R_WIFI] = {"Wi-Fi"},
    [R_THEME] = {"Theme"},
    [R_ABOUT] = {"About"},
};

/* The rows as this screen shows them, filled at create: ROWS, the theme's Reveal row, then the theme's own rows. In
 * PSRAM (dw final review, M3: 720 B kept off internal RAM), allocated at the first create and kept; the CPU alone reads
 * it (no DMA, and LVGL keeps no pointer into it). */
#define ROW_MAX (R__COUNT + WATCH_SETTINGS_MAX)
_Static_assert(ROW_MAX <= 127, "S.vis holds row ids in int8_t");
/* row_t's uint8_t fields: ui holds a UI_SET_* code or a theme row's index; min, max <= 100; n <= the theme's uint8_t n */
_Static_assert(UI_SET_CITY >= 0 && UI_SET_BRIGHTNESS <= UINT8_MAX && UI_SET_SLEEP <= UINT8_MAX && UI_SET_H24 <= UINT8_MAX &&
               UI_SET_SOUND <= UINT8_MAX && UI_SET_VOLUME <= UINT8_MAX && UI_SET_REVEAL <= UINT8_MAX &&
               UI_SET_MOTION <= UINT8_MAX, "row_t.ui holds the UI_SET_* codes in uint8_t");
_Static_assert(WATCH_SETTINGS_MAX <= UINT8_MAX, "row_t.ui holds a theme row's index in uint8_t");
static row_t *s_rows;

/* The row in the window when another screen covered this one (spec 8.5): set before every push, creation opens on it,
 * so popping back lands on the row that pushed it (back from the list or provisioning: Wi-Fi). A close (Settings no
 * longer in the nav stack) resets it, and so does the face coming back on top (scr_settings_home: a stack collapsed
 * under another screen never re-creates Settings, Task 9 park). */
static int8_t s_restore_row = R_CITY;

/* The Reveal row as this theme has it: the ids < reveal_count in REVEAL_V's rim order, labelled by reveal_names. */
static const char *s_reveal_o[REVEAL_MAX], *s_reveal_s[REVEAL_MAX];
static int s_reveal_v[REVEAL_MAX];

static void reveal_row(void)
{
    const watch_theme_t *t = watch_theme();
    const int count = t ? t->limits.reveal_count : 0;
    s_rows[R_REVEAL] = ROWS[R_REVEAL];
    s_rows[R_REVEAL].n = 0;
    for (int i = 0; i < REVEAL_MAX; i++) {
        if (REVEAL_V[i] >= count) continue;
        s_reveal_o[s_rows[R_REVEAL].n] = t->reveal_names ? t->reveal_names[REVEAL_V[i]] : REVEAL_S[i];
        s_reveal_s[s_rows[R_REVEAL].n] = t->reveal_shorts ? t->reveal_shorts[REVEAL_V[i]] : REVEAL_S[i];
        s_reveal_v[s_rows[R_REVEAL].n] = REVEAL_V[i];
        s_rows[R_REVEAL].n++;
    }
    s_rows[R_REVEAL].opts = s_reveal_o;
    s_rows[R_REVEAL].shorts = s_reveal_s;
    s_rows[R_REVEAL].values = s_reveal_v;
}

/* The theme's rows (watch_theme.h), as omni_theme_rows judges them: count clamped to WATCH_SETTINGS_MAX (NULL
 * settings: none); a row without get, a choice row with NULL opts or shorts, or a row with neither options nor an hour
 * dial, is left out. Returns how many were filled, at s_rows[R__COUNT]. */
static int theme_rows(void)
{
    const watch_theme_t *t = watch_theme();
    const int count = t && t->settings ? t->settings_count : 0;
    omni_theme_row_t shape[WATCH_SETTINGS_MAX];
    uint8_t keep[WATCH_SETTINGS_MAX];
    for (int k = 0; k < count && k < WATCH_SETTINGS_MAX; k++) {
        const watch_setting_t *w = &t->settings[k];
        shape[k] = (omni_theme_row_t){.get = w->get != NULL, .opts = w->opts != NULL, .shorts = w->shorts != NULL,
                                      .hours = w->hours, .n = w->n};
    }
    const int m = omni_theme_rows(shape, count, WATCH_SETTINGS_MAX, keep);   /* the filter and the clamp (host-tested) */
    for (int i = 0; i < m; i++) {
        const watch_setting_t *w = &t->settings[keep[i]];
        s_rows[R__COUNT + i] = (row_t){.label = w->label, .kind = w->n ? K_OPTS : K_LEVEL, .n = w->n, .opts = w->opts,
                                       .shorts = w->shorts, .min = 0, .max = 23, .ui = keep[i], .hours = w->n == 0};
    }
    return m;
}

static const row_t *row_of(int id) { return &s_rows[id]; }

enum sheet { SHEET_NONE, SHEET_WIFI, SHEET_FORGET, SHEET_ABOUT };

static struct {
    int8_t vis[ROW_MAX];      /* the rows shown, in drum order (Reveal hidden when the theme has <= 1); int8_t (M3) */
    int nvis;
    int nthemed;              /* the theme's rows, ids R__COUNT .. + nthemed */
    lv_obj_t *root, *draw, *frame;
    lv_timer_t *anim;
    int value[ROW_MAX];       /* what this screen last set (city index, %, option index, hour) */
    drum_t d;                 /* the rows S.vis on the drum */
    uint32_t now;             /* draw_cb's tick, for its row callback */
    bool rim;                 /* a rim drag */
    float jog, last_a;        /* city jog: rim radians turned (clockwise +) since the drag began */
    int jog_base;
    uint32_t flash_t;
    enum sheet sheet;
    omni_forget_hold_t hold;  /* the Forget sheet's hold (C2): the only path to UI_WIFI_FORGET */
    uint32_t hold_t, sheet_t; /* when it armed (the ring's fill); the sheet's last gesture (its timeout) */
    int cur_shown;
    uint32_t t_prev;
    /* The painters' text, kept off the LVGL task's stack (spec 11). Painters never nest, so the window row's, the
     * city dial's and the sheet's buffers share. */
    char ssid[33];
    union {
        struct { char val[48], country[32]; } row;
        char name[12];
        char buf[64];
    } txt;
} S;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float wrapf(float a)
{
    while (a <= -(float)M_PI) a += TAU;
    while (a > (float)M_PI) a -= TAU;
    return a;
}
static int cur_row(void) { return drum_cur(&S.d); }                           /* a drum position */
static int cur_id(void) { return S.vis[cur_row()]; }                              /* the row in the window */
static float opt_angle(int i, int n) { return -(float)M_PI / 2 + (n == 1 ? 0 : ((float)i / (n - 1) - 0.5f) * ENUM_SPAN); }
static int city_wrap(int i)
{
    int n = (int)omni_city_count();
    return ((i % n) + n) % n;
}
static void upper(char *dst, const char *src, size_t n, size_t max_chars)
{
    size_t i = 0;
    for (; src[i] && i + 1 < n && i < max_chars; i++) dst[i] = (char)(src[i] >= 'a' && src[i] <= 'z' ? src[i] - 32 : src[i]);
    dst[i] = 0;
}

static void load_values(void)
{
    const omni_settings_t *st = settings();
    S.value[R_CITY] = st->city;
    S.value[R_BRIGHT] = st->brightness;
    S.value[R_VOLUME] = st->volume;
    int best = 0;
    for (int i = 1; i < 5; i++)
        if (abs(SLEEP_V[i] - st->sleep_s) < abs(SLEEP_V[best] - st->sleep_s)) best = i;
    S.value[R_SLEEP] = best;
    S.value[R_CLOCK] = st->h24 ? 1 : 0;
    S.value[R_SOUND] = st->sound_on ? 0 : 1;
    S.value[R_MOTION] = st->reduced_motion ? 1 : 0;
    S.value[R_REVEAL] = 0;
    for (int i = 0; i < s_rows[R_REVEAL].n; i++)
        if (s_reveal_v[i] == st->reveal_style) S.value[R_REVEAL] = i;
    for (int i = R__COUNT; i < R__COUNT + S.nthemed; i++) {
        const row_t *r = row_of(i);
        int v = watch_theme()->settings[r->ui].get();
        S.value[i] = v < (r->hours ? 24 : r->n) ? v : 0;
    }
}

/* Repaint only what changed (a full-screen frame costs ~100 ms here): */
static void inval_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_area_t a = {x1, y1, x2, y2};
    lv_obj_invalidate_area(S.draw, &a);
}
static void inval_window(void) { inval_rect(WIN_X, WIN_Y - 6, WIN_X + WIN_W, WIN_Y + WIN_H + 6); }
static void inval_drum(void) { inval_rect(CX - 165, 96, CX + 165, 392); } /* the rows, the window, the pips */
static void inval_top_arc(void) { inval_rect(30, 0, THEME_W - 30, 192); } /* the city jog's labels */

/* A value changed here: show it at once (flash), and have the event loop apply it. */
static void set_value(int row, int v)
{
    if (S.value[row] == v) return;
    const row_t *r = row_of(row);
    if (r->kind == K_LEVEL)
        theme_invalidate_level(S.draw, (float)(S.value[row] - r->min) / (r->max - r->min), (float)(v - r->min) / (r->max - r->min));
    else if (r->kind == K_JOG) inval_top_arc();
    else lv_obj_invalidate(S.draw); /* option labels round the rim */
    inval_window();
    S.value[row] = v;
    S.flash_t = lv_tick_get();
    if (row >= R__COUNT) shell_post(UI_THEME_SET, omni_theme_set_pack((uint8_t)r->ui, (uint8_t)v));   /* the theme's row: its index, the value */
    else shell_post(r->ui, r->kind == K_OPTS ? r->values[v] : v);
}

/* esp_app_desc's version without a leading 'v' (the screen adds its own) */
static const char *app_version(void)
{
    const char *v = esp_app_get_description()->version;
    return v[0] == 'v' ? v + 1 : v;
}

static void row_value(int row, char *out, size_t n)
{
    const row_t *r = row_of(row);
    switch (r->kind) {
    case K_LEVEL: snprintf(out, n, r->hours ? "%02d:00" : "%d %%", S.value[row]); break;
    case K_OPTS: snprintf(out, n, "%s", r->opts[S.value[row]]); break;
    case K_LINK:
        if (row == R_FACES) {
            snprintf(out, n, "%u of %u \xe2\x80\xba", settings()->carousel_len, watch_theme()->face_count);
        } else if (row == R_ABOUT) {
            const int pct = shell_status()->battery_pct;
            if (pct >= 0) snprintf(out, n, "v%.12s \xc2\xb7 %d %%", app_version(), pct);
            else snprintf(out, n, "v%.12s", app_version());
        } else {   /* Wi-Fi */
            net_ssid(S.ssid);
            if (net_is_provisioned() && S.ssid[0]) snprintf(out, n, "%.20s \xe2\x80\xba", S.ssid);
            else snprintf(out, n, "Set up \xe2\x80\xba");
        }
        break;
    case K_JOG: {
        const omni_city_t *c = omni_city_at((size_t)S.value[row]);
        snprintf(out, n, "%s", c ? c->name : "");
        break;
    }
    case K_INFO: snprintf(out, n, "%s", watch_theme() ? watch_theme()->name : ""); break;
    }
}

/* Where rim input may start, so a finger resting on the edge of a worn watch does not change a setting: a
 * choice row only on the top arc that carries its labels, a level row only on its dial (not the bottom gap,
 * where the wrist and strap press), the city jog anywhere (it needs a deliberate turn of 0.25 rad, ~50 px
 * along the rim, beyond the 12 px drag threshold, before the city changes, and taps do nothing). A drag that
 * starts inside keeps following the finger wherever it goes. */
static bool rim_accepts(int16_t x, int16_t y)
{
    float a = atan2f(y - CY, x - CX);
    switch (row_of(cur_id())->kind) {
    case K_OPTS: return fabsf(wrapf(a + (float)M_PI / 2)) <= ENUM_SPAN / 2 + OPTS_MARGIN;
    case K_LEVEL: return theme_level_hit(a, LEVEL_MARGIN);
    case K_JOG: return true;
    default: return false;
    }
}

/* setRimAt(): a touch on the rim at (x, y) sets the windowed row's value. */
static void rim_at(int16_t x, int16_t y)
{
    int row = cur_id();
    const row_t *r = row_of(row);
    float a = atan2f(y - CY, x - CX);
    if (r->kind == K_LEVEL) {
        float v = r->min + (r->max - r->min) * theme_level_at(a);
        const float step = r->hours ? 1 : 5;
        set_value(row, (int)clampf(roundf(v / step) * step, (float)r->min, (float)r->max));
    } else if (r->kind == K_OPTS) {
        int best = 0;
        float bd = 1e9f;
        for (int i = 0; i < r->n; i++) {
            float d = fabsf(wrapf(a - opt_angle(i, r->n)));
            if (d < bd) bd = d, best = i;
        }
        set_value(row, best);
    }
}

/* ------------------------------------------------------------ drawing
 * Flat (spec 11): draw_cb dispatches to noinline painters (a dial, the window row, a dimmed row, the sheet), none of
 * which calls another, so their frames never stack on each other; their text is in S. */
#define PAINTER static __attribute__((noinline)) void
static void line_at(lv_layer_t *layer, float a, float r0, float r1, int32_t w, uint32_t rgb, lv_opa_t opa)
{
    theme_draw_line(layer, (int32_t)lroundf(CX + cosf(a) * r0), (int32_t)lroundf(CY + sinf(a) * r0),
                    (int32_t)lroundf(CX + cosf(a) * r1), (int32_t)lroundf(CY + sinf(a) * r1), w, rgb, opa, false);
}

PAINTER level_dial(lv_layer_t *layer, int row)
{
    const row_t *r = row_of(row);
    theme_draw_level(layer, (float)(S.value[row] - r->min) / (float)(r->max - r->min),
                     row == R_BRIGHT ? THEME_LEVEL_SUN : THEME_LEVEL_SOUND);
}

PAINTER opts_dial(lv_layer_t *layer, const row_t *r, int cur)
{
    for (int i = 0; i < r->n; i++) {
        float a = opt_angle(i, r->n);
        bool on = i == cur;
        line_at(layer, a, 172, 180, on ? 2 : 1, on ? THEME_SETUP_BRASS_HI : THEME_BRASS, on ? LV_OPA_COVER : THEME_OPA(0.4));
        theme_draw_arc_text(layer, r->shorts[i], on ? &font_plex_mono_500_13 : &font_plex_mono_400_11, 196, a,
                            on ? THEME_SETUP_BRASS_HI : THEME_INK, on ? LV_OPA_COVER : THEME_OPA(0.55), 1.2f);
        if (on) theme_draw_red_index(layer, a);
    }
}

PAINTER city_dial(lv_layer_t *layer)
{
    float pos = S.rim ? S.jog / JOG_STEP : 0; /* labels follow the finger between detents */
    int base = S.rim ? S.jog_base : S.value[R_CITY], j0 = -(int)lroundf(pos);
    for (int j = j0 - 3; j <= j0 + 3; j++) {
        float a = -(float)M_PI / 2 + (j + pos) * JOG_STEP, k = clampf(2.6f - fabsf(j + pos), 0, 1);
        if (k <= 0) continue;
        bool on = j == j0;
        const omni_city_t *c = omni_city_at((size_t)city_wrap(base + j));
        upper(S.txt.name, c ? c->name : "", sizeof S.txt.name, on ? 9 : 8);
        line_at(layer, a, 172, 180, on ? 2 : 1, on ? THEME_SETUP_BRASS_HI : THEME_BRASS,
                (lv_opa_t)((on ? 255 : 0.4f * 255) * k));
        theme_draw_arc_text(layer, S.txt.name, on ? &font_plex_mono_500_13 : &font_plex_mono_400_11, 196, a,
                            on ? THEME_SETUP_BRASS_HI : THEME_INK, (lv_opa_t)((on ? 255 : 0.55f * 255) * k), 1.2f);
    }
    theme_draw_red_index(layer, -(float)M_PI / 2);
}

PAINTER window_row(lv_layer_t *layer, int i, int32_t y, lv_opa_t opa, bool flash)
{
    char *const val = S.txt.row.val, *const country = S.txt.row.country;
    uint32_t vc = flash ? 0xfff3d6 : THEME_SETUP_BRASS_HI;
    if (row_of(i)->kind != K_JOG) {
        theme_draw_text(layer, row_of(i)->label, &font_plex_sans_500_17, THEME_INK, opa, 0, THEME_LEFT, WIN_X + 20, y + 6);
        row_value(i, val, sizeof S.txt.row.val);
        theme_draw_text(layer, val, &font_plex_sans_500_17, vc, opa, 0, THEME_RIGHT, WIN_X + WIN_W - 18, y + 6);
        return;
    }
    /* the home city: name over its country on the left, its local time on the right */
    const omni_city_t *c = omni_city_at((size_t)S.value[R_CITY]);
    if (c == NULL) return;
    upper(country, omni_country_name(c->country), sizeof S.txt.row.country, sizeof S.txt.row.country);
    theme_draw_text(layer, c->name, &font_plex_sans_500_17, THEME_INK, opa, 0, THEME_LEFT, WIN_X + 20, y - 1);
    theme_draw_text(layer, country, &font_plex_sans_600_10, THEME_BRASS, opa, 2, THEME_LEFT, WIN_X + 20, y + 15);
    /* the time is the city's only once the event loop has applied it (TZ is global) */
    int64_t now = (int64_t)time(NULL);
    if (settings()->city == S.value[R_CITY] && omni_time_is_set(now)) {
        char hhmm[6], ap[3];
        omni_fmt_clock(now, settings()->h24, hhmm, ap);
        snprintf(val, sizeof S.txt.row.val, "%s%s%s", hhmm, ap[0] ? " " : "", ap);
        theme_draw_text(layer, val, &font_plex_sans_500_17, vc, opa, 0, THEME_RIGHT, WIN_X + WIN_W - 18, y + 6);
    }
}

/* The sheets (Settings' grammar, theme_draw_sheet): Wi-Fi, its Forget confirm, About. */
#define SHEET_R_X 239       /* theme_draw_sheet's right key */
#define CONFIRM_Y 240       /* the Forget sheet's keys */
#define CHOOSE_X 133        /* the Wi-Fi sheet's "Choose network": the primary key, wide enough for its Cormorant 20 */
#define CHOOSE_Y 206
#define CHOOSE_W 200
#define PAIR_Y 262          /* its "Use your phone" and Forget */

static bool in_key(const omni_gesture_t *g, int32_t x, int32_t y, int32_t w)   /* inclusive at the edges */
{
    return g->x >= x && g->x <= x + w && g->y >= y && g->y <= y + PILL_H;
}
/* Forget can act: a writable boot, no phone session, net idle (I4, as the event loop's guard; net checks again) */
static bool forget_ok(void) { return omni_join_forget_allowed(store_read_only(), net_provisioning(), net_busy()); }

/* The Wi-Fi sheet's keys, for its painter and its taps (no text formatting: the taps run on the event loop's stack):
 * "Use your phone" and Forget (when set up); none on a read-only boot. Returns whether it is set up. */
static bool wifi_keys(theme_sheet_t *s)
{
    net_ssid(S.ssid);
    const bool prov = net_is_provisioned() && S.ssid[0];
    *s = (theme_sheet_t){.key = {"Use your phone", prov ? "Forget" : NULL}, .style = {THEME_KEY_DARK, THEME_KEY_DARK},
                         .key_y = store_read_only() ? 0 : PAIR_Y, .footer = true};
    return prov;
}

/* The Wi-Fi sheet's status line into S.txt.buf: this boot's last join from the watch (net_trial_last), else the sync
 * line. Its own frame, gone before the sheet draws (spec 11). */
static __attribute__((noinline)) void wifi_status(bool prov)
{
    char *const buf = S.txt.buf;
    omni_net_state_t ts;
    char tn[33], fit[THEME_NAME_BUF];
    if (net_trial_last(&ts, tn) && ts != NET_IDLE) {   /* NET_IDLE: a cancelled attempt, nothing to report */
        if (ts == NET_OK || ts == NET_ERR_SYNC) {
            theme_fit_name(fit, (const uint8_t *)tn, (int)strnlen(tn, 32), &font_plex_sans_400_13, 150);
            snprintf(buf, sizeof S.txt.buf, ts == NET_OK ? "Joined %s \xc2\xb7 time set" : "Joined %s \xc2\xb7 time not set", fit);
        } else {
            snprintf(buf, sizeof S.txt.buf, "%s \xc2\xb7 saved network unchanged", ts == NET_ERR_PASSWORD ? "Wrong password"
                     : ts == NET_ERR_NO_AP ? "Not found" : "Wi-Fi didn't start");
        }
    } else if (prov && shell_status()->sync.last_ok) {
        char age[8];
        omni_fmt_age((int64_t)time(NULL) - shell_status()->sync.last_ok, age);
        snprintf(buf, sizeof S.txt.buf, "Time synced %s%s", age, strcmp(age, "now") ? " ago" : "");
    } else {
        snprintf(buf, sizeof S.txt.buf, prov ? "Not synced yet" : "Set up from your phone");
    }
}

/* The Wi-Fi sheet: the name, the status line, the keys and "Choose network"; on a read-only boot a line replaces every
 * key. */
PAINTER wifi_sheet(lv_layer_t *layer)
{
    char *const ssid = S.ssid;
    theme_sheet_t s;
    const bool prov = wifi_keys(&s), ro = s.key_y == 0;
    wifi_status(prov);
    const lv_font_t *f = &font_cormorant_700_26;
    const bool big = !prov || theme_text_width(ssid, f, 0) <= 280;
    s.line[0] = (theme_line_t){"WI-FI", &font_plex_sans_600_10, THEME_BRASS, 136, 3};
    s.line[1] = (theme_line_t){prov ? ssid : "Not set up", big ? f : &font_plex_sans_500_16, big ? THEME_SETUP_HEAD : THEME_INK,
                               big ? 170 : 168, 0};
    s.line[2] = (theme_line_t){S.txt.buf, &font_plex_sans_400_13, THEME_SETUP_DIM, 194, 0};
    if (ro) s.line[3] = (theme_line_t){"Read-only this boot", &font_plex_sans_400_14, THEME_INK_DIM, 244, 0};
    theme_draw_sheet(layer, &s);
    if (ro) return;
    theme_draw_key(layer, CHOOSE_X, CHOOSE_Y, CHOOSE_W, PILL_H, THEME_KEY_PRIMARY);
    theme_draw_text(layer, "Choose network", &font_cormorant_700_20, 0x1f1606, LV_OPA_COVER, 0, THEME_CENTER, CX, CHOOSE_Y + 30);
}

/* The Forget confirm: Cancel, and the red Forget key, held 1.5 s (a brass ring fills round it; "Hold to forget" under
 * it). Dim and inert unless forget_ok(). Line 1, the quoted name, is filled by the painter. */
static const theme_sheet_t FORGET = {
    .line = {{"FORGET WI-FI", &font_plex_sans_600_10, THEME_RED, 150, 3},
             {NULL, &font_plex_sans_500_16, THEME_INK, 186, 0},
             {"The watch forgets this network and", &font_plex_sans_400_13, THEME_SETUP_DIM, 212, 0},
             {"stops setting its clock until set up.", &font_plex_sans_400_13, THEME_SETUP_DIM, 229, 0}},
    .key = {"Cancel", "Forget"},
    .style = {THEME_KEY_DARK, THEME_KEY_ALARM},
    .key_y = CONFIRM_Y,
    .footer = true};

#define RING_BOX SHEET_R_X - 4, CONFIRM_Y - 4, SHEET_R_X + THEME_SHEET_KEY_W + 4, CONFIRM_Y + PILL_H + 4
PAINTER forget_sheet(lv_layer_t *layer)
{
    theme_sheet_t s = FORGET;
    net_ssid(S.ssid);
    snprintf(S.txt.buf, sizeof S.txt.buf, "\xe2\x80\x9c%s\xe2\x80\x9d", S.ssid);
    s.line[1].s = S.txt.buf;
    theme_draw_sheet(layer, &s);
    if (!forget_ok()) /* dim: the key under a dark veil */
        theme_draw_rect(layer, SHEET_R_X, CONFIRM_Y, SHEET_R_X + THEME_SHEET_KEY_W, CONFIRM_Y + PILL_H, PILL_H / 2, 0x000000,
                        THEME_OPA(0.6), 0, 0, 0);
    theme_draw_text(layer, "Hold to forget", &font_plex_sans_400_12, THEME_SETUP_DIM, LV_OPA_COVER, 0, THEME_CENTER,
                    SHEET_R_X + THEME_SHEET_KEY_W / 2, CONFIRM_Y + 66);
    theme_draw_pill_ring(layer, RING_BOX, 1, 2, THEME_BRASS, THEME_OPA(0.16));
    const int32_t age = (int32_t)(lv_tick_get() - S.hold_t);
    if (S.hold.armed && age > 0)
        theme_draw_pill_ring(layer, RING_BOX, age >= OMNI_FORGET_HOLD_MS ? 1 : (float)age / OMNI_FORGET_HOLD_MS, 3,
                             THEME_SETUP_BRASS_HI, LV_OPA_COVER);
}

/* About (spec 8.4), read-only: the theme, the firmware's version and build date, the battery, the assets, the settings'
 * origin and the BLE name. Never the PoP, a password or anything of nvs.net80211. */
PAINTER about_sheet(lv_layer_t *layer)
{
    char *const b = S.txt.buf;
    const size_t n = sizeof S.txt.buf;
    const esp_app_desc_t *d = esp_app_get_description();
    const shell_status_t *st = shell_status();
    theme_draw_text(layer, "ABOUT", &font_plex_sans_600_10, THEME_BRASS, LV_OPA_COVER, 3, THEME_CENTER, CX, 136);
    theme_draw_text(layer, watch_theme()->name, &font_cormorant_700_26, THEME_SETUP_HEAD, LV_OPA_COVER, 0, THEME_CENTER, CX, 170);
    for (int i = 0; i < 5; i++) {
        switch (i) {
        case 0: snprintf(b, n, "v%.24s%s%.16s", app_version(), d->date[0] ? " \xc2\xb7 " : "", d->date); break;
        case 1:
            if (st->battery_pct >= 0) snprintf(b, n, "Battery %d %%%s", st->battery_pct, st->charging ? " \xc2\xb7 charging" : "");
            else strcpy(b, "Battery unknown");
            break;
        case 2:
            snprintf(b, n, "Assets %d files \xc2\xb7 %s", assets_ready() ? assets_count() : watch_theme()->assets_files,
                     assets_ready() ? "OK" : "missing");
            break;
        case 3:
            strcpy(b, "Settings: ");
            store_status(b + 10, n - 10, NULL, 0);   /* store_settings_origin(), copied under the store's lock */
            break;
        default: snprintf(b, n, "BLE %s", net_prov_name()); break;
        }
        theme_draw_text(layer, b, &font_plex_sans_400_13, i ? THEME_SETUP_DIM : THEME_INK, LV_OPA_COVER, 0, THEME_CENTER, CX,
                        202 + i * 22);
    }
    theme_draw_text(layer, "tap outside to close", &font_plex_mono_400_10, THEME_SETUP_FAINT, LV_OPA_COVER, 0, THEME_CENTER,
                    CX, 330);
}

/* A row off the window: its label, dimmed and smaller with distance. */
PAINTER dim_row(lv_layer_t *layer, int i, int32_t y, lv_opa_t opa)
{
    float px = 16 * drum_scale(&S.d, i);
    const lv_font_t *f = px >= 13.5f ? &font_plex_sans_400_14 : px >= 12.5f ? &font_plex_sans_400_13
                       : px >= 11.5f ? &font_plex_sans_400_12 : &font_plex_sans_400_11;
    theme_draw_text(layer, row_of(S.vis[i])->label, f, THEME_SETUP_DIM, opa, 0, THEME_CENTER, CX, y + 5);
}

/* The drum's row i (its callback, a dispatcher): in the window, the row with its value; else dimmed. */
static void drum_row(lv_layer_t *layer, int i, int32_t y, lv_opa_t opa, bool in_window, void *ctx)
{
    (void)ctx;
    if (in_window) window_row(layer, S.vis[i], y, opa, S.now - S.flash_t < FLASH_MS && i == cur_row());
    else dim_row(layer, i, y, opa);
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    theme_arc_text_frame();
    const int cur = cur_id();
    const row_t *row = row_of(cur);
    uint32_t now = lv_tick_get();

    /* rim dial for the windowed row */
    if (S.sheet == SHEET_NONE) {
        if (row->kind == K_LEVEL) level_dial(layer, cur);
        else if (row->kind == K_OPTS) opts_dial(layer, row, S.value[cur]);
        else if (row->kind == K_JOG) city_dial(layer);
        const char *cap = row->kind == K_LINK ? "TAP TO OPEN" : row->kind == K_LEVEL || row->kind == K_INFO ? NULL
                                                                                                        : "TURN THE RIM";
        if (cap) theme_draw_arc_text(layer, cap, &font_plex_sans_500_9, 196, (float)M_PI / 2, THEME_SETUP_DIM, THEME_OPA(0.6), 2.5f);
    }
    theme_draw_text(layer, "SETTINGS", &font_cormorant_700_15, THEME_BRASS, LV_OPA_COVER, 3, THEME_CENTER, CX, 92);

    switch (S.sheet) {
    case SHEET_WIFI: wifi_sheet(layer); break;
    case SHEET_FORGET: forget_sheet(layer); break;
    case SHEET_ABOUT: about_sheet(layer); break;
    default: theme_draw_box(layer, WIN_X, WIN_Y, WIN_X + WIN_W - 1, WIN_Y + WIN_H - 1, 12, THEME_SETUP_GLASS, LV_OPA_COVER, 0, 0);
    }
    S.now = now;
    drum_draw(&S.d, layer, S.sheet != SHEET_NONE ? NULL : drum_row, NULL); /* the rows (none under a sheet), the pips */
    theme_draw_text(layer, "BOOT \xe2\x80\xb9 back", &font_plex_mono_400_10, THEME_SETUP_FAINT, LV_OPA_COVER, 0, THEME_CENTER, CX, 405);
    /* console builds: the LVGL task's stack after this draw (a no-op without the console) */
    static const char *const TAG[] = {"settings/drum", "settings/wifi", "settings/forget", "settings/about"};
    ui_stack_note(TAG[S.sheet]);
}

/* ------------------------------------------------------------ motion and input */
/* Input task, no LVGL lock: every drag here is the screen's (centre = drum, rim = value; on the Forget sheet a slide
 * disarms the hold). */
static bool claim(int16_t x0, int16_t y0, void *ctx)
{
    (void)x0, (void)y0, (void)ctx;
    return true;
}

static void inval_ring(void) { inval_rect(SHEET_R_X - 8, CONFIRM_Y - 8, SHEET_R_X + THEME_SHEET_KEY_W + 8, CONFIRM_Y + PILL_H + 8); }

/* A sheet opens or closes (sheets are states, spec 8.5). The hold starts disarmed, and the claim asks for the hold's
 * gestures only while the Forget sheet is up, so the drum's input is exactly as before (C2). */
static void set_sheet(enum sheet sh)
{
    S.sheet = sh;
    S.sheet_t = lv_tick_get();
    omni_forget_hold_reset(&S.hold);
    board_set_touch_claim_w(claim, NULL, sh == SHEET_FORGET ? FORGET_WANT : 0);
    if (sh == SHEET_NONE) lv_obj_remove_flag(S.frame, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(S.frame, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(S.draw);
}

static void anim_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get(), dt = now - S.t_prev;
    S.t_prev = now;
    if (drum_anim(&S.d, dt)) inval_drum();
    if (cur_row() != S.cur_shown) { /* a new row in the window: its dial round the rim */
        S.cur_shown = cur_row();
        lv_obj_invalidate(S.draw);
    }
    if (now - S.flash_t < FLASH_MS + 40) inval_window();
    if (S.sheet != SHEET_FORGET) return;
    if (S.hold.armed) inval_ring();   /* the hold's ring fills */
    /* the sleep timeout (with its 30 s floor for this screen, omni_nav_sleep_s) closes the sheet, as Cancel would */
    if (now - S.sheet_t >= omni_nav_sleep_s(shell_nav(), settings()->sleep_s) * 1000u) set_sheet(SHEET_NONE);
}

static void jog_to(int16_t x, int16_t y)
{
    float a = atan2f(y - CY, x - CX);
    S.jog += wrapf(a - S.last_a);
    S.last_a = a;
    set_value(R_CITY, city_wrap(S.jog_base - (int)lroundf(S.jog / JOG_STEP)));
    inval_top_arc(); /* the labels follow the finger between cities */
}

/* A push (spec 8.5): the windowed row first, so popping back lands on it. */
static void push(int code)
{
    s_restore_row = (int8_t)cur_id();
    shell_post(code, 0);
}

static void sheet_gesture(const omni_gesture_t *g)
{
    theme_sheet_t s;
    S.sheet_t = lv_tick_get();
    if (S.sheet == SHEET_FORGET) {   /* C2: every gesture feeds the hold; it alone posts UI_WIFI_FORGET */
        const bool was = S.hold.armed;
        if (omni_forget_hold_step(&S.hold, g->kind, g->hold_ms, forget_ok() && in_key(g, SHEET_R_X, CONFIRM_Y, THEME_SHEET_KEY_W))) {
            if (shell_post(UI_WIFI_FORGET, 0)) {
                set_sheet(SHEET_NONE);
            } else {   /* dropped (queue full): the sheet stays and a fresh hold may retry (review M4) */
                omni_forget_hold_reset(&S.hold);
                inval_ring();
                ESP_LOGW("settings", "forget dropped (queue full): hold again");
            }
            return;
        }
        if (S.hold.armed && !was) S.hold_t = S.sheet_t;
        if (S.hold.armed != was) inval_ring();
        if (g->kind != G_TAP && g->kind != G_LIFT_STILL) return;
        if (theme_sheet_key(&FORGET, g->x, g->y) != 1) set_sheet(SHEET_NONE);   /* Cancel, or a tap outside; Forget waits */
        return;
    }
    if (g->kind != G_TAP) return;
    if (S.sheet == SHEET_ABOUT) {   /* read-only: any tap closes it */
        set_sheet(SHEET_NONE);
        return;
    }
    wifi_keys(&s);
    const int k = theme_sheet_key(&s, g->x, g->y);
    const bool choose = s.key_y && in_key(g, CHOOSE_X, CHOOSE_Y, CHOOSE_W);   /* key_y 0: a read-only boot, no keys */
    set_sheet(k == 1 ? SHEET_FORGET : SHEET_NONE);
    if (choose) push(UI_OPEN_NETLIST);   /* R212: the event loop opens the list (omni_nav_open_netlist) */
    else if (k == 0) push(UI_WIFI_SETUP);   /* "Use your phone": provisioning (the saved network stays) */
}

static void gesture(const omni_gesture_t *g, uint32_t t)
{
    int dx = g->x0 - CX, dy = g->y0 - CY;
    bool rim_start = dx * dx + dy * dy >= RIM_R * RIM_R;
    if (S.sheet != SHEET_NONE) {
        sheet_gesture(g);
        return;
    }
    const int cur = cur_id();
    switch (g->kind) {
    case G_DRAG_START:
        if (rim_start) {
            if (!rim_accepts(g->x0, g->y0)) break; /* not where this row's dial is: ignore the whole drag */
            S.rim = true;
            S.jog = 0;
            S.jog_base = S.value[R_CITY];
            S.last_a = atan2f(g->y0 - CY, g->x0 - CX);
            if (row_of(cur)->kind == K_JOG) jog_to(g->x, g->y);
            else rim_at(g->x, g->y);
        } else {
            drum_drag(&S.d, g, t);
            inval_drum();
        }
        break;
    case G_DRAG_MOVE:
        if (S.rim) {
            if (row_of(cur)->kind == K_JOG) jog_to(g->x, g->y);
            else rim_at(g->x, g->y);
        } else if (S.d.drag) {
            drum_drag(&S.d, g, t);
            inval_drum();
        }
        break;
    case G_DRAG_END:
        if (S.rim) { /* the release point too: moves may have been dropped while the loop was behind */
            if (row_of(cur)->kind == K_JOG) jog_to(g->x, g->y);
            else rim_at(g->x, g->y);
            S.rim = false;
            S.jog = 0;
            lv_obj_invalidate(S.draw);
        } else if (S.d.drag) {
            drum_drag(&S.d, g, t); /* the release: the flick's momentum sets the target */
            inval_drum();
        }
        break;
    case G_TAP: {
        int tdx = g->x - CX, tdy = g->y - CY;
        if (tdx * tdx + tdy * tdy >= RIM_R * RIM_R) {
            if (rim_accepts(g->x, g->y)) rim_at(g->x, g->y);
            break;
        }
        if (drum_tap(&S.d, g->y) < 0) { /* the windowed row: a link opens, a choice steps */
            switch (omni_rows_link(cur)) {
            case OMNI_LINK_FIND_CITY:   /* the keyboard in city mode; without its assets the jog alone stays */
                if (scr_keyboard_ready()) {
                    const scr_kb_open_t o = {.mode = KB_MODE_CITY};
                    scr_keyboard_prepare(&o);
                    push(UI_OPEN_KEYBOARD);
                }
                break;
            case OMNI_LINK_FACES: push(UI_OPEN_FACES); break;
            case OMNI_LINK_WIFI_SHEET: set_sheet(SHEET_WIFI); break;
            case OMNI_LINK_ABOUT_SHEET: set_sheet(SHEET_ABOUT); break;
            default:
                if (row_of(cur)->kind == K_OPTS) set_value(cur, (S.value[cur] + 1) % row_of(cur)->n);
                break;   /* K_INFO (Theme): nothing */
            }
        }   /* else the drum steps toward the tap */
        break;
    }
    default: break;
    }
}


/* ------------------------------------------------------------ screen interface */
static void create(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    if (s_rows == NULL) s_rows = heap_caps_calloc(ROW_MAX, sizeof *s_rows, MALLOC_CAP_SPIRAM);
    if (s_rows == NULL) s_rows = calloc(ROW_MAX, sizeof *s_rows);   /* no PSRAM free: internal, as before M3 */
    if (s_rows == NULL) {
        ESP_LOGE("settings", "no memory for the rows");
        abort();
    }
    memcpy(s_rows, ROWS, sizeof ROWS);
    reveal_row();
    for (int i = 0; i < R__COUNT; i++) s_rows[i].kind = (uint8_t)omni_rows_kind(i);
    S.nthemed = theme_rows();
    /* the core's order: Reveal only when the theme offers more than one; the theme's rows after Reduced motion */
    S.nvis = omni_rows_order((uint8_t)s_rows[R_REVEAL].n, S.nthemed, S.vis, ROW_MAX);
    int start = 0;
    for (int i = 0; i < S.nvis; i++)
        if (S.vis[i] == s_restore_row) start = i;
    s_restore_row = R_CITY;
    drum_init(&S.d, S.nvis, start);
    load_values();
    S.cur_shown = -1;
    S.flash_t = lv_tick_get() - 10000;
    S.root = theme_box(parent, 0, 0, THEME_W, THEME_W);
    theme_background(S.root, THEME_BG_SETTINGS);
    S.draw = theme_box(S.root, 0, 0, THEME_W, THEME_W);
    lv_obj_add_event_cb(S.draw, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    S.frame = theme_window(S.root, &DRUM_WINDOW);
    S.t_prev = lv_tick_get();
    S.anim = lv_timer_create(anim_cb, ANIM_MS, NULL);
    board_set_touch_claim(claim, NULL);
}

/* Every second, and right after the event loop applied a change: the city's time, Wi-Fi state. Values set
 * elsewhere (quick settings, the console) are picked up unless this screen is mid-drag. */
static void update(int64_t now)
{
    (void)now;
    int before[ROW_MAX];
    memcpy(before, S.value, sizeof before);
    if (!S.rim && lv_tick_get() - S.flash_t > 1000) load_values();
    if (memcmp(before, S.value, sizeof before) != 0 || S.sheet != SHEET_NONE) lv_obj_invalidate(S.draw);
    else inval_window(); /* the city's time, the network name */
}

static void destroy(void)
{
    /* covered by a pushed screen (still in the stack): keep the windowed row for the pop back; closed: forget it */
    s_restore_row = S.nvis > 0 && omni_nav_in_stack(shell_nav(), SCR_SETTINGS) ? (int8_t)cur_id() : R_CITY;
    board_set_touch_claim(NULL, NULL);
    if (S.anim) lv_timer_delete(S.anim);
    if (S.root) lv_obj_delete(S.root);
    memset(&S, 0, sizeof S);
}

static void dim(bool on)
{
    if (on && S.sheet == SHEET_FORGET) set_sheet(SHEET_NONE);   /* the glance (the sleep timeout) cancels a Forget */
    lv_obj_set_style_opa(S.root, on ? DIM_OPA : LV_OPA_COVER, 0);
}

void scr_settings_open(void) { shell_post(UI_OPEN_SETTINGS, 0); }
void scr_settings_home(void) { s_restore_row = R_CITY; }

const omni_screen_vt_t SCR_SETTINGS_VT = {
    .id = SCR_SETTINGS, .create = create, .update = update, .destroy = destroy, .gesture = gesture, .dim = dim,
};
