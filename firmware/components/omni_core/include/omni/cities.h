#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const char *country;
    const char *tz_name;
    const char *posix_tz;
    float lat, lon;
} omni_city_t;

size_t omni_city_count(void);
const omni_city_t *omni_city_at(size_t i);   /* NULL if out of range */
int omni_city_find(const char *name);        /* index or -1 */
int omni_city_find_key(const char *name, const char *country); /* the stable key (settings.c): index or -1 */
/* The neutral default home city of a fresh watch. It never applies over a saved record. */
#define OMNI_DEFAULT_CITY_NAME "London"
#define OMNI_DEFAULT_CITY_CC "GB"
uint16_t omni_city_default(void);            /* the default's index (0 if the table lacks it) */
/* A version 1 settings blob's city index (Stage 1's table, frozen in cities_v1.c) as an index into this table, or -1. */
int omni_city_from_v1(uint16_t v1_index);
/* English short name of an ISO 3166 code used by the table ("JP" -> "Japan"); unknown codes are returned as is. */
const char *omni_country_name(const char *code);
