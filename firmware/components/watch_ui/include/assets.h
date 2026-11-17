#pragma once
/* The "assets" partition (the app's own image, firmware/apps/<theme>/assets/, packed by esp_mmap_assets): LVGL .bin
 * images and raw files such as the globe textures, memory-mapped from flash. Nothing is copied into internal RAM; the
 * only copies are the PSRAM ones assets_image_psram makes on request. Names are file names, e.g. "earth512.tex" or
 * "bg_bezel.bin" (the app's asset manifest has a macro for each, with its geometry). The file count and checksum the
 * image must have come from the theme descriptor (watch_theme.h). Call from any task after assets_init; the mapping
 * never changes. */
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "lvgl.h"

esp_err_t assets_init(void);                              /* mmap partition "assets"; on error, faces needing it are not registered */
bool assets_ready(void);
const void *assets_raw(const char *name, size_t *size);   /* e.g. "earth512.tex"; NULL if absent */
bool assets_image(const char *name, lv_image_dsc_t *out); /* LVGL .bin: header parsed, data points into flash */
lv_draw_buf_t *assets_image_psram(const char *name);      /* PSRAM copy (theme_buf_create); free with theme_buf_destroy */

/* For the console: entry i (0 .. assets_count()-1), and the whole-partition checksum check (reads all of it). */
int assets_count(void);
const char *assets_entry(int i, const void **data, size_t *size);
bool assets_checksum_ok(void);
