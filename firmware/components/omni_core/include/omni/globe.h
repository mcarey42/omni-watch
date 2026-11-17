#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/sun.h"

/* Globe lookup table, projection and sun terms: a port of the mockup's Globe.prototype.build and project
 * (design/mockups/faces-practical.js). The LUT holds, for every pixel of the disc, everything the renderer
 * needs that does not depend on the view longitude or the sun. */

typedef enum { OMNI_PROJ_ORTHO, OMNI_PROJ_POLAR } omni_proj_t;

typedef struct {
    uint16_t size;          /* S: square output box, px (even); the globe is centred in it */
    uint16_t diam;          /* D: globe diameter, px */
    float lat0_deg;         /* ortho: view-centre latitude */
    float roll_deg;         /* ortho: screen-plane tilt (spin 23.44) */
    omni_proj_t proj;
    float max_colat_deg;    /* polar only (world, Stage 4: 145); 0 means the mockup's default 150 */
    bool studio;            /* fixed light: plane a = n.L, b = c = 0 (ortho only) */
    float light[3];         /* studio light, unit, in the ROLLED globe frame (xv, yv, z), as the mockup's ndl (R48/C2) */
    uint16_t tex_w;         /* power of two, 2..2048; texture is tex_w x tex_w/2 */
    uint8_t limb_kind;      /* 0: 0.42 + 0.58 z^0.55, rim (1-z)^2.6;  1 (Omni): 0.62 + 0.38 z^0.5, rim 0 (ortho only) */
} omni_globe_cfg_t;         /* always written with designated initialisers (R48/D10) */

/* One row of the disc: box pixels (x0 .. x0+n-1, y), stored at plane entries off .. off+n-1. */
typedef struct { int16_t y, x0, n; uint32_t off; } omni_globe_span_t;   /* off % 8 == 0 */

typedef struct {
    omni_globe_cfg_t cfg;
    uint16_t nspans; omni_globe_span_t *span;
    uint32_t n;                               /* plane entries incl. padding */
    int16_t *u;                               /* Q4 texels: lonRel * tex_w / 2pi */
    uint16_t *row;                            /* texture row, floor((0.5 - lat/pi) * tex_h) clamped */
    int16_t *a, *b, *c;                       /* Q14: sin lat, cos lat cos lonRel, cos lat sin lonRel */
    uint8_t *limb, *rim, *alpha;              /* x255: 0.42+0.58 z^0.55, (1-z)^2.6, edge coverage */
} omni_globe_lut_t;

/* Bytes omni_globe_lut_build needs for cfg (any alignment of mem), or 0 if cfg is invalid. */
size_t omni_globe_lut_bytes(const omni_globe_cfg_t *cfg);

/* Builds the LUT into mem (bytes >= omni_globe_lut_bytes(cfg)); the planes are 16-byte aligned, each span's
 * off is a multiple of 8 and the padding entries are all zero (alpha 0). False if cfg is invalid or mem short. */
bool omni_globe_lut_build(omni_globe_lut_t *lut, const omni_globe_cfg_t *cfg, void *mem, size_t bytes);
/* The same build in steps (the device's render workers yield and check for a stop between them, and split the rows
 * between the cores): begin lays out mem and fills the spans, then rows fills the planes of globe rows [py0, py1)
 * (0 .. diam). begin + rows over every row, in any order or split, gives exactly omni_globe_lut_build's bytes. The
 * build is single precision (globe.c), so the device builds the host's bytes. */
bool omni_globe_lut_begin(omni_globe_lut_t *lut, const omni_globe_cfg_t *cfg, void *mem, size_t bytes);
void omni_globe_lut_rows(omni_globe_lut_t *lut, int py0, int py1);

/* Projects lat/lon (deg) with lon0 (deg) at the view centre: out = unit-disc screen coords (x right, y down)
 * and z (> 0 visible; polar: 1 inside max_colat, else -1). The pixel is (S/2 + x*D/2, S/2 + y*D/2). */
void omni_globe_project(const omni_globe_cfg_t *cfg, double lat, double lon, double lon0, float out[3]);

/* Sun terms, Q14. A pixel's mu = (a*sd >> 14) + (b*k1 >> 14) - (c*k2 >> 14), with sd = sin decl,
 * k1 = cos decl cos(lon0 - sunlon), k2 = cos decl sin(lon0 - sunlon). */
typedef struct { int16_t sd, k1, k2; } omni_globe_sun_t;   /* Q14 */
void omni_globe_sun(omni_globe_sun_t *o, double decl_deg, double sun_lon_deg, double lon0_deg);
void omni_globe_sun_studio(omni_globe_sun_t *o);           /* sd = 1<<14, k1 = k2 = 0; studio LUTs only */
/* The sun a live face lights with: the real one, or for an unset clock (sun == NULL) a sun at the view centre
 * (decl = lat0, lon = lon0), so mu >= 0 on the whole disc (R48/C3). Returns false when unset (then no halo lobe). */
bool omni_globe_sun_now(omni_globe_sun_t *o, const omni_sun_t *sun, double lat0_deg, double lon0_deg);

/* omni_core relies on >> of a negative int being an arithmetic (floor) shift, as GCC documents for its
 * targets; this fails to compile on any compiler that does otherwise. */
typedef char omni_globe_arith_shift_check[(-3 >> 1) == -2 ? 1 : -1];

/* A pixel's mu (Q14, about -16384..16384) for plane entry k: the formula above. */
static inline int32_t omni_globe_mu(const omni_globe_lut_t *lut, uint32_t k, const omni_globe_sun_t *s)
{
    return ((lut->a[k] * s->sd) >> 14) + ((lut->b[k] * s->k1) >> 14) - ((lut->c[k] * s->k2) >> 14);
}

/* ---- Rendering: a port of the mockup's Globe.prototype.render (faces-practical.js 195-226) and halo (243-258).
 * The fixed-point arithmetic below is the reference that every optimised span kernel must match bit for bit. */

typedef struct { float gain, night, atmo; uint8_t kind; } omni_globe_look_t;
/* kind 0: day/night terminator, live {g,1.25,1}, spin {g,1.1,0.8}, analog {g,1.25,0.7};
 * kind 1 (Omni): day = (.32 + .78 max(0, mu)) gain, night 0, atmo 0 */

/* Terminator shade table: entry i holds the mockup's day/night/atmosphere factors at mu_i = (i + 0.5)/256 - 1,
 * x256 and rounded; a pixel uses i = clamp((mu + 16384) >> 6, 0, 511). */
#define OMNI_SHADE_N 512
typedef struct { uint16_t dr, dg, db, nr, ng, nb, at, pad; } omni_shade_e_t;  /* Q8 */
typedef struct { omni_shade_e_t e[OMNI_SHADE_N]; } omni_globe_shade_t;
void omni_globe_shade_build(omni_globe_shade_t *sh, const omni_globe_look_t *look);

/* Equirectangular texture, w x w/2 with w == the LUT's tex_w. */
typedef struct { uint16_t w, h; const uint32_t *px; } omni_globe_tex_t;      /* px = day565 | night565 << 16 */

/* Overlay: the runs of alpha > 0 of an ARGB8888 (0xAARRGGBB) image placed at the box origin, pixels copied
 * compactly (span pixel j is argb[off + j]); spans are in row order. */
typedef struct { uint16_t y, x0, n; uint32_t off; } omni_ospan_t;
typedef struct { uint32_t nspans; const omni_ospan_t *span; const uint32_t *argb; } omni_overlay_t;
/* Bytes omni_overlay_build needs for this image (any alignment of mem). */
size_t omni_overlay_bytes(const uint32_t *argb, uint16_t w, uint16_t h, uint16_t stride_px);
bool omni_overlay_build(omni_overlay_t *ov, const uint32_t *argb, uint16_t w, uint16_t h, uint16_t stride_px,
                        void *mem, size_t bytes);   /* runs of alpha > 0, pixels copied compactly */
/* The overlay alone composited over box (size x size RGB565, row stride size), by the frame's rule: the box a frame
 * would show where there is no globe. */
void omni_overlay_draw(const omni_overlay_t *ov, uint16_t *box, uint16_t size);
/* One box row y of the overlay over row, x clipped to [xa, xb): cursor is the caller's span index (advanced past rows
 * above y, so call rows in order); run composites n pixels (NULL = the C rule). */
void omni_overlay_row(const omni_overlay_t *ov, uint32_t *cursor, uint16_t y, uint16_t *row, int xa, int xb,
                      void (*run)(const uint32_t *argb, uint16_t *dst, int n));

struct omni_globe_clut;   /* omni/globe_compact.h */
typedef struct {
    const omni_globe_lut_t *lut; const omni_globe_tex_t *tex; const omni_globe_shade_t *shade;
    const struct omni_globe_clut *clut;   /* the compact LUT (omni_globe_render_c); NULL for the planar path */
    omni_globe_sun_t sun; double lon0_deg;
    const uint16_t *under;      /* S x S RGB565 base (NULL = black) */
    const omni_overlay_t *over; /* NULL = none */
    uint8_t fade;               /* 255 = none; multiplies the whole box (mockup globalAlpha) */
    /* omni_globe_render_c's overlay over n pixels: dst[j] = argb[j] composited, bit-exact with the C rule above
     * (alpha' = a + a>>7, (src alpha' + dst (256 - alpha')) >> 8, truncating pack); NULL = that C (Task 11) */
    void (*over_run)(const uint32_t *argb, uint16_t *dst, int n);
} omni_globe_frame_t;

/* The texture column offset, Q4 texels, exactly: in double, v = floor(lon0_deg * tex_w / 360.0 * 16.0 + 0.5)
 * (round half up, so lon0 and lon0 + 360 agree) + tex_w * 8.0; then v = fmod(v, 16.0 * tex_w), plus 16 tex_w if
 * negative; the result is (int32_t)v + 16 tex_w, in [16 tex_w, 32 tex_w). Reducing mod 16 tex_w leaves every column
 * ((u + uoff_q4) >> 4) & (tex_w - 1) unchanged, and with |u| <= 8 tex_w keeps u + uoff_q4 > 0. */
int32_t omni_globe_uoff_q4(uint16_t tex_w, double lon0_deg);

/* Shades one LUT span onto dst (= the span's first pixel in the out row, which already holds the base).
 * uoff_q4 is the texture column offset omni_globe_render passes, omni_globe_uoff_q4(tex_w, lon0).
 * Only entries off .. off+n-1 are touched: when S == D a span's 8-entry padding runs past the box row, so a
 * vector kernel must mask its tail store. The uint8 planes are only 8-byte aligned at a span start. */
typedef void (*omni_globe_span_fn)(const omni_globe_frame_t *f, const omni_globe_span_t *sp, int32_t uoff_q4,
                                   uint16_t *dst);  /* dst = first pixel of the span in the out row */
void omni_globe_span_ref(const omni_globe_frame_t *f, const omni_globe_span_t *sp, int32_t uoff_q4, uint16_t *dst);
/* span_ref's output bit for bit, about twice as fast on the ESP32-S3 (globe_fast.c; Task 11). */
void omni_globe_span_fast(const omni_globe_frame_t *f, const omni_globe_span_t *sp, int32_t uoff_q4, uint16_t *dst);

/* Renders box rows [y0, y1) of out (S x S RGB565, row stride S): per row the base (or black), fn over the
 * globe span, the overlay spans, then the fade. fn NULL = omni_globe_span_ref. */
void omni_globe_render(const omni_globe_frame_t *f, uint16_t *out, uint16_t y0, uint16_t y1, omni_globe_span_fn fn);

/* mockup halo() (243-258), source-over onto an RGB565 box; dir NULL = no sun-side lobe */
void omni_globe_halo(uint16_t *box, uint16_t size, float cx, float cy, float R, const float *dir, float strength);
/* A moving lobe without redrawing the ring (Task 11). Outside its candidates (pixels with d >= 0.99 R within 0.75 R of
 * the lobe centre, plus a 1 px margin) the halo with a lobe equals the halo without, bit for bit. So with
 * ring = src + halo(dir NULL) kept and box = ring + lobe(old): unlobe(old) copies the old candidates back from ring,
 * then lobe(new) draws the new candidates from src (NULL: black) exactly as omni_globe_halo would
 * (test_halo_lobe_moves). Both touch only box rows [y0, y1) (two cores split them). lobe_rect: the candidates'
 * bounding rect (inclusive x0 y0 x1 y1; empty for dir NULL). */
void omni_globe_halo_lobe_rect(uint16_t size, float cx, float cy, float R, const float *dir, float strength, int rect[4]);
void omni_globe_halo_lobe(uint16_t *box, const uint16_t *src, uint16_t size, float cx, float cy, float R,
                          const float *dir, float strength, int y0, int y1);
void omni_globe_halo_unlobe(uint16_t *box, const uint16_t *ring, uint16_t size, float cx, float cy, float R,
                            const float *dir, float strength, int y0, int y1);
