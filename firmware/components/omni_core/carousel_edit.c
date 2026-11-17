#include "omni/carousel_edit.h"

static int find(const omni_carousel_t *c, uint8_t id)
{
    for (int i = 0; i < c->len; i++) if (c->ids[i] == id) return i;
    return -1;
}

bool omni_carousel_toggle(omni_carousel_t *c, uint8_t id, const omni_theme_limits_t *lim)
{
    int i = find(c, id);
    if (i < 0) {
        if (id >= lim->face_count || c->len >= OMNI_CAROUSEL_MAX) return false;
        c->ids[c->len++] = id;
        return true;
    }
    if (c->len <= 1) return false;
    for (int k = i; k + 1 < c->len; k++) c->ids[k] = c->ids[k + 1];
    c->ids[--c->len] = 0;   /* nothing stale past the length (the record is canonical) */
    if (c->face == id) c->face = c->ids[i < c->len ? i : i - 1];
    return true;
}

bool omni_carousel_move(omni_carousel_t *c, uint8_t id, int dir)
{
    int i = find(c, id), j = i + dir;
    if (i < 0 || (dir != -1 && dir != 1) || j < 0 || j >= c->len) return false;
    c->ids[i] = c->ids[j];
    c->ids[j] = id;
    return true;
}

void omni_carousel_restore(omni_carousel_t *c, const omni_theme_limits_t *lim)
{
    uint8_t n = lim->def_len > OMNI_CAROUSEL_MAX ? OMNI_CAROUSEL_MAX : lim->def_len;
    for (uint8_t i = 0; i < n; i++) c->ids[i] = lim->def_carousel[i];
    for (uint8_t i = n; i < OMNI_CAROUSEL_MAX; i++) c->ids[i] = 0;
    c->len = n;
    if (find(c, c->face) < 0) c->face = lim->def_carousel[0];
}

int omni_carousel_ring(const omni_carousel_t *c, uint8_t face_count, uint8_t out[OMNI_CAROUSEL_MAX], uint8_t *shown)
{
    int n = 0;
    for (; n < c->len; n++) out[n] = c->ids[n];
    *shown = c->len;
    for (uint8_t id = 0; id < face_count && n < OMNI_CAROUSEL_MAX; id++)
        if (find(c, id) < 0) out[n++] = id;
    return n;
}

uint8_t omni_carousel_face_idx(const omni_carousel_t *c)
{
    int i = find(c, c->face);
    return i < 0 ? 0 : (uint8_t)i;
}
