/* OFA1 fonts from the assets mapping, zero copy (fontasset.h). Every offset and count is checked against the mapped
 * size before anything points into it, in overflow-safe arithmetic (each sum compared as a difference against what is
 * left), and LVGL's own view of each descriptor must equal the bytes as read, so a layout other than LVGL 9.5's refuses
 * the font instead of drawing garbage. The host suite builds this file against a stand-in lvgl.h (FONTASSET_HOST). */
#include "fontasset.h"
#include <stdint.h>
#include <string.h>
#ifndef FONTASSET_HOST
#include "assets.h"
#else
const void *assets_raw(const char *name, size_t *size);
#endif

#if LV_FONT_FMT_TXT_LARGE
#error "fontasset: OFA1 holds LVGL 9.5's 8-byte glyph descriptors (LV_FONT_FMT_TXT_LARGE 0)"
#endif
_Static_assert(sizeof(lv_font_fmt_txt_glyph_dsc_t) == 8, "OFA1 descriptors are 8 bytes, LVGL 9.5's layout");

#define OFA_HDR 24u
#define OFA_FIRST 0x20u
#define OFA_LAST 0x7Eu
#define OFA_EXTRA 0x2022u                                  /* the bullet: a masked password character */
#define OFA_COUNT (1u + (OFA_LAST - OFA_FIRST + 1u) + 1u)  /* id 0 reserved, 95 ASCII, the bullet: 97 */

static const uint16_t s_extra_list[1] = {0};
static const lv_font_fmt_txt_cmap_t s_cmaps[2] = {
    {.range_start = OFA_FIRST, .range_length = OFA_LAST - OFA_FIRST + 1u, .glyph_id_start = 1, .unicode_list = NULL,
     .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},
    {.range_start = OFA_EXTRA, .range_length = 1, .glyph_id_start = OFA_COUNT - 1u, .unicode_list = s_extra_list,
     .glyph_id_ofs_list = NULL, .list_length = 1, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},
};

typedef struct {
    uint8_t bpp, line_h, ul_th;
    int8_t base, ul_pos;
    uint32_t dsc_off, bitmap_off;
} ofa_t;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static bool ofa_check(const uint8_t *p, size_t size, ofa_t *h)
{
    if (p == NULL || size < OFA_HDR || memcmp(p, "OFA1", 4) != 0 || p[4] != 1) return false;
    const uint8_t bpp = p[5];
    if ((bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) || p[6] != 0 || p[9] != 0 || rd16(p + 10) != OFA_FIRST ||
        rd16(p + 12) != OFA_COUNT)
        return false;
    const uint32_t dsc_off = rd32(p + 16), bitmap_off = rd32(p + 20);
    if (((dsc_off | bitmap_off) & 3u) || dsc_off < OFA_HDR || dsc_off > size || size - dsc_off < 8u * OFA_COUNT ||
        bitmap_off < dsc_off || bitmap_off - dsc_off < 8u * OFA_COUNT || bitmap_off > size)
        return false;
    if (((uintptr_t)(p + dsc_off) & 3u) != 0) return false; /* the mapping itself must keep the alignment */
    const lv_font_fmt_txt_glyph_dsc_t *g = (const lv_font_fmt_txt_glyph_dsc_t *)(const void *)(p + dsc_off);
    const size_t blen = size - bitmap_off;
    for (uint32_t i = 0; i < OFA_COUNT; i++) {
        const uint8_t *d = p + dsc_off + 8u * i;
        const uint32_t v = rd32(d), idx = v & 0xFFFFFu, adv = v >> 20;
        const uint32_t need = ((uint32_t)d[4] * d[5] * bpp + 7u) / 8u; /* <= 255 x 255 x 8 / 8: no overflow */
        if (idx > blen || need > blen - idx) return false;
        if (i == 0 && (v != 0 || rd32(d + 4) != 0)) return false; /* glyph id 0 is reserved */
        if (g[i].bitmap_index != idx || g[i].adv_w != adv || g[i].box_w != d[4] || g[i].box_h != d[5] ||
            g[i].ofs_x != (int8_t)d[6] || g[i].ofs_y != (int8_t)d[7])
            return false; /* LVGL's view of the bytes is not the file's: another layout */
    }
    h->bpp = bpp;
    h->line_h = p[7];
    h->base = (int8_t)p[8];
    h->ul_pos = (int8_t)p[14];
    h->ul_th = p[15];
    h->dsc_off = dsc_off;
    h->bitmap_off = bitmap_off;
    return true;
}

bool fontasset_parse(const void *data, size_t size, lv_font_t *font, lv_font_fmt_txt_dsc_t *dsc)
{
    const uint8_t *p = data;
    ofa_t h;
    if (font == NULL || dsc == NULL || !ofa_check(p, size, &h)) return false;
    memset(dsc, 0, sizeof *dsc);
    dsc->glyph_bitmap = p + h.bitmap_off;
    dsc->glyph_dsc = (const lv_font_fmt_txt_glyph_dsc_t *)(const void *)(p + h.dsc_off);
    dsc->cmaps = s_cmaps;
    dsc->kern_dsc = NULL;
    dsc->cmap_num = 2;
    dsc->bpp = h.bpp;
    dsc->kern_classes = 0;
    dsc->bitmap_format = LV_FONT_FMT_TXT_PLAIN;
    dsc->stride = 0;
    memset(font, 0, sizeof *font);
    font->get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
    font->get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
    font->line_height = h.line_h;
    font->base_line = h.base;
    font->subpx = LV_FONT_SUBPX_NONE;
    font->underline_position = h.ul_pos;
    font->underline_thickness = (int8_t)h.ul_th;
    font->dsc = dsc;
    font->fallback = NULL;
    font->user_data = NULL;
    return true;
}

bool fontasset_load(lv_font_t *font, lv_font_fmt_txt_dsc_t *dsc, const char *asset)
{
    size_t size = 0;
    const void *p = asset ? assets_raw(asset, &size) : NULL;
    return p != NULL && fontasset_parse(p, size, font, dsc);
}

bool fontasset_info(const void *data, size_t size, unsigned *glyphs, unsigned *line_height)
{
    ofa_t h;
    if (!ofa_check(data, size, &h)) return false;
    if (glyphs) *glyphs = OFA_COUNT;
    if (line_height) *line_height = h.line_h;
    return true;
}
