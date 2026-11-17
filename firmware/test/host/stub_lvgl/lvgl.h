#pragma once
/* A stand-in for the few LVGL 9.5 font types watch_ui/fontasset.c uses, so the host suite can prove its bounds checks
 * (test_fontasset). The structs copy lvgl 9.5.0's src/font/lv_font.h and fmt_txt/lv_font_fmt_txt.h field for field
 * (LV_FONT_FMT_TXT_LARGE 0); the firmware build uses the real header and fontasset.c's static assert. Host only. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LV_FONT_FMT_TXT_LARGE 0

typedef struct _lv_font_t lv_font_t;
typedef struct lv_font_glyph_dsc_t lv_font_glyph_dsc_t;
typedef struct lv_draw_buf_t lv_draw_buf_t;

typedef enum { LV_FONT_SUBPX_NONE, LV_FONT_SUBPX_HOR, LV_FONT_SUBPX_VER, LV_FONT_SUBPX_BOTH } lv_font_subpx_t;

struct _lv_font_t {
    bool (*get_glyph_dsc)(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t letter, uint32_t letter_next);
    const void *(*get_glyph_bitmap)(lv_font_glyph_dsc_t *, lv_draw_buf_t *);
    void (*release_glyph)(const lv_font_t *, lv_font_glyph_dsc_t *);
    int32_t line_height;
    int32_t base_line;
    uint8_t subpx : 2;
    uint8_t kerning : 1;
    uint8_t static_bitmap : 1;
    int8_t underline_position;
    int8_t underline_thickness;
    const void *dsc;
    const lv_font_t *fallback;
    void *user_data;
};

typedef struct {
    uint32_t bitmap_index : 20;
    uint32_t adv_w : 12;
    uint8_t box_w;
    uint8_t box_h;
    int8_t ofs_x;
    int8_t ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;

typedef enum {
    LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
    LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
    LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
} lv_font_fmt_txt_cmap_type_t;

typedef struct {
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    const uint16_t *unicode_list;
    const void *glyph_id_ofs_list;
    uint16_t list_length;
    lv_font_fmt_txt_cmap_type_t type;
} lv_font_fmt_txt_cmap_t;

typedef enum {
    LV_FONT_FMT_TXT_PLAIN = 0,
    LV_FONT_FMT_TXT_COMPRESSED = 1,
    LV_FONT_FMT_TXT_COMPRESSED_NO_PREFILTER = 2,
} lv_font_fmt_txt_bitmap_format_t;

typedef struct {
    const uint8_t *glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
    const lv_font_fmt_txt_cmap_t *cmaps;
    const void *kern_dsc;
    uint16_t kern_scale;
    uint16_t cmap_num : 9;
    uint16_t bpp : 4;
    uint16_t kern_classes : 1;
    uint16_t bitmap_format : 2;
    uint8_t stride;
} lv_font_fmt_txt_dsc_t;

const void *lv_font_get_bitmap_fmt_txt(lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf);
bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t unicode_letter,
                                   uint32_t unicode_letter_next);
