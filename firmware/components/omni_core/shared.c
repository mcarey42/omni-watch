#include "omni/shared.h"
#include "omni/cities.h"
#include <string.h>

#define NAME_OFF 4
#define NAME_LEN 32
#define COUNTRY_OFF 36
#define COUNTRY_LEN 4
#define FIELDS_OFF 40

_Static_assert(FIELDS_OFF + 6 == OMNI_SHARED_V1_SIZE, "shared v1 layout");
_Static_assert(NAME_LEN + COUNTRY_LEN == OMNI_SHARED_CITY_BYTES && COUNTRY_OFF == NAME_OFF + NAME_LEN, "city bytes");

void omni_shared_defaults(omni_shared_t *s)
{
    *s = (omni_shared_t){0};
    s->city = omni_city_default();
    s->brightness = 70;
    s->sleep_s = 15;
    s->h24 = false;
    s->sound_on = true;
    s->volume = 30;
    s->reduced_motion = false;
}

bool omni_shared_validate(omni_shared_t *s)
{
    bool ok = true;
    if (s->city >= omni_city_count()) { s->city = omni_city_default(); ok = false; }
    if (s->brightness < 5) { s->brightness = 5; ok = false; }
    if (s->brightness > 100) { s->brightness = 100; ok = false; }
    if (s->sleep_s < 5) { s->sleep_s = 5; ok = false; }
    if (s->sleep_s > 60) { s->sleep_s = 60; ok = false; }
    if (s->volume > 100) { s->volume = 100; ok = false; }
    return ok;
}

size_t omni_shared_encode(const omni_shared_t *s, uint8_t *buf, size_t cap)
{
    if (s->tail_len > sizeof s->tail) return 0;
    size_t n = OMNI_SHARED_V1_SIZE + s->tail_len;
    if (cap < n) return 0;
    memset(buf, 0, OMNI_SHARED_V1_SIZE);
    buf[0] = OMNI_SHARED_VERSION & 0xff;
    buf[1] = OMNI_SHARED_VERSION >> 8;
    buf[2] = (uint8_t)(n & 0xff);
    buf[3] = (uint8_t)(n >> 8);
    const omni_city_t *c = omni_city_at(s->city);
    if (s->city_kept && s->city == s->city_kept_idx) {
        memcpy(&buf[NAME_OFF], s->city_raw, OMNI_SHARED_CITY_BYTES);   /* not ours to replace (I1) */
    } else if (c != NULL) {
        strncpy((char *)&buf[NAME_OFF], c->name, NAME_LEN - 1);
        strncpy((char *)&buf[COUNTRY_OFF], c->country, COUNTRY_LEN - 1);
    }
    uint8_t *f = &buf[FIELDS_OFF];
    f[0] = s->brightness;
    f[1] = s->sleep_s;
    f[2] = s->h24 ? 1 : 0;
    f[3] = s->sound_on ? 1 : 0;
    f[4] = s->volume;
    f[5] = s->reduced_motion ? 1 : 0;
    if (s->tail_len) memcpy(&buf[OMNI_SHARED_V1_SIZE], s->tail, s->tail_len);
    return n;
}

omni_rec_load_t omni_shared_decode(omni_shared_t *s, const uint8_t *buf, size_t n)
{
    omni_shared_defaults(s);
    uint16_t ver, size;
    if (!omni_rec_header(buf, n, &ver, &size)) return OMNI_REC_UNUSABLE;
    if (ver != OMNI_SHARED_VERSION) return OMNI_REC_UNKNOWN_VERSION;
    if (size < OMNI_SHARED_V1_SIZE) return OMNI_REC_UNUSABLE;

    char name[NAME_LEN], country[COUNTRY_LEN];
    memcpy(s->city_raw, &buf[NAME_OFF], OMNI_SHARED_CITY_BYTES);
    memcpy(name, &buf[NAME_OFF], NAME_LEN);
    memcpy(country, &buf[COUNTRY_OFF], COUNTRY_LEN);
    name[NAME_LEN - 1] = country[COUNTRY_LEN - 1] = '\0';
    const uint8_t *f = &buf[FIELDS_OFF];
    s->brightness = f[0];
    s->sleep_s = f[1];
    s->h24 = f[2] != 0;
    s->sound_on = f[3] != 0;
    s->volume = f[4];
    s->reduced_motion = f[5] != 0;
    int i = omni_city_find_key(name, country);
    if (i >= 0) s->city = (uint16_t)i; /* else the default city from omni_shared_defaults */
    s->tail_len = (uint16_t)(size - OMNI_SHARED_V1_SIZE);
    if (s->tail_len) memcpy(s->tail, &buf[OMNI_SHARED_V1_SIZE], s->tail_len);
    omni_shared_validate(s);
    if (i < 0) {
        s->city_kept = true;
        s->city_kept_idx = s->city;
        return OMNI_REC_CITY_UNKNOWN;
    }
    memset(s->city_raw, 0, sizeof s->city_raw);
    return s->tail_len ? OMNI_REC_TAIL_KEPT : OMNI_REC_LOADED;
}
