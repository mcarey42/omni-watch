#include "omni/kb.h"
#include "omni/wipe.h"
#include <math.h>
#include <string.h>

#define TAU 6.28318530717958647692f
#define CX 233
#define CY 233
#define OMNI_KB_EDGE_R 233

bool omni_kb_layout_load(omni_kb_layout_t *L, const void *data, size_t size)
{
    const uint8_t *d = (const uint8_t *)data;
    if (!L || !d || size < 10 || memcmp(d, "OKL1", 4) || d[4] != 1 || d[5] != OMNI_KB_SETS) return false;
    size_t total = 0;
    for (int s = 0; s < OMNI_KB_SETS; s++) {
        if (d[6 + s] == 0 || d[6 + s] > OMNI_KB_SET_MAX) return false;
        total += d[6 + s];
    }
    if (size != 10 + total) return false;
    for (size_t i = 10; i < size; i++)
        if (d[i] < 0x21 || d[i] > 0x7E) return false;
    memset(L, 0, sizeof *L);
    const uint8_t *k = d + 10;
    for (int s = 0; s < OMNI_KB_SETS; s++) {
        L->n[s] = d[6 + s];
        memcpy(L->keys[s], k, L->n[s]);
        k += L->n[s];
    }
    return true;
}

static int r2(int16_t x, int16_t y)
{
    int dx = x - CX, dy = y - CY;
    return dx * dx + dy * dy;
}

float omni_kb_key_angle(int i, int n) { return TAU * (float)i / (float)n; }

int omni_kb_index_at(int16_t x, int16_t y, int n)
{
    if (n <= 0 || r2(x, y) < OMNI_KB_CANCEL_R * OMNI_KB_CANCEL_R) return OMNI_KB_CANCEL;
    float a = atan2f((float)(x - CX), (float)(CY - y));   /* clockwise from 12 o'clock */
    if (a < 0) a += TAU;
    int i = (int)(a * (float)n / TAU + 0.5f);
    return i >= n ? 0 : i;
}

bool omni_kb_in_band(int16_t x, int16_t y)
{
    int q = r2(x, y);
    /* a ring start may begin anywhere from the band's inner edge to the screen edge (r 233, wider than the drawn
     * band's OMNI_KB_BAND_OUT 222, so a touch on the bezel side still reaches the nearest key) */
    return q >= OMNI_KB_BAND_IN * OMNI_KB_BAND_IN && q <= OMNI_KB_EDGE_R * OMNI_KB_EDGE_R;
}

void omni_kb_loupe_at(int i, int n, int16_t *x, int16_t *y)
{
    float a = omni_kb_key_angle(i, n), r = (float)(OMNI_KB_RING_R - OMNI_KB_LOUPE_IN);
    *x = (int16_t)(CX + (int)lroundf(r * sinf(a)));
    *y = (int16_t)(CY - (int)lroundf(r * cosf(a)));
}

omni_kb_stroke_t omni_kb_stroke(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool start_on_key)
{
    if (omni_kb_in_band(x0, y0)) return KB_STROKE_RING;
    if (start_on_key) return KB_STROKE_KEY;
    if (r2(x0, y0) < OMNI_KB_CENTRE_R * OMNI_KB_CENTRE_R && omni_kb_in_band(x1, y1)) return KB_STROKE_RESERVED;
    return KB_STROKE_NONE;
}

void omni_kb_open(omni_kb_edit_t *e, omni_kb_mode_t m)
{
    e->mode = (uint8_t)m;
    omni_kb_wipe(e);   /* the whole struct zeroed, then the mode kept */
}

static bool is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

static bool all_hex(const char *p, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++)
        if (!is_hex(p[i])) return false;
    return true;
}

uint8_t omni_kb_cap(const omni_kb_edit_t *e)
{
    if (e->mode == KB_MODE_SSID) return 32;
    if (e->mode == KB_MODE_CITY) return 15;
    return all_hex(e->buf, e->len) ? 64 : 63;
}

omni_kb_rc_t omni_kb_append(omni_kb_edit_t *e, char c, uint32_t t_ms)
{
    if (c < 0x20 || c > 0x7E) return KB_AT_CAP;   /* printable ASCII only: the buffer stays a clean string */
    uint8_t cap = omni_kb_cap(e);
    /* the 64th character is allowed only to a buffer that stays all hex (a raw PSK) */
    if (e->len >= cap || (e->len == 63 && !is_hex(c))) return KB_AT_CAP;
    e->buf[e->len++] = c;
    e->buf[e->len] = 0;
    e->t_type = t_ms;
    if (e->set == 0 && e->shift == KB_SHIFT_ONCE) e->shift = KB_SHIFT_OFF;
    return KB_TYPED;
}

bool omni_kb_delete(omni_kb_edit_t *e)
{
    if (!e->len) return false;
    e->buf[--e->len] = 0;
    /* end the last-typed show window: the character now last was typed earlier and has already been masked, so it
     * must not show again (unsigned wrap keeps now - t_type >= 900 for any now at or after that typing) */
    e->t_type -= 900;
    return true;
}

void omni_kb_shift_tap(omni_kb_edit_t *e, uint32_t t_ms)
{
    if (e->set != 0) {
        e->set = e->set == 2 ? 3 : 2;
        return;
    }
    if (e->shift == KB_SHIFT_OFF) {
        e->shift = KB_SHIFT_ONCE;
        e->t_shift = t_ms;
    } else if (e->shift == KB_SHIFT_ONCE && (uint32_t)(t_ms - e->t_shift) < 400) {
        e->shift = KB_SHIFT_LOCK;
    } else {
        e->shift = KB_SHIFT_OFF;
    }
}

void omni_kb_123_tap(omni_kb_edit_t *e)
{
    e->set = e->set == 0 ? 2 : 0;
    e->shift = KB_SHIFT_OFF;
}

uint8_t omni_kb_set(const omni_kb_edit_t *e)
{
    return e->set ? e->set : (e->shift != KB_SHIFT_OFF ? 1 : 0);
}

bool omni_kb_pass_ok(const char *p, uint8_t len, uint8_t auth_class)
{
    if (auth_class == OMNI_AUTH_OPEN) return true;
    if (auth_class != OMNI_AUTH_PSK && auth_class != OMNI_AUTH_SAE) return false;
    return (len >= 8 && len <= 63) || (len == 64 && all_hex(p, len));
}

bool omni_kb_shows(const omni_kb_edit_t *e, uint8_t i, uint32_t now)
{
    return e->reveal || (e->len && i == e->len - 1 && (uint32_t)(now - e->t_type) < 900);
}

uint16_t omni_kb_repeats(uint16_t hold_ms)
{
    return hold_ms < 450 ? 0 : (uint16_t)(1 + (hold_ms - 450) / 85);
}

omni_kb_idle_t omni_kb_idle(uint32_t idle_ms)
{
    return idle_ms >= 60000 ? KB_IDLE_CLOSE : idle_ms >= 45000 ? KB_IDLE_DIM : KB_IDLE_AWAKE;
}

void omni_kb_wipe(omni_kb_edit_t *e)
{
    uint8_t m = e->mode;
    omni_wipe(e, sizeof *e);
    e->mode = m;
}
