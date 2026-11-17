/* SCR_KEYBOARD: design/mockups/faces-setup.js `rim-keyboard` (setup-keyboard-loupe.png, setup-keyboard-typed.png); spec 6,
 * 7.2 and 8.4. The pure core is omni/kb.h (the layout, the ring's hit test, the buffer, masking, Delete's repeat, the idle
 * stages); this file draws and routes the recogniser's events (G_DOWN, G_HOLD and G_LIFT_STILL asked for in the claim).
 *
 * Three modes: the password of a network from the list, a hidden network's name (then its password, as WPA2 PSK, spec
 * 7.1) and the city search (its matches on the drum inside this screen, spec 8.4). Join hands the network and the
 * password to net_try_join and shows the result in place (spec 7.2's table, the provisioning step art); one attempt at a
 * time (Join waits for NET_EV_TRIAL's end, even across a re-open).
 *
 * The secret (spec 6.5): the password is typed into `edit` (omni_kb_edit_t, a static in internal RAM) and reaches net
 * only through net_try_join. It is wiped (omni_kb_wipe) on destroy (BOOT, sleep: the event loop pops this screen, the
 * O4 wake), the 60 s close, Done, Scan again, the OK view's close and a mode change; it is kept while the result view
 * is up so "Try again" can bring it back masked (spec 7.2, R224). Nothing here logs it; the field draws dots except the
 * last character for 0.9 s and the eye's reveal, from its own static buffer that LVGL draws in place (no heap copy) and
 * that is wiped with the buffer. Every opening, the dim, the glance and every wake start masked (scr_keyboard_mask);
 * `shot` refuses while the password field is open (scr_keyboard_secret_open). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scr_keyboard.h"
#include "scr_netlist.h"
#include "theme.h"
#include "drum.h"
#include "stepart.h"
#include "assets.h"
#include "fontasset.h"
#include "kb_meta.h"
#include "sfx.h"
#include "net.h"
#include "store.h"
#include "debug_console.h"
#include "omni/kb.h"
#include "omni/wipe.h"
#include "omni/join.h"
#include "omni/city_find.h"
#include "omni/cities.h"
#include "esp_heap_caps.h"

#define CX THEME_CX
#define CY THEME_CY
#define TAU (2 * (float)M_PI)
#define R0 OMNI_KB_BAND_IN    /* the band 171 .. 222 */
#define R1 OMNI_KB_BAND_OUT
#define LOUPE_R (OMNI_KB_RING_R - OMNI_KB_LOUPE_IN)
#define RIM_R 168              /* the city results: touches from here out are the rim */
#define DARK_INK 0x1f1606
#define CREAM THEME_SETUP_CREAM
#define LOUPE_INK 0xfff3d6
#define FIELD_X 101
#define FIELD_Y 144
#define FIELD_W 214
#define FIELD_H 52
#define FIELD_MAX 14           /* characters the field shows (the mockup's maxC) */
#define HINT_Y 348
#define RESULT_KEY_Y 316
#define DONE_MS 2000           /* "Joined · time set": back to the face */
#define WORK_FRAME_MS 48       /* the result's pulsing mark and comet */
#define Q 1024                 /* fixed point for the unit vectors */

enum { ST_EDIT, ST_RESULT, ST_CITY, ST_NOMATCH };
enum { K_BACK, K_EYE, K_SHIFT, K_MODE, K_SPACE, K_DEL, K_JOIN, K__N, K_NONE = -1 };
/* faces-setup.js KB.btn */
static const struct { int16_t x, y; uint8_t w, h, r; } KEY[K__N] = {
    [K_BACK] = {158, 86, 150, 44, 22}, [K_EYE] = {321, 148, 44, 44, 22}, [K_SHIFT] = {116, 212, 54, 48, 14},
    [K_MODE] = {176, 212, 54, 48, 14}, [K_SPACE] = {236, 212, 54, 48, 14}, [K_DEL] = {296, 212, 54, 48, 14},
    [K_JOIN] = {161, 274, 144, 50, 25},
};

/* The secret (spec 6.5): internal RAM. Named `edit` so check_apps' secret guard (edit.buf) sees every print of it. */
static omni_kb_edit_t edit;
static scr_kb_open_t s_open;          /* scr_keyboard_prepare: the next create */
static bool s_in_flight;              /* a trial this screen started, until NET_EV_TRIAL ends it (survives a re-open) */
static volatile bool s_secret;        /* open in password mode: the shot guard (any task reads it) */
/* The field's visible text (dots, the shown characters), internal RAM: LVGL draws it in place (theme_draw_text_ref), so
 * it changes only between refreshes (in draw_cb) and is wiped wherever edit is (wipe_secret). */
static char s_field[FIELD_MAX * 3 + 1];
/* theme_draw_text_ref keeps the pointer until the refresh is drawn: safe only while one software draw unit renders every
 * task of a band before draw_cb runs again (with more, a unit could still read s_field as the next band rebuilds it). */
_Static_assert(LV_DRAW_SW_DRAW_UNIT_CNT == 1, "s_field is drawn in place: one SW draw unit only");

/* The assets, loaded once (scr_keyboard_ready), and the ring's geometry per set, in PSRAM. */
typedef struct {
    omni_kb_layout_t lay;
    lv_font_t m21, m42;
    lv_font_fmt_txt_dsc_t d21, d42;
    lv_image_dsc_t icons;
    /* per key of the set shown: its glyph's centre at r 196, its direction (Q), and its separator's two ticks (r 174 .. 180
     * and r 213 .. 219) */
    struct { int16_t x, y, ux, uy, sep[8]; } key[OMNI_KB_SET_MAX];
    /* the fish-eye: the active key's 42 px glyph and its two neighbours' (A8), made in ring_to between refreshes */
    struct { uint32_t cp; int16_t dx, dy; lv_draw_buf_t buf; uint8_t *data; } big[3];
    uint32_t big_cap;
} kb_assets_t;
static kb_assets_t *A;
static int8_t s_ready = -1;

static struct {
    drum_screen_t ds;                 /* root, draw, frame (the city results only), anim */
    lv_timer_t *done;
    uint8_t st, set, n;
    int8_t press, idx;                /* the centre key held; the ring key under the finger (OMNI_KB_CANCEL: inside) */
    bool ring, dimmed, closed, caret, showing, had_saved;
    uint8_t rep;                      /* Delete's repeats in this press */
    int16_t ux, uy;                   /* the loupe's direction (Q): the key's, or the finger's while cancelling */
    uint32_t t_touch, t_loupe, t_shake, t_ring, t_res, t_frame;
    uint8_t ssid[32], ssid_len, authmode;
    omni_net_state_t res;
    const char *msg;                  /* the hint's refusal line (NULL: the count) */
    drum_t d;
    uint16_t city[OMNI_CITY_FIND_MAX];
    int ncity;
    char txt[THEME_NAME_BUF + 8];     /* the painters' text: off the LVGL task's stack */
} S;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float ease_out(float t) { t = clampf(t, 0, 1); return 1 - (1 - t) * (1 - t) * (1 - t); }
static bool password(void) { return edit.mode == KB_MODE_PASSWORD; }

/* ------------------------------------------------------------ assets */
bool scr_keyboard_ready(void)
{
    if (s_ready >= 0) return s_ready;
    s_ready = 0;
    A = heap_caps_calloc(1, sizeof *A, MALLOC_CAP_SPIRAM);
    size_t n = 0;
    const void *lay = assets_raw(KB_ASSET_LAY, &n);
    if (A == NULL || lay == NULL || !omni_kb_layout_load(&A->lay, lay, n) ||
        !fontasset_load(&A->m21, &A->d21, KB_ASSET_MONO21) || !fontasset_load(&A->m42, &A->d42, KB_ASSET_MONO42) ||
        !assets_image(KB_ASSET_ICONS, &A->icons))
        return false;   /* A stays: it is never used without s_ready */
    A->big_cap = lv_draw_buf_width_to_stride(A->m42.line_height, LV_COLOR_FORMAT_A8) * A->m42.line_height;
    for (int k = 0; k < 3; k++)
        if ((A->big[k].data = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, A->big_cap, MALLOC_CAP_SPIRAM)) == NULL) return false;
    s_ready = 1;
    return true;
}

/* The ring for the set shown: each key's direction once per set change, never per draw. */
static void ring_set(bool anim)
{
    S.set = omni_kb_set(&edit);
    S.n = A->lay.n[S.set];
    static const uint8_t SEP_R[4] = {R0 + 3, R0 + 9, R1 - 3, R1 - 9};
    for (int i = 0; i < S.n; i++) {
        const float a = omni_kb_key_angle(i, S.n) - (float)M_PI / 2, s = a + (float)M_PI / S.n;
        const float c = cosf(a), sn = sinf(a), sc = cosf(s), ss = sinf(s);
        A->key[i].x = (int16_t)(CX + lroundf(c * OMNI_KB_RING_R)), A->key[i].y = (int16_t)(CY + lroundf(sn * OMNI_KB_RING_R));
        A->key[i].ux = (int16_t)lroundf(c * Q), A->key[i].uy = (int16_t)lroundf(sn * Q);
        for (int k = 0; k < 4; k++)
            A->key[i].sep[2 * k] = (int16_t)(CX + lroundf(sc * SEP_R[k])), A->key[i].sep[2 * k + 1] = (int16_t)(CY + lroundf(ss * SEP_R[k]));
    }
    for (int k = 0; k < 3; k++) A->big[k].cp = 0;   /* another set: the fish-eye is made again */
    if (anim) S.t_ring = lv_tick_get();
    if (S.ds.draw) lv_obj_invalidate(S.ds.draw);
}

/* The secret and every copy of it this screen keeps, wiped (spec 6.5) */
static void wipe_secret(void)
{
    omni_kb_wipe(&edit);
    omni_wipe(s_field, sizeof s_field);
}

/* ------------------------------------------------------------ drawing (spec 11: flat, noinline painters) */
#define PAINTER static __attribute__((noinline)) void

static void inval(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_area_t a = {x1, y1, x2, y2};
    lv_obj_invalidate_area(S.ds.draw, &a);
}
static void inval_key(int k)
{
    if (k >= 0) inval(KEY[k].x - 1, KEY[k].y - 1, KEY[k].x + KEY[k].w + 1, KEY[k].y + KEY[k].h + 1);
}
static void inval_field(void) { inval(FIELD_X - 10, FIELD_Y - 2, KEY[K_EYE].x + KEY[K_EYE].w + 1, FIELD_Y + FIELD_H + 2); }
static void inval_hint(void) { inval(CX - 140, HINT_Y - 12, CX + 140, HINT_Y + 4); }
static int32_t at(int32_t c, int32_t u, int32_t r) { return c + u * r / Q; }
/* key i's segment with its fish-eye neighbours and the red notch (i >= 0), and the loupe where S.ux, S.uy put it */
static void inval_ring_key(int i)
{
    if (i >= 0) inval(A->key[i].x - 100, A->key[i].y - 100, A->key[i].x + 100, A->key[i].y + 100);
    const int32_t lx = at(CX, S.ux, LOUPE_R), ly = at(CY, S.uy, LOUPE_R);
    inval(lx - 72, ly - 72, lx + 72, ly + 72);
}

/* A dark centre key (key()): fill, 1 px brass hairline .38 (.85 when on), radius r; pressed #2a2317. */
static void dark_key(lv_layer_t *layer, int k, bool on)
{
    theme_draw_rect(layer, KEY[k].x, KEY[k].y, KEY[k].x + KEY[k].w - 1, KEY[k].y + KEY[k].h - 1, KEY[k].r,
                    S.press == k ? 0x2a2317 : on ? THEME_SETUP_KEY_ON : THEME_SETUP_KEY, LV_OPA_COVER, 1, THEME_BRASS,
                    on ? THEME_OPA(0.85) : THEME_OPA(0.38));
}

/* kb_icons.bin's cell k centred on (x, y), tinted: the strip drawn under a clip to the cell. */
static void icon(lv_layer_t *layer, int k, int32_t x, int32_t y, uint32_t rgb)
{
    const lv_area_t cell = {x - KB_ICON_CELL / 2, y - KB_ICON_CELL / 2, x + KB_ICON_CELL / 2 - 1, y + KB_ICON_CELL / 2 - 1};
    const lv_area_t keep = layer->_clip_area;
    const lv_area_t clip = {LV_MAX(keep.x1, cell.x1), LV_MAX(keep.y1, cell.y1), LV_MIN(keep.x2, cell.x2), LV_MIN(keep.y2, cell.y2)};
    if (clip.x1 > clip.x2 || clip.y1 > clip.y2) return;
    layer->_clip_area = clip;   /* the draw task copies the clip */
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &A->icons;
    d.recolor = lv_color_hex(rgb);
    d.recolor_opa = LV_OPA_COVER;
    const lv_area_t a = {cell.x1 - k * KB_ICON_CELL, cell.y1, cell.x1 - k * KB_ICON_CELL + KB_ICONS_W - 1, cell.y2};
    lv_draw_image(layer, &d, &a);
    layer->_clip_area = keep;
}

/* Text with its em middle at y (canvas textBaseline 'middle'). */
static void text_mid(lv_layer_t *layer, const char *s, const lv_font_t *f, uint32_t rgb, lv_opa_t opa, int32_t x, int32_t y)
{
    theme_draw_text(layer, s, f, rgb, opa, 0, THEME_CENTER, x, y + (f->line_height - 2 * f->base_line) / 2);
}

/* The fish-eye (the mockup's 21 px x s, s up to 1.32): slot k's 42 px glyph (made by make_big) drawn by LVGL scaled
 * q / 64 about its em middle, which sits at (x, y), recoloured. */
static void big(lv_layer_t *layer, int k, uint8_t q, int32_t x, int32_t y, uint32_t rgb, lv_opa_t opa)
{
    const int32_t m = A->m42.line_height * q / 64 + 2;   /* the scaled glyph lies within m of (x, y) */
    if (A->big[k].cp == 0 || !theme_on_layer(layer, x - m, y - m, x + m, y + m)) return;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &A->big[k].buf;
    d.recolor = lv_color_hex(rgb);
    d.opa = opa;
    d.scale_x = d.scale_y = q * 4;   /* 256: 1 */
    d.pivot.x = -A->big[k].dx, d.pivot.y = -A->big[k].dy;
    const lv_area_t a = {x + A->big[k].dx, y + A->big[k].dy, x + A->big[k].dx + (int32_t)A->big[k].buf.header.w - 1,
                         y + A->big[k].dy + (int32_t)A->big[k].buf.header.h - 1};
    lv_draw_image(layer, &d, &a);
}

static char key_char(int i) { return i >= 0 && i < S.n ? A->lay.keys[S.set][i] : ' '; }

/* Slots 0..2: keys i - 1, i, i + 1 as A8 glyphs of the 42 px font, kept while they stay the same. Called on the input
 * path with the LVGL lock held, so never under a refresh that reads them. */
static void make_big(int i)
{
    const lv_font_t *f = &A->m42;
    for (int k = 0; k < 3; k++) {
        const uint32_t cp = (uint8_t)key_char((i + k - 1 + S.n) % S.n);
        if (A->big[k].cp == cp) continue;
        A->big[k].cp = 0;
        lv_font_glyph_dsc_t g;
        if (!lv_font_get_glyph_dsc(f, &g, cp, 0) || g.box_w == 0 || g.box_h == 0) continue;
        const uint32_t st = lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8);
        if (st * g.box_h > A->big_cap) continue;
        lv_draw_buf_init(&A->big[k].buf, g.box_w, g.box_h, LV_COLOR_FORMAT_A8, st, A->big[k].data, st * g.box_h);
        g.req_raw_bitmap = 0;
        if (lv_font_get_glyph_bitmap(&g, &A->big[k].buf) == NULL) continue;
        A->big[k].dx = (int16_t)(g.ofs_x - g.adv_w / 2), A->big[k].dy = (int16_t)((f->line_height - 2 * f->base_line) / 2 - g.box_h - g.ofs_y);
        A->big[k].cp = cp;
    }
}

/* The ring: the active segment (brass, the red notch), the separators, and each key at r 196 with the fish-eye of its
 * neighbours; after a set change the keys fade in from 10 px out over 260 ms. */
PAINTER ring_view(lv_layer_t *layer, uint32_t now)
{
    const bool act = S.ring && S.idx >= 0;
    const float step = TAU / S.n, in = clampf((float)(now - S.t_ring) / 260, 0, 1), ring_in = in * in * (3 - 2 * in);
    if (act) {
        const float a = omni_kb_key_angle(S.idx, S.n) - (float)M_PI / 2;
        for (int b = 0; b < 5; b++) {   /* radial #a07b38 -> #e8cb88 -> #a07b38 in five bands */
            static const uint32_t C[5] = {0xa98543, 0xcbab68, 0xe8cb88, 0xcbab68, 0xa98543};
            theme_draw_arc(layer, CX, CY, R1 - 1 - b * 10, b == 4 ? 9 : 10, theme_arc_deg(a - step / 2), theme_arc_deg(a + step / 2), C[b],
                           LV_OPA_COVER, false);
        }
        theme_draw_red_index(layer, a);
    }
    const int32_t out = (int32_t)((1 - ring_in) * 256);
    const lv_opa_t opa = (lv_opa_t)(ring_in * 255);
    for (int i = 0; i < S.n; i++) {
        const int16_t *p = A->key[i].sep;
        for (int k = 0; k < 8; k += 4) theme_draw_line(layer, p[k], p[k + 1], p[k + 2], p[k + 3], 1, THEME_BRASS, THEME_OPA(0.28), false);
        int d = act ? (i - S.idx + S.n) % S.n : 99;   /* signed: -1, 0, +1 are the fish-eye */
        if (d > S.n / 2 && d != 99) d -= S.n;
        const int32_t x = at(A->key[i].x, A->key[i].ux, 10 * out / 256), y = at(A->key[i].y, A->key[i].uy, 10 * out / 256);
        const char ch[2] = {key_char(i), 0};
        if (d >= -1 && d <= 1) {   /* s = 1 + 0.32 exp(-d^2 / 1.2): 1.32 and 1.14 of 21 px, from the 42 px font: q = 32 s */
            if (d) big(layer, d + 1, 36, x, y + 1, 0x000000, (lv_opa_t)(opa * 0.6f));   /* the cream ones' shadow */
            big(layer, d + 1, d ? 36 : 42, x, y, d ? CREAM : 0x1b1206, opa);
            continue;
        }
        if (!theme_on_layer(layer, x - 16, y - 18, x + 16, y + 18)) continue;
        text_mid(layer, ch, &A->m21, 0x000000, (lv_opa_t)(opa * 0.6f), x, y + 1);   /* the shadow */
        text_mid(layer, ch, &A->m21, CREAM, opa, x, y);
    }
}

/* The loupe 85 px inward along the key's radius: the bezel (vertical brass gradient), the glass, the key at 42 px with its
 * neighbours (or the red cross when the finger is inside r 131), opening over 140 ms. */
PAINTER loupe_view(lv_layer_t *layer, uint32_t now)
{
    const bool cancel = S.idx < 0;
    const float lk = ease_out((float)(now - S.t_loupe) / 140);
    const int32_t R = (int32_t)(40 * (0.6f + 0.4f * lk) + 0.5f), ux = S.ux, uy = S.uy;
    const int32_t lx = at(CX, ux, LOUPE_R), ly = at(CY, uy, LOUPE_R);
    const lv_opa_t opa = (lv_opa_t)(lk * 255);
    if (!theme_on_layer(layer, lx - R - 30, ly - R - 30, lx + R + 30, ly + R + 30)) return;
    lv_draw_triangle_dsc_t t;   /* the pointer tail toward the finger: the base +-0.5 rad round the loupe (cos 899, sin 491) */
    lv_draw_triangle_dsc_init(&t);
    t.color = lv_color_hex(0x8d6c34);
    t.opa = opa;
    t.p[0].x = at(CX, ux, LOUPE_R + R + 14), t.p[0].y = at(CY, uy, LOUPE_R + R + 14);
    t.p[1].x = at(lx, (ux * 899 - uy * 491) / Q, R + 2), t.p[1].y = at(ly, (ux * 491 + uy * 899) / Q, R + 2);
    t.p[2].x = at(lx, (ux * 899 + uy * 491) / Q, R + 2), t.p[2].y = at(ly, (uy * 899 - ux * 491) / Q, R + 2);
    lv_draw_triangle(layer, &t);
    theme_draw_box(layer, lx - R - 10, ly - R - 8, lx + R + 10, ly + R + 12, LV_RADIUS_CIRCLE, 0x000000,
                   (lv_opa_t)(opa * 0.35f), 0, 0);   /* the shadow */
    theme_draw_grad(layer, lx - R - 6, ly - R - 6, lx + R + 6, ly + R + 6, LV_RADIUS_CIRCLE, true, 0xf0d595, LV_OPA_COVER,
                    0x3f2d14, LV_OPA_COVER, opa);   /* the bezel */
    theme_draw_box(layer, lx - R, ly - R, lx + R, ly + R, LV_RADIUS_CIRCLE, 0x110e0a, opa, 0, 0);   /* the glass */
    if (cancel) {
        theme_draw_line(layer, lx - 9, ly - 13, lx + 9, ly + 5, 3, THEME_RED, opa, true);
        theme_draw_line(layer, lx + 9, ly - 13, lx - 9, ly + 5, 3, THEME_RED, opa, true);
        text_mid(layer, "CANCEL", &font_plex_sans_500_9, THEME_SETUP_DIM, opa, lx, ly + 22);
    } else {
        for (int k = -1; k <= 1; k++) {   /* the neighbours at +-27 px, .38; the key at 42 px */
            const char ch[2] = {key_char((S.idx + k + S.n) % S.n), 0};
            text_mid(layer, ch, k ? &font_plex_mono_500_13 : &A->m42, k ? CREAM : LOUPE_INK, k ? (lv_opa_t)(opa * 0.38f) : opa,
                     lx + 27 * k, ly + (k ? 2 : 1));
        }
    }
    theme_draw_box(layer, lx - R * 85 / 100, ly - R * 73 / 100, lx + R * 35 / 100, ly - R * 17 / 100, LV_RADIUS_CIRCLE, 0xffffff,
                   (lv_opa_t)(opa * 0.07f), 0, 0);   /* the glass glint */
}

static const char *set_name(void)
{
    static const char *const N[] = {"abc", "ABC", "123", "#+="};
    return S.set == 1 && edit.shift == KB_SHIFT_LOCK ? "ABC LOCK" : N[S.set];
}

/* The field's text: the last FIELD_MAX characters (FIELD_MAX - 1 after a cut), dots where masked; into s_field. Returns
 * the count shown. */
static int field_text(uint32_t now)
{
    const int from = edit.len > FIELD_MAX ? edit.len - (FIELD_MAX - 1) : 0;
    int k = 0;
    for (int i = from; i < edit.len; i++) {
        if (!password() || omni_kb_shows(&edit, (uint8_t)i, now)) s_field[k++] = edit.buf[i];
        else k += sprintf(s_field + k, "\xe2\x80\xa2");
    }
    s_field[k] = 0;
    return edit.len - from;
}

/* The centre: caption, back pill, field (with its shake, caret, cut fade and the eye), the four keys, Join, the hint. */
PAINTER centre_view(lv_layer_t *layer, uint32_t now)
{
    static const char *const CAP[] = {"PASSWORD", "NETWORK NAME", "FIND CITY"};
    theme_draw_text(layer, CAP[edit.mode], &font_plex_sans_500_9, THEME_SETUP_FAINT, LV_OPA_COVER, 3, THEME_CENTER, CX, 76);
    dark_key(layer, K_BACK, false);
    theme_draw_text(layer, "\xe2\x80\xb9", &font_plex_sans_500_17, THEME_BRASS, LV_OPA_COVER, 0, THEME_CENTER, KEY[K_BACK].x + 22,
                    KEY[K_BACK].y + 29);
    if (password()) theme_fit_name(S.txt, S.ssid, S.ssid_len, &font_plex_sans_500_14, KEY[K_BACK].w - 48);
    else strcpy(S.txt, edit.mode == KB_MODE_SSID ? "Networks" : "Settings");
    theme_draw_text(layer, S.txt, &font_plex_sans_500_14, THEME_INK, LV_OPA_COVER, 0, THEME_CENTER, KEY[K_BACK].x + 83,
                    KEY[K_BACK].y + 27);

    const float st = (float)(now - S.t_shake);
    const int32_t sh = st < 380 ? lroundf(sinf(st / 22) * 7 * (1 - st / 380)) : 0, fx = FIELD_X + sh;
    theme_draw_rect(layer, fx, FIELD_Y, fx + FIELD_W - 1, FIELD_Y + FIELD_H - 1, 12, 0x080706, LV_OPA_COVER, 1,
                    sh ? THEME_RED : THEME_BRASS, sh ? LV_OPA_COVER : THEME_OPA(0.55));
    int32_t caret_x = fx + FIELD_W / 2 + 38;
    if (edit.len == 0) {
        static const char *const PH[] = {"password", "network name", "city or country"};
        theme_draw_text(layer, PH[edit.mode], &font_plex_sans_400_14, THEME_SETUP_FAINT, LV_OPA_COVER, 0, THEME_CENTER,
                        fx + FIELD_W / 2, FIELD_Y + 32);
    } else {
        const int n = field_text(now);
        const int32_t tw = n * lv_font_get_glyph_width(&A->m21, '0', 0);
        const bool cut = edit.len > FIELD_MAX;
        const int32_t tx = cut ? fx + FIELD_W - 18 - tw : CX - tw / 2;
        theme_draw_text_ref(layer, s_field, &A->m21, THEME_INK, tx, FIELD_Y + 34);   /* in place: no copy in LVGL's heap */
        if (cut)   /* the fade at the left edge */
            theme_draw_grad(layer, fx + 1, FIELD_Y + 1, fx + 34, FIELD_Y + FIELD_H - 2, 0, false, 0x080706, LV_OPA_COVER, 0x080706,
                            LV_OPA_TRANSP, LV_OPA_COVER);
        caret_x = tx + tw + 2;
    }
    if (S.caret) theme_draw_box(layer, caret_x, FIELD_Y + 15, caret_x + 1, FIELD_Y + 37, 0, THEME_BRASS, LV_OPA_COVER, 0, 0);
    /* the eye (password mode) and the four keys: an icon cell, or a label in Plex Mono 500 15 */
    static const int8_t ICON_DY[K__N] = {[K_SHIFT] = -1, [K_SPACE] = 1};
    const bool letters = S.set < 2;
    for (int k = password() ? K_EYE : K_SHIFT; k <= K_DEL; k++) {
        int ic = -1;
        const char *lab = NULL;
        uint32_t rgb = THEME_INK;
        bool on = false;
        switch (k) {
        case K_EYE: on = edit.reveal, ic = on ? KB_ICON_EYE_OPEN : KB_ICON_EYE_SHUT, rgb = on ? THEME_SETUP_BRASS_HI : THEME_SETUP_DIM; break;
        case K_SHIFT:
            if (!letters) lab = S.set == 2 ? "#+=" : "123";
            else on = edit.shift != KB_SHIFT_OFF, ic = KB_ICON_SHIFT_OFF + edit.shift, rgb = on ? THEME_SETUP_BRASS_HI : THEME_INK;
            break;
        case K_MODE: lab = letters ? "123" : "abc"; break;
        case K_SPACE: ic = KB_ICON_SPACE; break;
        default: ic = KB_ICON_DELETE; break;
        }
        dark_key(layer, k, on);
        const int32_t x = KEY[k].x + KEY[k].w / 2, y = KEY[k].y + KEY[k].h / 2;
        if (lab) theme_draw_text(layer, lab, &font_plex_mono_500_15, THEME_INK, LV_OPA_COVER, 0, THEME_CENTER, x, y + 5);
        else icon(layer, ic, x, y + ICON_DY[k], rgb);
    }

    theme_draw_key(layer, KEY[K_JOIN].x, KEY[K_JOIN].y, KEY[K_JOIN].w, KEY[K_JOIN].h, THEME_KEY_PRIMARY);
    static const char *const GO[] = {"Join", "Next", "Search"};
    theme_draw_text(layer, GO[edit.mode], &font_cormorant_700_20, DARK_INK, LV_OPA_COVER, 0, THEME_CENTER, CX, KEY[K_JOIN].y + 32);
    if (S.press == K_JOIN || (password() && s_in_flight && net_busy()))   /* held, or an attempt still runs: Join waits */
        theme_draw_box(layer, KEY[K_JOIN].x, KEY[K_JOIN].y, KEY[K_JOIN].x + KEY[K_JOIN].w - 1, KEY[K_JOIN].y + KEY[K_JOIN].h - 1,
                       KEY[K_JOIN].r, 0x000000, THEME_OPA(0.35), 0, 0);

    snprintf(S.txt, sizeof S.txt, edit.len ? "%u character%s \xc2\xb7 %s" : "tap or drag the rim%.0u%.0s \xc2\xb7 %s", edit.len,
             edit.len == 1 ? "" : "s", set_name());
    theme_draw_text(layer, S.msg ? S.msg : S.txt, &font_plex_mono_400_11, S.msg ? THEME_INK_DIM : THEME_SETUP_FAINT, LV_OPA_COVER,
                    0, THEME_CENTER, CX, HINT_Y);
}

/* spec 7.2's table: the heading, the network, the saved network's line, and the keys */
static __attribute__((noinline)) void result_sheet(theme_sheet_t *sh)
{
    stepart_copy_t c;
    stepart_copy(S.res, true, S.had_saved, &c);   /* line2: unchanged, or nothing saved (a trial never drops it) */
    const char *head = "Joining", *k0 = NULL, *k1 = "Done";
    uint32_t rgb = THEME_SETUP_HEAD;
    switch (S.res) {
    case NET_JOINING: k0 = "Cancel", k1 = NULL; break;
    case NET_SYNCING: head = "Setting the time", k1 = NULL; break;
    case NET_OK: head = "Joined \xc2\xb7 time set \xe2\x9c\x93", rgb = THEME_GREEN, k1 = NULL; break;
    case NET_ERR_PASSWORD: head = "Wrong password", k0 = "Try again"; break;
    case NET_ERR_NO_AP: head = "Can't find", k0 = "Scan again"; break;
    case NET_ERR_SYNC: head = "Joined \xc2\xb7 time not set yet", c.line2 = "The new network is saved", k0 = "Done", k1 = NULL; break;
    default: head = "Wi-Fi couldn't start \xc2\xb7 try again", k0 = "Try again"; break;   /* NET_ERR_NO_MEM */
    }
    const lv_font_t *hf = theme_text_width(head, &font_cormorant_700_30, 0) <= 300 ? &font_cormorant_700_30
                        : theme_text_width(head, &font_cormorant_700_26, 0) <= 300 ? &font_cormorant_700_26
                                                                                     : &font_cormorant_700_20;
    *sh = (theme_sheet_t){
        .line = {{head, hf, rgb, 238, 0}, {S.txt, &font_plex_sans_500_16, THEME_INK, 266, 0},
                 {c.line2, &font_plex_sans_400_13, THEME_INK_DIM, 290, 0}},
        .key = {k0, k1},
        .style = {S.res == NET_JOINING || S.res == NET_ERR_SYNC ? THEME_KEY_DARK : THEME_KEY_PRIMARY, THEME_KEY_DARK},
        .key_y = k0 ? RESULT_KEY_Y : 0,
    };
}

/* The join's result in place (spec 7.2): the progress track and comet while working, the step art, the sheet. */
PAINTER result_view(lv_layer_t *layer, uint32_t now)
{
    const omni_net_state_t st = S.res;
    const bool work = st == NET_JOINING || st == NET_SYNCING;
    if (work) {
        theme_draw_arc(layer, CX, CY, STEPART_RING_R + 1, 3, 0, 360, THEME_SETUP_TRACK, LV_OPA_COVER, false);
        stepart_ring(layer, now, THEME_SETUP_BRASS_HI);
        stepart_wifi(layer, CX, 160, THEME_SETUP_BRASS_HI, true, now);
    } else if (st == NET_OK) {
        stepart_check(layer, CX, 136, clampf((float)(now - S.t_res) / 900, 0, 1));
    } else if (st == NET_ERR_SYNC) {
        stepart_wifi(layer, CX, 160, THEME_BRASS, false, 0);
    } else {
        stepart_wifi(layer, CX, 160, THEME_SETUP_FAINT, false, 0);
        theme_draw_line(layer, CX - 22, 150, CX + 22, 190, 3, THEME_RED, LV_OPA_COVER, true);
    }
    char q[THEME_NAME_BUF];
    theme_fit_name(q, S.ssid, S.ssid_len, &font_plex_sans_500_16, 260);
    snprintf(S.txt, sizeof S.txt, "\xe2\x80\x9c%s\xe2\x80\x9d", q);
    theme_sheet_t sh;
    result_sheet(&sh);
    theme_draw_sheet(layer, &sh);
}

/* The city search's matches on the drum (spec 8.4): the window row as Settings' Home city (the name, the country in
 * tracked caps), the rest dimmed. */
static void city_row(lv_layer_t *layer, int i, int32_t y, lv_opa_t opa, bool in_window, void *ctx)
{
    (void)ctx;
    const omni_city_t *c = i >= 0 && i < S.ncity ? omni_city_at(S.city[i]) : NULL;
    if (c == NULL) return;
    if (!in_window) {
        theme_draw_text(layer, c->name, drum_dim_font(16 * drum_scale(&S.d, i)), THEME_SETUP_DIM, opa, 0, THEME_CENTER, CX, y + 5);
        return;
    }
    theme_draw_text(layer, c->name, &font_plex_sans_500_17, THEME_INK, opa, 0, THEME_LEFT, DRUM_WIN_X + 20, y - 1);
    snprintf(S.txt, sizeof S.txt, "%s", omni_country_name(c->country));
    for (char *p = S.txt; *p; p++) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    theme_draw_text(layer, S.txt, &font_plex_sans_600_10, THEME_BRASS, opa, 2, THEME_LEFT, DRUM_WIN_X + 20, y + 15);
}

PAINTER city_view(lv_layer_t *layer)
{
    theme_draw_text(layer, "FIND CITY", &font_cormorant_700_15, THEME_BRASS, LV_OPA_COVER, 1, THEME_CENTER, CX, 92);
    if (S.st == ST_NOMATCH) {
        /* the search text (city mode: not a secret), copied rather than printed so the secret guard stays lexical */
        const int k = snprintf(S.txt, sizeof S.txt, "No city starts with '");
        memcpy(S.txt + k, edit.buf, edit.len);
        strcpy(S.txt + k + edit.len, "'");
        const theme_sheet_t sh = {
            .line = {{S.txt, &font_plex_sans_500_16, THEME_INK, 200, 0},
                     {"Try fewer letters, or a country", &font_plex_sans_400_13, THEME_SETUP_DIM, 226, 0}},
            .key = {"Edit"}, .style = {THEME_KEY_DARK}, .key_y = 256,
        };
        theme_draw_sheet(layer, &sh);
        return;
    }
    theme_draw_arc_text(layer, "TAP TO SET \xc2\xb7 THE RIM TO EDIT", &font_plex_sans_500_9, 196, (float)M_PI / 2, THEME_SETUP_DIM,
                        THEME_OPA(0.6), 2.5f);
    theme_draw_box(layer, DRUM_WIN_X, DRUM_WIN_Y, DRUM_WIN_X + DRUM_WIN_W - 1, DRUM_WIN_Y + DRUM_WIN_H - 1, 12, THEME_SETUP_GLASS,
                   LV_OPA_COVER, 0, 0);
    drum_draw(&S.d, layer, city_row, NULL);
}

static void draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    const uint32_t now = lv_tick_get();
    theme_arc_text_frame();
    if (S.st == ST_EDIT) {
        ring_view(layer, now);
        centre_view(layer, now);
        if (S.ring) loupe_view(layer, now);
    } else if (S.st == ST_RESULT) {
        result_view(layer, now);
    } else {
        city_view(layer);
    }
    if (S.st >= ST_CITY)
        theme_draw_text(layer, "BOOT \xe2\x80\xb9 back", &font_plex_mono_400_10, THEME_SETUP_FAINT, LV_OPA_COVER, 0, THEME_CENTER, CX, 405);
    /* console builds: the LVGL task's stack after this draw */
    ui_stack_note(S.st == ST_RESULT ? "kb/result" : S.st != ST_EDIT ? "kb/city" : password() ? "kb/password"
                  : edit.mode == KB_MODE_SSID ? "kb/ssid" : "kb/find");
}

/* ------------------------------------------------------------ states */
static void set_st(int st)
{
    S.st = (uint8_t)st;
    S.ring = false, S.press = K_NONE;
    if (st == ST_CITY) lv_obj_remove_flag(S.ds.frame, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(S.ds.frame, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(S.ds.draw);
}

static void shake(const char *msg)
{
    S.t_shake = lv_tick_get();
    S.msg = msg;
    inval_field();
    inval_hint();
}

/* Back to typing with the text kept and masked (Try again, a cancelled attempt, Edit) */
static void to_edit(void)
{
    edit.reveal = false;
    S.msg = NULL;
    set_st(ST_EDIT);
}

/* Leaving: the buffer wiped, then the event loop pops (back to the list, or Settings) or goes home. */
static void leave(int ui)
{
    wipe_secret();
    shell_post(ui, 0);
}

static void done_cb(lv_timer_t *t)
{
    (void)t;
    S.done = NULL;
    leave(UI_HOME);
}

static void show_result(omni_net_state_t st)
{
    S.res = st;
    S.t_res = lv_tick_get();
    set_st(ST_RESULT);
    if (st == NET_OK && S.done == NULL) {
        S.done = lv_timer_create(done_cb, DONE_MS, NULL);
        lv_timer_set_repeat_count(S.done, 1);
    }
}

void scr_keyboard_net(int ev)
{
    const omni_net_state_t st = NET_EV_STATE(ev);
    if (st != NET_JOINING && st != NET_SYNCING) {   /* the attempt ended: Join is free again (drawn so on a re-open) */
        s_in_flight = false;
        if (S.ds.draw) inval_key(K_JOIN);
    }
    if (S.ds.root == NULL || S.st != ST_RESULT) return;   /* left meanwhile: shown at the next look (B2) */
    if (st == NET_IDLE) to_edit();   /* cancelled: the text is still there */
    else show_result(st);
}

/* Join / Next / Search (spec 6.5, 7.1, 7.2, 8.4). Returns NULL, or why it shook (the hint's line; "" the count). */
static const char *go_why(void)
{
    if (edit.len == 0 && (edit.mode != KB_MODE_PASSWORD || omni_netlist_auth(S.authmode) != OMNI_AUTH_OPEN)) return "";
    if (edit.mode == KB_MODE_CITY) {
        S.ncity = omni_city_find_all(edit.buf, S.city);
        drum_init(&S.d, S.ncity, 0);
        set_st(S.ncity ? ST_CITY : ST_NOMATCH);
        return NULL;
    }
    if (edit.mode == KB_MODE_SSID) {   /* the name typed: then its password, as WPA2 PSK */
        S.ssid_len = edit.len;
        memcpy(S.ssid, edit.buf, edit.len);
        S.authmode = OMNI_WA_WPA2_PSK;
        wipe_secret();
        omni_kb_open(&edit, KB_MODE_PASSWORD);
        s_secret = true;
        S.msg = NULL;
        ring_set(true);
        return NULL;
    }
    /* one attempt at a time: until NET_EV_TRIAL ends it (net_busy too, so a dropped event cannot hold Join for good) */
    if (s_in_flight && net_busy()) return "Joining \xc2\xb7 wait for the result";
    static const char *const WHY[] = {[OMNI_JOIN_REFUSE_READ_ONLY] = "Read-only this boot",
                                      [OMNI_JOIN_REFUSE_PASS] = "8 characters at least",
                                      [OMNI_JOIN_REFUSE_SSID] = "No network name",
                                      [OMNI_JOIN_REFUSE_AUTH] = "This network is not supported"};
    static const char BUSY[] = "Wi-Fi is busy \xc2\xb7 try again";
    const unsigned r = omni_join_check(store_read_only(), net_busy(), net_provisioning(), S.authmode, S.ssid_len, edit.buf, edit.len);
    if (r != OMNI_JOIN_OK) return r < sizeof WHY / sizeof WHY[0] && WHY[r] ? WHY[r] : BUSY;   /* BUSY, PHONE: busy */
    S.had_saved = net_is_provisioned();   /* before the attempt can change it */
    if (net_try_join(S.ssid, S.ssid_len, edit.buf, edit.len, S.authmode) != OMNI_JOIN_OK) return BUSY;
    s_in_flight = true;
    show_result(NET_JOINING);   /* the text stays (masked) only while the result is up: Try again */
    return NULL;
}

static void go(void)
{
    const char *why = go_why();
    if (why) shake(why[0] ? why : NULL);
}

/* ------------------------------------------------------------ input */
static int key_at(int16_t x, int16_t y)
{
    for (int k = 0; k < K__N; k++)
        if ((k != K_EYE || password()) && x >= KEY[k].x && x <= KEY[k].x + KEY[k].w && y >= KEY[k].y &&
            y <= KEY[k].y + KEY[k].h)
            return k;
    return K_NONE;
}

static void typed(omni_kb_rc_t rc)
{
    if (rc == KB_AT_CAP) {
        shake(NULL);
        return;
    }
    S.msg = NULL;
    if (omni_kb_set(&edit) != S.set) ring_set(false);   /* one capital typed: back to abc */
    inval_field();
    inval_hint();
}

static void key_act(int k, uint32_t t)
{
    switch (k) {
    case K_BACK: leave(UI_POP); return;
    case K_EYE: edit.reveal = !edit.reveal; break;
    case K_SHIFT: omni_kb_shift_tap(&edit, t); ring_set(true); return;
    case K_MODE: omni_kb_123_tap(&edit); ring_set(true); return;
    case K_SPACE: typed(omni_kb_append(&edit, ' ', t)); return;
    case K_DEL: omni_kb_delete(&edit); break;
    case K_JOIN: go(); return;
    default: return;
    }
    S.msg = NULL;
    inval_field();
    inval_hint();
}

/* The ring key under (x, y): a new one lights, ticks, moves the loupe and makes the fish-eye's glyphs; inside r 131 the
 * loupe (the red cross) follows the finger's direction. */
static void ring_to(int16_t x, int16_t y)
{
    const int i = omni_kb_index_at(x, y, S.n);
    int16_t ux, uy;
    if (i >= 0) {
        if (i == S.idx) return;
        ux = A->key[i].ux, uy = A->key[i].uy;
    } else {
        const float dx = (float)(x - CX), dy = (float)(y - CY), r = sqrtf(dx * dx + dy * dy);
        ux = (int16_t)(r > 1 ? dx * Q / r : 0), uy = (int16_t)(r > 1 ? dy * Q / r : -Q);
        if (i == S.idx && abs(ux - S.ux) + abs(uy - S.uy) < 50) return;
    }
    inval_ring_key(S.idx);
    S.idx = (int8_t)i;
    S.ux = ux, S.uy = uy;
    inval_ring_key(i);
    if (i >= 0) {
        make_big(i);
        sfx_play_asset(KB_ASSET_TICK);
    }
}

static void edit_gesture(const omni_gesture_t *g, uint32_t t)
{
    switch (g->kind) {
    case G_DOWN:
        S.press = (int8_t)key_at(g->x, g->y);
        S.rep = 0;
        inval_key(S.press);
        if (S.press == K_NONE && omni_kb_in_band(g->x, g->y)) {   /* the ring: the key lights, the loupe opens */
            S.ring = true;
            S.idx = -3;
            S.t_loupe = lv_tick_get();
            ring_to(g->x, g->y);
        }
        break;
    case G_DRAG_START:
    case G_DRAG_MOVE:
        if (S.press != K_NONE) {   /* a drag that starts on a key cancels it */
            inval_key(S.press);
            S.press = K_NONE;
        }
        if (S.ring) ring_to(g->x, g->y);
        break;   /* a stroke from the centre (KB_STROKE_RESERVED, spec 6.6) does nothing */
    case G_HOLD:   /* Delete repeats as the hold's age asks (450 ms, then every 85 ms): a late tick catches up */
        while (S.press == K_DEL && S.rep < omni_kb_repeats(g->hold_ms) && omni_kb_delete(&edit)) {
            S.rep++;
            inval_field();
            inval_hint();
        }
        break;
    case G_TAP:
    case G_LIFT_STILL:
    case G_DRAG_END:
        if (S.ring) {   /* release commits the key under the finger, unless inside r 131 */
            ring_to(g->x, g->y);
            S.ring = false;
            inval_ring_key(S.idx);
            if (S.idx >= 0) typed(omni_kb_append(&edit, key_char(S.idx), lv_tick_get()));
        } else if (S.press != K_NONE) {   /* a key acts on release inside it; the release after a repeat deletes nothing */
            const int k = S.press;
            S.press = K_NONE;
            inval_key(k);
            if (g->kind != G_DRAG_END && key_at(g->x, g->y) == k && !(k == K_DEL && S.rep)) key_act(k, t);
        }
        break;
    default: break;
    }
}

static void gesture(const omni_gesture_t *g, uint32_t t)
{
    if (S.ds.root == NULL) return;   /* not opened (no assets): popped already */
    S.t_touch = lv_tick_get();
    if (S.dimmed) shell_dim(false);
    const bool tap = g->kind == G_TAP || g->kind == G_LIFT_STILL;
    switch (S.st) {
    case ST_EDIT: edit_gesture(g, t); return;
    case ST_RESULT: {
        if (!tap) return;
        theme_sheet_t sh;
        result_sheet(&sh);
        const int k = theme_sheet_key(&sh, g->x, g->y);
        if (k < 0) return;
        if (S.res == NET_JOINING) net_try_cancel();   /* no write: it acts only before the commit (spec 7.3) */
        else if (k == 1 || S.res == NET_ERR_SYNC) leave(UI_POP);   /* Done */
        else if (S.res == NET_ERR_NO_AP) {   /* Scan again */
            scr_netlist_rescan();
            leave(UI_POP);
        } else to_edit();   /* Try again */
        return;
    }
    case ST_CITY: {
        const int tx = g->x - CX, ty = g->y - CY;
        if (tap && tx * tx + ty * ty >= RIM_R * RIM_R) {   /* the rim: back to the search */
            to_edit();
            return;
        }
        switch (drum_gesture(&S.d, g, t, RIM_R)) {
        case DRUM_G_MOVED: drum_inval(S.ds.draw); break;
        case DRUM_G_WINDOW:   /* the windowed city: set (as the jog does), then Settings with Home city in the window */
            shell_post(UI_SET_CITY, S.city[drum_cur(&S.d)]);
            leave(UI_POP);
            break;
        default: break;
        }
        return;
    }
    default:   /* no match: Edit */
        if (tap && g->x >= CX - 64 && g->x <= CX + 64 && g->y >= 256 && g->y <= 256 + THEME_SHEET_KEY_H) to_edit();
        return;
    }
}

/* Input task, no LVGL lock: every drag that starts here is the screen's. */
static bool claim(int16_t x0, int16_t y0, void *ctx)
{
    (void)x0, (void)y0, (void)ctx;
    return true;
}

/* 16 ms: the idle stages (45 s dim, 60 s close), and only what moves: the loupe opening, the ring fading in, the shake,
 * the caret, a typed character's mask, the result's mark and comet, the drum. */
static void anim_cb(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t now = lv_tick_get();
    const omni_kb_idle_t idle = omni_kb_idle(now - S.t_touch);
    if (idle == KB_IDLE_CLOSE && !S.closed) {   /* spec 6.7: wiped, back to the face; no resume */
        S.closed = true;
        leave(UI_HOME);
        return;
    }
    if (idle == KB_IDLE_DIM && !S.dimmed) shell_dim(true);   /* dims only: the text stays */
    if (S.st == ST_EDIT) {
        if (now - S.t_ring < 300) lv_obj_invalidate(S.ds.draw);
        if (S.ring && now - S.t_loupe < 160) inval_ring_key(-1);   /* the loupe opening */
        if (now - S.t_shake < 420) inval_field();
        const bool caret = (now / 530) % 2 == 0 || now - edit.t_type < 600;
        const bool showing = password() && edit.len && omni_kb_shows(&edit, edit.len - 1, now);
        if (caret != S.caret || showing != S.showing) inval_field();
        S.caret = caret, S.showing = showing;
    } else if (S.st == ST_RESULT && now - S.t_frame >= WORK_FRAME_MS) {
        S.t_frame = now;
        if (S.res == NET_JOINING || S.res == NET_SYNCING) {
            inval(CX - 36, 136, CX + 36, 180);
            lv_area_t a;   /* the comet's last span and this one */
            const float ca = fmodf((float)now / 900, 1) * TAU;
            theme_arc_area(CX, CY, STEPART_RING_R + 4, 9, theme_arc_deg(ca - 0.65f), theme_arc_deg(ca + 0.05f), &a);
            lv_obj_invalidate_area(S.ds.draw, &a);
        } else if (S.res == NET_OK && now - S.t_res < 1300) {
            inval(CX - 34, 104, CX + 34, 168);
        }
    } else if (S.st == ST_CITY && drum_anim(&S.d, 16)) {
        drum_inval(S.ds.draw);
    }
}

/* ------------------------------------------------------------ screen interface */
void scr_keyboard_prepare(const scr_kb_open_t *o) { s_open = *o; }

bool scr_keyboard_secret_open(void) { return s_secret; }

static void create(lv_obj_t *parent)
{
    memset(&S, 0, sizeof S);
    S.press = K_NONE;
    S.t_ring = S.t_shake = lv_tick_get() - 100000;
    S.t_touch = lv_tick_get();
    omni_kb_open(&edit, (omni_kb_mode_t)(s_open.mode <= KB_MODE_CITY ? s_open.mode : KB_MODE_PASSWORD));   /* masked */
    S.ssid_len = s_open.ssid_len <= 32 ? s_open.ssid_len : 32;
    memcpy(S.ssid, s_open.ssid, S.ssid_len);
    S.authmode = s_open.authmode;
    if (!scr_keyboard_ready()) {   /* spec 6.4: no keyboard without its assets */
        shell_post(UI_POP, 0);
        return;
    }
    s_secret = password();
    drum_screen_create(&S.ds, parent, draw_cb, anim_cb, claim, OMNI_GW_DOWN | OMNI_GW_HOLD | OMNI_GW_LIFT_STILL);
    lv_obj_add_flag(S.ds.frame, LV_OBJ_FLAG_HIDDEN);
    ring_set(true);
    /* an open network (the list's confirm sheet): no password to type, it joins at once */
    if (password() && omni_netlist_auth(S.authmode) == OMNI_AUTH_OPEN) go();
}

static void destroy(void)
{
    wipe_secret();   /* every way out: BOOT, sleep's pop, the O4 wake, a push over it */
    s_secret = false;
    for (int k = 0; A && s_ready > 0 && k < 3; k++) {   /* the fish-eye's last glyphs hint at the last key typed */
        A->big[k].cp = 0;
        omni_wipe(A->big[k].data, A->big_cap);
    }
    if (S.done) lv_timer_delete(S.done);
    drum_screen_destroy(&S.ds);
    omni_wipe(&S, sizeof S);
}

/* Every wake, the glance and the dim start masked (spec 6.5): the reveal is dropped (the text stays). */
void scr_keyboard_mask(void)
{
    edit.reveal = false;
    if (S.ds.draw && S.st == ST_EDIT) inval_field();
}

static void dim(bool on)
{
    S.dimmed = on;
    if (on) scr_keyboard_mask();
    drum_screen_dim(&S.ds, on);
}

const omni_screen_vt_t SCR_KEYBOARD_VT = {
    .id = SCR_KEYBOARD, .create = create, .destroy = destroy, .gesture = gesture, .dim = dim,
};
