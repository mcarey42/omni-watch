/* Host-only test oracle (never a production path): main 4ef03a7's components/omni_core/gesture.c, copied verbatim
 * apart from the two renamed functions (oracle_gesture_reset / oracle_gesture_feed). test_gesture.c feeds the same
 * touch streams through this and through omni_gesture_feed (want 0) and requires identical output. */
#include "omni/input.h"
#include <stdlib.h>

#define MOVE_PX      12
#define TAP_MS       300
#define LONG_MS      600
#define SWIPE_PX     60
#define TOP_START_Y  60
#define CENTRE       233
#define EDGE_R       193

enum { PH_IDLE, PH_PRESSED, PH_DRAG, PH_MOVED, PH_DONE };

void oracle_gesture_reset(omni_gesture_sm_t *g)
{
    g->x0 = g->y0 = g->x = g->y = 0;
    g->t0 = 0;
    g->phase = PH_IDLE;
    g->claimed = false;
}

static void emit(const omni_gesture_sm_t *g, omni_gesture_t *o, omni_gesture_kind_t k)
{
    int dx = g->x0 - CENTRE, dy = g->y0 - CENTRE;
    o->kind = k;
    o->x = g->x;
    o->y = g->y;
    o->x0 = g->x0;
    o->y0 = g->y0;
    o->edge_start = dx * dx + dy * dy >= EDGE_R * EDGE_R;
}

static omni_gesture_kind_t swipe_kind(const omni_gesture_sm_t *g)
{
    int dx = g->x - g->x0, dy = g->y - g->y0;
    if (abs(dx) >= abs(dy)) {
        if (abs(dx) >= SWIPE_PX) return dx < 0 ? G_SWIPE_L : G_SWIPE_R;
    } else if (abs(dy) >= SWIPE_PX) {
        if (dy < 0) return G_SWIPE_UP;
        if (g->y0 < TOP_START_Y) return G_SWIPE_DOWN_FROM_TOP;
    }
    return G_NONE;
}

int oracle_gesture_feed(omni_gesture_sm_t *g, uint32_t t_ms, bool pressed, int16_t x, int16_t y,
                      bool (*claim_drag)(int16_t x0, int16_t y0, void *ctx), void *ctx,
                      omni_gesture_t out[2])
{
    int n = 0;
    bool moved_pos;

    if (g->phase == PH_IDLE) {
        if (!pressed) return 0;
        g->x0 = g->x = x;
        g->y0 = g->y = y;
        g->t0 = t_ms;
        g->phase = PH_PRESSED;
        g->claimed = false;
        return 0;
    }

    moved_pos = (x != g->x || y != g->y);
    g->x = x;
    g->y = y;

    if (g->phase == PH_PRESSED) {
        int dx = x - g->x0, dy = y - g->y0;
        if (dx * dx + dy * dy > MOVE_PX * MOVE_PX) {
            g->claimed = claim_drag ? claim_drag(g->x0, g->y0, ctx) : false;
            if (g->claimed) {
                g->phase = PH_DRAG;
                emit(g, &out[n++], G_DRAG_START);
            } else {
                g->phase = PH_MOVED;
            }
            moved_pos = false; /* the start sample already reported this position */
        } else if (pressed && t_ms - g->t0 >= LONG_MS) {
            g->phase = PH_DONE;
            emit(g, &out[n++], G_LONG_PRESS);
            return n;
        }
    }

    if (pressed) {
        if (g->phase == PH_DRAG && moved_pos) emit(g, &out[n++], G_DRAG_MOVE);
        return n;
    }

    /* release */
    switch (g->phase) {
    case PH_PRESSED:
        if (t_ms - g->t0 < TAP_MS) emit(g, &out[n++], G_TAP);
        break;
    case PH_DRAG:
        emit(g, &out[n++], G_DRAG_END);
        break;
    case PH_MOVED: {
        omni_gesture_kind_t k = swipe_kind(g);
        if (k != G_NONE) emit(g, &out[n++], k);
        break;
    }
    default:
        break;
    }
    g->phase = PH_IDLE;
    g->claimed = false;
    return n;
}
