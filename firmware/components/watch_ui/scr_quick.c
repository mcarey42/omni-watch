/* SCR_QUICK: quick settings, pulled down from the top edge. No mockup (spec 13): drawn in the setup screens'
 * house style - the thin brass bezel, a Cormorant caption, Plex for reading, the settings drum's rim grammar.
 *
 *   status    battery, the saved network and how long ago the time was set (the spec's "status sheet" line)
 *   rim       brightness: the settings' 270-degree level dial; touch or turn the rim (r >= 168), applied live
 *   Sync now  a key; while the attempt runs it shows the net state (joining / setting the time) and the line
 *             under it says how it went (green when the time was set, red when it failed)
 *   12 h/24 h two keys, the current one lit
 * Swipe up or BOOT closes it. Changes go to the event loop as UI_SET_* / UI_SYNC_NOW (R26/R27). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "shell.h"
#include "theme.h"
#include "store.h"
#include "board.h"
#include "net.h"
#include "omni/timefmt.h"

#define CX THEME_CX
#define CY THEME_CY
#define RIM_R 168
#define SYNC_X 149
#define SYNC_Y 210
#define SYNC_W 168
#define KEY_H 46
#define FMT_Y 300
#define FMT_W 82
#define FMT_H 44
#define FMT_L_X (CX - 4 - FMT_W)
#define FMT_R_X (CX + 4)
#define POLL_MS 100
#define RESULT_S 30         /* the last sync result line stays this long */
#define DIM_OPA LV_OPA_40

static struct {
    lv_obj_t *root, *draw;
    lv_timer_t *poll;
    int brightness;
    bool h24;
    bool rim;
    omni_net_state_t net;   /* as last drawn */
    bool busy, mine;        /* an attempt runs; this screen started it */
    omni_net_state_t result;
    int64_t result_at;
    bool done;
    uint32_t changed_t;     /* the last change made here (lv_tick): until it is applied, keep showing it */
} S;

static bool in_box(const omni_gesture_t *g, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return g->x >= x && g->x < x + w && g->y >= y && g->y < y + h;
}

static void set_brightness(int v)
{
    if (v == S.brightness) return;
    theme_invalidate_level(S.draw, (S.brightness - 5) / 95.0f, (v - 5) / 95.0f);
    lv_area_t a = {CX - 60, 160, CX + 60, 196}; /* the value */
    lv_obj_invalidate_area(S.draw, &a);
    S.brightness = v;
    S.changed_t = lv_tick_get();
    shell_post(UI_SET_BRIGHTNESS, v);
}

static void rim_at(int16_t x, int16_t y)
{
    float v = 5 + 95 * theme_level_at(atan2f(y - CY, x - CX)); /* 5..100 % in 5 % steps */
    set_brightness((int)fminf(100, fmaxf(5, roundf(v / 5) * 5)));
}

static const char *sync_label(void)
{
    if (!net_is_provisioned()) return "Set up Wi-Fi";
    switch (S.net) {
    case NET_JOINING: return "Joining\xe2\x80\xa6";
    case NET_SYNCING: return "Setting time\xe2\x80\xa6";
    default: return S.busy ? "Busy\xe2\x80\xa6" : "Sync now";
    }
}

/* The line under the key: the attempt's progress or result, else when the time was last set. */
static void sync_line(char *out, size_t n, uint32_t *rgb, int64_t now)
{
    char ssid[33];
    net_ssid(ssid);
    *rgb = THEME_SETUP_DIM;
    if (S.busy && (S.net == NET_JOINING || S.net == NET_SYNCING)) {
        snprintf(out, n, S.net == NET_JOINING ? "joining %.20s" : "asking pool.ntp.org", ssid);
        return;
    }
    if (S.result_at && now - S.result_at < RESULT_S) {
        switch (S.result) {
        case NET_OK: *rgb = THEME_GREEN; snprintf(out, n, "time set \xe2\x9c\x93"); return;
        case NET_ERR_NO_AP: *rgb = THEME_RED; snprintf(out, n, "%.20s not in range", ssid); return;
        case NET_ERR_PASSWORD: *rgb = THEME_RED; snprintf(out, n, "password refused"); return;
        case NET_ERR_SYNC: *rgb = THEME_RED; snprintf(out, n, "time server did not answer"); return;
        case NET_ERR_NO_MEM: *rgb = THEME_RED; snprintf(out, n, "Wi-Fi did not start (memory)"); return;
        default: break;
        }
    }
    const omni_sync_t *sy = &shell_status()->sync;
    if (!net_is_provisioned()) snprintf(out, n, "no network saved");
    else if (sy->last_ok == 0) snprintf(out, n, "time never synced");
    else {
        char age[8];
        omni_fmt_age(now - sy->last_ok, age);
        snprintf(out, n, "synced %s%s", age, strcmp(age, "now") ? " ago" : "");
    }
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    theme_arc_text_frame();
    int64_t now = (int64_t)time(NULL);
    char buf[64], ssid[33];
    theme_draw_level(layer, (S.brightness - 5) / 95.0f, THEME_LEVEL_SUN);
    theme_draw_arc_text(layer, "TURN THE RIM", &font_plex_sans_500_9, 196, (float)M_PI / 2, THEME_SETUP_DIM, THEME_OPA(0.6), 2.5f);
    theme_draw_text(layer, "QUICK SETTINGS", &font_cormorant_700_15, THEME_BRASS, LV_OPA_COVER, 3, THEME_CENTER, CX, 92);

    /* status: battery, network, last sync */
    const shell_status_t *st = shell_status();
    net_ssid(ssid);
    char batt[24];
    if (st->battery_pct >= 0) snprintf(batt, sizeof batt, "%d%%%s", st->battery_pct, st->charging ? " charging" : "");
    else snprintf(batt, sizeof batt, "\xe2\x80\x94");
    snprintf(buf, sizeof buf, "%s \xc2\xb7 %.18s", batt, net_is_provisioned() && ssid[0] ? ssid : "no Wi-Fi");
    theme_draw_text(layer, buf, &font_plex_mono_400_11, THEME_SETUP_DIM, LV_OPA_COVER, 0, THEME_CENTER, CX, 118);

    /* brightness */
    theme_draw_text(layer, "BRIGHTNESS", &font_plex_sans_600_10, THEME_BRASS, LV_OPA_COVER, 3, THEME_CENTER, CX, 150);
    snprintf(buf, sizeof buf, "%d %%", S.brightness);
    theme_draw_text(layer, buf, &font_cormorant_700_30, THEME_SETUP_HEAD, LV_OPA_COVER, 0, THEME_CENTER, CX, 188);

    /* sync now */
    theme_draw_key(layer, SYNC_X, SYNC_Y, SYNC_W, KEY_H, S.busy ? THEME_KEY_ON : THEME_KEY_DARK);
    theme_draw_text(layer, sync_label(), &font_plex_sans_500_14, S.busy ? THEME_SETUP_BRASS_HI : THEME_INK, LV_OPA_COVER, 0,
                    THEME_CENTER, CX, SYNC_Y + 28);
    uint32_t rgb;
    sync_line(buf, sizeof buf, &rgb, now);
    theme_draw_text(layer, buf, &font_plex_mono_400_11, rgb, LV_OPA_COVER, 0, THEME_CENTER, CX, SYNC_Y + KEY_H + 22);

    /* 12 h / 24 h */
    theme_draw_key(layer, FMT_L_X, FMT_Y, FMT_W, FMT_H, S.h24 ? THEME_KEY_DARK : THEME_KEY_ON);
    theme_draw_key(layer, FMT_R_X, FMT_Y, FMT_W, FMT_H, S.h24 ? THEME_KEY_ON : THEME_KEY_DARK);
    theme_draw_text(layer, "12 h", &font_plex_sans_500_14, S.h24 ? THEME_SETUP_DIM : THEME_SETUP_BRASS_HI, LV_OPA_COVER, 0,
                    THEME_CENTER, FMT_L_X + FMT_W / 2, FMT_Y + 27);
    theme_draw_text(layer, "24 h", &font_plex_sans_500_14, S.h24 ? THEME_SETUP_BRASS_HI : THEME_SETUP_DIM, LV_OPA_COVER, 0,
                    THEME_CENTER, FMT_R_X + FMT_W / 2, FMT_Y + 27);

    theme_draw_text(layer, "BOOT \xe2\x80\xb9 back \xc2\xb7 swipe up", &font_plex_mono_400_10, THEME_SETUP_FAINT, LV_OPA_COVER, 0,
                    THEME_CENTER, CX, 380);
}

/* Follow the net state (net_state() is safe from any task) for the key and the line under it. */
static void poll_cb(lv_timer_t *t)
{
    (void)t;
    omni_net_state_t st = net_state();
    bool busy = net_busy() && !net_provisioning();
    if (st == S.net && busy == S.busy) return;
    if (S.mine && S.busy && !busy) { /* the attempt this screen started has ended */
        S.result = st;
        S.result_at = (int64_t)time(NULL);
        S.mine = false;
    }
    S.net = st;
    S.busy = busy;
    lv_area_t a = {SYNC_X - 40, SYNC_Y, SYNC_X + SYNC_W + 40, SYNC_Y + KEY_H + 30};
    lv_obj_invalidate_area(S.draw, &a);
}

static void gesture(const omni_gesture_t *g, uint32_t t)
{
    (void)t;
    if (S.done) return;
    int dx = g->x0 - CX, dy = g->y0 - CY;
    switch (g->kind) {
    case G_DRAG_START:
        S.rim = dx * dx + dy * dy >= RIM_R * RIM_R && theme_level_hit(atan2f(g->y0 - CY, g->x0 - CX), 0.15f);
        if (S.rim) rim_at(g->x, g->y);
        break;
    case G_DRAG_MOVE:
        if (S.rim) rim_at(g->x, g->y);
        break;
    case G_DRAG_END:
        if (S.rim) rim_at(g->x, g->y);
        S.rim = false;
        break;
    case G_SWIPE_UP:
        S.done = true;
        shell_post(UI_POP, 0);
        break;
    case G_TAP: {
        int tx = g->x - CX, ty = g->y - CY;
        if (tx * tx + ty * ty >= RIM_R * RIM_R) { /* on the dial only, not the bottom gap */
            if (theme_level_hit(atan2f(g->y - CY, g->x - CX), 0.15f)) rim_at(g->x, g->y);
        } else if (in_box(g, SYNC_X, SYNC_Y, SYNC_W, KEY_H)) {
            if (!net_is_provisioned()) {
                shell_post(UI_WIFI_SETUP, 0);
            } else if (!S.busy) {
                S.mine = true;
                S.result_at = 0;
                shell_post(UI_SYNC_NOW, 0);
            }
        } else if (in_box(g, FMT_L_X, FMT_Y, FMT_W, FMT_H) || in_box(g, FMT_R_X, FMT_Y, FMT_W, FMT_H)) {
            bool h24 = g->x >= FMT_R_X;
            if (h24 != S.h24) {
                S.h24 = h24;
                S.changed_t = lv_tick_get();
                shell_post(UI_SET_H24, h24);
                lv_obj_invalidate(S.draw);
            }
        }
        break;
    }
    default: break;
    }
}

/* Input task, no LVGL lock: drags that start on the rim are the brightness dial; the rest stay swipes. */
static bool claim(int16_t x0, int16_t y0, void *ctx)
{
    (void)ctx;
    int dx = x0 - CX, dy = y0 - CY;
    return dx * dx + dy * dy >= RIM_R * RIM_R;
}

static void create(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    S.brightness = settings()->brightness;
    S.h24 = settings()->h24;
    S.changed_t = lv_tick_get() - 10000;
    S.net = net_state();
    S.busy = net_busy() && !net_provisioning();
    S.root = theme_box(parent, 0, 0, THEME_W, THEME_W);
    theme_bezel(S.root);
    S.draw = theme_box(S.root, 0, 0, THEME_W, THEME_W);
    lv_obj_add_event_cb(S.draw, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    S.poll = lv_timer_create(poll_cb, POLL_MS, NULL);
    board_set_touch_claim(claim, NULL);
}

static void update(int64_t now)
{
    (void)now;
    int br = settings()->brightness;
    bool h24 = settings()->h24;
    if (!S.rim && lv_tick_get() - S.changed_t > 1000 && (br != S.brightness || h24 != S.h24)) { /* set elsewhere */
        S.brightness = br;
        S.h24 = h24;
        lv_obj_invalidate(S.draw);
    }
    lv_area_t a = {60, 100, THEME_W - 60, 130}; /* the status line (battery, network) */
    lv_obj_invalidate_area(S.draw, &a);
    lv_area_t b = {60, SYNC_Y + KEY_H, THEME_W - 60, SYNC_Y + KEY_H + 30}; /* "synced 2m ago" */
    lv_obj_invalidate_area(S.draw, &b);
}

static void destroy(void)
{
    board_set_touch_claim(NULL, NULL);
    if (S.poll) lv_timer_delete(S.poll);
    if (S.root) lv_obj_delete(S.root);
    memset(&S, 0, sizeof S);
}

void scr_quick_open(void) { shell_post(UI_OPEN_QUICK, 0); }

const omni_screen_vt_t SCR_QUICK_VT = {
    .id = SCR_QUICK, .create = create, .update = update, .destroy = destroy, .gesture = gesture,
};
