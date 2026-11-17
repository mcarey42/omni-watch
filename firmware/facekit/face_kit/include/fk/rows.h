#pragma once
/* The row renderer (rows.c; rd wave 1 Task 3): a flat leaf hinged on a horizontal screen line (the hinge above the
 * leaf), drawn one screen row at a time over an interior the face draws. Each screen row shows one source row of the
 * leaf, scaled across the row about the eye's column, as the lid's hinge engine maps it (omni/lid.h,
 * omni_hinge_lines_at, horizontal axis). Pure C, single precision in a fixed order (-ffp-contract=off), no
 * allocation: the device builds the frames the host tests pin. The column renderer (fk/cols.h) is its transpose for
 * two leaves; this one is one leaf, with a uniform shade, the lower edge's thickness and its rim light (the reveal's
 * look, spec section 9.2), which the theme works out from its own timeline (fk_rows_state_t).
 *
 * Host and device: theta 0 maps without trig (the identity, shifted by dy), so a shut leaf's frames are host-identical.
 * Any other theta goes through omni_hinge_map's sinf and cosf (the hinge engine's, unchanged), and the host's libm and
 * the device's newlib may differ by an ulp, which can move a row's v or inv_s at a boundary: never CRC-compare a
 * mid-swing frame between host and device. On one machine every frame is deterministic and band-split exact. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/geom.h"
#include "omni/lid.h"

#define FK_ROWS_N 466
/* One screen row: v the leaf's source row (-1: no leaf on this screen row), inv_s_q16 = 1 / scale across the row in
 * Q16 (65536 = 1.0), exactly omni_hinge_lines_at's v and inv_s_q16 for the line y - dy. */
typedef struct { int16_t v; uint32_t inv_s_q16; } fk_row_t;

typedef struct {
    float theta;          /* rad; 0 shut */
    int16_t dy;           /* a whole-leaf vertical offset before the swing (the seal pop), px: screen row y shows the
                           * hinge engine's line y - dy */
    uint8_t shade;        /* uniform leaf darkening, 0..255 (black at shade over every leaf pixel) */
    uint16_t edge_rgb; uint8_t edge_px;          /* the lower edge's thickness quad: fill and height in screen rows
                                                  * (0: no quad and no rim) */
    uint16_t rim_rgb; uint8_t rim_a8; float rim_px;   /* the cold-light line along the quad's lower edge: colour, alpha,
                                                       * width in px */
    uint8_t fade;         /* the whole frame up from black, 0..255 (255: no fade) */
} fk_rows_state_t;

/* Screen rows [y0, y1) into rows[0 .. y1 - y0): each row's v and inv_s exactly as omni_hinge_lines_at gives them for
 * the line y - st->dy (by the same single-precision expressions in the same order), without its per-line dark and
 * spec (two cosf and two expf a line), as the column renderer's leaf columns do (cols.c). PR13 */
void fk_rows_map(const omni_hinge_t *h, const fk_rows_state_t *st, int y0, int y1, fk_row_t *rows);
/* The leaf's screen rows [*y0, *y1) at st, of the h->n screen rows (empty: *y0 >= *y1). The leaf's rows are
 * contiguous. For the band, with theta >= 0, every one of them is drawn from the leaf alone (inv_s <= 1: the scaled row
 * covers the whole screen row), so the band reads no interior there and a reveal's need rect is the rest of the
 * screen. The edge quad and its rim lie just below, in rows [*y1, *y1 + edge_px + rim), and blend over the interior. */
void fk_rows_extent(const omni_hinge_t *h, const fk_rows_state_t *st, int *y0, int *y1);
/* The leaf's free edge on screen, for the edge quad and its rim: L the leaf's last row of rows[0 .. nrows) (v in
 * [0, lh) there, and row L + 1 inside with no leaf: the free edge on screen; -1 if none, or edge_px is 0), and [x0, x1)
 * the columns of [0, lw) where row L draws the leaf. Once per frame, after fk_rows_map (Task 3 review M2: the bands
 * read it instead of each finding it again). */
typedef struct { int16_t L, x0, x1; } fk_rows_edge_t;
void fk_rows_edge(const fk_rows_state_t *st, const fk_row_t *rows, int nrows, int lw, int lh, fk_rows_edge_t *e);
/* One band of the frame. rows: all FK_ROWS_N screen rows (rows[y] for screen row y, from fk_rows_map(h, st, 0,
 * FK_ROWS_N, rows)); edge: fk_rows_edge(st, rows, FK_ROWS_N, 466, 466, edge) for the same frame. leaf and interior:
 * 466 x 466 RGB565,
 * row stride 466 (row 0 = screen row 0). out: the band's buffer, row 0 = band.y0, column x at offset x, stride 466
 * (omni_band_pack's layout); only band's pixels are written. interior NULL (rd wave 1 Task 10): out already holds
 * the band's interior on the panel (in its own layout), which the leaf, the quad, the rim and the fade then draw over
 * in place, so a theme can lay its own layers between the interior and the leaf (the reveal's flash and mist). Per
 * pixel, in order:
 *   - the interior (not read on a row the leaf covers whole);
 *   - the leaf: row v, column u = floor(233 + (x + .5 - 233) inv_s) (nearest, at pixel centres), black at shade
 *     over it;
 *   - the edge quad: edge_rgb over the edge_px screen rows below the leaf's last row L (its v >= 0 and row L + 1's
 *     < 0: the free edge on screen), in the columns where row L draws the leaf;
 *   - the rim: rim_rgb at rim_a8 over the rows whose centres lie within rim_px / 2 of the quad's lower edge
 *     (y = L + 1 + edge_px), [y - rim_px / 2, y + rim_px / 2), whole pixel rows, in the same columns;
 *   - the fade from black.
 * Off the round panel (fk_panel_row): black. Every value depends only on the pixel and the inputs, never on the band:
 * a frame built in bands is the frame built whole, byte for byte. */
void fk_rows_band(const fk_rows_state_t *st, const fk_row_t *rows, const fk_rows_edge_t *edge, const uint16_t *leaf,
                  const uint16_t *interior, omni_rect_t band, uint16_t *out);
/* The same leaf rows clipped to a box on a canvas (a lid or a half-card on a face's canvas): the leaf is box-local,
 * lw x lh RGB565 (stride lw), drawn into box (screen coordinates), which must be lw x lh (anything else draws
 * nothing: Task 3 review M1); rows: one per box row, lh of them (rows[0] = box row 0, from fk_rows_map with h the
 * box's hinge, n = lh). The free edge is found per call (one call a frame per box). Per box pixel: the leaf (column
 * u = floor(lw / 2 + (x + .5 - lw / 2) inv_s), x box-local; shaded), then the edge quad and the rim, all as
 * fk_rows_band draws them, over what fb holds; rows with no leaf, quad or rim are left as they are. No fade (the face
 * fades its own canvas) and no panel clip. fb is the pixel at (clip.x0, clip.y0), row stride `stride` px (fk/compose.h's convention); only pixels of box
 * and clip are written. A pixel's value depends only on the pixel and the inputs, never on the clip. */
void fk_rows_blit(const fk_rows_state_t *st, const fk_row_t *rows, const uint16_t *leaf, int lw, int lh,
                  omni_rect_t box, omni_rect_t clip, uint16_t *fb, int stride);
