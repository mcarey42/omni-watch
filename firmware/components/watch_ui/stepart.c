/* The step art and copy (stepart.h): moved from scr_provision.c, unchanged in its drawing. */
#include <math.h>
#include "stepart.h"
#include "theme.h"

#define CX THEME_CX
#define CY THEME_CY
#define TAU (2 * (float)M_PI)
#define RING_R STEPART_RING_R

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
static float ease_out(float t) { t = clampf(t, 0, 1); return 1 - powf(1 - t, 3); }
#define deg theme_arc_deg   /* canvas radians -> LVGL arc degrees (theme.c, the same expression) */

void stepart_phone(lv_layer_t *layer, int32_t x, int32_t y)
{
    stepart_phone_at(layer, x, y, 13, 22, 5, 4, 17, THEME_SETUP_BRASS_HI); /* rrect 26 x 44, r 5, stroke 2.2 */
}

void stepart_phone_at(lv_layer_t *layer, int32_t x, int32_t y, int32_t hw, int32_t hh, int32_t r, int32_t bar, int32_t bar_dy,
                      uint32_t rgb)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = r;
    d.bg_opa = LV_OPA_TRANSP;
    d.border_width = 2;
    d.border_color = lv_color_hex(rgb);
    d.border_opa = LV_OPA_COVER;
    lv_area_t a = {x - hw, y - hh, x + hw, y + hh};
    lv_draw_rect(layer, &d, &a);
    theme_draw_line(layer, x - bar, y + bar_dy, x + bar, y + bar_dy, 2, rgb, LV_OPA_COVER, false);
}

void stepart_wifi(lv_layer_t *layer, int32_t x, int32_t y, uint32_t rgb, bool animate, uint32_t t)
{
    for (int i = 0; i < 3; i++) { /* arcs r 10/21/32 over -0.78pi..-0.22pi around (x, y + 14), width 3.2 */
        float on = animate ? 0.3f + 0.7f * smooth(sinf((float)t / 260 - i * 0.9f) * 0.5f + 0.5f) : 1;
        theme_draw_arc(layer, x, y + 14, 10 + i * 11, 3, 220, 320, rgb, (lv_opa_t)(on * 255), true);
    }
    theme_draw_box(layer, x - 3, y + 11, x + 3, y + 17, LV_RADIUS_CIRCLE, rgb, LV_OPA_COVER, 0, 0);
}

void stepart_check(lv_layer_t *layer, int32_t x, int32_t y, float k)
{
    float sweep = 360 * ease_out(k * 1.4f);
    if (sweep > 1) theme_draw_arc(layer, x, y, 26, 4, 270, 270 + (int32_t)sweep, THEME_GREEN, THEME_OPA(0.95), true);
    float p = clampf((k - 0.35f) / 0.5f, 0, 1);
    if (p <= 0) return;
    static const int P[3][2] = {{-11, 1}, {-3, 9}, {13, -8}};
    float l1 = fminf(1, p * 2), l2 = clampf(p * 2 - 1, 0, 1);
    theme_draw_line(layer, x + P[0][0], y + P[0][1], x + (int32_t)lroundf(P[0][0] + (P[1][0] - P[0][0]) * l1),
                    y + (int32_t)lroundf(P[0][1] + (P[1][1] - P[0][1]) * l1), 4, THEME_GREEN, THEME_OPA(0.95), true);
    if (l2 > 0)
        theme_draw_line(layer, x + P[1][0], y + P[1][1], x + (int32_t)lroundf(P[1][0] + (P[2][0] - P[1][0]) * l2),
                        y + (int32_t)lroundf(P[1][1] + (P[2][1] - P[1][1]) * l2), 4, THEME_GREEN, THEME_OPA(0.95), true);
}

void stepart_ring(lv_layer_t *layer, uint32_t t, uint32_t rgb)
{
    /* working comet chasing round the track */
    stepart_comet(layer, RING_R + 1, 3, fmodf((float)t / 900, 1) * TAU, 18, 0.03f, 0.02f, 0.5f, rgb, -1e9f);
}

void stepart_comet(lv_layer_t *layer, int32_t r, int32_t w, float a, int n, float step, float lap, float head_opa,
                   uint32_t rgb, float stop)
{
    for (int i = 0; i < n; i++) {
        float e = a - i * step, s = a - (i + 1) * step;
        if (s < stop) break;
        theme_draw_arc(layer, CX, CY, r, w, deg(s), deg(e + lap), rgb, (lv_opa_t)((1 - i / (float)n) * head_opa * 255),
                       false);
    }
}

void stepart_copy(omni_net_state_t st, bool saved_kept, bool had_saved, stepart_copy_t *out)
{
    *out = (stepart_copy_t){.head_rgb = THEME_SETUP_HEAD};
    switch (st) {
    case NET_PROV_PHONE:
        *out = (stepart_copy_t){"Phone linked", "Now choose your network", "in the app on your phone", THEME_SETUP_HEAD};
        break;
    case NET_JOINING: out->head = "Joining", out->line1 = "credentials received from phone"; break;
    case NET_SYNCING: out->head = "Setting the time", out->line1 = "asking pool.ntp.org"; break;
    case NET_ERR_PASSWORD: out->head = "Wrong password", out->line1 = "Retry on your phone, or scan again"; break;
    case NET_ERR_NO_AP: out->head = "Network not found", out->line1 = "Check the name, then scan again"; break;
    case NET_ERR_NO_MEM: out->head = "Wi-Fi did not start", out->line1 = "Low memory: try again"; break;
    case NET_ERR_SYNC: out->head = "Sync failed", out->line1 = "pool.ntp.org did not answer"; break;
    default: break;
    }
    /* spec 7.6 / 7.2: an attempt that did not join says what became of the saved network (an ERR_SYNC joined: the new
     * network is saved) */
    if (st == NET_ERR_PASSWORD || st == NET_ERR_NO_AP || st == NET_ERR_NO_MEM)
        out->line2 = !had_saved ? "Nothing was saved" : saved_kept ? "Your saved network is unchanged"
                                                                    : "Your saved network was not kept";
}
