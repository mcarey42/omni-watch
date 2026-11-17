#pragma once
/* The two-plane atlases (rd wave 1 Task 7, PR29; spec section 12.2): one ATL2 file (tools/facepack.py's layout) holds a
 * glow plane and a core plane of the same cells, read in place as two ordinary fk_atlas_t, so the existing
 * fk_atlas_text* draws each plane: the glow first in its colour, then the core in its own (the phosphor's two passes).
 *   "ATL2", u16 w, u16 h (one plane's), u16 n (cells), u16 line_h, u16 glyph_bytes (even), u16 0,
 *   n cells of 7 int16 (x, y, w, h, ox, oy, adv), the glyph string (one Latin-1 byte per cell, NUL-terminated, padded
 *   to glyph_bytes), the glow plane (w x h A8), the core plane (w x h A8). Little-endian.
 * Both atlases share the file's cell table and glyph string, and each points at its image here: an rd_atlas2_t is used
 * where it was filled and never copied by value (a copy's img pointers would still name the original). Pure: no
 * esp_*, freertos or LVGL; no allocation. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "fk/compose.h"

typedef struct {
    fk_atlas_t glow, core;      /* the two atlases: glow.img is &glow_img, core.img &core_img */
    fk_img_t glow_img, core_img; /* the planes, A8 images over the file's bytes */
} rd_atlas2_t;

/* False (out untouched) on a NULL or odd-addressed bin, a bad magic or reserved field, a zero size or cell count, an
 * odd or short glyph block, a glyph string that is not exactly n bytes before its NUL, a cell outside the plane, or a
 * size n other than the header's parts exactly. */
bool rd_atlas2_from_bin(const uint8_t *bin, size_t n, rd_atlas2_t *out);
