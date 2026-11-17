#pragma once
/* House style: colour tokens and fonts from design/mockups/faces-practical.js (const C, the FD/FS/FM stacks),
 * plus small label helpers. The ground is true black, so a mockup rgba(...) colour is a token + opacity. */
#include "lvgl.h"
#include "sdkconfig.h"

#define THEME_W 466
#define THEME_CX 233
#define THEME_CY 233

/* const C in faces-practical.js */
#define THEME_BRASS      0xc9a35b
#define THEME_BRASS_HI   0xecd29a
#define THEME_BRASS_DIM  0x8c703c
#define THEME_BRASS_DEEP 0x4a3818
#define THEME_INK        0xece6dc
#define THEME_INK_DIM    0xa39a8c
#define THEME_INK_FAINT  0x5d564c
#define THEME_RED        0xd8443a   /* semantic: not fixed / alarm */
#define THEME_GREEN      0x4fd06a   /* semantic: fixed / OK */
#define THEME_PIP_OFF    0x3a3229   /* faces-setup.js position pips */

/* faces-setup.js (const C there and the wifi-qr frame) where it differs from the practical faces */
#define THEME_SETUP_HEAD     0xeadcbc   /* headings ("Scan to set up", "Joining") */
#define THEME_SETUP_TIME     0xf1e4c4   /* the time on "Time set" */
#define THEME_SETUP_FAINT    0x6d655a
#define THEME_SETUP_BRASS_HI 0xf0d595
#define THEME_SETUP_TRACK    0x231e17   /* progress ring track */
#define THEME_SETUP_STEP_OFF 0x2b261f   /* step ticks not reached */
#define THEME_PLATE          0xf6f3ec   /* the QR plate's warm white */
#define THEME_SETUP_CREAM    0xf3e6c6   /* C.cream: thumbnail time, hands */
#define THEME_SETUP_DIM      0xa39a8c   /* C.dim */
#define THEME_SETUP_KEY      0x12100c   /* key(): dark pill fill */
#define THEME_SETUP_KEY_ON   0x1f1a12   /* key(): "on" fill */
#define THEME_SETUP_GLASS    0x0d0b08   /* the settings window's glass */

/* Mockup alpha (0..1) as an LVGL opacity. */
#define THEME_OPA(a) ((lv_opa_t)((a) * 255 + 0.5))

/* Fonts (tools/fonts.sh): family_weight_size. Cormorant SC = FD, IBM Plex Sans = FS, IBM Plex Mono = FM. These are the
 * shared ones (watch_ui/fonts); a theme declares the fonts only its faces use itself. */
LV_FONT_DECLARE(font_cormorant_700_25);
LV_FONT_DECLARE(font_cormorant_500_17);
LV_FONT_DECLARE(font_cormorant_700_15);
LV_FONT_DECLARE(font_plex_sans_500_118);
LV_FONT_DECLARE(font_plex_sans_500_76);
LV_FONT_DECLARE(font_plex_sans_600_15);
LV_FONT_DECLARE(font_plex_sans_500_10);
LV_FONT_DECLARE(font_plex_mono_500_15);
LV_FONT_DECLARE(font_plex_mono_500_13);
/* setup screens (faces-setup.js) */
LV_FONT_DECLARE(font_cormorant_700_26);
LV_FONT_DECLARE(font_cormorant_700_30);
LV_FONT_DECLARE(font_cormorant_700_70);
LV_FONT_DECLARE(font_plex_sans_400_14);
LV_FONT_DECLARE(font_plex_sans_400_13);
LV_FONT_DECLARE(font_plex_sans_500_16);
LV_FONT_DECLARE(font_plex_sans_500_14);
LV_FONT_DECLARE(font_plex_sans_600_10);
LV_FONT_DECLARE(font_plex_mono_400_11);
/* face picker, settings drum, quick settings */
LV_FONT_DECLARE(font_cormorant_700_32);
LV_FONT_DECLARE(font_cormorant_700_20);
LV_FONT_DECLARE(font_cormorant_700_16);
LV_FONT_DECLARE(font_plex_sans_500_17);
LV_FONT_DECLARE(font_plex_sans_500_15);
LV_FONT_DECLARE(font_plex_sans_500_12);   /* only the help-line arrows */
LV_FONT_DECLARE(font_plex_sans_500_9);
LV_FONT_DECLARE(font_plex_sans_400_12);
LV_FONT_DECLARE(font_plex_sans_400_11);
LV_FONT_DECLARE(font_plex_mono_400_10);

/* An image buffer in PSRAM (lv_draw_buf_create would put anything under 16 KB in scarce internal RAM). */
lv_draw_buf_t *theme_buf_create(uint32_t w, uint32_t h, lv_color_format_t cf);
void theme_buf_destroy(lv_draw_buf_t *b);

/* A plain, non-clickable, non-scrollable, transparent container of the given size at (x, y). */
lv_obj_t *theme_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/* A label in font/colour/opacity with letter spacing (px). */
lv_obj_t *theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb, lv_opa_t opa, int32_t letter_space);
/* The label's text, set only when it changed (a set re-lays out and invalidates the label). */
void theme_set_text(lv_obj_t *label, const char *s);

typedef enum { THEME_LEFT, THEME_CENTER, THEME_RIGHT } theme_align_t;

/* Place a label like canvas fillText: its text's alphabetic baseline at y, and x the left edge, centre or
 * right edge of the text. Canvas letterSpacing also follows the last glyph, so a centred spaced label is
 * shifted by half a spacing, as in the browser. Call after setting the text. */
void theme_place(lv_obj_t *label, theme_align_t align, int32_t x, int32_t baseline_y);

/* Drawing in a custom draw callback (absolute screen coordinates). */
void theme_draw_arc(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t r, int32_t w, int32_t a0, int32_t a1,
                    uint32_t rgb, lv_opa_t opa, bool rounded);
void theme_draw_line(lv_layer_t *layer, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint32_t rgb,
                     lv_opa_t opa, bool round);
void theme_draw_box(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius, uint32_t fill,
                    lv_opa_t fill_opa, int32_t border, lv_opa_t border_opa);

/* faces-setup.js lens(): a jewel lens (r 7) in a brass bezel centred at (x, y); red = not fixed, green = fixed.
 * Returns the jewel; theme_lens_lit sets how lit it is (0..1000 = the mockup's lit 0..1) and is also an
 * lv_anim exec callback. */
lv_obj_t *theme_lens(lv_obj_t *parent, int32_t x, int32_t y, bool green);
void theme_lens_lit(void *jewel, int32_t v);

/* faces-setup.js thinBezel(): the brass case band (r 222..233) with the engraved minute ring every setup screen
 * shares, as a full-screen opaque image over true black (THEME_BG_BEZEL). */
lv_obj_t *theme_bezel(lv_obj_t *parent);

/* True if the box (inclusive) touches the area being drawn now: draw callbacks run once per render band, so
 * skipping what lies outside it saves most of their work. The theme_draw_* helpers check it themselves. */
bool theme_on_layer(const lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2);

/* Text in a draw callback, placed like canvas fillText (baseline y; x = left edge / centre / right edge). The
 * width counts letter spacing between glyphs only, like the mockups' tracked() captions. */
void theme_draw_text(lv_layer_t *layer, const char *txt, const lv_font_t *font, uint32_t rgb, lv_opa_t opa,
                     int32_t letter_space, theme_align_t align, int32_t x, int32_t baseline);
int32_t theme_text_width(const char *txt, const lv_font_t *font, int32_t letter_space);

/* faces-setup.js arcText(): text along the circle of radius r round the centre, centred on angle a (canvas
 * radians: 0 = 3 o'clock, clockwise), upright at the top and along the bottom. track = extra px per glyph. */
void theme_draw_arc_text(lv_layer_t *layer, const char *s, const lv_font_t *font, float r, float a, uint32_t rgb,
                         lv_opa_t opa, float track);
/* Call at the start of every draw callback that draws arc text: labels drawn since are kept (cache stamp). */
void theme_arc_text_frame(void);

/* Canvas createRadialGradient(x0, y0, r0, x1, y1, r1): the gradient position of (px, py), 0..1. */
float theme_radial_t(float px, float py, float x0, float y0, float r0, float x1, float y1, float r1);

/* Full-screen opaque backgrounds over true black (RGB565): thinBezel() (the setup screens' band), the picker's
 * navBezel() (wide brass ring r 146..233 with the dark centre) and the settings' settingsBezel() (thin band + dark
 * inner band 170..222). Flash assets since R112 (bg_bezel.bin, bg_picker.bin, bg_settings.bin; nothing in PSRAM). If
 * the assets image is not mapped, each is rendered on first use (1-2 s, omni_core's brassbg.c) into PSRAM and kept. */
typedef enum { THEME_BG_BEZEL, THEME_BG_PICKER, THEME_BG_SETTINGS, THEME_BG__COUNT } theme_bg_t;
lv_obj_t *theme_background(lv_obj_t *parent, theme_bg_t which);
#if CONFIG_OMNI_DEBUG_CONSOLE
/* The console's `theme dump`: the background's RGB565 pixels as the screens get them (THEME_W x THEME_W, stride
 * THEME_W * 2: the asset, or the fallback, rendered first if need be), or NULL. Call under bsp_display_lock, so it
 * never races LVGL's first use (B7). The pixels stay valid: a background, once loaded or made, is never freed. */
const uint8_t *theme_bg_pixels(theme_bg_t which);
#endif

/* A brass window frame (the mockups' magnifier windows): the ring between rrect(x - border, y - border,
 * w + 2 border, h + 2 border, r_out) and rrect(x, y, w, h, r_in), filled with the linear gradient
 * #f2d797 / #a07b38 / #4a3518 from (gx0, gy0) to (gx1, gy1); an optional 1 px black edge on the inner rrect;
 * and the glass: white glass_opa at the top fading out at glass_stop of the height. Rendered once per spec
 * (ARGB8888, PSRAM; the spec must be static) and kept. */
typedef struct {
    int32_t x, y, w, h, border, r_out, r_in;
    float gx0, gy0, gx1, gy1, edge_opa, glass_opa, glass_stop;
} theme_window_t;
lv_obj_t *theme_window(lv_obj_t *parent, const theme_window_t *w);

/* The settings' brass rim knob (r 13, radial #fff0c8 / #c9a35b / #5a4220), centred in a THEME_KNOB_SIZE square. */
#define THEME_KNOB_SIZE 28
const lv_draw_buf_t *theme_knob(void);

/* faces-setup.js key(): a pill (radius h / 2) with a 1 px hairline. PRIMARY: brass #e3c582 -> #a07b38 with a
 * light hairline (dark text on it); DARK: #12100c, brass .38; ON: #1f1a12, brass .85 (a selected choice);
 * ALARM: DARK with a red .85 hairline (a destructive choice). */
typedef enum { THEME_KEY_PRIMARY, THEME_KEY_DARK, THEME_KEY_ON, THEME_KEY_ALARM } theme_key_t;
void theme_draw_key(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, theme_key_t style);
/* Appended: the same key at opacity opa (a row fading on the drum); LV_OPA_COVER is theme_draw_key. */
void theme_draw_key_opa(lv_layer_t *layer, int32_t x, int32_t y, int32_t w, int32_t h, theme_key_t style, lv_opa_t opa);

/* faces-setup.js settings level row: the rim dial at r 196 over 270 degrees (7:30 -> 4:30 through 12): track,
 * gradient fill to v (0..1), 21 ticks, the brass knob with its red dot, and an end glyph pair (suns for
 * brightness, a speaker for volume). */
#define THEME_LEVEL_A0 ((float)M_PI * 0.75f)
#define THEME_LEVEL_SWEEP ((float)M_PI * 1.5f)
/* Appended: THEME_LEVEL_INFO, a scale that only shows (the network list's signal): no knob, no red dot, no end glyphs.
 * v < 0: the track and ticks only (no fill). */
typedef enum { THEME_LEVEL_SUN, THEME_LEVEL_SOUND, THEME_LEVEL_INFO } theme_level_glyph_t;
void theme_draw_level(lv_layer_t *layer, float v, theme_level_glyph_t glyph);
/* The level (0..1) a rim touch at angle a (canvas radians) asks for; beyond the ends it snaps to the nearer one. */
float theme_level_at(float a);
/* True if angle a (canvas radians) lies on the level dial's 270-degree arc, widened by margin at each end. */
bool theme_level_hit(float a, float margin);
/* The red index at the rim's edge (r 224..233) at angle a (canvas radians): a dial's current value (moved from
 * scr_settings.c; the network list's signal reading too). */
void theme_draw_red_index(lv_layer_t *layer, float a);
/* Invalidate only what a level change moves on obj (full-screen): the fill between the values and the knob. */
void theme_invalidate_level(lv_obj_t *obj, float v_old, float v_new);
/* The bounding box of an arc (theme_draw_arc's arguments): its end points and the axis extremes it passes. */
void theme_arc_area(int32_t cx, int32_t cy, int32_t r, int32_t w, int32_t a0, int32_t a1, lv_area_t *out);

/* ---- shared by the Setup screens (R223(c), Task 12) */
/* A sheet (Settings' grammar; the network list's two sheets, the keyboard's join result and city search): up to four
 * centred lines, then one key (centred) or two (left 99, right 239; 128 x 46) at key_y (0: none), and the footer "tap
 * outside to close" at 330. A key's label: PRIMARY Cormorant 700 20 dark ink; DARK Plex Sans 500 14 ink; ALARM the same
 * in red. */
typedef struct { const char *s; const lv_font_t *f; uint32_t rgb; int16_t y; uint8_t ls; } theme_line_t;
typedef struct {
    theme_line_t line[4];   /* s NULL: none */
    const char *key[2];     /* key[1] NULL: one key */
    uint8_t style[2];       /* theme_key_t */
    int16_t key_y;
    bool footer;
} theme_sheet_t;
#define THEME_SHEET_KEY_W 128
#define THEME_SHEET_KEY_H 46
void theme_draw_sheet(lv_layer_t *layer, const theme_sheet_t *s);
/* The key at (x, y) (inclusive at the edges): 0, 1, or -1 (none). */
int theme_sheet_key(const theme_sheet_t *s, int32_t x, int32_t y);
/* "\u2026", or "..." in the fonts without it (Plex Sans 400 14 and 13). */
const char *theme_ellipsis(const lv_font_t *f);
/* A network name (len bytes, at most 32, not NUL-terminated) for display into out (40 B): control bytes as '?', cut at
 * a UTF-8 boundary with an ellipsis to the longest prefix that fits max px in f. */
#define THEME_NAME_BUF 40
void theme_fit_name(char out[THEME_NAME_BUF], const uint8_t *b, int len, const lv_font_t *f, int32_t max);
/* Canvas radians -> LVGL arc degrees 0..359 (the dial's, the list's and the step art's: one copy). */
int32_t theme_arc_deg(float rad);
/* theme_draw_text left-aligned at full opacity, but LVGL keeps the pointer instead of copying the text (text_local 0):
 * txt must stay unchanged until the refresh that draws it has finished (a screen's static buffer; the keyboard's field,
 * so a shown password character never lands in LVGL's heap). */
void theme_draw_text_ref(lv_layer_t *layer, const char *txt, const lv_font_t *font, uint32_t rgb, int32_t x, int32_t baseline);
/* A rounded box with a border (px) of its own colour; theme_draw_box is this with the fill's colour. */
void theme_draw_rect(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius, uint32_t fill,
                     lv_opa_t fill_opa, int32_t border, uint32_t border_rgb, lv_opa_t border_opa);
/* A box filled with a two-stop linear gradient (top to bottom, or left to right), at opa. */
void theme_draw_grad(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius, bool vertical,
                     uint32_t c0, lv_opa_t o0, uint32_t c1, lv_opa_t o1, lv_opa_t opa);
/* A hold key's ring (Task 14; moved from scr_faces.c's Restore ring): the pill path round (x1, y1)-(x2, y2) (radius half
 * the height), from its top centre clockwise, the fraction p (0..1) of it, w px wide with round caps. The ring of a key
 * at (x, y, w, h) is (x - 4, y - 4, x + w + 4, y + h + 4). */
void theme_draw_pill_ring(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, float p, int32_t w,
                          uint32_t rgb, lv_opa_t opa);
