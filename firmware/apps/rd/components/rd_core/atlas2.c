/* rd/atlas2.h: the two-plane atlases read in place (rd wave 1 Task 7, PR29). */
#include "rd/atlas2.h"
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "rd_atlas2_from_bin reads the ATL2 cells in place: little-endian only (the ESP32-S3 and the host)"
#endif
_Static_assert(sizeof(fk_cell_t) == 14, "an ATL2 cell is 7 int16, read in place as an fk_cell_t");

#define ATL2_HEAD 16u

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool rd_atlas2_from_bin(const uint8_t *bin, size_t n, rd_atlas2_t *out)
{
    if (!bin || !out || ((uintptr_t)bin & 1u) || n < ATL2_HEAD || memcmp(bin, "ATL2", 4) != 0) return false;
    const uint16_t w = u16(bin + 4), h = u16(bin + 6), cells = u16(bin + 8), line_h = u16(bin + 10);
    const uint16_t gb = u16(bin + 12), reserved = u16(bin + 14);
    if (!w || !h || !cells || reserved || (gb & 1u) || gb < (uint32_t)cells + 1u) return false;
    const size_t at_glyphs = ATL2_HEAD + (size_t)cells * sizeof(fk_cell_t);
    const size_t plane = (size_t)w * h, at_planes = at_glyphs + gb;
    if (n != at_planes + 2 * plane) return false;
    const char *glyphs = (const char *)bin + at_glyphs;
    if (memchr(glyphs, 0, gb) != glyphs + cells) return false;   /* exactly one byte per cell, then the NUL */
    const fk_cell_t *cell = (const fk_cell_t *)(const void *)(bin + ATL2_HEAD);
    for (unsigned i = 0; i < cells; i++) {
        const fk_cell_t *c = &cell[i];
        if (c->x < 0 || c->y < 0 || c->w < 0 || c->h < 0 || c->x + c->w > w || c->y + c->h > h) return false;
    }
    out->glow_img = (fk_img_t){w, h, NULL, bin + at_planes, FK_CF_A8};
    out->core_img = (fk_img_t){w, h, NULL, bin + at_planes + plane, FK_CF_A8};
    out->glow = (fk_atlas_t){&out->glow_img, cell, glyphs, (int16_t)line_h};
    out->core = (fk_atlas_t){&out->core_img, cell, glyphs, (int16_t)line_h};
    return true;
}
