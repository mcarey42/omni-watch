#include "omni/settings.h"
#include "omni/cities.h"
#include "omni/shared.h"
#include <string.h>

/* The shared fields only. The carousel and reveal are the theme's (t_<id>/local): omni_local_defaults gives them from
 * the theme's limits and omni_local_validate checks them (Task 6), so here the carousel is empty and the reveal FLIP. */
void omni_settings_defaults(omni_settings_t *s)
{
    *s = (omni_settings_t){0};
    s->city = omni_city_default();
    s->brightness = 70;
    s->sleep_s = 15;
    s->h24 = false;
    s->sound_on = true;
    s->volume = 30;
    s->reduced_motion = false;
    s->reveal_style = OMNI_REVEAL_FLIP;
}

bool omni_settings_validate(omni_settings_t *s)
{
    omni_shared_t sh;
    omni_shared_defaults(&sh);
    sh.city = s->city; sh.brightness = s->brightness; sh.sleep_s = s->sleep_s; sh.volume = s->volume;
    bool ok = omni_shared_validate(&sh);
    s->city = sh.city; s->brightness = sh.brightness; s->sleep_s = sh.sleep_s; s->volume = sh.volume;
    return ok;
}

int omni_city_find_key(const char *name, const char *country)
{
    for (size_t i = 0; i < omni_city_count(); i++) {
        const omni_city_t *c = omni_city_at(i);
        if (strcmp(c->name, name) == 0 && strcmp(c->country, country) == 0) return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------------ persisted form (layout in settings.h) */
#define NAME_OFF 2
#define NAME_LEN 32
#define COUNTRY_OFF 34
#define COUNTRY_LEN 4
#define FIELDS_OFF 38  /* brightness .. carousel_len, as in version 1 from its offset 4 */
#define V1_CITY_OFF 2
#define V1_FIELDS_OFF 4
#define FIELDS_LEN 24  /* brightness, sleep_s, h24, sound_on, volume, reduced_motion, reveal_style, carousel[16], len */

_Static_assert(FIELDS_OFF + FIELDS_LEN == OMNI_SETTINGS_BLOB_SIZE, "settings blob layout");
_Static_assert(V1_FIELDS_OFF + FIELDS_LEN == OMNI_SETTINGS_V1_BLOB_SIZE, "settings v1 blob layout");

static void put_fields(const omni_settings_t *s, uint8_t *f)
{
    f[0] = s->brightness;
    f[1] = s->sleep_s;
    f[2] = s->h24 ? 1 : 0;
    f[3] = s->sound_on ? 1 : 0;
    f[4] = s->volume;
    f[5] = s->reduced_motion ? 1 : 0;
    f[6] = s->reveal_style;
    memcpy(&f[7], s->carousel, OMNI_CAROUSEL_MAX);
    f[7 + OMNI_CAROUSEL_MAX] = s->carousel_len;
}

static void get_fields(omni_settings_t *s, const uint8_t *f)
{
    s->brightness = f[0];
    s->sleep_s = f[1];
    s->h24 = f[2] != 0;
    s->sound_on = f[3] != 0;
    s->volume = f[4];
    s->reduced_motion = f[5] != 0;
    s->reveal_style = f[6];
    memcpy(s->carousel, &f[7], OMNI_CAROUSEL_MAX);
    s->carousel_len = f[7 + OMNI_CAROUSEL_MAX];
}

size_t omni_settings_encode(const omni_settings_t *s, uint8_t *buf)
{
    memset(buf, 0, OMNI_SETTINGS_BLOB_SIZE);
    buf[0] = OMNI_SETTINGS_BLOB_VERSION & 0xff;
    buf[1] = OMNI_SETTINGS_BLOB_VERSION >> 8;
    const omni_city_t *c = omni_city_at(s->city);
    if (c != NULL) {
        strncpy((char *)&buf[NAME_OFF], c->name, NAME_LEN - 1);
        strncpy((char *)&buf[COUNTRY_OFF], c->country, COUNTRY_LEN - 1);
    }
    put_fields(s, &buf[FIELDS_OFF]);
    return OMNI_SETTINGS_BLOB_SIZE;
}

omni_settings_load_t omni_settings_decode(omni_settings_t *s, const uint8_t *buf, size_t n)
{
    omni_settings_defaults(s);
    uint16_t ver = n >= 2 ? (uint16_t)(buf[0] | buf[1] << 8) : 0;
    omni_settings_load_t r;
    if (ver == OMNI_SETTINGS_BLOB_VERSION && n == OMNI_SETTINGS_BLOB_SIZE) {
        char name[NAME_LEN], country[COUNTRY_LEN];
        memcpy(name, &buf[NAME_OFF], NAME_LEN);
        memcpy(country, &buf[COUNTRY_OFF], COUNTRY_LEN);
        name[NAME_LEN - 1] = country[COUNTRY_LEN - 1] = '\0';
        get_fields(s, &buf[FIELDS_OFF]);
        int i = omni_city_find_key(name, country);
        if (i >= 0) s->city = (uint16_t)i; /* else the default city from omni_settings_defaults */
        r = i >= 0 ? OMNI_SETTINGS_LOADED : OMNI_SETTINGS_CITY_UNKNOWN;
    } else if (ver == 1 && n == OMNI_SETTINGS_V1_BLOB_SIZE) {
        /* the index is into Stage 1's table, which later tables need not keep in order (a city added sorts in) */
        int i = omni_city_from_v1((uint16_t)(buf[V1_CITY_OFF] | buf[V1_CITY_OFF + 1] << 8));
        if (i >= 0) s->city = (uint16_t)i; /* else the default city */
        get_fields(s, &buf[V1_FIELDS_OFF]);
        r = OMNI_SETTINGS_MIGRATED_V1;
    } else {
        return OMNI_SETTINGS_UNUSABLE;
    }
    omni_settings_validate(s);
    return r;
}
