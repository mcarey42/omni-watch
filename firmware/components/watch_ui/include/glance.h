#pragma once
/* The shared glance (always-on) layout: design/mockups/faces-practical.js glanceTime(). The time in fixed cells
 * (IBM Plex Sans 76 px at 30 %) under the short date (Cormorant SC 15 px, brass at 38 %), an optional sub-line
 * (IBM Plex Mono 13 px at 38 %), the whole layout offset by omni_orbit_offset against burn-in. Colours only (R28).
 * LVGL lock held for every call. */
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef struct glance glance_t;

glance_t *glance_create(lv_obj_t *parent); /* hidden; children of parent. NULL if out of memory */
void glance_update(glance_t *g, int64_t now, bool h24, const char *sub); /* sub NULL or "" = no sub-line */
void glance_show(glance_t *g, bool on);
void glance_free(glance_t *g);             /* frees the struct only; the objects go with parent */

/* drawCells: the characters of s in fixed cells (digits `cell` wide, ':' `colon`) centred on cx, baseline y, one
 * label per character; labels past the end of s are emptied. Returns the total width. */
#define GLANCE_MAX_CELLS 5
float glance_place_cells(lv_obj_t *const *cells, const char *s, float cx, int32_t y, float cell, float colon);
