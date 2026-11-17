#pragma once
/* The Omni dial compositor (spec §7.3, Stage 3 Task 6): one 466 x 466 RGB565 frame from omni_model's state, in the
 * mockup's drawDial order (design/mockups/faces-omni.js 501), drawn in any rects, with the rects a change dirties.
 * Pure C: no allocation, no statics; two cores may render disjoint rects of one frame at once. Stack (Task 7, after
 * the M4 hoist): omni_dial_render_prepared's path is the shallower one (the context is the caller's); omni_dial_render
 * holds the context on its stack as well (OMNI_DIAL_PREP_BYTES); `dial` prints the view's tasks' high-water marks.
 * Stage 2's omni/dial.h (the analog seconds hand) is a different thing and stays as it is (C1). */
#include <stdbool.h>
#include <stdint.h>
#include "omni/globe.h"
#include "omni/globe_compact.h"
#include "omni/omni_model.h"
#include "omni/raster.h"
#include "omni/ring.h"

#define OMNI_DIAL_W 466

/* The Omni globe (Task 5, mockup initGlobe/renderGlobe): D 224 in a 224 box at (121, 121), tilt 22, studio light
 * (-.42, .52, .74), limb_kind 1, the 512 texture (compact path); look kind 1, gain 1. */
extern const omni_globe_cfg_t OMNI_DIAL_GLOBE_CFG;
extern const omni_globe_look_t OMNI_DIAL_GLOBE_LOOK;

typedef struct { omni_overlay_t ov; int16_t x, y; } omni_dial_layer_t;   /* spans image-relative; (x, y) its screen origin */
/* Tinted A8 (blooms), added 'lighter': a pixel of alpha a adds colour(a) x a / 255 x the level, colour(a) =
 * rgb_out + (rgb_in - rgb_out) x a / 255 per 8-bit channel (the gradient's outer stop to its inner one). */
typedef struct { const uint8_t *a; int16_t x, y, w, h; uint16_t rgb_in, rgb_out; } omni_dial_a8_t;
typedef struct {          /* everything a frame draws with: built once by dial_view (Task 7), which owns it */
    const uint16_t *base;                                  /* omni_base, 466x466, read from the flash mapping (R70) */
    const omni_strip_t *strips[3]; omni_pmap_t dial_map, mag_map; uint16_t *mag_base;   /* magnifier: k 1.4 */
    omni_pmap_t *loupe_map; uint16_t *loupe_base;          /* mutable: the view points them at the loupe size it built
                                                              for the frame's st.loupe, before a render (C8) */
    omni_dial_layer_t top, over, dome, glass, lens[4];     /* lens: red off, red on, green off, green on */
    omni_dial_a8_t bloom[2];
    omni_globe_frame_t globe; const omni_globe_clut_t *clut; omni_globe_cspan_fn ckernel;
    omni_rings_span_fn rkernel;
    void (*over_run)(const uint32_t *argb, uint16_t *dst, int n);   /* the layers' overlay rule over n pixels at full
                                                                     * alpha (omni_overlay_row's run; NULL: the C rule;
                                                                     * the device: globe_over_pie, bit-exact, Task 7) */
    const uint8_t *lid;          /* omni_lid, 466 x 466 RGB565A8 (the RGB565 plane, then the A8 plane), read from the
                                  * flash mapping (R70); NULL: no lid layer (and no dim) */
    const int16_t *lid_run;      /* omni_dial_lid_runs of lid (4 per source row); NULL: the dial is drawn under it all */
    const uint8_t *lid_spec;     /* omni_dial_lid_spec (3 per source column), built with the view; NULL: no lid layer */
    const uint8_t *iris_grain;   /* omni_blade's A8 plane (256 x 512), read from the flash mapping (R70); NULL: the
                                  * iris without its grain (Task 10) */
} omni_dial_res_t;
/* res->globe: lut/tex/shade/sun as the Omni look (Task 5), clut = res->clut, under and over NULL (the dial frame is the
 * base), fade 255; lon0 comes from the state. mag_base NULL: no magnifier (it and its glass are not drawn). A layer
 * with no spans draws nothing. loupe_map/loupe_base: omni_dial_loupe_cfg(st.loupe) and its omni_dial_zoom_base,
 * needed only while st.loupe > 0. The view rebuilds them for a frame's st.loupe, and the rebuild must complete before
 * either core starts rendering any rect of that frame (a happens-before through the worker's semaphore, not just
 * "before the render"): both cores read them, and neither writes them. A render whose loupe_map was not built for its
 * st.loupe (omni_dial_loupe_ready false) draws the loupe's shadow and frame but no content, rather than the old
 * content at the old rect; the view should treat that as a bug (dial_view can assert omni_dial_loupe_ready). */
typedef struct { const omni_dial_res_t *res; omni_dial_state_t st; omni_rings_pose_t pose; bool fast; } omni_dial_frame_t;
/* fast (the fidelity rule, Task 2's ruling): no hand shadows, here or in the magnifier and loupe. The strips' level
 * comes from pose (omni_rings_fidelity). The ruling's "magnifier and loupe refresh paused" is not done here: a render
 * keeps no state to pause with, and they are 5 k and at most 13 k of the frame's pixels; the view may skip their rects.
 * res: base, strips, dial_map and rkernel are required; mag_base NULL: no magnifier; clut NULL: no globe and no
 * marker; a bloom with a NULL a: none. */

/* Composes rect r of the 466x466 frame fb, in the mockup's order (drawDial 501): base, rings, V marks, hands,
 * top, globe (disc spans), marker, dome, over, magnifier
 * (black, mag_base, rings, marks and hands through mag_map with the hands' affine, the additive (40,32,18) x .35,
 * glass), lenses (off, on x level, bloom additive x level x .55), loupe, the lid (Task 9, below), flash, the iris
 * (Task 10: st.iris, omni_iris_draw at st.iris_ap / iris_phi / iris_s with res->iris_grain; the dial is not drawn under
 * its opaque blades, omni_iris_opaque), the reveal's fade.
 * The lid (st.lid, res->lid and res->lid_spec; the entry's drawLid, faces-omni.js 893 and 962): the dial is dimmed
 * toward black by dim8 = round(omni_flip_dim(st.lid_theta) x 255) (omni_blend565); then each screen row y with a line
 * l (omni_hinge_lines of OMNI_LID_HINGE at st.lid_theta, l.v >= 0) samples source row l.v at column u = (U0 + x inv)
 * >> 16, inv = l.inv_s_q16, U0 = 233 x 65536 - (465 inv >> 1) (the pixel's centre, no division per pixel). A source
 * pixel s of alpha a > 0, with ka = amul((l.dark a + 127) / 255), sa = amul(l.spec), aa = amul(a), sc_k =
 * res->lid_spec[3u + k] = (cols[u] COL_k + 127) / 255 (omni_lid_spec_cols, COL (255, 240, 205)), per 8-bit channel:
 * p = min(blend8(0, s, ka) + (sc_k sa aa >> 16), 255), then blend8(p, frame, aa), packed. Last (after the flash), the
 * fade: every pixel toward black by fade8 = round(st.fade x 255) (omni_blend565), the mockup's fadeIn over the
 * whole composite. The dial is not drawn under pixels the opaque lid covers (res->lid_run), and off the round panel
 * ((2x + 1 - 466)^2 + (2y + 1 - 466)^2 > 466^2: no panel pixel shows it; the mockup's shots are masked so) the lid
 * frame is black. Only the horizontal axis is drawn (the one Stage 3 uses). The fade (either entry's) applies to the whole
 * frame with or without the lid.
 * fb is the whole frame (row stride 466); only pixels inside r are written. A disc span that r cuts is shaded whole on
 * a row copy (as omni_rings_draw does), so r need not be widened to the globe box's columns. */
void omni_dial_render(const omni_dial_frame_t *f, uint16_t *fb, const omni_rect_t *r);
/* omni_dial_render in two steps (Task 7, T6 M4): the frame's per-frame work done once (the V marks, the globe's terms
 * and marker, the loupe geometry, the flash table, and the hands built through each affine: dial, magnifier, loupe),
 * then any number of rects rendered from it, by any number of cores at once (read-only), each pixel as
 * omni_dial_render(f, ..) gives it. f must outlive p (p points into it), and p is self-referencing: prepare it where it
 * is rendered from. The loupe map must be ready (see above) before the prepare. omni_dial_render(f, fb, r) is
 * omni_dial_prepare into a stack copy (OMNI_DIAL_PREP_BYTES, 30 KB with the iris's tables, Task 10) and
 * omni_dial_render_prepared. */
#define OMNI_DIAL_PREP_BYTES 30720
typedef union { unsigned char b[OMNI_DIAL_PREP_BYTES]; double d; void *p; } omni_dial_prep_t;
void omni_dial_prepare(const omni_dial_frame_t *f, omni_dial_prep_t *p);
void omni_dial_render_prepared(const omni_dial_prep_t *p, uint16_t *fb, const omni_rect_t *r);
/* The render's two stages (R88), each over any rects: under = base, rings, V marks, hands, top (all the globe is drawn
 * over); over = globe, marker, dome, over, magnifier, lenses, loupe, the lid (with its dim and the black off the panel),
 * flash, the iris, the fade. Every step writes a pixel from that pixel's value alone, so under then over is
 * omni_dial_render_prepared, and a cached under (for a frame with the same rings, hands and marks) restored and
 * followed by over gives the frame exactly. With the lid or the iris, both stages leave out the same pixels (under the
 * opaque lid or blades, off the panel), which the lid's or the iris's own step (in over) then writes; the drift path
 * never sees a lid or iris frame (omni_dial_dirty gives it the whole screen, or the iris's box, never the globe box). */
void omni_dial_render_under(const omni_dial_prep_t *p, uint16_t *fb, const omni_rect_t *r);
void omni_dial_render_over(const omni_dial_prep_t *p, uint16_t *fb, const omni_rect_t *r);
/* Every pixel the home marker draws (its rings + AA), empty when it is not drawn: outside the disc, a globe turn
 * changes only these. */
void omni_dial_marker_box(const omni_dial_frame_t *f, omni_rect_t *out);
/* What changed from prev to cur, as at most OMNI_DIRTY_MAX rects (merged when their union is cheaper). prev NULL, or
 * another res: the full screen; so too while either frame has the lid (it moves, and dims the dial, every frame),
 * unless the two frames are identical (nothing). While either frame has the iris (C12): out[0] is the disc's box
 * OMNI_IRIS_BOX (34, 34, 432, 432), then the rects of whatever else changed, less those inside the box (not merged with
 * it); the whole screen while either frame's fade is on; nothing for two identical frames. */
#define OMNI_DIRTY_MAX 8
int omni_dial_dirty(const omni_dial_frame_t *prev, const omni_dial_frame_t *cur, omni_rect_t out[OMNI_DIRTY_MAX]);
/* additive flash (drawDial's last block): radial stops (255,250,235)xmin(1,1.1f), (255,220,170)x.8f at .45, (255,160,90)x.4f
 * at 1 (r 260), clamped beyond; colour x alpha added, per pixel from a squared-distance table, no sqrt per pixel.
 * fb is the whole frame (stride 466), as omni_dial_render. */
void omni_flash(uint16_t *fb, int stride, const omni_rect_t *r, float f);
/* The colour the flash adds at distance d from the centre (8-bit channels, unclamped float): colour and alpha
 * interpolated between the stops apart (Skia's unpremultiplied gradient), then multiplied. */
void omni_flash_rgb(float f, float d, float out[3]);

/* ---- The magnifier and the loupe: polar maps (ring.h) of a zoomed source, and the zoomed base under them. */
#define OMNI_MAG_X0 207
#define OMNI_MAG_Y0 18
#define OMNI_MAG_X1 259
#define OMNI_MAG_Y1 122
/* {rect (207,18,259,122), d = s = (233, 70.5), k 1.4, rr 3}, the dial map's r_min/n_r, at n_theta. */
omni_pmap_cfg_t omni_dial_mag_cfg(uint16_t n_theta);
/* drawLoupe (549) at a (st.loupe, 0..1): e = outCubic(a); centre (233, lerp(70.5, 239, e)); size lerp 52 -> 74 by
 * 104 -> 176; k 2.35; source centre (233, 71); alpha min(1, 2a). The content rect is (x0, y0, w, h) rounded to whole
 * pixels (the map's rect, rr 5); the frame is that rect grown by 5 (rr 8). */
typedef struct { float cx, cy, w, h, ga; omni_rect_t content; } omni_loupe_geom_t;
void omni_dial_loupe_geom(float a, omni_loupe_geom_t *g);
omni_pmap_cfg_t omni_dial_loupe_cfg(float a, uint16_t n_theta);
/* True if res->loupe_map (and loupe_base) are the ones omni_dial_loupe_cfg(a, the dial map's n_theta) gives, bit for
 * bit (true for a <= 0: no loupe). */
bool omni_dial_loupe_ready(const omni_dial_res_t *res, float a);
/* Every pixel the loupe at a can touch (its drop shadow included); empty for a <= 0. */
void omni_dial_loupe_bbox(float a, omni_rect_t *out);
/* The zoomed base under a map's rings: out (cfg's rect, w x h, row stride w) samples base (466 x 466) bilinearly at
 * each pixel centre's source point s + (p + 0.5 - d) / k (8-bit channels, then rounded to RGB565 as the strips are). */
void omni_dial_zoom_base(const uint16_t *base, const omni_pmap_cfg_t *cfg, uint16_t *out);

/* ---- The lid (Task 9) */
/* Per source row v of the lid bitmap (RGB565A8, 466 x 466), 4 int16: [run[4v], run[4v+1]) holds every pixel of nonzero
 * alpha, [run[4v+2], run[4v+3]) is the row's longest run of alpha 255 (empty: x0 >= x1). The view builds it once
 * (res->lid_run): a render then skips the dial beneath the opaque run and reads no alpha inside it. */
void omni_dial_lid_runs(const uint8_t *lid, int16_t run[4 * OMNI_DIAL_W]);
/* The lid's spec colour by source column (3 per column: the gradient omni_lid_spec_cols x (255, 240, 205), rounded),
 * for res->lid_spec: constant, so the view builds it once. */
void omni_dial_lid_spec(uint8_t spec[3 * OMNI_DIAL_W]);
