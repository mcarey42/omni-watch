#pragma once
/* watch/shared: the settings every theme shares (city, brightness, sleep, clock format, sound, motion). */
#include "omni/record.h"

#define OMNI_SHARED_VERSION 1
#define OMNI_SHARED_V1_SIZE 46  /* 0 ver | 2 size | 4 name[32] | 36 country[4] | 40 brightness | 41 sleep_s | 42 h24 |
                                   43 sound_on | 44 volume | 45 reduced_motion */
#define OMNI_SHARED_CITY_BYTES 36  /* name[32] + country[4], at offset 4 */

typedef struct {
    uint16_t city;              /* index into the city table; persisted as name + country */
    /* The stored city is not in this build's table (OMNI_REC_CITY_UNKNOWN, e.g. one a newer theme added): city is the
     * default city for display and the sun, and city_raw (the stored name and country bytes) is re-encoded verbatim
     * on every save while city still equals city_kept_idx. The default is never written over it (final review I1);
     * only choosing another city in RAM replaces it. */
    bool city_kept;
    uint16_t city_kept_idx;
    uint8_t city_raw[OMNI_SHARED_CITY_BYTES];
    uint8_t brightness, sleep_s;
    bool h24, sound_on;
    uint8_t volume;
    bool reduced_motion;
    uint16_t tail_len;          /* bytes past V1 that this build does not understand, kept verbatim */
    uint8_t tail[OMNI_REC_MAX - OMNI_SHARED_V1_SIZE];
} omni_shared_t;

void omni_shared_defaults(omni_shared_t *s);
bool omni_shared_validate(omni_shared_t *s);            /* clamps; false if anything was changed */
omni_rec_load_t omni_shared_decode(omni_shared_t *s, const uint8_t *buf, size_t n);
size_t omni_shared_encode(const omni_shared_t *s, uint8_t *buf, size_t cap);   /* V1_SIZE + tail_len; 0 if cap short */
