#pragma once
/* The three full-screen brass backgrounds over true black (faces-setup.js thinBezel(), the picker's navBezel() and
 * settingsBezel()), rendered as RGB565. Since R112 the watch ships them as flash assets (bg_bezel.bin, bg_picker.bin,
 * bg_settings.bin: tools/bgpack, or the Stage 3 device captures, R115); this renderer is theme.c's fallback when the
 * assets image is missing or rejected, and bgpack's source. The code moved from theme.c verbatim (R112, B3): every
 * pass is per pixel, in the same order for every pixel, so any strip height gives the bytes of a whole-square render
 * (the seeded speckle runs whole for each strip, clipped to its rows; R110).
 * The bytes depend on the maths library and on fused multiply-add contraction: the device builds this file with
 * theme.c's flags (-ffp-contract=fast, R121/B1), so its fallback reproduces the captured Stage 3 bytes; a host build
 * with glibc may differ (test_brassbg only warns). */
#include <stdint.h>

#define OMNI_BG_W 466
#define OMNI_BG_C 233                           /* centre (THEME_CX/CY) */
#define OMNI_BG_STRIP 59                        /* rows of ARGB working memory (R110) */
typedef enum { OMNI_BG_BEZEL, OMNI_BG_PICKER, OMNI_BG_SETTINGS, OMNI_BG__COUNT } omni_bg_t;   /* = theme_bg_t order */
/* rgb565: OMNI_BG_W x OMNI_BG_W, stride OMNI_BG_W * 2; strip: OMNI_BG_W * 4 * rows bytes of scratch; the square is
 * rendered `rows` rows at a time (B2); yield (may be NULL) is called with each row index, as bg_yield was.
 * rows <= 0 renders nothing; rows above OMNI_BG_W are taken as OMNI_BG_W (the strip never needs more). */
void omni_bg_render_rows(omni_bg_t which, uint16_t *rgb565, uint8_t *strip, int rows,
                         void (*yield)(int y, void *ctx), void *ctx);
void omni_bg_render(omni_bg_t which, uint16_t *rgb565, uint8_t *strip,   /* rows = OMNI_BG_STRIP */
                    void (*yield)(int y, void *ctx), void *ctx);

/* The canvas pixel helpers, one copy for the backgrounds and for theme.c's window frames and knob (so every brass
 * pixel is built by this file, with its flags). A gradient stop: offset t, straight colour 0..255, alpha 0..1. */
typedef struct { float t, r, g, b, a; } omni_bg_stop_t;
/* Canvas gradient at t over n stops: premultiplied rgb + alpha in out. */
void omni_bg_grad(const omni_bg_stop_t *s, int n, float t, float out[4]);
float omni_bg_clampf(float v, float lo, float hi);
/* Composite an rgba (straight, 0..255 colour, alpha 0..1) scaled by coverage k over an ARGB8888 (B, G, R, A) pixel. */
void omni_bg_px_rgba(uint8_t *p, float r, float g, float b, float a, float k);
