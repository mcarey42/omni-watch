#pragma once
/* The swipe-order face list's editing (Settings > Faces): show, hide, reorder, restore. Pure; the caller writes the
 * result into t_<id>/local through the event loop. */
#include <stdbool.h>
#include <stdint.h>
#include "omni/local.h"

typedef struct {
    uint8_t ids[OMNI_CAROUSEL_MAX];
    uint8_t len;
    uint8_t face;   /* the current (saved) face id */
} omni_carousel_t;

/* shown -> hidden (refused for the last shown face); hidden -> appended at the end (refused at 16, or for an id at or
 * above face_count). Hiding the current face makes the face that takes its slot current, or the one before it when it
 * was last. */
bool omni_carousel_toggle(omni_carousel_t *c, uint8_t id, const omni_theme_limits_t *lim);
/* -1 earlier, +1 later; refused at the ends, for a hidden id, or for any other dir. The current face stays current. */
bool omni_carousel_move(omni_carousel_t *c, uint8_t id, int dir);
/* The default; face kept if in it, else def_carousel[0]. */
void omni_carousel_restore(omni_carousel_t *c, const omni_theme_limits_t *lim);
/* The editor's ring: the carousel in order, then every other id < face_count ascending; *shown = len. Returns the ring's
 * length. */
int omni_carousel_ring(const omni_carousel_t *c, uint8_t face_count, uint8_t out[OMNI_CAROUSEL_MAX], uint8_t *shown);
/* The index of face in ids (0 when it is not there). */
uint8_t omni_carousel_face_idx(const omni_carousel_t *c);
