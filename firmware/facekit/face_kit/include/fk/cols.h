#pragma once
/* The column renderer (cols.c): two flat vertical-axis leaves hinged at x = 0 and x = 466 over a 466 x 466 interior,
 * each 233 source columns of one 466 x 466 closed-leaves image; F 560. Pure C, single precision in a fixed order
 * (-ffp-contract=off), no allocation: the device builds the frames the host tests pin. The column maps are the lid's
 * hinge engine (omni_hinge_lines_at) over the vertical axis, each leaf clipped to its 233 columns. Any leaf art of that
 * geometry works. A kind (fk_cols_kind_t) says which way the leaves swing and where they stop; the theme owns the
 * timeline that moves them (its state is fk_cols_state_t). */
#include <stdbool.h>
#include <stdint.h>
#include "omni/geom.h"
#include "omni/lid.h"

extern const omni_hinge_t FK_COLS_HINGE;  /* { OMNI_HINGE_VERTICAL, 0, 560, 466, 0, 0 } */
#define FK_LEAF_W 233
/* u < 0: no leaf in this column; u is the door image's source column; row v = cy + (y - cy) * inv_s (cy 233, both at
 * pixel centres: v = floor(233 + (y + .5 - 233) * inv_s)); shade darkens by 1 - cos (drawLeaf's outward
 * clamp(.72 (1 - cos th) + .1 sin th (1 - f), 0, .8) x 255, f = (d + 1) / 233 at distance d from the hinge); warm is
 * drawLeaf's rim light near the free edge (light(th) x f^7 x .55 x 255, light rising with the pop; Task 12 moved it
 * here from the band, so fk_cols_band takes it from the columns, made for the state's thetas). */
typedef struct { int16_t u; uint32_t inv_s_q16; uint8_t shade, warm; } fk_col_t;
/* A face's doors (wave 2 Task 3): inward, the leaves swing away from the eye (faces-reveal.js leafProj with inward:
 * Z = -d sin th, so s = F / (F + d sin th)): each leaf narrows toward its hinge, never crosses x 233, and leaves the
 * interior showing above and below it (inv_s > 1: no column is covered whole). Its shading is drawLeaf's inward one
 * (:174-175): shade = clamp(.35 sin th (1 - f) + .15 sin th, 0, .6), warm = light(th) sin th (.08 + .3 f^2), the same
 * warm (255, 170, 90) added. keep: the leaves stay at open_deg when the doors are done (the band then draws the
 * interior and the kept leaves, and the face draws the same leaves itself once the doors are over: fk_cols_leaves).
 * open_deg: where the swing ends. The timeline is the same for every kind (the device default's or the A/B's), with
 * the kind's open_deg and its free edges for the gap's glow. */
/* The leaf's look (rd wave 2 Task 4, W2-D3): what the first theme's drawLeaf fixed in constants, so a theme's outward
 * leaves take their own. The shade is clamp(dark_a (1 - cos th) + dark_b sin th (1 - f), 0, dark_max); the warm rim
 * is light x f^warm_pow x warm_k, (warm_rgb) added; the free edge's thickness is edge_t px in edge_rgb under
 * edge_lit_rgb at edge_lit_k x light. FK_COLS_LOOK_DW is the first theme's, and every expression keeps its order (f^7
 * is f2 f2 f2 f), so its frames stay bit-identical. An inward kind always takes FK_COLS_LOOK_DW: a look on one is
 * unsupported and ignored (its shading is drawLeaf's inward one, below; the first theme's host tests pin it). */
typedef struct fk_cols_look {
    float dark_a, dark_b, dark_max;
    float warm_k;
    uint8_t warm_pow;
    uint8_t warm_rgb[3];
    float edge_t;
    uint8_t edge_rgb[3], edge_lit_rgb[3];
    float edge_lit_k;
} fk_cols_look_t;
/* .72, .1, .8; .55, 7, (255, 170, 90); 9; #120c0a, (255, 186, 108), .75 */
extern const fk_cols_look_t FK_COLS_LOOK_DW;

typedef struct fk_cols_kind {
    bool inward, keep;
    float open_deg;
    float pop_deg;                 /* appended (PR1a): the latch pop the leaf's light rises with (0 to 1 by pop) */
    float kept_sin, kept_cos;      /* appended (PR1a): the correctly rounded float sine and cosine of the float angle
                                    * open_deg x D2R, taken instead of libm by an inward leaf at exactly that angle (a
                                    * keep kind's still frames: host-identical); an outward kind never reads them */
    const fk_cols_look_t *look;    /* appended (rd wave 2 Task 4): the leaf's look; NULL: FK_COLS_LOOK_DW. Read for an
                                    * outward kind only */
} fk_cols_kind_t;
void fk_cols_k(const fk_cols_kind_t *k, float th_l, float th_r, fk_col_t cols[466]);

typedef struct {
    float th_l, th_r;              /* radians; leaves gone past omni_hinge_gone_rad (67.4 deg) */
    uint8_t fade;                  /* 0..255 up from black (the opening fade) */
    uint8_t seam, glow;            /* the light crack and the gap's glow column, 0..255 */
    uint8_t xfade;                 /* reduced motion: the doors' weight over the interior, 255 -> 0 */
    uint8_t tail;                  /* 0..255 through the tail: lances flare, the script winds in, the hands grow */
    bool leaves, done;             /* any leaf on screen; at or past the end (the interior exactly) */
    bool keep;                     /* appended (wave 2 Task 3): the kind's keep. Done with keep: the leaves at
                                    * open_deg with the rim light full, so the band draws the interior plus the kept
                                    * leaves, not the interior alone (and leaves stays true) */
    bool inward;                   /* appended (wave 2 Task 3): the kind's inward (the band's free-edge quads) */
} fk_cols_state_t;
/* The free edge of the left leaf on screen at th (drawLeaf's proj(233)); the right one's is 466 minus it. k's kept trig
 * is read for an inward leaf at its kept angle. The timeline's gap glow reads it. */
float fk_cols_free_edge(const fk_cols_kind_t *k, float th, bool inward);
/* Whether a leaf at th still shows on screen: inward always; outward until both its face (th < gone, the hinge's gone
 * angle) and its free-edge thickness (to about 69.6 deg) have left it. */
bool fk_cols_leaf_on_screen(float th, float gone, bool inward);
/* appended (rd wave 2 Task 4, I8): whether a leaf of kind k at th still shows, its free edge at k's look's edge_t
 * (NULL k or a NULL look: FK_COLS_LOOK_DW's 9 px, so fk_cols_leaf_on_screen's answer is k NULL's): inward always;
 * outward until both its face (th < omni_hinge_gone_rad(&FK_COLS_HINGE)) and its free edge have left the screen. */
bool fk_cols_leaf_on_screen_k(const fk_cols_kind_t *k, float th);

/* One band: interior (the face's settled frame, stride 466) under the leaves, glow and fade. k: the kind st was made
 * for (its pop_deg lights the free edges; its kept trig for an inward leaf at its kept angle). door: the closed doors
 * (466 x 466 RGB565); glow: the 64 x 256 A8 sprite, tinted warm, stretched over the gap. Writes out (stride 466).
 * interior and door are whole frames (row 0 = screen row 0). out is the band's buffer: row 0 = band.y0, column x at
 * offset x (omni_band_pack's layout); only band's pixels are written. Per pixel, in drawDoorsPhase's order: the
 * interior; each moving leaf's free-edge thickness (one omni_fill_poly quad, rim-lit); the leaf (the door's pixel,
 * shaded, plus drawLeaf's warm rim near the free edge, at xfade over what is there); the seam and the gap's glow
 * ('lighter'); the fade from black. Every value depends only on the pixel and the inputs, never on the band: a frame
 * built in bands is the frame built whole, byte for byte. done: the interior exactly (done with keep, wave 2 Task 3:
 * the interior plus the kept leaves, each step as the swing's at that state: the free edges, the leaves at full weight).
 * Host and device: only still frames are byte-identical across them: the closed doors (theta 0: an identity map), the
 * end and a tap (a copy of the interior). A mid-swing frame goes through sinf, cosf and atan2f per column, and the
 * host's libm and the device's newlib may differ by an ulp, which can move a column's u or inv_s (or a leaf's edge) by
 * one at a boundary. Never CRC-compare a mid-swing frame between host and device, and never call one host-identical;
 * on one machine, every frame is deterministic and band-split exact. A keep kind's end and tap frames (and
 * fk_cols_leaves) are still frames: an inward leaf at exactly a kept angle (k->open_deg x D2R) takes its sine and
 * cosine from exact constants, not libm (wave 2 Task 11), so they are host-identical, and #3's stills that show the
 * kept leaves CRC the same on the host and the device. */
void fk_cols_band(const fk_cols_kind_t *k, const fk_cols_state_t *st, const fk_col_t cols[466], const uint16_t *door,
                  const uint16_t *interior, const uint8_t *glow, omni_rect_t band, uint16_t *out);
/* The round panel (Task 12): pixel (x, y) shows only if its centre lies inside the 466 px circle,
 * (x + .5 - 233)^2 + (y + .5 - 233)^2 < 233^2; row y's such columns are [*x0, *x1) (empty off the rows). The band
 * writes black off the panel and does no work there (a fifth of the square), so a frame is the description above
 * inside the panel and black outside, and "the interior exactly" means the interior's panel pixels. */
void fk_panel_row(int y, int *x0, int *x1);
/* The kept leaves alone, over fb (#3's draw; wave 2 Task 3): each leaf pixel exactly as fk_cols_band draws it in the
 * done state of k, from cols = fk_cols_k(k, open, open) (open = k->open_deg in radians): the free edges'
 * thickness quads (drawLeaf's, faces-reveal.js:157-166; an inward one lies behind its leaf), then the leaves, shaded and
 * rim-lit at full weight. fb is the pixel at (clip.x0, clip.y0), row stride `stride` px (fk/compose.h); only clip's
 * pixels on the round panel are written, every other pixel of fb is left as it is. A pixel's value depends only on the
 * pixel and the inputs, never on the clip. With fb the band's interior, the result is the band's done frame on the
 * panel (test_doors_inward_end_seam); over a face that already shows the leaves it changes nothing. */
void fk_cols_leaves(const fk_cols_kind_t *k, const fk_col_t cols[466], const uint16_t *door, omni_rect_t clip,
                     uint16_t *fb, int stride);
