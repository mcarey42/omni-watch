#include "omni/nav.h"

#define STACK_MAX 4

void omni_nav_init(omni_nav_t *n)
{
    *n = (omni_nav_t){0};
    n->stack[0] = SCR_FACE;
    n->depth = 1;
}

omni_screen_t omni_nav_top(const omni_nav_t *n) { return n->stack[n->depth - 1]; }

uint8_t omni_nav_current_face(const omni_nav_t *n, const omni_settings_t *s)
{
    if (n->transient) return (uint8_t)(n->transient - 1);
    if (s->carousel_len == 0) return 0;
    uint8_t i = n->face_idx < s->carousel_len ? n->face_idx : (uint8_t)(s->carousel_len - 1);
    return s->carousel[i];
}

omni_nav_effect_t omni_nav_push(omni_nav_t *n, omni_screen_t scr)
{
    if (n->depth >= STACK_MAX) return NAV_NONE;
    n->stack[n->depth++] = scr;
    return NAV_SCREEN_PUSHED;
}

omni_nav_effect_t omni_nav_pop(omni_nav_t *n)
{
    if (n->depth <= 1) return NAV_NONE;
    n->depth--;
    return NAV_SCREEN_POPPED;
}

static omni_nav_effect_t step(omni_nav_t *n, const omni_settings_t *s, int dir)
{
    if (s->carousel_len == 0) return NAV_NONE;
    n->transient = 0;
    n->face_idx = (uint8_t)((n->face_idx + s->carousel_len + dir) % s->carousel_len);
    return NAV_FACE_CHANGED;
}

omni_nav_effect_t omni_nav_gesture(omni_nav_t *n, const omni_settings_t *s, const omni_gesture_t *g)
{
    if (omni_nav_top(n) != SCR_FACE) return NAV_NONE;
    switch (g->kind) {
    case G_SWIPE_L: return step(n, s, +1);
    case G_SWIPE_R: return step(n, s, -1);
    case G_LONG_PRESS: return omni_nav_push(n, SCR_PICKER);
    case G_SWIPE_DOWN_FROM_TOP: return omni_nav_push(n, SCR_QUICK);
    default: return NAV_NONE;   /* swipe up on a face: nothing (R144 retired the timer shortcut) */
    }
}

omni_nav_effect_t omni_nav_boot(omni_nav_t *n, const omni_settings_t *s, omni_press_t p)
{
    switch (p) {
    case B_SHORT:
        if (n->depth > 1) return omni_nav_pop(n);
        return step(n, s, +1);
    case B_DOUBLE: return NAV_REPLAY_REVEAL;
    case B_LONG: return omni_nav_top(n) == SCR_PICKER ? NAV_NONE : omni_nav_push(n, SCR_PICKER);
    default: return NAV_NONE;
    }
}

omni_nav_effect_t omni_nav_pick(omni_nav_t *n, const omni_settings_t *s, uint8_t face_idx)
{
    if (s->carousel_len == 0) return NAV_NONE;
    if (face_idx >= s->carousel_len) face_idx = (uint8_t)(s->carousel_len - 1);
    const bool moved = n->face_idx != face_idx || n->transient;   /* a transient face is redrawn as the saved one */
    n->transient = 0;
    n->face_idx = face_idx;
    n->depth = 1;
    return moved ? NAV_FACE_CHANGED : NAV_SCREEN_POPPED;
}

omni_nav_effect_t omni_nav_lift(omni_nav_t *n, const omni_settings_t *s, int idx)
{
    if (idx < 0 || s->carousel_len == 0 || omni_nav_top(n) != SCR_FACE) return NAV_NONE;
    const uint8_t i = idx >= s->carousel_len ? (uint8_t)(s->carousel_len - 1) : (uint8_t)idx;
    if (i == n->face_idx) return NAV_NONE;
    n->transient = 0;
    n->face_idx = i;
    return NAV_FACE_CHANGED;
}

int omni_theme_set_pack(uint8_t row, uint8_t value) { return (int)row | (int)value << 8; }

bool omni_theme_set_unpack(int arg, uint8_t count, uint8_t *row, uint8_t *value)
{
    const unsigned r = (unsigned)arg & 0xffu;
    if (r >= count) return false;
    *row = (uint8_t)r;
    *value = (uint8_t)((unsigned)arg >> 8);
    return true;
}

int omni_theme_rows(const omni_theme_row_t *rows, int count, int max, uint8_t keep[])
{
    int m = 0;
    for (int k = 0; rows && k < count && k < max; k++) {
        const omni_theme_row_t *w = &rows[k];
        if (w->get && (w->n ? w->opts && w->shorts : w->hours)) keep[m++] = (uint8_t)k;
    }
    return m;
}

bool omni_nav_pick_saves(omni_nav_effect_t eff, uint8_t face_idx, uint8_t saved)
{
    return eff == NAV_FACE_CHANGED || (eff == NAV_SCREEN_POPPED && face_idx != saved);
}

omni_nav_effect_t omni_nav_replace(omni_nav_t *n, omni_screen_t scr)
{
    if (n->depth <= 1) return NAV_NONE;
    n->stack[n->depth - 1] = scr;
    return NAV_SCREEN_PUSHED;
}

omni_nav_effect_t omni_nav_home(omni_nav_t *n)
{
    if (n->depth <= 1) return NAV_NONE;
    n->depth = 1;
    return NAV_SCREEN_POPPED;
}

omni_nav_effect_t omni_nav_show_id(omni_nav_t *n, const omni_settings_t *s, uint8_t face_id, uint8_t face_count)
{
    for (uint8_t i = 0; i < s->carousel_len; i++)
        if (s->carousel[i] == face_id) return omni_nav_pick(n, s, i);
    if (face_id >= face_count) return NAV_NONE;
    n->depth = 1;
    n->transient = (uint8_t)(face_id + 1);
    return NAV_TRANSIENT;
}

bool omni_nav_is_tool(omni_screen_t s)
{
    return (1u << s) & ((1u << SCR_SETTINGS) | (1u << SCR_NETLIST) | (1u << SCR_FACES) | (1u << SCR_KEYBOARD));
}

omni_nav_effect_t omni_nav_wake_full(omni_nav_t *n)
{
    if (omni_nav_is_tool(omni_nav_top(n))) {
        uint8_t floor = n->depth;   /* never pops SCR_PROVISION or anything below its topmost */
        while (floor > 1 && n->stack[floor - 1] != SCR_PROVISION) floor--;
        n->depth = floor;
        n->transient = 0;
        return NAV_SCREEN_POPPED;
    }
    if (!n->transient) return NAV_NONE;
    n->transient = 0;
    return NAV_TRANSIENT;
}

uint8_t omni_nav_sleep_s(const omni_nav_t *n, uint8_t sleep_s)
{
    const omni_screen_t t = omni_nav_top(n);
    if (t == SCR_KEYBOARD) return 61;
    if (omni_nav_is_tool(t) && sleep_s < 30) return 30;
    return sleep_s;
}

bool omni_nav_in_stack(const omni_nav_t *n, omni_screen_t scr)
{
    for (uint8_t i = 0; i < n->depth; i++)
        if (n->stack[i] == scr) return true;
    return false;
}

omni_nav_effect_t omni_nav_open_netlist(omni_nav_t *n)
{
    uint8_t d = n->depth;   /* pop down to the topmost SCR_PROVISION and replace it */
    while (d > 1 && n->stack[d - 1] != SCR_PROVISION) d--;
    if (d <= 1) return omni_nav_push(n, SCR_NETLIST);
    n->depth = d;
    return omni_nav_replace(n, SCR_NETLIST);
}

omni_nav_effect_t omni_nav_open_settings(omni_nav_t *n)
{
    uint8_t d = n->depth;   /* the topmost SCR_SETTINGS, if any */
    while (d > 1 && n->stack[d - 1] != SCR_SETTINGS) d--;
    if (d > 1) {
        if (d == n->depth) return NAV_NONE;
        n->depth = d;
        return NAV_SCREEN_POPPED;
    }
    d = n->depth;
    if (omni_nav_top(n) == SCR_PICKER) d--;   /* in its place */
    while (d > 1 && omni_nav_is_tool(n->stack[d - 1])) d--;
    if (d >= STACK_MAX) return NAV_NONE;
    n->depth = d;
    return omni_nav_push(n, SCR_SETTINGS);
}
