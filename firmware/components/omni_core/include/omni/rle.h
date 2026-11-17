#pragma once
/* Run-length frames (the owner's pre-rendered-sprite technique, measured in Stage 3 Task 2 against the ring strips;
 * R69/C6: benched on a subset only, no RLE assets ship). Little-endian, byte-packed:
 *   u16 w, h, ox, oy; then per row u16 nruns, then per run u16 skip, u16 len, len x (u16 rgb565, u8 a).
 * skip counts from the end of the previous run (the row start for the first). The frame sits at (ox, oy) on screen. */
#include <stddef.h>
#include <stdint.h>
#include "omni/geom.h"

#define OMNI_RLE_HDR 8
/* Encodes w x h unpremultiplied RGBA (row-major, 4 B/px): runs of alpha > 0, colour rounded to RGB565. Returns the
 * bytes written, or 0 if cap is short (host tool). */
size_t omni_rle_encode(const uint8_t *rgba, int w, int h, int ox, int oy, uint8_t *out, size_t cap);
/* Blends the frame over fb with omni_blend565. fb is the pixel at (clip->x0, clip->y0) (clip NULL: the screen's
 * (0, 0), nothing clipped); only pixels inside clip are written. */
void omni_rle_draw(const uint8_t *frame, uint16_t *fb, int stride, const omni_rect_t *clip);
/* The byte size of a well-formed frame (walks its rows), or 0 if it would run past size. */
size_t omni_rle_size(const uint8_t *frame, size_t size);
