#pragma once
/* Fonts read from the assets partition's mapping, zero copy (rim keyboard Task 8; spec section 10: the keyboard's
 * two Plex Mono sizes live in assets, not in code). tools/kb_assets.py font writes the asset from lv_font_conv's
 * output (tools/fonts.sh).
 *
 * Asset "OFA1" (little-endian): "OFA1" | u8 version 1 | u8 bpp | u8 bitmap_format (LVGL's: plain or compressed; only
 * plain, 0, is accepted, since a compressed glyph's length cannot be bounded) | u8 line_height | i8 base_line |
 * u8 subpx 0 | u16 first 0x20 | u16 count | i8 underline_position | u8 underline_thickness | u32 dsc_off |
 * u32 bitmap_off; then count lv_font_fmt_txt_glyph_dsc_t (8 B each, LVGL 9.5's layout: the bitmap index, adv_w, box_w,
 * box_h, ofs_x, ofs_y): glyph id 0 reserved (lv_font_conv's glyph_id_start 1), then 0x20..0x7E, then U+2022; then the
 * bitmap. dsc_off and bitmap_off are 4-byte aligned in the mapping (Minor 19). One range cmap (0x20..0x7E) and one
 * sparse entry (U+2022) live in fontasset.c's rodata. */
#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

/* The glyph descriptors and bitmap point into the mapping (zero copy); false (font and dsc untouched) when the asset is
 * missing, the magic, version or sizes are wrong, an offset is misaligned, or the descriptor table or any glyph's
 * bitmap runs past the file. dsc must outlive font (font->dsc points at it). */
bool fontasset_load(lv_font_t *font, lv_font_fmt_txt_dsc_t *dsc, const char *asset);

/* The same on bytes already in hand (data, size: a mapped file). */
bool fontasset_parse(const void *data, size_t size, lv_font_t *font, lv_font_fmt_txt_dsc_t *dsc);

/* For the console's `assets`: true and the glyph count and line height when data is a valid OFA1 font. */
bool fontasset_info(const void *data, size_t size, unsigned *glyphs, unsigned *line_height);
