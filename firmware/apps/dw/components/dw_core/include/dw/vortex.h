#pragma once
/* The Vortex tunnel (dw wave 2 Task 13, the spike behind the gate WD38; faces-modern.js:677-765 "Vortex"): a turning,
 * rushing tunnel confined to the ring r 104..236, drawn per pixel through a polar lookup table on a half-resolution grid
 * (WD37). Task 14 makes it face #12 (DW_FACE_VORTEX, dw/faces.h; vortex.c) and the console's `dw vortex bench` draws it
 * alone.
 *
 * The assets (tools/dw_vortex.py, our own generator; nothing traced, no image read):
 *   - dw_v12_lut.dat: "DWL1", u16 w, u16 h (little-endian; 233 x 233), then w x h cells of 3 bytes (u, v, m) row-major
 *     (cell (i, j) is screen block (2 i, 2 j) .. (2 i + 1, 2 j + 1)), then one 0 pad byte, since 8 + 233^2 x 3 is odd
 *     and every assets file is an even length (dw_pack.py; fk_img_from_bin relies on even offsets): 162 876 B.
 *     u: the texture column (the angle, two turns, twisted with depth), v: the row (1 / r, the depth), m: the mask (0 =
 *     black: r < 104 and r > 236; full by r 150, dimmer past 200; :723-726).
 *   - dw_v12_tx0.bin, _tx1.bin, _tx2.bin: the three palettes' 256 x 256 RGB565 textures (LVGL bins; tileable streaky
 *     value noise, :704-713): GOLD & BLUE, STORM, PRISM. RGB565, not palette-indexed (R150's fallback: each palette
 *     uses more than 256 colours, test_vortex_palette_index_lossless).
 *
 * Per pixel: the cell's texel ((v + rush) & 255, (u + rot) & 255) (row, column), each RGB565 channel (5, 6, 5 bits)
 * times (m + 1) >> 8 for m > 0 (Task 14's ruling: a full mask, 255, is the texel itself); m 0 is black. Integer table lookups only: no float, no libm, no allocation. A pixel's value depends only
 * on the pixel and the inputs, never on the clip: a frame drawn in bands is the frame drawn whole. */
#include <stdbool.h>
#include <stdint.h>
#include "dw/faces.h"
#include "omni/geom.h"

#define DW_V12_N 233                    /* the grid: DW_V12_N^2 cells */
#define DW_V12_TEX 256                  /* the texture: DW_V12_TEX^2 RGB565 */
#define DW_V12_LUT_BYTES 162876u        /* 8 + 233^2 x 3, padded to an even length */
#define DW_V12_PALETTES 3               /* GOLD & BLUE, STORM, PRISM (faces-modern.js:682-686) */
#define DW_V12_Q 1                      /* the face's resolution: the gate's verdict, half (WD38, Task 13) */

/* lut: DW_V12_N^2 cells of (u, v, m), m 0 = black. tex: 256 x 256 RGB565. */
typedef struct {
    const uint8_t *lut;
    const uint16_t *tex;
} dw_vortex_t;

/* False (v zeroed) unless dw_v12_lut.dat is a DWL1 table of 233 x 233 cells, exactly 8 + w h 3 bytes padded to an even
 * length with a 0 byte, and palette pal's texture (pal < DW_V12_PALETTES) is a 256 x 256 RGB565 bin. */
bool dw_vortex_bind(dw_vortex_t *v, fk_asset_fn get, uint8_t pal);
uint8_t dw_vortex_rot(uint32_t ms);    /* floor(ms x 22 / 1000) & 255 (:733), in 64 bits: no overflow */
uint8_t dw_vortex_rush(uint32_t ms);   /* floor(ms x 70 / 1000) & 255 */
/* q 1: each lut cell is a 2 x 2 px block; q 2: quarter resolution, cell (2i, 2j) as a 4 x 4 block (the last block
 * column and row are 2 px, 466 = 116 x 4 + 2). fb is the pixel at (clip.x0, clip.y0), row stride `stride` px. The clip
 * is first cut to the screen, {0, 0, 466, 466} (the Task 13 review's M3: the face feeds it dirty rects): only its
 * on-screen part is written, at its own place in fb. */
void dw_vortex_draw(const dw_vortex_t *v, uint8_t rot, uint8_t rush, uint8_t q, omni_rect_t clip, uint16_t *fb,
                    int stride);
/* The palette-indexed form (R146, the owner's open question; the bench's only, no asset ships): dw_vortex_draw with each
 * texel pal[idx[row x 256 + column]] in place of tex's. v->tex is not read. Equal to dw_vortex_draw over a texture
 * whose texel is pal[idx[...]], byte for byte. */
void dw_vortex_draw_idx(const dw_vortex_t *v, const uint8_t *idx, const uint16_t pal[256], uint8_t rot, uint8_t rush,
                        uint8_t q, omni_rect_t clip, uint16_t *fb, int stride);
