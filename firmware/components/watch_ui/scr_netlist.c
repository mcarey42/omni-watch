/* SCR_NETLIST: design/mockups/faces-setup.js `netlist` (spec 7.1; Task 2's approved mockup; R206).
 *
 * The Settings drum (drum.h) holds the scan's networks, strongest first (omni_netlist_build), then "Other network..." and
 * "Scan again". The window row is the name (Plex Sans 17, cut with an ellipsis at min(250 px, the room beside its
 * tags)), a lock for a secured network, four bars and the brass "saved" tag; an unsupported network is faint, tagged
 * "not supported", and does nothing. The rim only shows: the windowed network's signal on the level scale (-90 .. -30
 * dBm) with the red index, or, while a scan runs, a brass tick sweeping it under "Listening..." in the window. The
 * states without a list: "No networks found" and "Wi-Fi couldn't start" (with Scan again and Use your phone), "Wi-Fi
 * is busy" (a sync or a phone session: the 500 ms timer scans once net is free, I5) and the read-only boot.
 *
 * A tap on the windowed row: a secured network or "Other network..." opens the keyboard (scr_keyboard_prepare, then
 * UI_OPEN_KEYBOARD); an open network opens the confirm sheet (its Join opens the keyboard, which joins at once with no
 * password), the saved one the "Saved" sheet (Join now posts UI_SYNC_NOW; Change password opens the keyboard). Without
 * the keyboard's assets (scr_keyboard_ready) each of those taps takes the phone path instead (spec 6.4). The list never
 * joins (net_try_join is the keyboard's) and never logs a network's name. The last scan is kept 60 s in PSRAM (spec
 * 8.5), so popping back from the keyboard shows it again on the same row. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scr_netlist.h"
#include "scr_keyboard.h"
#include "omni/kb.h"
#include "theme.h"
#include "drum.h"
#include "stepart.h"
#include "board.h"
#include "store.h"
#include "net.h"
#include "debug_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#define CX THEME_CX
#define CY THEME_CY
#define WIN_X DRUM_WIN_X
#define WIN_Y DRUM_WIN_Y
#define WIN_W DRUM_WIN_W
#define WIN_H DRUM_WIN_H
#define ROW_X0 (WIN_X + 20)        /* the window row's name */
#define ROW_XR (WIN_X + WIN_W - 18) /* its right edge */
#define RIM_R 168                  /* touches from here out are the rim (information only) */
#define LISTEN_FRAME_MS 48         /* the sweep's repaint (the drum's is ANIM_MS) */
#define LISTEN_MS 1400             /* one sweep of the scale */
#define BUSY_MS 500                /* I5: the busy retry */
#define KEEP_MS 60000              /* spec 8.5: the results kept */
#define SCAN_RECS 20               /* net_scan_results' most */
#define NAME_MAX_PX 250
#define KEY_X 158                  /* the empty state's keys */
#define KEY_W 150
#define KEY_H 44
#define KEY_SCAN_Y 280
#define KEY_PHONE_Y 332
#define SHEET_Y 240                /* the sheets' keys: Settings' Forget sheet's */
#define ENGRAVED 0x5b5246          /* the glyph and slash when nothing is heard */
#define DARK_INK 0x1f1606          /* text on a brass key */

enum { NL_LISTENING, NL_LIST, NL_EMPTY, NL_BUSY, NL_NOSTART, NL_READONLY };
enum { SH_NONE, SH_SAVED, SH_OPEN };

/* The last scan (spec 8.5), in PSRAM: allocated at the first use and kept. LVGL lock (create, scr_netlist_net). */
typedef struct {
    omni_scan_rec_t recs[SCAN_RECS];
    omni_net_row_t rows[OMNI_NETLIST_MAX];
    int nrecs, nrows;
    uint32_t t_ms;   /* esp_timer ms when it arrived */
    bool have;
    char name[THEME_NAME_BUF];   /* the painters' text (they never nest, and draw only with a list): off the LVGL task's stack */
    char hint[40];
} netlist_cache_t;
static netlist_cache_t *s_c;
static bool s_rescan;           /* scr_netlist_rescan: the next create scans */
/* The windowed row while a pushed screen (the keyboard) covers the list, by what it is (F4: a scan meanwhile may reorder
 * the rows): a network by its name's bytes, or Other network / Scan again. */
enum { RS_NONE, RS_NET, RS_OTHER, RS_SCAN };
static uint8_t s_restore, s_restore_len, s_restore_ssid[32];

static struct {
    drum_screen_t ds;   /* root, draw, frame, anim */
    lv_timer_t *busy;
    drum_t d;
    uint8_t st, sheet;
    int8_t sheet_row;    /* the row the sheet was opened on (the drum is frozen while it is open) */
    int cur_shown;
    uint32_t t_prev, scan_t, listen_t;
    float listen_a;      /* the sweep's head as last painted */
} S;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
#define deg theme_arc_deg
static int nrows(void) { return s_c ? s_c->nrows : 0; }
/* Every rows[] read goes through here: NULL unless i is one of the live rows. */
static const omni_net_row_t *row_at(int i)
{
    return i >= 0 && i < nrows() && i < OMNI_NETLIST_MAX ? &s_c->rows[i] : NULL;
}

static netlist_cache_t *cache(void)
{
    if (s_c == NULL) s_c = heap_caps_calloc(1, sizeof *s_c, MALLOC_CAP_SPIRAM);
    if (s_c == NULL) s_c = calloc(1, sizeof *s_c);   /* no PSRAM free: internal */
    return s_c;
}

/* The rows from the kept records, the saved network marked by its exact bytes (net's cached name). */
static void rebuild(void)
{
    char saved[33] = "";
    if (net_is_provisioned()) net_ssid(saved);
    s_c->nrows = omni_netlist_build(s_c->recs, s_c->nrecs, (const uint8_t *)saved, (uint8_t)strlen(saved), s_c->rows);
}

/* ------------------------------------------------------------ states */
static void inval_rect(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_area_t a = {x1, y1, x2, y2};
    lv_obj_invalidate_area(S.ds.draw, &a);
}

static void set_state(int st)
{
    if (st == S.st && st != NL_LIST && S.ds.root) return;
    S.st = (uint8_t)st;
    S.sheet = SH_NONE;
    S.sheet_row = -1;
    S.cur_shown = -1;
    if (S.ds.frame) lv_obj_remove_flag(S.ds.frame, LV_OBJ_FLAG_HIDDEN);
    if (S.ds.draw) lv_obj_invalidate(S.ds.draw);
}

/* The kept results on the drum: start (a row index; -1: the saved network's row, else the strongest). */
static void show_results(int start)
{
    const int n = nrows();
    if (n == 0) {
        set_state(NL_EMPTY);
        return;
    }
    if (start < 0 || start >= n + 2) {
        start = 0;
        for (int i = 0; i < n; i++)
            if (row_at(i)->saved) start = i;
    }
    drum_init(&S.d, n + 2, start);
    S.d.pips_max = 9;   /* the mockup: no pips past nine rows */
    set_state(NL_LIST);
}

static void start_scan(void)
{
    if (net_scan()) {   /* false while busy or provisioning: wait (I5) */
        S.scan_t = S.listen_t = lv_tick_get();
        S.listen_a = THEME_LEVEL_A0;
        set_state(NL_LISTENING);
    } else {
        set_state(NL_BUSY);
    }
}

void scr_netlist_net(int ev)
{
    const omni_net_state_t st = NET_EV_STATE(ev);
    if (st == NET_IDLE && cache()) {   /* the scan's records, kept whether or not the list is shown */
        s_c->nrecs = net_scan_results(s_c->recs, SCAN_RECS);
        s_c->t_ms = now_ms();
        s_c->have = true;
        rebuild();
    }
    if (S.ds.root == NULL || S.st == NL_READONLY) return;
    if (st == NET_SCANNING && S.st != NL_LISTENING) {   /* a scan started elsewhere (the console's) */
        S.scan_t = S.listen_t = lv_tick_get();
        S.listen_a = THEME_LEVEL_A0;
        set_state(NL_LISTENING);
    } else if (st == NET_IDLE && s_c) {
        show_results(S.st == NL_LIST ? drum_cur(&S.d) : -1);
    } else if (st == NET_ERR_NO_MEM && (S.st == NL_LISTENING || S.st == NL_BUSY)) {
        set_state(NL_NOSTART);
    }
}

void scr_netlist_rescan(void) { s_rescan = true; }

/* ------------------------------------------------------------ drawing: flat (spec 11), noinline painters */
#define PAINTER static __attribute__((noinline)) void

/* The name into s_c->name, cut with an ellipsis to max px in f (theme_fit_name). */
static void fit(const uint8_t *b, int len, const lv_font_t *f, int32_t max) { theme_fit_name(s_c->name, b, len, f, max); }

/* barsGlyph: four bars, x left, y baseline, at scale s; lit ones on, the rest off at off_opa. */
static void bars(lv_layer_t *layer, float x, int32_t y, int lit, float s, uint32_t on, uint32_t off, lv_opa_t off_opa,
                 lv_opa_t opa)
{
    for (int i = 0; i < 4; i++) {
        const float bx = x + i * 5 * s;
        theme_draw_box(layer, (int32_t)lroundf(bx), (int32_t)lroundf(y - (4.5f + i * 3) * s),
                       (int32_t)lroundf(bx + 3.2f * s) - 1, y - 1, 1, i < lit ? on : off,
                       i < lit ? opa : (lv_opa_t)(off_opa * opa / 255), 0, 0);
    }
}

/* lockGlyph: x centre, y baseline. */
static void lock(lv_layer_t *layer, int32_t x, int32_t y, lv_opa_t opa)
{
    theme_draw_arc(layer, x, y - 10, 4, 2, 180, 360, THEME_SETUP_DIM, opa, false);
    theme_draw_line(layer, x - 3, y - 7, x - 3, y - 10, 2, THEME_SETUP_DIM, opa, false);
    theme_draw_line(layer, x + 3, y - 7, x + 3, y - 10, 2, THEME_SETUP_DIM, opa, false);
    theme_draw_box(layer, x - 5, y - 7, x + 5, y, 2, THEME_SETUP_DIM, opa, 0, 0);
    theme_draw_box(layer, x, y - 5, x, y - 2, 0, THEME_SETUP_GLASS, opa, 0, 0);
}

/* rescanGlyph: a 7 px circle open at the top right, its arrowhead at the start. */
static void rescan(lv_layer_t *layer, int32_t x, int32_t y, uint32_t rgb, lv_opa_t opa)
{
    theme_draw_arc(layer, x, y, 8, 2, 297, 243, rgb, opa, true);
    if (!theme_on_layer(layer, x + 1, y - 11, x + 9, y - 3)) return;
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = lv_color_hex(rgb);
    t.opa = opa;
    t.p[0].x = x + 8, t.p[0].y = y - 8;
    t.p[1].x = x + 2, t.p[1].y = y - 10;
    t.p[2].x = x + 4, t.p[2].y = y - 4;
    lv_draw_triangle(layer, &t);
}

static void rim_hint(lv_layer_t *layer, const char *s)
{
    theme_draw_arc_text(layer, s, &font_plex_sans_500_9, 196, (float)M_PI / 2, THEME_SETUP_DIM, THEME_OPA(0.6), 2.5f);
}

static float listen_angle(uint32_t now)
{
    float p = (float)((now - S.scan_t) % LISTEN_MS) / LISTEN_MS;
    p = 1 - (1 - p) * (1 - p) * (1 - p);   /* easeOut */
    return THEME_LEVEL_A0 + THEME_LEVEL_SWEEP * p * 0.999f + 0.0005f;
}

/* The rim on a list row: the signal on the level scale, the red index, a bar glyph at each end, the hint. */
PAINTER rim_signal(lv_layer_t *layer, int i)
{
    const omni_net_row_t *r = row_at(i);
    if (r == NULL) {
        theme_draw_level(layer, -1, THEME_LEVEL_INFO);
        rim_hint(layer, i == nrows() ? "TAP TO TYPE A NAME" : "TAP TO LISTEN AGAIN");
        return;
    }
    const float v = clampf((r->rec.rssi + 90) / 60.0f, 0, 1);
    theme_draw_level(layer, v, THEME_LEVEL_INFO);
    theme_draw_red_index(layer, THEME_LEVEL_A0 + THEME_LEVEL_SWEEP * v);
    for (int e = 0; e < 2; e++) {
        const float a = e ? THEME_LEVEL_A0 + THEME_LEVEL_SWEEP + 0.15f : THEME_LEVEL_A0 - 0.15f;
        bars(layer, CX + cosf(a) * 196 - 7.5f, CY + (int32_t)lroundf(sinf(a) * 196 + 5.5f), e ? 4 : 1, 0.78f, THEME_BRASS,
             THEME_BRASS, THEME_OPA(0.28), LV_OPA_COVER);
    }
    snprintf(s_c->hint, sizeof s_c->hint, "%s%d dBm \xc2\xb7 %s", r->rec.rssi < 0 ? "\xe2\x80\x93" : "", abs(r->rec.rssi),
             r->auth == OMNI_AUTH_UNSUPPORTED ? "CAN'T JOIN" : r->saved ? "TAP FOR OPTIONS" : "TAP TO JOIN");
    rim_hint(layer, s_c->hint);
}

/* The rim without a list: the empty scale, the sweep while listening, the state's hint. */
PAINTER rim_plain(lv_layer_t *layer, uint32_t now)
{
    static const char *const HINT[] = {[NL_LISTENING] = "LISTENING ON EVERY CHANNEL", [NL_EMPTY] = "NOTHING IN RANGE",
                                       [NL_BUSY] = "WAITING FOR WI-FI", [NL_NOSTART] = "TRY AGAIN"};
    theme_draw_level(layer, -1, THEME_LEVEL_INFO);
    if (S.st == NL_LISTENING) {   /* a comet of 26 pieces behind a bright tick */
        const float a = listen_angle(now);
        stepart_comet(layer, 199, 5, a, 26, 0.033f, 0.01f, 0.9f, THEME_SETUP_BRASS_HI, THEME_LEVEL_A0);
        const int32_t x0 = CX + (int32_t)lroundf(cosf(a) * 185), y0 = CY + (int32_t)lroundf(sinf(a) * 185);
        const int32_t x1 = CX + (int32_t)lroundf(cosf(a) * 207), y1 = CY + (int32_t)lroundf(sinf(a) * 207);
        theme_draw_line(layer, x0, y0, x1, y1, 8, THEME_SETUP_BRASS_HI, THEME_OPA(0.25), true);   /* the glow */
        theme_draw_line(layer, x0, y0, x1, y1, 3, THEME_SETUP_BRASS_HI, LV_OPA_COVER, true);
    }
    if (S.st < sizeof HINT / sizeof HINT[0] && HINT[S.st]) rim_hint(layer, HINT[S.st]);
}

/* The windowed row: the name and its tags from the right (bars, lock, saved), or the two actions. */
PAINTER window_row(lv_layer_t *layer, int i, int32_t y, lv_opa_t opa)
{
    const lv_font_t *f = &font_plex_sans_500_17;
    const int32_t base = y + 6;
    if (!theme_on_layer(layer, WIN_X, y - 20, WIN_X + WIN_W, y + 12)) return;
    const omni_net_row_t *r = row_at(i);
    if (r == NULL) {
        const bool other = i == nrows();
        theme_draw_text(layer, other ? "Other network\xe2\x80\xa6" : "Scan again", f, THEME_INK, opa, 0, THEME_LEFT, ROW_X0, base);
        if (other) theme_draw_text(layer, "\xe2\x80\xba", f, THEME_SETUP_BRASS_HI, opa, 0, THEME_RIGHT, ROW_XR, base);
        else rescan(layer, ROW_XR - 8, y, THEME_SETUP_BRASS_HI, opa);
        return;
    }
    int32_t right;
    uint32_t ink = THEME_INK;
    if (r->auth == OMNI_AUTH_UNSUPPORTED) {
        theme_draw_text(layer, "not supported", &font_plex_sans_400_12, THEME_SETUP_FAINT, opa, 0, THEME_RIGHT, ROW_XR, base - 1);
        right = ROW_XR - theme_text_width("not supported", &font_plex_sans_400_12, 0) - 12;
        ink = THEME_SETUP_FAINT;
    } else {
        bars(layer, ROW_XR - 18.2f, base, r->bars, 1, THEME_SETUP_BRASS_HI, THEME_PIP_OFF, LV_OPA_COVER, opa);
        right = ROW_XR - 29;
        if (r->auth != OMNI_AUTH_OPEN) {
            lock(layer, right - 5, base, opa);
            right -= 20;
        }
        if (r->saved) {   /* the brass tag: a primary key 18 px high */
            const int32_t w = theme_text_width("saved", &font_plex_sans_600_10, 0) + 14;
            right -= w;
            theme_draw_key_opa(layer, right, y - 9, w, 18, THEME_KEY_PRIMARY, opa);
            theme_draw_text(layer, "saved", &font_plex_sans_600_10, DARK_INK, opa, 0, THEME_CENTER, right + w / 2, y + 4);
            right -= 10;
        }
    }
    fit(r->rec.ssid, r->rec.ssid_len, f, right - ROW_X0 < NAME_MAX_PX ? right - ROW_X0 : NAME_MAX_PX);
    theme_draw_text(layer, s_c->name, f, ink, opa, 0, THEME_LEFT, ROW_X0, base);
}

/* A row off the window: dimmed and smaller with distance, centred with its tag; cut at 250 px and, like the mockup's
 * r 168 clip, at the chord its line has inside r 168. */
PAINTER dim_row(lv_layer_t *layer, int i, int32_t y, lv_opa_t opa)
{
    if (!theme_on_layer(layer, CX - RIM_R, y - 16, CX + RIM_R, y + 10)) return;
    const float px = 16 * drum_scale(&S.d, i);
    const lv_font_t *f = drum_dim_font(px);
    const omni_net_row_t *r = row_at(i);
    if (r == NULL) {
        snprintf(s_c->name, sizeof s_c->name, "%s", i == nrows() ? "Other network" : "Scan again");
        if (i == nrows()) strcat(s_c->name, theme_ellipsis(f));
        theme_draw_text(layer, s_c->name, f, THEME_SETUP_DIM, opa, 0, THEME_CENTER, CX, y + 5);
        return;
    }
    const bool bad = r->auth == OMNI_AUTH_UNSUPPORTED;
    const char *tag = bad ? " \xc2\xb7 not supported" : r->saved ? " \xc2\xb7 saved" : NULL;
    const lv_font_t *tf = px * 0.8f >= 10.5f ? &font_plex_sans_400_11 : px * 0.8f >= 9.5f ? &font_plex_sans_500_10
                                                                                          : &font_plex_sans_500_9;
    const int32_t tw = tag ? theme_text_width(tag, tf, 0) : 0, dy = abs(y - CY) + 10;
    const int32_t chord = dy < RIM_R ? (int32_t)(2 * sqrtf((float)(RIM_R * RIM_R - dy * dy))) : 0;
    fit(r->rec.ssid, r->rec.ssid_len, f, (chord < NAME_MAX_PX ? chord : NAME_MAX_PX) - tw);
    const int32_t nw = theme_text_width(s_c->name, f, 0), x = CX - (nw + tw) / 2;
    theme_draw_text(layer, s_c->name, f, bad ? THEME_SETUP_FAINT : THEME_SETUP_DIM, opa, 0, THEME_LEFT, x, y + 5);
    if (tag) theme_draw_text(layer, tag, tf, bad ? THEME_SETUP_FAINT : THEME_BRASS, opa, 0, THEME_LEFT, x + nw, y + 5);
}

static void drum_row(lv_layer_t *layer, int i, int32_t y, lv_opa_t opa, bool in_window, void *ctx)
{
    (void)ctx;
    if (in_window) window_row(layer, i, y, opa);
    else dim_row(layer, i, y, opa);
}

/* No list: the Wi-Fi mark (pulsing while listening; struck through when nothing can be heard), the state's line in
 * the window, and the empty states' two keys. */
PAINTER state_view(lv_layer_t *layer, uint32_t now)
{
    static const char *const LINE[] = {[NL_LISTENING] = "Listening\xe2\x80\xa6", [NL_EMPTY] = "No networks found",
                                       [NL_BUSY] = "Wi-Fi is busy", [NL_NOSTART] = "Wi-Fi couldn\xe2\x80\x99t start",
                                       [NL_READONLY] = "Read-only this boot"};
    const bool listen = S.st == NL_LISTENING;
    stepart_wifi(layer, CX, 140, listen ? THEME_SETUP_BRASS_HI : ENGRAVED, listen, now - S.scan_t);
    if (S.st != NL_LISTENING && S.st != NL_BUSY) theme_draw_line(layer, CX - 20, 166, CX + 20, 122, 2, ENGRAVED, LV_OPA_COVER, true);
    theme_draw_text(layer, LINE[S.st], &font_plex_sans_500_17, THEME_INK, LV_OPA_COVER, 0, THEME_CENTER, CX, WIN_Y + WIN_H / 2 + 6);
    if (S.st == NL_READONLY)
        theme_draw_text(layer, "Wi-Fi can\xe2\x80\x99t be changed", &font_plex_sans_400_13, THEME_SETUP_DIM, LV_OPA_COVER, 0,
                        THEME_CENTER, CX, 296);
    if (S.st != NL_EMPTY && S.st != NL_NOSTART) return;
    theme_draw_key(layer, KEY_X, KEY_SCAN_Y, KEY_W, KEY_H, THEME_KEY_PRIMARY);
    rescan(layer, KEY_X + 25, KEY_SCAN_Y + 22, DARK_INK, LV_OPA_COVER);
    theme_draw_text(layer, "Scan again", &font_cormorant_700_20, DARK_INK, LV_OPA_COVER, 0, THEME_CENTER, KEY_X + KEY_W / 2 + 12,
                    KEY_SCAN_Y + 29);
    theme_draw_key(layer, KEY_X, KEY_PHONE_Y, KEY_W, KEY_H, THEME_KEY_DARK);
    stepart_phone_at(layer, KEY_X + 21, KEY_PHONE_Y + 22, 5, 9, 2, 2, 7, THEME_BRASS);   /* phoneGlyph at 0.4: rrect 10 x 18 */
    theme_draw_text(layer, "Use your phone", &font_plex_sans_500_14, THEME_INK, LV_OPA_COVER, 0, THEME_CENTER, KEY_X + KEY_W / 2 + 10,
                    KEY_PHONE_Y + 27);
}

/* The windowed network's sheet (no mockup: Settings' sheet grammar): Saved (Join now, Change password) or the open
 * network's confirm (Cancel, Join). */
static __attribute__((noinline)) void sheet_of(theme_sheet_t *sh)
{
    const bool saved = S.sheet == SH_SAVED;
    *sh = (theme_sheet_t){
        .line = {{saved ? "SAVED NETWORK" : "OPEN NETWORK", &font_plex_sans_600_10, THEME_BRASS, 150, 3},
                 {s_c->name, &font_plex_sans_500_17, THEME_INK, 186, 0},
                 {saved ? "Join it now, or change its password" : "Join an open network?", &font_plex_sans_400_13,
                  THEME_SETUP_DIM, 214, 0}},
        .key = {saved ? "Join now" : "Cancel", saved ? "Change password" : "Join"},   /* the primary: Join now or Join */
        .style = {saved ? THEME_KEY_PRIMARY : THEME_KEY_DARK, saved ? THEME_KEY_DARK : THEME_KEY_PRIMARY},
        .key_y = SHEET_Y,
        .footer = true,
    };
}

PAINTER sheet_view(lv_layer_t *layer)
{
    const omni_net_row_t *r = row_at(S.sheet_row);
    if (r == NULL) return;   /* the rows changed under it (never expected: new results close it) */
    fit(r->rec.ssid, r->rec.ssid_len, &font_plex_sans_500_17, 280);
    theme_sheet_t sh;
    sheet_of(&sh);
    theme_draw_sheet(layer, &sh);
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    theme_arc_text_frame();
    const uint32_t now = lv_tick_get();
    const bool list = S.st == NL_LIST;
    if (S.sheet == SH_NONE) {
        if (list) rim_signal(layer, drum_cur(&S.d));
        else rim_plain(layer, now);
    }
    theme_draw_text(layer, "CHOOSE NETWORK", &font_cormorant_700_15, THEME_BRASS, LV_OPA_COVER, 1, THEME_CENTER, CX, 92);
    if (S.sheet != SH_NONE) sheet_view(layer);
    else theme_draw_box(layer, WIN_X, WIN_Y, WIN_X + WIN_W - 1, WIN_Y + WIN_H - 1, 12, THEME_SETUP_GLASS, LV_OPA_COVER, 0, 0);
    if (list) drum_draw(&S.d, layer, S.sheet != SH_NONE ? NULL : drum_row, NULL);   /* the rows (none under a sheet), the pips */
    else state_view(layer, now);
    if (S.st != NL_EMPTY && S.st != NL_NOSTART)
        theme_draw_text(layer, "BOOT \xe2\x80\xb9 back", &font_plex_mono_400_10, THEME_SETUP_FAINT, LV_OPA_COVER, 0, THEME_CENTER, CX, 405);
    /* console builds: the LVGL task's stack after this draw */
    ui_stack_note(S.sheet ? "netlist/sheet" : list ? "netlist/list" : S.st == NL_LISTENING ? "netlist/listen" : "netlist/state");
}

/* ------------------------------------------------------------ motion and input */
static void inval_drum(void) { drum_inval(S.ds.draw); }

/* The sweep from head angle a0 to a1 (the comet behind it, the tick and its glow). */
static void inval_sweep(float a0, float a1)
{
    lv_area_t a;
    theme_arc_area(CX, CY, 212, 30, deg(a0 - 0.9f), deg(a1 + 0.05f), &a);
    lv_obj_invalidate_area(S.ds.draw, &a);
}

static void anim_cb(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t now = lv_tick_get(), dt = now - S.t_prev;
    S.t_prev = now;
    if (S.st == NL_LIST) {
        if (S.sheet == SH_NONE && drum_anim(&S.d, dt)) inval_drum();   /* frozen under a sheet */
        if (drum_cur(&S.d) != S.cur_shown) {   /* a new row in the window: its signal round the rim */
            S.cur_shown = drum_cur(&S.d);
            lv_obj_invalidate(S.ds.draw);
        }
    } else if (S.st == NL_LISTENING && now - S.listen_t >= LISTEN_FRAME_MS) {
        S.listen_t = now;
        const float a = listen_angle(now);
        if (a >= S.listen_a) {
            inval_sweep(S.listen_a, a);
        } else {   /* the sweep started again */
            inval_sweep(S.listen_a, S.listen_a);
            inval_sweep(THEME_LEVEL_A0, a);
        }
        S.listen_a = a;
        inval_rect(CX - 36, 116, CX + 36, 160);   /* the pulsing mark */
    }
}

/* I5: while busy, scan once net is free (the phone session's end after "Type it instead", a sync). */
static void busy_cb(lv_timer_t *timer)
{
    (void)timer;
    if (S.st == NL_BUSY && !net_busy() && !net_provisioning()) start_scan();
}

static bool in_box(const omni_gesture_t *g, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return g->x >= x && g->x <= x + w && g->y >= y && g->y <= y + h;   /* inclusive at the edge */
}

/* The keyboard for row r (NULL: "Other network...", a name typed). Without its assets (spec 6.4) the phone path instead. */
static void open_kb(const omni_net_row_t *r)
{
    if (!scr_keyboard_ready()) {
        shell_post(UI_LIST_TO_PROV, 0);
        return;
    }
    scr_kb_open_t o = {.mode = r ? KB_MODE_PASSWORD : KB_MODE_SSID};
    if (r) {
        o.ssid_len = r->rec.ssid_len <= 32 ? r->rec.ssid_len : 32;
        memcpy(o.ssid, r->rec.ssid, o.ssid_len);
        o.authmode = r->rec.authmode;
    }
    scr_keyboard_prepare(&o);
    shell_post(UI_OPEN_KEYBOARD, 0);
}

static void close_sheet(void)
{
    S.sheet = SH_NONE;
    S.sheet_row = -1;
    lv_obj_remove_flag(S.ds.frame, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(S.ds.draw);
}

static void sheet_tap(const omni_gesture_t *g)
{
    if (row_at(S.sheet_row) == NULL) {   /* its row is gone: nothing to act on */
        close_sheet();
        return;
    }
    theme_sheet_t sh;
    sheet_of(&sh);
    const int k = theme_sheet_key(&sh, g->x, g->y);
    if (k == 1) {   /* Change password / Join: the keyboard (an open network's joins at once) */
        open_kb(row_at(S.sheet_row));
        return;
    }
    /* "Join now" runs today's net_sync_now (failures shown); Cancel, or a tap anywhere else, closes */
    if (S.sheet == SH_SAVED && k == 0) shell_post(UI_SYNC_NOW, 0);
    close_sheet();
}

static void row_tap(int i)
{
    const omni_net_row_t *r = row_at(i);
    if (r == NULL) {
        if (i == nrows()) open_kb(NULL);
        else if (i == nrows() + 1) start_scan();
        return;
    }
    if (r->auth == OMNI_AUTH_UNSUPPORTED) return;   /* listed so it never seems to vanish; it does nothing */
    if (r->saved || r->auth == OMNI_AUTH_OPEN) {
        S.sheet = r->saved ? SH_SAVED : SH_OPEN;
        S.sheet_row = (int8_t)i;
        lv_obj_add_flag(S.ds.frame, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(S.ds.draw);
        return;
    }
    open_kb(r);
}

static void gesture(const omni_gesture_t *g, uint32_t t)
{
    if (S.sheet != SH_NONE) {
        if (g->kind == G_TAP) sheet_tap(g);
        return;
    }
    if (S.st == NL_EMPTY || S.st == NL_NOSTART) {
        if (g->kind != G_TAP) return;
        if (in_box(g, KEY_X, KEY_SCAN_Y, KEY_W, KEY_H)) start_scan();
        else if (in_box(g, KEY_X, KEY_PHONE_Y, KEY_W, KEY_H)) shell_post(UI_LIST_TO_PROV, 0);   /* B1: in the list's place */
        return;
    }
    if (S.st != NL_LIST) return;
    switch (drum_gesture(&S.d, g, t, RIM_R)) {   /* from the centre only: the rim only shows */
    case DRUM_G_MOVED: inval_drum(); break;
    case DRUM_G_WINDOW: row_tap(drum_cur(&S.d)); break;
    default: break;
    }
}

/* Input task, no LVGL lock: every drag here is the screen's (the drum). */
static bool claim(int16_t x0, int16_t y0, void *ctx)
{
    (void)x0, (void)y0, (void)ctx;
    return true;
}

/* ------------------------------------------------------------ screen interface */
/* destroy: covered by a pushed screen (the list still in the stack): keep what is in the window for the pop back. */
static void remember_row(void)
{
    s_restore = RS_NONE;
    if (S.st != NL_LIST || !omni_nav_in_stack(shell_nav(), SCR_NETLIST)) return;
    const int i = drum_cur(&S.d);
    const omni_net_row_t *r = row_at(i);
    if (r) {
        s_restore = RS_NET;
        s_restore_len = r->rec.ssid_len <= 32 ? r->rec.ssid_len : 32;
        memcpy(s_restore_ssid, r->rec.ssid, s_restore_len);
    } else {
        s_restore = i == nrows() ? RS_OTHER : RS_SCAN;
    }
}

/* create: the remembered row in the kept results (a network by its exact bytes; gone: row 0), or -1 (none: the saved
 * network's row). */
static int restore_row(void)
{
    if (s_restore == RS_OTHER) return nrows();
    if (s_restore == RS_SCAN) return nrows() + 1;
    if (s_restore != RS_NET) return -1;
    for (int i = 0; i < nrows(); i++) {
        const omni_net_row_t *r = row_at(i);
        if (r->rec.ssid_len == s_restore_len && memcmp(r->rec.ssid, s_restore_ssid, s_restore_len) == 0) return i;
    }
    return 0;
}

static void create(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    S.st = 0xff;
    S.t_prev = lv_tick_get();
    drum_screen_create(&S.ds, parent, draw_cb, anim_cb, claim, 0);
    S.busy = lv_timer_create(busy_cb, BUSY_MS, NULL);
    if (store_read_only()) set_state(NL_READONLY);   /* spec 7.1: Wi-Fi can't be changed */
    else if (cache() == NULL) set_state(NL_NOSTART);
    else if (!s_rescan && s_c->have && (int32_t)(now_ms() - s_c->t_ms) < KEEP_MS) {
        rebuild();   /* the saved mark as net has it now */
        show_results(restore_row());
    } else start_scan();
    s_rescan = false;
    s_restore = RS_NONE;
}

static void destroy(void)
{
    /* covered by a pushed screen (still in the stack): keep the windowed row for the pop back */
    remember_row();
    if (S.busy) lv_timer_delete(S.busy);
    drum_screen_destroy(&S.ds);
    memset(&S, 0, sizeof S);
}

static void dim(bool on) { drum_screen_dim(&S.ds, on); }

const omni_screen_vt_t SCR_NETLIST_VT = {
    .id = SCR_NETLIST, .create = create, .destroy = destroy, .gesture = gesture, .dim = dim,
};
