#pragma once
/* The faces' compositor (from the first theme's wave 1 Task 6, WD9): images, additive glow sprites and glyph atlases
 * (the one-ring polar annulus for the turning script rings is fk/annulus.h). Pure C, integer per pixel, no
 * allocation: the faces build their frames from
 * these over the core's RGB565 blend (omni/blend565.h), in any band split.
 *
 * Every draw call takes fb as the pixel at (clip.x0, clip.y0) with row stride `stride` px, as omni_fill_poly's, and
 * writes only pixels inside clip. A pixel's value depends only on the pixel and the inputs, never on the clip: a frame
 * built in bands is the frame built whole, byte for byte (test_compose_band_exact). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/geom.h"
#include "omni/ring.h"

/* ---- Images: an LVGL v9 .bin read in place (the flash mapping). A 12-byte header (u8 magic 0x19, u8 cf, u16 flags,
 * u16 w, h, stride, u16 reserved), then the planes packed at stride = w x bpp (tools/assets.sh): RGB565 (cf 0x12), the
 * RGB565 plane then the A8 plane (RGB565A8, cf 0x14), or the A8 plane alone (A8, cf 0x0E; rgb NULL). */
#define FK_CF_A8 0x0E
#define FK_CF_RGB565 0x12
#define FK_CF_RGB565A8 0x14
typedef struct { uint16_t w, h; const uint16_t *rgb; const uint8_t *a; uint8_t cf; } fk_img_t;
/* False (img untouched) on a bad magic, an unknown cf, a zero size, a stride other than w x bpp, the premultiplied or
 * compressed flag, a bin shorter than its planes, or an odd address (the RGB565 plane is read as uint16). */
bool fk_img_from_bin(fk_img_t *img, const void *bin, size_t size);
/* img's top-left at screen (x, y): each pixel omni_blend565(dst, rgb, a x opa / 255) (integer division), a the
 * image's alpha (255 without an A8 plane). An A8-only image has no colour and draws nothing (fk_add_a8 draws it). */
void fk_blit(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *img, int x, int y, uint8_t opa);
/* Additive tinted glow ('lighter'): a8's alpha plane stretched over the screen rect (x, y, w, h), sampled nearest at
 * pixel centres (source column ((2 (px - x) + 1) a8->w) / (2 w), all integer); per pixel a' = omni_amul(alpha) x
 * omni_amul(gain) >> 8 and each 8-bit channel of rgb, times a' >> 8, added to the pixel's, saturating at 255 (the
 * doors' glow rule, doors.c). Gain 0 leaves the frame as it was. Any image with an A8 plane works. */
void fk_add_a8(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *a8, int x, int y, int w, int h,
               uint16_t rgb, uint8_t gain);
/* fk_add_a8 at gain1, then again at gain2, byte for byte, reading a8 once (Task 12: the crystal's lances above 1). */
void fk_add_a8_2(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *a8, int x, int y, int w, int h,
                 uint16_t rgb, uint8_t gain1, uint8_t gain2);

/* ---- Glyph atlases: one coloured image (RGB565 or RGB565A8) holding every glyph. glyphs[i] is cells[i]'s character
 * (a NUL-terminated byte string). A cell's box (x, y, w, h) in the atlas lands with its top-left at (pen + ox,
 * baseline + oy); adv moves the pen. */
typedef struct { int16_t x, y, w, h, ox, oy, adv; } fk_cell_t;  /* a glyph's box in its atlas, offset, advance */
typedef struct { const fk_img_t *img; const fk_cell_t *cells; const char *glyphs; int16_t line_h; } fk_atlas_t;
/* fixed_cell > 0: every character takes fixed_cell px, its advance box centred in that cell (left edge pen +
 * floor((fixed_cell - adv) / 2)); 0, proportional: each glyph takes its adv. A character the atlas lacks draws
 * nothing: in a fixed cell it still takes its cell, proportional it takes no room. */
int fk_atlas_width(const fk_atlas_t *at, const char *text, int fixed_cell);
void fk_atlas_text(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *text,
                   int x, int baseline, int fixed_cell, uint8_t opa);   /* fixed_cell 0 = proportional */
/* (Task 13) fk_atlas_text drawing the atlas's alpha plane alone, in rgb: each pixel omni_blend565(dst, rgb, a' x opa /
 * 255) (integer division), a' the cell's alpha a stretched from (lo, 255] onto (0, 255]: (a - lo) x 255 / (255 - lo),
 * 0 at or below lo (lo 0: a itself; a larger lo drops a glyph's soft bloom, for a glance). Any atlas with an A8 plane
 * (an A8 or RGB565A8 image); without one it draws nothing. */
void fk_atlas_text_rgb(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *text, int x,
                       int baseline, int fixed_cell, uint16_t rgb, uint8_t opa, uint8_t lo);
/* Tracked text (canvas letterSpacing, Task 13): track px after each glyph, centred on cx over the width
 * fk_atlas_width_tracked (the advances and track x (n - 1)); each glyph at its pen rounded to the pixel (floor of
 * pen + 0.5). tint false: the atlas's colour (fk_atlas_text); true: its alpha in rgb (fk_atlas_text_rgb, lo 0). */
float fk_atlas_width_tracked(const fk_atlas_t *at, const char *text, float track);
void fk_atlas_text_tracked(uint16_t *fb, int stride, omni_rect_t clip, const fk_atlas_t *at, const char *text,
                           float cx, int baseline, float track, bool tint, uint16_t rgb, uint8_t opa);
#ifdef FK_TEST_HOOKS
/* Host test builds only (rd wave 1 Task 9, PR18: the host CMake defines FK_TEST_HOOKS for this library; no device build
 * does, so no device code or frame changes). Each glyph fk_atlas_text, fk_atlas_text_rgb and fk_atlas_text_tracked
 * place calls the hook, when set, with its atlas and its box on screen, [x0, x1) x [y0, y1) (cut to the atlas, not to
 * the clip): a theme's harness checks its text radius and counts the atlases a face draws from. */
typedef void (*fk_test_glyph_fn)(const fk_atlas_t *at, int x0, int y0, int x1, int y1);
extern fk_test_glyph_fn fk_test_glyph_hook;
#endif
/* (Task 13) An image's alpha plane (A8 or RGB565A8) in rgb, its top-left at (x, y): omni_blend565(dst, rgb, a x opa /
 * 255) (integer division). */
void fk_blit_rgb(uint16_t *fb, int stride, omni_rect_t clip, const fk_img_t *img, int x, int y, uint16_t rgb,
                 uint8_t opa);
