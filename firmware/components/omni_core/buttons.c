#include "omni/input.h"

#define DOUBLE_MS 350
#define LONG_MS   800

omni_press_t omni_button_feed(omni_button_sm_t *b, uint32_t t_ms, bool down)
{
    if (down) {
        if (!b->down) {
            b->down = true;
            b->t_down = t_ms;
            b->long_sent = false;
            return B_NONE;
        }
        if (!b->long_sent && t_ms - b->t_down >= LONG_MS) {
            b->long_sent = true;
            b->clicks = 0; /* a long hold cancels any pending click */
            return B_LONG;
        }
        return B_NONE;
    }

    if (b->down) { /* release edge */
        b->down = false;
        if (b->long_sent) {
            b->long_sent = false;
            return B_NONE;
        }
        if (b->clicks == 1 && b->t_down - b->t_last_up <= DOUBLE_MS) {
            b->clicks = 0;
            return B_DOUBLE;
        }
        b->clicks = 1;
        b->t_last_up = t_ms;
        return B_NONE;
    }

    if (b->clicks == 1 && t_ms - b->t_last_up >= DOUBLE_MS) {
        b->clicks = 0;
        return B_SHORT;
    }
    return B_NONE;
}
