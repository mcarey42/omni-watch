#pragma once
/* t_<id>/local: the prefix every theme's own record shares (reveal, carousel, current face). */
#include "omni/record.h"
#include "omni/settings.h"
#include "omni/shared.h"

#define OMNI_LOCAL_VERSION 1
#define OMNI_LOCAL_V1_SIZE 23   /* 0 ver | 2 size | 4 reveal_style | 5 carousel_len | 6 carousel[16] | 22 face_idx */

typedef struct {
    uint8_t face_count, reveal_count, reveal_default, def_len;
    const uint8_t *def_carousel;
} omni_theme_limits_t;

typedef struct {
    uint8_t reveal_style, carousel_len, carousel[OMNI_CAROUSEL_MAX], face_idx;
    uint16_t tail_len;          /* bytes past V1 that this build does not understand, kept verbatim */
    uint8_t tail[OMNI_REC_MAX - OMNI_LOCAL_V1_SIZE];
} omni_local_t;

void omni_local_defaults(omni_local_t *l, const omni_theme_limits_t *lim);
/* ids < face_count and no duplicates (both dropped), len >= 1, reveal < reveal_count, face_idx < len */
bool omni_local_validate(omni_local_t *l, const omni_theme_limits_t *lim);
omni_rec_load_t omni_local_decode(omni_local_t *l, const uint8_t *buf, size_t n, const omni_theme_limits_t *lim);
size_t omni_local_encode(const omni_local_t *l, uint8_t *buf, size_t cap);

/* the RAM view the UI keeps using (omni_settings_t), to and from the two records; tails untouched */
void omni_settings_join(const omni_shared_t *sh, const omni_local_t *lo, omni_settings_t *s);
void omni_settings_split(const omni_settings_t *s, omni_shared_t *sh, omni_local_t *lo);
