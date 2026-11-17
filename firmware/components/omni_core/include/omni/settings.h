#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Face ids (carousel[]) are the theme's own uint8_t (plan PD2): the Omni's are apps/omni/main/omni_faces.h. */

enum omni_reveal { OMNI_REVEAL_FLIP = 0, OMNI_REVEAL_IRIS = 1, OMNI_REVEAL_NONE = 2 };

#define OMNI_CAROUSEL_MAX 16

typedef struct {
    uint16_t city;            /* index into the city table (RAM only: persisted as name + country, below) */
    uint8_t  brightness;      /* 5..100, default 70 */
    uint8_t  sleep_s;         /* 5..60, default 15 */
    bool     h24;             /* default false */
    bool     sound_on;        /* default true */
    uint8_t  volume;          /* 0..100, default 30 */
    bool     reduced_motion;  /* default false */
    uint8_t  reveal_style;    /* enum omni_reveal; default FLIP */
    uint8_t  carousel[OMNI_CAROUSEL_MAX];
    uint8_t  carousel_len;
} omni_settings_t;

/* The shared fields' defaults and checks (watch/shared). The carousel and reveal belong to the theme: defaults leaves
 * the carousel empty, validate leaves both alone; omni_local_defaults / omni_local_validate (local.h) own them. */
void omni_settings_defaults(omni_settings_t *s);
bool omni_settings_validate(omni_settings_t *s);   /* clamps the shared fields; false if anything was changed */

/* Persisted form (store.c, NVS blob "settings"). Version 2 keeps the home city as a stable key, its name and
 * country code, so a regenerated city table (a city added, a tzdata refresh) cannot move a saved watch to another
 * city (m4). Little-endian, no padding:
 *   0 uint16 version (2) | 2 name[32] (NUL-padded) | 34 country[4] | 38 brightness | 39 sleep_s | 40 h24 |
 *   41 sound_on | 42 volume | 43 reduced_motion | 44 reveal_style | 45 carousel[16] | 61 carousel_len
 * Version 1 (Stage 1 until this change) was uint16 1 + the raw struct with the city as a table index. It is migrated:
 * the index is read against Stage 1's table, frozen in cities_v1.c (omni_city_from_v1), and resolved by name +
 * country in the current one (an index past that table gives the default city); the store rewrites it as
 * version 2 at once. Bytes for bools are normalised
 * (non-zero = true), and the result is always run through omni_settings_validate (the carousel and reveal as stored:
 * omni_migrate_omni_local checks them against the theme's limits). */
#define OMNI_SETTINGS_BLOB_VERSION 2
#define OMNI_SETTINGS_BLOB_SIZE 62
#define OMNI_SETTINGS_V1_BLOB_SIZE 28

typedef enum {
    OMNI_SETTINGS_UNUSABLE,     /* size or version unknown: the defaults */
    OMNI_SETTINGS_LOADED,       /* version 2 */
    OMNI_SETTINGS_MIGRATED_V1,  /* version 1: the caller should save it again (as version 2) */
    OMNI_SETTINGS_CITY_UNKNOWN, /* version 2, but the city is not in this table: the default city, the rest kept */
} omni_settings_load_t;

size_t omni_settings_encode(const omni_settings_t *s, uint8_t *buf); /* OMNI_SETTINGS_BLOB_SIZE bytes; returns it */
omni_settings_load_t omni_settings_decode(omni_settings_t *s, const uint8_t *buf, size_t n);
