#pragma once
/* World time (Stage 5): a POSIX TZ evaluator that parses each call (no state, no setenv, so a city's clock can be read
 * without touching the process's own TZ), the city band's list, taps and angles, and the cartouche's text. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/globe_motion.h"

/* POSIX TZ as the city table writes it: std offset [dst [offset] [,Mm.w.d[/time],Mm.w.d[/time]]], quoted <+04> names,
 * signed offsets with minutes, rule times beyond 24 h (Cairo's /24). off_s = local - UTC. false: unparsable (off 0,
 * dst false), as it is for |utc| beyond OMNI_TZ_UTC_LIMIT (about 31 700 years: no overflow anywhere below it). A dst
 * name without rules takes glibc's M3.2.0,M11.1.0; the Jn and n day forms are not accepted. */
#define OMNI_TZ_UTC_LIMIT 1000000000000LL
bool omni_tz_offset(const char *posix, int64_t utc, int32_t *off_s, bool *dst);

#define OMNI_WT_MAX 13
typedef struct { char code[4]; uint16_t city; bool home; } omni_wt_city_t;
/* the twelve (owner Q1, mockup order) found by name + country, then home last; a home among the twelve is marked
 * there, not added. Returns the count (12 or 13). */
int omni_wt_list(uint16_t home_city, omni_wt_city_t out[OMNI_WT_MAX]);
void omni_wt_code(const char *name, char code[4]);  /* the twelve's codes, Columbus "CMH", else the first three ASCII
                                                       letters (spaces and punctuation skipped), upper (B8) */
float omni_wt_angle(float city_lon, float lon_c);   /* canvas rad: -pi/2 - (lon - lonC) */
bool omni_wt_flip(float a);                          /* sin(a) > 0.15: bottom-half text */
/* a tap: r < 60 -> -2 (clear); r < 120 -> -1; else the nearest city by angle within 0.25 rad, or -1 */
int omni_wt_hit(const omni_wt_city_t *l, int n, int16_t x, int16_t y, float lon_c);
/* the cartouche: sel NULL (or the home entry) = home (big home time without am/pm, top home code, sub "SUN PM");
 * a city: its civil time, its code, "MON AM +14h" (offset vs home: "+5:30h", "–3h" with an en dash). Clock unset:
 * big "--:--", sub "". h24: no AM/PM anywhere. */
void omni_wt_cartouche(const omni_wt_city_t *sel, const omni_wt_city_t *home, int64_t now, bool h24,
                       char big[8], char top[4], char sub[24]);
void omni_wt_glance_text(const omni_wt_city_t *c, int64_t now, bool h24, char out[24]); /* "LON  3:42 pm" */
/* R138, fade-only: fade t/300 (done at 300 ms), lon_off 0, gain 1 and settle 1 throughout; reduced motion the same.
 * The world face applies the fade itself over the map on screen (no globe frame per step). */
void omni_wt_reveal(uint32_t t_ms, bool reduced, omni_reveal_t *r);

/* R141: a band code drawn glyph by glyph on its arc (the face's own placement; lv_arclabel steps whole degrees with
 * LV_USE_FLOAT off: 3.2 px at r 182). adv: the n glyphs' advances (px, kerning included), spacing:
 * letter spacing (px), r: the text's middle line, mid: the middle-to-baseline distance (px), a: the label's centre
 * angle (canvas rad, as omni_wt_angle), flip: bottom-half text (omni_wt_flip), read anticlockwise with tops inward,
 * (cx, cy): the dial centre on screen.
 * Glyph centres sit (adv_i + adv_i+1) / 2 + spacing apart in arc length along r, the run centred on a. Out: each
 * glyph's baseline centre (x, y from the dial centre, px; mid inside r, or outside when flipped) and its rotation (deg,
 * clockwise: the angle + 90, or - 90 flipped), and (px, py): that centre on screen, rounded, which is the point
 * lv_draw_letter takes as it is (LVGL 9.5 moves the glyph by its pivot, (adv / 2, ascent), itself and turns it about
 * that point). Returns n, or 0 for n outside 1 .. OMNI_WT_GLYPHS. */
#define OMNI_WT_GLYPHS 4
typedef struct { float x, y, rot_deg; int32_t px, py; } omni_wt_glyph_t;
int omni_wt_glyphs(const float *adv, int n, float spacing, float r, float mid, float a, bool flip, int32_t cx,
                   int32_t cy, omni_wt_glyph_t *out);
