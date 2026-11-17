#include "omni/local.h"
#include <string.h>

_Static_assert(OMNI_LOCAL_V1_SIZE == 4 + 1 + 1 + OMNI_CAROUSEL_MAX + 1, "local v1 layout");

#define REVEAL_OFF 4
#define LEN_OFF 5
#define CAROUSEL_OFF 6
#define FACE_OFF (CAROUSEL_OFF + OMNI_CAROUSEL_MAX)

static void default_carousel(omni_local_t *l, const omni_theme_limits_t *lim)
{
    memset(l->carousel, 0, sizeof l->carousel);
    uint8_t n = lim->def_len > OMNI_CAROUSEL_MAX ? OMNI_CAROUSEL_MAX : lim->def_len;
    for (uint8_t i = 0; i < n; i++) l->carousel[i] = lim->def_carousel[i];
    l->carousel_len = n;
}

void omni_local_defaults(omni_local_t *l, const omni_theme_limits_t *lim)
{
    *l = (omni_local_t){0};
    l->reveal_style = lim->reveal_default;
    default_carousel(l, lim);
    l->face_idx = 0;
}

bool omni_local_validate(omni_local_t *l, const omni_theme_limits_t *lim)
{
    bool ok = true;
    if (l->reveal_style >= lim->reveal_count) { l->reveal_style = lim->reveal_default; ok = false; }
    if (l->carousel_len > OMNI_CAROUSEL_MAX) { l->carousel_len = OMNI_CAROUSEL_MAX; ok = false; }
    /* keep valid face ids only, dropping unknown ones and duplicates */
    uint8_t kept[OMNI_CAROUSEL_MAX];
    uint8_t n = 0;
    for (uint8_t i = 0; i < l->carousel_len; i++) {
        uint8_t f = l->carousel[i];
        bool dup = false;
        for (uint8_t j = 0; j < n; j++) if (kept[j] == f) dup = true;
        if (f >= lim->face_count || dup) { ok = false; continue; }
        kept[n++] = f;
    }
    if (n == 0) {
        default_carousel(l, lim);
        ok = false;
    } else if (n != l->carousel_len) {
        for (uint8_t i = 0; i < n; i++) l->carousel[i] = kept[i];
        l->carousel_len = n;
    }
    if (l->face_idx >= l->carousel_len) { l->face_idx = 0; ok = false; }
    /* canonical: nothing stale past the length, so a re-encode is deterministic */
    for (uint8_t i = l->carousel_len; i < OMNI_CAROUSEL_MAX; i++) l->carousel[i] = 0;
    return ok;
}

size_t omni_local_encode(const omni_local_t *l, uint8_t *buf, size_t cap)
{
    if (l->tail_len > sizeof l->tail) return 0;
    size_t n = OMNI_LOCAL_V1_SIZE + l->tail_len;
    if (cap < n) return 0;
    memset(buf, 0, OMNI_LOCAL_V1_SIZE);
    buf[0] = OMNI_LOCAL_VERSION & 0xff;
    buf[1] = OMNI_LOCAL_VERSION >> 8;
    buf[2] = (uint8_t)(n & 0xff);
    buf[3] = (uint8_t)(n >> 8);
    buf[REVEAL_OFF] = l->reveal_style;
    buf[LEN_OFF] = l->carousel_len;
    memcpy(&buf[CAROUSEL_OFF], l->carousel, OMNI_CAROUSEL_MAX);
    buf[FACE_OFF] = l->face_idx;
    if (l->tail_len) memcpy(&buf[OMNI_LOCAL_V1_SIZE], l->tail, l->tail_len);
    return n;
}

omni_rec_load_t omni_local_decode(omni_local_t *l, const uint8_t *buf, size_t n, const omni_theme_limits_t *lim)
{
    omni_local_defaults(l, lim);
    uint16_t ver, size;
    if (!omni_rec_header(buf, n, &ver, &size)) return OMNI_REC_UNUSABLE;
    if (ver != OMNI_LOCAL_VERSION) return OMNI_REC_UNKNOWN_VERSION;
    if (size < OMNI_LOCAL_V1_SIZE) return OMNI_REC_UNUSABLE;
    l->reveal_style = buf[REVEAL_OFF];
    l->carousel_len = buf[LEN_OFF];
    memcpy(l->carousel, &buf[CAROUSEL_OFF], OMNI_CAROUSEL_MAX);
    l->face_idx = buf[FACE_OFF];
    l->tail_len = (uint16_t)(size - OMNI_LOCAL_V1_SIZE);
    if (l->tail_len) memcpy(l->tail, &buf[OMNI_LOCAL_V1_SIZE], l->tail_len);
    omni_local_validate(l, lim);
    return l->tail_len ? OMNI_REC_TAIL_KEPT : OMNI_REC_LOADED;
}

/* The RAM view: omni_settings_t has no face index, so split leaves face_idx and both tails as the caller had them. */
void omni_settings_join(const omni_shared_t *sh, const omni_local_t *lo, omni_settings_t *s)
{
    *s = (omni_settings_t){0};
    s->city = sh->city;
    s->brightness = sh->brightness;
    s->sleep_s = sh->sleep_s;
    s->h24 = sh->h24;
    s->sound_on = sh->sound_on;
    s->volume = sh->volume;
    s->reduced_motion = sh->reduced_motion;
    s->reveal_style = lo->reveal_style;
    memcpy(s->carousel, lo->carousel, OMNI_CAROUSEL_MAX);
    s->carousel_len = lo->carousel_len;
}

void omni_settings_split(const omni_settings_t *s, omni_shared_t *sh, omni_local_t *lo)
{
    sh->city = s->city;
    if (sh->city != sh->city_kept_idx) sh->city_kept = false;   /* another city chosen: the kept one is dropped (N1) */
    sh->brightness = s->brightness;
    sh->sleep_s = s->sleep_s;
    sh->h24 = s->h24;
    sh->sound_on = s->sound_on;
    sh->volume = s->volume;
    sh->reduced_motion = s->reduced_motion;
    lo->reveal_style = s->reveal_style;
    memcpy(lo->carousel, s->carousel, OMNI_CAROUSEL_MAX);
    lo->carousel_len = s->carousel_len;
}
