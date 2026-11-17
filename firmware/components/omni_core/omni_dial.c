#include "omni/omni_dial.h"
#include <math.h>
#include <string.h>
#include "omni/blend565.h"
#include "omni/iris.h"
#include "omni/lid.h"

/* The Omni dial compositor (omni_dial.h). A render walks its rect in bands of BAND rows and draws every layer of a band
 * in drawDial's order before the next band (the frame lives in PSRAM: a band stays in the cache). Every layer is a
 * per-pixel function of the frame and the pixel, so any split of a frame into rects gives the same bytes
 * (test_dial_rect_equals_full). Single precision in a fixed order (-ffp-contract=off). */

#define SW OMNI_DIAL_W
#define BAND 16
#define C 233.0f
#define TAU_F 6.28318530717958647692f

const omni_globe_cfg_t OMNI_DIAL_GLOBE_CFG = { .size = 224, .diam = 224, .lat0_deg = 22, .proj = OMNI_PROJ_ORTHO,
                                               .studio = true, .light = { -0.42f, 0.52f, 0.74f }, .limb_kind = 1,
                                               .tex_w = 512 };
const omni_globe_look_t OMNI_DIAL_GLOBE_LOOK = { .gain = 1, .kind = 1 };

static const float VD[OMNI_RINGS] = { 11.5f, 12.5f, 14.0f, 16.5f };   /* the V marks' half-gaps, px */

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ---- Rects */

static const omni_rect_t SCREEN = { 0, 0, SW, SW };
static bool empty(const omni_rect_t *r) { return r->x0 >= r->x1 || r->y0 >= r->y1; }
static omni_rect_t isect(omni_rect_t a, omni_rect_t b)
{
    return (omni_rect_t){ (int16_t)imax(a.x0, b.x0), (int16_t)imax(a.y0, b.y0), (int16_t)imin(a.x1, b.x1),
                          (int16_t)imin(a.y1, b.y1) };
}
static omni_rect_t unite(omni_rect_t a, omni_rect_t b)
{
    if (empty(&a)) return b;
    if (empty(&b)) return a;
    return (omni_rect_t){ (int16_t)imin(a.x0, b.x0), (int16_t)imin(a.y0, b.y0), (int16_t)imax(a.x1, b.x1),
                          (int16_t)imax(a.y1, b.y1) };
}
static int32_t area(const omni_rect_t *r) { return empty(r) ? 0 : (int32_t)(r->x1 - r->x0) * (r->y1 - r->y0); }
static bool meets(omni_rect_t a, omni_rect_t b) { const omni_rect_t i = isect(a, b); return !empty(&i); }
static omni_rect_t fbox(float x0, float y0, float x1, float y1)   /* the pixels a float box touches */
{
    x0 = clampf(x0, -1000, 1000);
    y0 = clampf(y0, -1000, 1000);
    x1 = clampf(x1, -1000, 1000);
    y1 = clampf(y1, -1000, 1000);
    return (omni_rect_t){ (int16_t)floorf(x0), (int16_t)floorf(y0), (int16_t)(floorf(x1) + 1), (int16_t)(floorf(y1) + 1) };
}

/* ---- Geometry: the magnifier and the loupe */

omni_pmap_cfg_t omni_dial_mag_cfg(uint16_t n_theta)
{
    return (omni_pmap_cfg_t){ .rect = { OMNI_MAG_X0, OMNI_MAG_Y0, OMNI_MAG_X1, OMNI_MAG_Y1 }, .dx = C, .dy = 70.5f,
                              .sx = C, .sy = 70.5f, .k = 1.4f, .rr = 3, .n_theta = n_theta, .r_min = 126, .n_r = 71 };
}

#define LOUPE_K 2.35f
#define LOUPE_SIGMA 9.0f     /* shadowBlur 18: a Gaussian of sigma 9 */
#define LOUPE_SHADOW_DY 6.0f
#define LOUPE_REACH 27.0f    /* 3 sigma: beyond it the shadow is below half a level */

void omni_dial_loupe_geom(float a, omni_loupe_geom_t *g)
{
    a = clampf(a, 0, 1);
    const float u = 1 - a, e = 1 - u * u * u;   /* outCubic */
    g->cx = C;
    g->cy = 70.5f + (239.0f - 70.5f) * e;
    g->w = 52.0f + (74.0f - 52.0f) * e;
    g->h = 104.0f + (176.0f - 104.0f) * e;
    g->ga = a * 2 < 1 ? a * 2 : 1;
    const float x0 = g->cx - g->w / 2, y0 = g->cy - g->h / 2;
    g->content = (omni_rect_t){ (int16_t)floorf(x0 + 0.5f), (int16_t)floorf(y0 + 0.5f), (int16_t)floorf(x0 + g->w + 0.5f),
                                (int16_t)floorf(y0 + g->h + 0.5f) };
}

omni_pmap_cfg_t omni_dial_loupe_cfg(float a, uint16_t n_theta)
{
    omni_loupe_geom_t g;
    omni_dial_loupe_geom(a, &g);
    return (omni_pmap_cfg_t){ .rect = g.content, .dx = g.cx, .dy = g.cy, .sx = C, .sy = 71.0f, .k = LOUPE_K, .rr = 5,
                              .n_theta = n_theta, .r_min = 126, .n_r = 71 };
}

bool omni_dial_loupe_ready(const omni_dial_res_t *res, float a)
{
    if (!(a > 0)) return true;
    if (!res->loupe_map || !res->loupe_base) return false;
    const omni_pmap_cfg_t w = omni_dial_loupe_cfg(a, res->dial_map.cfg.n_theta), *m = &res->loupe_map->cfg;
    return m->rect.x0 == w.rect.x0 && m->rect.y0 == w.rect.y0 && m->rect.x1 == w.rect.x1 && m->rect.y1 == w.rect.y1 &&
           m->dx == w.dx && m->dy == w.dy && m->sx == w.sx && m->sy == w.sy && m->k == w.k && m->rr == w.rr &&
           m->n_theta == w.n_theta && m->r_min == w.r_min && m->n_r == w.n_r;
}

/* The frame (the content grown by 5, rr 8) as floats, from the unrounded geometry, as drawLoupe. */
static void loupe_frame(const omni_loupe_geom_t *g, float f[4])
{
    f[0] = g->cx - g->w / 2 - 5;
    f[1] = g->cy - g->h / 2 - 5;
    f[2] = g->cx + g->w / 2 + 5;
    f[3] = g->cy + g->h / 2 + 5;
}

void omni_dial_loupe_bbox(float a, omni_rect_t *out)
{
    if (!(a > 0)) {
        *out = (omni_rect_t){ 0, 0, 0, 0 };
        return;
    }
    omni_loupe_geom_t g;
    omni_dial_loupe_geom(a, &g);
    float f[4];
    loupe_frame(&g, f);
    const omni_rect_t frame = fbox(f[0], f[1], f[2], f[3]);
    const omni_rect_t sh = fbox(f[0] - LOUPE_REACH, f[1] + LOUPE_SHADOW_DY - LOUPE_REACH, f[2] + LOUPE_REACH,
                                f[3] + LOUPE_SHADOW_DY + LOUPE_REACH);
    *out = isect(unite(frame, sh), SCREEN);
}

/* The map's source rect (the dial pixels its rect shows), + 1 px. */
static omni_rect_t map_source(const omni_pmap_cfg_t *c)
{
    const float x0 = c->sx + ((float)c->rect.x0 - c->dx) / c->k, y0 = c->sy + ((float)c->rect.y0 - c->dy) / c->k;
    const float x1 = c->sx + ((float)c->rect.x1 - c->dx) / c->k, y1 = c->sy + ((float)c->rect.y1 - c->dy) / c->k;
    return fbox(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
}

static int ex8(const uint16_t *p, int ch) { return ch == 0 ? omni_ex_r(*p) : ch == 1 ? omni_ex_g(*p) : omni_ex_b(*p); }

void omni_dial_zoom_base(const uint16_t *base, const omni_pmap_cfg_t *cfg, uint16_t *out)
{
    const omni_rect_t *r = &cfg->rect;
    const int w = r->x1 - r->x0;
    for (int y = r->y0; y < r->y1; y++)
        for (int x = r->x0; x < r->x1; x++) {
            const float u = cfg->sx + ((float)x + 0.5f - cfg->dx) / cfg->k - 0.5f;
            const float v = cfg->sy + ((float)y + 0.5f - cfg->dy) / cfg->k - 0.5f;
            const int u0 = (int)floorf(u), v0 = (int)floorf(v);
            const float fu = u - (float)u0, fv = v - (float)v0;
            const int x0 = imin(imax(u0, 0), SW - 1), x1 = imin(imax(u0 + 1, 0), SW - 1);
            const int y0 = imin(imax(v0, 0), SW - 1), y1 = imin(imax(v0 + 1, 0), SW - 1);
            const uint16_t *p00 = base + y0 * SW + x0, *p01 = base + y0 * SW + x1;
            const uint16_t *p10 = base + y1 * SW + x0, *p11 = base + y1 * SW + x1;
            int c[3];
            for (int ch = 0; ch < 3; ch++) {   /* rounded to the nearest 5/6-bit level, as the strips are (ring.c) */
                const float t = (float)ex8(p00, ch) * (1 - fu) + (float)ex8(p01, ch) * fu;
                const float b = (float)ex8(p10, ch) * (1 - fu) + (float)ex8(p11, ch) * fu;
                const float top = ch == 1 ? 63.0f : 31.0f;
                c[ch] = imin(imax((int)floorf((t * (1 - fv) + b * fv) * top / 255.0f + 0.5f), 0), (int)top);
            }
            out[(y - r->y0) * w + (x - r->x0)] = (uint16_t)(c[0] << 11 | c[1] << 5 | c[2]);
        }
}

/* The pixel centre inside cfg's rounded rect (ring.c's in_clip: the maps keep exactly these pixels). */
static bool in_rrect(const omni_pmap_cfg_t *c, int x, int y)
{
    if (!(c->rr > 0)) return true;
    const float px = (float)x + 0.5f, py = (float)y + 0.5f;
    const float ax = (float)c->rect.x0 + c->rr, bx = (float)c->rect.x1 - c->rr;
    const float ay = (float)c->rect.y0 + c->rr, by = (float)c->rect.y1 - c->rr;
    const float dx = px < ax ? ax - px : px > bx ? px - bx : 0.0f;
    const float dy = py < ay ? ay - py : py > by ? py - by : 0.0f;
    return dx * dx + dy * dy <= c->rr * c->rr;
}

/* ---- The flash */

#define FT_SHIFT 9                            /* the table is indexed by 4 d^2 >> 9 (d^2 / 128) */
#define FT_N ((4 * 260 * 260 >> FT_SHIFT) + 2)   /* knots up to beyond r 260 */
typedef struct { uint8_t c[FT_N][3]; } ftab_t;

void omni_flash_rgb(float f, float d, float out[3])
{
    static const float CS[3][3] = { { 255, 250, 235 }, { 255, 220, 170 }, { 255, 160, 90 } };
    const float as[3] = { clampf(f * 1.1f, 0, 1), clampf(f * 0.8f, 0, 1), clampf(f * 0.4f, 0, 1) };
    const float t = clampf(d / 260.0f, 0, 1);
    const int s = t <= 0.45f ? 0 : 1;
    const float u = s ? (t - 0.45f) / 0.55f : t / 0.45f;
    const float a = as[s] + (as[s + 1] - as[s]) * u;
    for (int k = 0; k < 3; k++) out[k] = (CS[s][k] + (CS[s + 1][k] - CS[s][k]) * u) * a;
}

static void ftab_build(ftab_t *t, float f)
{
    for (int i = 0; i < FT_N; i++) {
        float c[3];
        omni_flash_rgb(f, sqrtf((float)(i << FT_SHIFT)) * 0.5f, c);
        for (int k = 0; k < 3; k++) t->c[i][k] = (uint8_t)imin(imax((int)floorf(c[k] + 0.5f), 0), 255);
    }
}

static inline uint16_t add565(uint16_t d, int r, int g, int b)
{
    return omni_pack565(imin(omni_ex_r(d) + r, 255), imin(omni_ex_g(d) + g, 255), imin(omni_ex_b(d) + b, 255));
}

static void flash_apply(const ftab_t *t, uint16_t *fb, int stride, const omni_rect_t *r)
{
    const int last = FT_N - 1, M = (1 << FT_SHIFT) - 1;
    for (int y = r->y0; y < r->y1; y++) {
        const int dy = 2 * y - (SW - 1), dy2 = dy * dy;
        uint16_t *row = fb + (size_t)y * stride;
        for (int x = r->x0; x < r->x1; x++) {
            const int dx = 2 * x - (SW - 1), D = dx * dx + dy2;   /* (2 d)^2 at the pixel centre */
            const int i = D >> FT_SHIFT;
            int c[3];
            if (i >= last) {
                for (int k = 0; k < 3; k++) c[k] = t->c[last][k];
            } else {
                const int fr = D & M;
                for (int k = 0; k < 3; k++) c[k] = (t->c[i][k] * (M + 1 - fr) + t->c[i + 1][k] * fr + (1 << (FT_SHIFT - 1))) >> FT_SHIFT;
            }
            row[x] = add565(row[x], c[0], c[1], c[2]);
        }
    }
}

void omni_flash(uint16_t *fb, int stride, const omni_rect_t *r, float f)
{
    const omni_rect_t c = isect(*r, SCREEN);
    if (!(f > 0) || empty(&c)) return;
    ftab_t t;
    ftab_build(&t, f);
    flash_apply(&t, fb, stride, &c);
}

/* ---- Layers */

static uint32_t first_ospan(const omni_overlay_t *ov, int iy)   /* spans are in row order */
{
    uint32_t lo = 0, hi = ov->nspans;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if ((int)ov->span[mid].y < iy) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* The layer over rect b at global alpha ga (255: the layer as it is, through omni_overlay_row). */
static void layer_draw(uint16_t *fb, const omni_dial_layer_t *L, const omni_rect_t *b, int ga,
                       void (*run)(const uint32_t *, uint16_t *, int))
{
    const omni_overlay_t *ov = &L->ov;
    if (!ov->nspans || ga <= 0) return;
    const int iy0 = b->y0 - L->y, iy1 = b->y1 - L->y;
    uint32_t si = first_ospan(ov, iy0 > 0 ? iy0 : 0);
    if (ga >= 255 && L->x >= 0) {
        for (int iy = iy0 > 0 ? iy0 : 0; iy < iy1 && si < ov->nspans; iy++)
            omni_overlay_row(ov, &si, (uint16_t)iy, fb + (size_t)(L->y + iy) * SW + L->x, b->x0 - L->x, b->x1 - L->x, run);
        return;
    }
    for (; si < ov->nspans && (int)ov->span[si].y < iy1; si++) {   /* the same rule, alpha x ga */
        const omni_ospan_t *s = &ov->span[si];
        const int y = L->y + s->y, sx = L->x + s->x0, xa = imax(sx, b->x0), xb = imin(sx + s->n, b->x1);
        const uint32_t *src = ov->argb + s->off;
        uint16_t *d = fb + (size_t)y * SW;
        for (int x = xa; x < xb; x++) {
            const uint32_t c = src[x - sx];
            const int a = ((int)(c >> 24) * ga + 127) / 255;
            if (!a) continue;
            const int ap = omni_amul(a);
            d[x] = omni_pack565(omni_blend8((int)(c >> 16 & 255), omni_ex_r(d[x]), ap),
                                omni_blend8((int)(c >> 8 & 255), omni_ex_g(d[x]), ap), omni_blend8((int)(c & 255), omni_ex_b(d[x]), ap));
        }
    }
}

/* Image row iy of the layer, columns [0, GLASS_W): its pixels, 0 where it has none. */
#define GLASS_W 52   /* the glass's width (the mockup's MAG window) */
static void layer_row(const omni_overlay_t *ov, int iy, uint32_t out[GLASS_W])
{
    memset(out, 0, GLASS_W * sizeof *out);
    for (uint32_t si = ov->nspans ? first_ospan(ov, iy) : 0; si < ov->nspans && (int)ov->span[si].y == iy; si++) {
        const omni_ospan_t *sp = &ov->span[si];
        for (int k = 0; k < sp->n; k++)
            if (sp->x0 + k < GLASS_W) out[sp->x0 + k] = ov->argb[sp->off + (uint32_t)k];
    }
}

static omni_rect_t layer_box(const omni_dial_layer_t *L)
{
    const omni_overlay_t *ov = &L->ov;
    if (!ov->nspans) return (omni_rect_t){ 0, 0, 0, 0 };
    int x0 = 32767, x1 = -32767;
    for (uint32_t i = 0; i < ov->nspans; i++) {
        x0 = imin(x0, ov->span[i].x0);
        x1 = imax(x1, ov->span[i].x0 + ov->span[i].n);
    }
    return (omni_rect_t){ (int16_t)(L->x + x0), (int16_t)(L->y + ov->span[0].y), (int16_t)(L->x + x1),
                          (int16_t)(L->y + ov->span[ov->nspans - 1].y + 1) };
}

/* The bloom added at level lv (0..1, x .55 here). */
static void bloom_add(uint16_t *fb, const omni_dial_a8_t *B, const omni_rect_t *b, float lv)
{
    if (!B->a) return;
    const omni_rect_t r = isect(*b, (omni_rect_t){ B->x, B->y, (int16_t)(B->x + B->w), (int16_t)(B->y + B->h) });
    const int lq = (int)floorf(clampf(lv, 0, 1) * 0.55f * 256.0f + 0.5f);
    const int ci[3] = { omni_ex_r(B->rgb_in), omni_ex_g(B->rgb_in), omni_ex_b(B->rgb_in) };
    const int co[3] = { omni_ex_r(B->rgb_out), omni_ex_g(B->rgb_out), omni_ex_b(B->rgb_out) };
    for (int y = r.y0; y < r.y1; y++) {
        const uint8_t *src = B->a + (size_t)(y - B->y) * B->w;
        uint16_t *d = fb + (size_t)y * SW;
        for (int x = r.x0; x < r.x1; x++) {
            const int a = src[x - B->x];
            if (!a) continue;
            int c[3];
            for (int k = 0; k < 3; k++) {
                const int col = co[k] + ((ci[k] - co[k]) * a + 127) / 255;
                c[k] = (col * a * lq + 255 * 128) / (255 * 256);
            }
            d[x] = add565(d[x], c[0], c[1], c[2]);
        }
    }
}

/* ---- The composite under the hands: base, rings, V marks, hands, through an affine (the dial is k 1) */

typedef struct { float ca, sa; uint8_t ring, a_hi, a_red; } mark_t;
typedef struct {
    const omni_pmap_t *map; const uint16_t *base; omni_rect_t brect;   /* base: brect's pixels, row stride its width */
    float k, dx, dy, sx, sy;
    int zi;                                                            /* the hands built for it: ctx_t.hand[zi] */
} zoom_t;

typedef struct {
    const omni_dial_frame_t *f;
    const omni_dial_res_t *R;
    mark_t mark[OMNI_RINGS * 4];
    int nmark;
    zoom_t zdial, zmag, zloupe;
    bool mag, loupe, loupe_content, globe, marker, flash;
    omni_globe_frame_t gf;
    omni_globe_cterms_t gt;
    int gx, gy;
    float mx, my, mrad;
    uint8_t ma_ring, ma_dot;
    omni_loupe_geom_t lg;
    ftab_t ft;
    omni_hand_t hand[3][2];   /* hour, minute: through the dial's, the magnifier's and the loupe's affine (T6 M4) */
    omni_rrect_t lframe;      /* the loupe's brass frame outline (rr 8), built once a frame */
    omni_rect_t lbb;          /* the loupe's shadow: its box, and its separable factors per column and per row */
    float lsx[SW], lsy[SW];
    bool hand_ok[3][2];
    bool valid;               /* res and its base are there: the frame draws */
    /* the lid (Task 9), built once a frame (its lines are built by each band for its own rows: lband_t) */
    bool lid;
    int dim8, fade8;          /* the dial's dim toward black (omni_flip_dim) and the reveal's fade over the frame, x 255 */
    float theta;
    const uint16_t *lid_rgb;  /* the bitmap's planes */
    const uint8_t *lid_a;
    const uint8_t (*lsc)[3];  /* res->lid_spec */
    bool iris;                /* the iris (Task 10), prepared once a frame */
    omni_iris_t iris_p;
} ctx_t;
typedef char omni_dial_prep_size_check[sizeof(ctx_t) <= sizeof(omni_dial_prep_t) ? 1 : -1];

static zoom_t zoom_of(const omni_pmap_t *m, const uint16_t *base)
{
    const omni_pmap_cfg_t *c = &m->cfg;
    return (zoom_t){ m, base, c->rect, c->k, c->dx, c->dy, c->sx, c->sy, 0 };
}

static void marks_init(ctx_t *c)
{
    const omni_dial_state_t *s = &c->f->st;
    c->nmark = 0;
    for (int i = 0; i < OMNI_RINGS; i++) {
        const omni_ring_geom_t *g = &OMNI_RING_GEOM[i];
        const float rc = (g->r0 + g->r1) / 2, da = VD[i] / rc;
        for (int k = 0; k < 2; k++) {
            const float sel = k ? s->sel_new[i] : s->sel_old[i], al = clampf(k ? s->mark_new : s->mark_old, 0, 1);
            if (al < 0.02f) continue;
            const float a = (sel - c->f->pose.val[i]) / (float)g->slots * TAU_F;
            for (int sg = -1; sg <= 1; sg += 2) {
                mark_t *m = &c->mark[c->nmark++];
                const float t = a + (float)sg * da;
                m->ca = cosf(t);
                m->sa = sinf(t);
                m->ring = (uint8_t)i;
                m->a_hi = (uint8_t)floorf(0.25f * al * 255 + 0.5f);
                m->a_red = (uint8_t)floorf(al * 255 + 0.5f);
            }
        }
    }
}

/* fillRect(-1.1, -(r1 - 2.6) + dy, 2.2, r1 - r0 - 5) rotated by the mark's angle, through z. */
static void mark_poly(const mark_t *m, float dy, const zoom_t *z, omni_pt_t p[4])
{
    const omni_ring_geom_t *g = &OMNI_RING_GEOM[m->ring];
    const float y0 = -(g->r1 - 2.6f) + dy, y1 = y0 + (g->r1 - g->r0 - 5);
    const float lx[4] = { -1.1f, 1.1f, 1.1f, -1.1f }, ly[4] = { y0, y0, y1, y1 };
    for (int i = 0; i < 4; i++) {
        const float qx = C + lx[i] * m->ca - ly[i] * m->sa, qy = C + lx[i] * m->sa + ly[i] * m->ca;
        p[i] = (omni_pt_t){ z->dx + z->k * (qx - z->sx), z->dy + z->k * (qy - z->sy) };
    }
}

/* The composite over clip (inside z->brect); fb is clip's first pixel, row stride `stride`. */
static void zoom_draw(const ctx_t *c, const zoom_t *z, uint16_t *fb, int stride, const omni_rect_t *clip)
{
    const omni_dial_res_t *R = c->R;
    const int bw = z->brect.x1 - z->brect.x0, w = clip->x1 - clip->x0;
    for (int y = clip->y0; y < clip->y1; y++)
        memcpy(fb + (size_t)(y - clip->y0) * stride, z->base + (size_t)(y - z->brect.y0) * bw + (clip->x0 - z->brect.x0),
               (size_t)w * 2);
    omni_rings_draw(R->strips, z->map, &c->f->pose, R->rkernel ? R->rkernel : omni_rings_span_codes, fb, stride, clip);
    const uint16_t hi = omni_pack565(255, 200, 170), red = omni_pack565(184, 38, 26);   /* #b8261a */
    for (int i = 0; i < c->nmark; i++) {
        omni_pt_t p[4];
        mark_poly(&c->mark[i], 0.7f, z, p);
        omni_fill_poly(fb, stride, clip, p, 4, hi, c->mark[i].a_hi);
        mark_poly(&c->mark[i], 0, z, p);
        omni_fill_poly(fb, stride, clip, p, 4, red, c->mark[i].a_red);
    }
    for (int h = 0; h < 2; h++)   /* built once a frame (ctx_init), as omni_draw_hand would build them */
        if (c->hand_ok[z->zi][h]) omni_hand_draw(&c->hand[z->zi][h], fb, stride, clip);
}

static void hands_build(ctx_t *c, zoom_t *z, int zi)
{
    const omni_dial_state_t *s = &c->f->st;
    z->zi = zi;
    c->hand_ok[zi][0] = omni_hand_prepare(&c->hand[zi][0], s->hour_rad, true, !c->f->fast, z->k, z->dx, z->dy, z->sx, z->sy);
    c->hand_ok[zi][1] = omni_hand_prepare(&c->hand[zi][1], s->min_rad, false, !c->f->fast, z->k, z->dx, z->dy, z->sx, z->sy);
}

/* ---- The frame's other steps, each over band b */

static void globe_draw(const ctx_t *c, uint16_t *fb, const omni_rect_t *b)
{
    const omni_globe_clut_t *cl = c->R->clut;
    const int S = cl->cfg.size, y0 = imax(b->y0, c->gy), y1 = imin(b->y1, c->gy + S);
    if (y0 >= y1 || b->x1 <= c->gx || b->x0 >= c->gx + S) return;
    const omni_globe_cspan_fn fn = c->R->ckernel ? c->R->ckernel : omni_globe_cspan_ref;
    unsigned lo = 0, hi = cl->nspans;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        if (cl->span[mid].y < y0 - c->gy) lo = mid + 1;
        else hi = mid;
    }
    for (unsigned si = lo; si < cl->nspans && cl->span[si].y < y1 - c->gy; si++) {
        const omni_globe_cspan_t *sp = &cl->span[si];
        const int sx = c->gx + sp->x0, xa = imax(sx, b->x0), xb = imin(sx + sp->n, b->x1);
        if (xa >= xb || sp->n > 256) continue;
        uint16_t *d = fb + (size_t)(c->gy + sp->y) * SW + sx;
        if (xa == sx && xb == sx + sp->n) {
            fn(&c->gf, sp, &c->gt, d);
            continue;
        }
        uint16_t tmp[256];   /* cut by the band: the whole span on a copy, its inside written back */
        memset(tmp, 0, (size_t)sp->n * 2);
        memcpy(tmp + (xa - sx), d + (xa - sx), (size_t)(xb - xa) * 2);
        fn(&c->gf, sp, &c->gt, tmp);
        memcpy(d + (xa - sx), tmp + (xa - sx), (size_t)(xb - xa) * 2);
    }
}

static void tint_add(uint16_t *fb, int stride, const omni_rect_t *r, int ar, int ag, int ab)
{
    for (int y = r->y0; y < r->y1; y++) {
        uint16_t *d = fb + (size_t)(y - r->y0) * stride;
        for (int x = 0; x < r->x1 - r->x0; x++) d[x] = add565(d[x], ar, ag, ab);
    }
}

#define CORNERS_MAX 64
static int corners_save(const omni_pmap_cfg_t *mc, const uint16_t *fb, const omni_rect_t *r, uint16_t *buf, int16_t *pos)
{
    const int rr = (int)ceilf(mc->rr);
    int n = 0;
    for (int y = r->y0; y < r->y1; y++) {
        if (y >= mc->rect.y0 + rr && y < mc->rect.y1 - rr) continue;
        for (int x = r->x0; x < r->x1; x++) {
            if (x >= mc->rect.x0 + rr && x < mc->rect.x1 - rr) continue;
            if (in_rrect(mc, x, y) || n >= CORNERS_MAX) continue;
            pos[2 * n] = (int16_t)x;
            pos[2 * n + 1] = (int16_t)y;
            buf[n++] = fb[(size_t)y * SW + x];
        }
    }
    return n;
}

static void magnifier_draw(const ctx_t *c, uint16_t *fb, const omni_rect_t *b)
{
    const omni_pmap_cfg_t *mc = &c->R->mag_map.cfg;
    const omni_rect_t r = isect(*b, mc->rect);
    if (empty(&r)) return;
    uint16_t keep[CORNERS_MAX];
    int16_t pos[2 * CORNERS_MAX];
    const int n = corners_save(mc, fb, &r, keep, pos);   /* the clip's corners: the frame shows there */
    uint16_t *d = fb + (size_t)r.y0 * SW + r.x0;
    zoom_draw(c, &c->zmag, d, SW, &r);
    tint_add(d, SW, &r, 14, 11, 6);   /* 'lighter' (40, 32, 18) x .35 */
    layer_draw(fb, &c->R->glass, &r, 255, c->R->over_run);
    for (int i = 0; i < n; i++) fb[(size_t)pos[2 * i + 1] * SW + pos[2 * i]] = keep[i];
}

static void lenses_draw(const ctx_t *c, uint16_t *fb, const omni_rect_t *b)
{
    for (int k = 0; k < 2; k++) {
        const float lv = clampf(k ? c->f->st.green : c->f->st.red, 0, 1);
        layer_draw(fb, &c->R->lens[2 * k], b, 255, c->R->over_run);
        if (!(lv > 0.01f)) continue;
        layer_draw(fb, &c->R->lens[2 * k + 1], b, (int)floorf(lv * 255 + 0.5f), c->R->over_run);
        bloom_add(fb, &c->R->bloom[k], b, lv);
    }
}

/* brass(l) of the mockup (its BR ramp, each channel `| 0`). */
static void brass(float l, float out[3])
{
    static const float BR[6][4] = { { 0, 8, 6, 3 },        { .22f, 38, 28, 13 },   { .45f, 80, 62, 31 },
                                    { .65f, 128, 101, 54 }, { .83f, 178, 146, 90 }, { 1, 230, 208, 152 } };
    l = clampf(l, 0, 1);
    int i = 1;
    while (i < 5 && l > BR[i][0]) i++;
    const float t = (l - BR[i - 1][0]) / (BR[i][0] - BR[i - 1][0]);
    for (int k = 0; k < 3; k++) out[k] = floorf(BR[i - 1][k + 1] + (BR[i][k + 1] - BR[i - 1][k + 1]) * t);
}

/* The loupe's frame gradient at x: createLinearGradient(x0, 0, x0 + w, 0), stops .2 / .6 / .36 / .14 of brass. */
static uint16_t loupe_brass(const ctx_t *c, int x)
{
    static const float POS[4] = { 0, .2f, .6f, 1 }, LV[4] = { .2f, .6f, .36f, .14f };
    const float t = clampf(((float)x + 0.5f - (c->lg.cx - c->lg.w / 2)) / c->lg.w, 0, 1);
    int i = 1;
    while (i < 3 && t > POS[i]) i++;
    const float u = (t - POS[i - 1]) / (POS[i] - POS[i - 1]);
    float a[3], b[3];
    brass(LV[i - 1], a);
    brass(LV[i], b);
    int o[3];
    for (int k = 0; k < 3; k++) o[k] = imin(imax((int)floorf(a[k] + (b[k] - a[k]) * u + 0.5f), 0), 255);
    return omni_pack565(o[0], o[1], o[2]);
}

#define LOUPE_TMP_W 96
#define LOUPE_TMP_H 4

static void loupe_draw(const ctx_t *c, uint16_t *fb, const omni_rect_t *b)
{
    const omni_loupe_geom_t *g = &c->lg;
    const float ga = g->ga;
    const int ga8 = (int)floorf(ga * 255 + 0.5f);
    float fr[4];
    loupe_frame(g, fr);
    /* the drop shadow: black .9 x a separable erf (a box blurred by sigma 9), offset 6 down (its factors: ctx_init) */
    const omni_rect_t sr = isect(c->lbb, *b);
    for (int y = sr.y0; y < sr.y1; y++) {
        const float sy = c->lsy[y - c->lbb.y0];
        uint16_t *d = fb + (size_t)y * SW;
        for (int x = sr.x0; x < sr.x1; x++) {
            const int a = (int)floorf(sy * c->lsx[x - c->lbb.x0] + 0.5f);
            if (a > 0) d[x] = omni_blend565(d[x], 0, a);
        }
    }
    /* the brass frame, rr 8, at ga */
    const omni_rect_t fbx = isect(fbox(fr[0], fr[1], fr[2], fr[3]), *b);
    for (int xa = fbx.x0; xa < fbx.x1; xa += OMNI_COVER_W) {   /* the gradient is horizontal: a colour per column */
        const int xb = imin(xa + OMNI_COVER_W, fbx.x1);
        uint16_t col[OMNI_COVER_W];
        for (int x = xa; x < xb; x++) col[x - xa] = loupe_brass(c, x);
        for (int y = fbx.y0; y < fbx.y1; y++) {
            uint8_t cov[OMNI_COVER_W];
            omni_cover_rrect_prepared(&c->lframe, y, xa, xb, cov);
            uint16_t *d = fb + (size_t)y * SW;
            for (int x = xa; x < xb; x++) {
                const int al = (cov[x - xa] * ga8 + 127) / 255;
                if (al) d[x] = omni_blend565(d[x], col[x - xa], al);
            }
        }
    }
    /* the content, clipped to its rect (rr 5): black at ga, the zoomed composite at ga, (46, 36, 20) x .4 x ga, the
     * glass (52 x 104) stretched to the content, at ga; only from a map built for this st.loupe (review M2) */
    if (!c->loupe_content) return;
    const omni_pmap_cfg_t *lc = &c->R->loupe_map->cfg;
    const omni_rect_t cr = isect(lc->rect, *b);
    const int cw = lc->rect.x1 - lc->rect.x0, ch = lc->rect.y1 - lc->rect.y0;
    if (empty(&cr) || cw > LOUPE_TMP_W) return;
    const int tr = (int)floorf(18.4f * ga + 0.5f), tg = (int)floorf(14.4f * ga + 0.5f), tb = (int)floorf(8.0f * ga + 0.5f);
    const int gap = omni_amul(ga8);
    const omni_dial_layer_t *gl = &c->R->glass;
    for (int y0 = cr.y0; y0 < cr.y1; y0 += LOUPE_TMP_H) {
        const omni_rect_t rr = { cr.x0, (int16_t)y0, cr.x1, (int16_t)imin(y0 + LOUPE_TMP_H, cr.y1) };
        uint16_t tmp[LOUPE_TMP_W * LOUPE_TMP_H];
        zoom_draw(c, &c->zloupe, tmp, LOUPE_TMP_W, &rr);
        for (int y = rr.y0; y < rr.y1; y++) {
            uint16_t *d = fb + (size_t)y * SW;
            const int gy = (int)(((float)(y - lc->rect.y0) + 0.5f) * 104.0f / (float)ch);
            uint32_t grow[GLASS_W];   /* the glass row gy (0 where it has no pixel), fetched once a row */
            layer_row(&gl->ov, gy, grow);
            for (int x = rr.x0; x < rr.x1; x++) {
                if (!in_rrect(lc, x, y)) continue;
                const uint16_t s = tmp[(y - rr.y0) * LOUPE_TMP_W + (x - rr.x0)], o = d[x];
                int px[3];
                for (int k = 0; k < 3; k++) {
                    const int dk = k == 0 ? omni_ex_r(o) : k == 1 ? omni_ex_g(o) : omni_ex_b(o);
                    const int sk = k == 0 ? omni_ex_r(s) : k == 1 ? omni_ex_g(s) : omni_ex_b(s);
                    const int add = k == 0 ? tr : k == 1 ? tg : tb;
                    px[k] = imin(omni_blend8(sk, omni_blend8(0, dk, gap), gap) + add, 255);
                }
                const int gx = (int)(((float)(x - lc->rect.x0) + 0.5f) * 52.0f / (float)cw);
                const uint32_t gc = gx >= 0 && gx < GLASS_W ? grow[gx] : 0;
                const int ga2 = ((int)(gc >> 24) * ga8 + 127) / 255;
                if (ga2) {
                    const int ap = omni_amul(ga2);
                    px[0] = omni_blend8((int)(gc >> 16 & 255), px[0], ap);
                    px[1] = omni_blend8((int)(gc >> 8 & 255), px[1], ap);
                    px[2] = omni_blend8((int)(gc & 255), px[2], ap);
                }
                d[x] = omni_pack565(px[0], px[1], px[2]);
            }
        }
    }
}

static void marker_draw(const ctx_t *c, uint16_t *fb, const omni_rect_t *b)
{
    const omni_rect_t box = fbox(c->mx - c->mrad - 2, c->my - c->mrad - 2, c->mx + c->mrad + 2, c->my + c->mrad + 2);
    if (!meets(box, *b)) return;   /* its rings' polygons (trig) only where it is */
    uint16_t *d = fb + (size_t)b->y0 * SW + b->x0;
    omni_fill_ring(d, SW, b, c->mx, c->my, c->mrad - 0.7f, c->mrad + 0.7f, omni_pack565(255, 70, 40), c->ma_ring);
    omni_fill_ring(d, SW, b, c->mx, c->my, 0, 1.8f, omni_pack565(255, 74, 42), c->ma_dot);   /* #ff4a2a */
}

/* The marker's centre, radius and alphas; false when it is not drawn (z <= .15, or alpha 0). */
static bool marker_at(const omni_dial_frame_t *f, float *x, float *y, float *rad, uint8_t *a_ring, uint8_t *a_dot)
{
    const omni_globe_clut_t *cl = f->res->clut;
    const omni_dial_state_t *s = &f->st;
    if (!cl || !s->marker) return false;
    float p[3];
    omni_globe_project(&cl->cfg, s->marker_lat, s->marker_lon, s->lon_deg, p);
    if (!(p[2] > 0.15f)) return false;
    const float al = clampf((p[2] - 0.15f) * 3, 0, 1) * clampf(s->marker_a, 0, 1);
    const float o = C - (float)cl->cfg.size / 2;
    *x = o + (float)cl->cfg.size / 2 + p[0] * (float)cl->cfg.diam / 2;
    *y = o + (float)cl->cfg.size / 2 + p[1] * (float)cl->cfg.diam / 2;
    *rad = 5 + 4 * s->marker_pulse;
    *a_ring = (uint8_t)floorf(0.95f * al * 255 + 0.5f);
    *a_dot = (uint8_t)floorf(al * 255 + 0.5f);
    return *a_ring || *a_dot;
}

/* ---- The lid (Task 9): composited last, before the flash */

void omni_dial_lid_runs(const uint8_t *lid, int16_t run[4 * OMNI_DIAL_W])
{
    const uint8_t *A = lid + (size_t)SW * SW * 2;
    for (int v = 0; v < SW; v++) {
        const uint8_t *a = A + (size_t)v * SW;
        int x0 = 0, x1 = 0, b0 = 0, b1 = 0;
        while (x0 < SW && !a[x0]) x0++;
        x1 = SW;
        while (x1 > x0 && !a[x1 - 1]) x1--;
        for (int x = x0; x < x1;) {   /* the longest run of 255 */
            if (a[x] != 255) {
                x++;
                continue;
            }
            int e = x;
            while (e < x1 && a[e] == 255) e++;
            if (e - x > b1 - b0) b0 = x, b1 = e;
            x = e;
        }
        run[4 * v] = (int16_t)x0;
        run[4 * v + 1] = (int16_t)x1;
        run[4 * v + 2] = (int16_t)b0;
        run[4 * v + 3] = (int16_t)b1;
    }
}

/* A row's source column at screen x: u(x) = U0 + x inv in Q16, U0 = 233 - 232.5 inv (u at the pixel's centre). inv is
 * at most 2.0 here (lid_band), so everything fits 32 bits (a 64-bit division is a library call on the device). */
static int32_t lid_u0(uint32_t inv) { return (int32_t)(233 * 65536) - (int32_t)((465u * inv) >> 1); }
/* The first screen x >= 0 whose source column is at least a (SW when none is). */
static int16_t lid_x_at(int32_t u0, uint32_t inv, int a)
{
    const int32_t need = a * 65536 - u0;
    if (need <= 0) return 0;
    const uint32_t x = ((uint32_t)need + inv - 1) / inv;
    return (int16_t)(x > SW ? SW : x);
}

/* Row y's columns on the round panel: pixel centres within 233 of the centre, (2x + 1 - 466)^2 + (2y + 1 - 466)^2 <=
 * 466^2. */
static void panel_row(int y, int16_t pv[2])
{
    const int dy = 2 * y + 1 - SW, rem = SW * SW - dy * dy;
    int h = (int)sqrtf((float)rem);   /* the largest odd |2x + 1 - 466| with a square <= rem, then x from it */
    while (h * h > rem) h--;
    while ((h + 1) * (h + 1) <= rem) h++;
    if (!(h & 1)) h--;
    pv[0] = (int16_t)((SW - 1 - h) / 2);
    pv[1] = (int16_t)((SW - 1 + h) / 2 + 1);
}

void omni_dial_lid_spec(uint8_t spec[3 * OMNI_DIAL_W])
{
    static const int COL[3] = { 255, 240, 205 };
    uint8_t cols[SW];
    omni_lid_spec_cols(cols);
    for (int u = 0; u < SW; u++)
        for (int k = 0; k < 3; k++) spec[3 * u + k] = (uint8_t)((cols[u] * COL[k] + 127) / 255);
}

static void lid_init(ctx_t *c, const omni_dial_frame_t *f)
{
    const omni_dial_res_t *R = f->res;
    c->theta = f->st.lid_theta;
    c->dim8 = (int)floorf(omni_flip_dim(c->theta) * 255 + 0.5f);
    c->lid_rgb = (const uint16_t *)(const void *)R->lid;
    c->lid_a = R->lid + (size_t)SW * SW * 2;
    c->lsc = (const uint8_t (*)[3])(const void *)R->lid_spec;
}

/* A band's rows of the lid (built by the band itself, so the two cores share the lines' cost): per row its line (v -1:
 * no lid), the screen columns of its source row's any and opaque runs, and the
 * round panel's columns (the rest is black while the lid is up). */
typedef struct {
    int y0;
    omni_hinge_line_t ll[BAND];
    int16_t lrow[BAND][4], pv[BAND][2];
} lband_t;

static void lid_band(const ctx_t *c, const omni_rect_t *b, lband_t *lb)
{
    const int16_t *runs = c->R->lid_run;
    lb->y0 = b->y0;
    omni_hinge_lines_at(&OMNI_LID_HINGE, c->theta, b->y0, b->y1, lb->ll);
    for (int y = b->y0; y < b->y1; y++) {
        omni_hinge_line_t *l = &lb->ll[y - b->y0];
        int16_t *o = lb->lrow[y - b->y0];
        o[0] = o[1] = o[2] = o[3] = 0;
        panel_row(y, lb->pv[y - b->y0]);
        if (l->v < 0 || !l->inv_s_q16 || l->inv_s_q16 > 2 * 65536) {
            l->v = -1;
            continue;
        }
        const uint32_t inv = l->inv_s_q16;
        const int32_t u0 = lid_u0(inv);
        const int16_t vx0 = lid_x_at(u0, inv, 0), vx1 = lid_x_at(u0, inv, SW);   /* the columns inside the bitmap */
        if (runs) {
            const int16_t *r = &runs[4 * l->v];
            o[0] = lid_x_at(u0, inv, r[0]);
            o[1] = lid_x_at(u0, inv, r[1]);
            o[2] = lid_x_at(u0, inv, r[2]);
            o[3] = lid_x_at(u0, inv, r[3]);
            o[0] = o[0] < vx0 ? vx0 : o[0];
            o[1] = o[1] > vx1 ? vx1 : o[1];
        } else {
            o[0] = vx0;
            o[1] = vx1;
        }
    }
}

/* Where band b needs the dial: the columns of the panel in any of its rows ([*p0, *p1)), less those the opaque lid
 * covers in every one of them ([*x0, *x1), empty: x0 >= x1). */
static void lid_core(const lband_t *lb, const omni_rect_t *b, int *p0, int *p1, int *x0, int *x1)
{
    *x0 = b->x0;
    *x1 = b->x1;
    *p0 = b->x1;
    *p1 = b->x0;
    for (int y = b->y0; y < b->y1; y++) {
        const int16_t *o = lb->lrow[y - lb->y0], *pv = lb->pv[y - lb->y0];
        if (o[2] > *x0) *x0 = o[2];
        if (o[3] < *x1) *x1 = o[3];
        if (pv[0] < *p0) *p0 = pv[0];
        if (pv[1] > *p1) *p1 = pv[1];
    }
    *p0 = imax(*p0, b->x0);
    *p1 = imin(*p1, b->x1);
}

/* Off the round panel the lid frame is black (no panel pixel shows it; the mockup's shots are masked the same). */
static void panel_black(const lband_t *lb, uint16_t *fb, const omni_rect_t *b)
{
    for (int y = b->y0; y < b->y1; y++) {
        uint16_t *d = fb + (size_t)y * SW;
        const int16_t *pv = lb->pv[y - lb->y0];
        const int a = imin(pv[0], b->x1), e = imax(pv[1], b->x0);
        for (int x = b->x0; x < a; x++) d[x] = 0;
        for (int x = e; x < b->x1; x++) d[x] = 0;
    }
}

/* Row y's lid columns inside band b and on the panel ([*xa, *xb)), and its opaque run inside them ([*oa, *ob), drawn
 * from the lid alone, the fade included); false: no lid on the row. */
static bool lid_row_spans(const lband_t *lb, const omni_rect_t *b, int y, int *xa, int *xb, int *oa, int *ob)
{
    if (lb->ll[y - lb->y0].v < 0) return false;
    const int16_t *o = lb->lrow[y - lb->y0], *pv = lb->pv[y - lb->y0];
    *xa = imax(imax(o[0], b->x0), pv[0]);
    *xb = imin(imin(o[1], b->x1), pv[1]);
    if (*xa >= *xb) return false;
    *oa = imin(imax(o[2], *xa), *xb);
    *ob = imax(imin(o[3], *xb), *oa);
    return true;
}

/* The flip's fade from black (the mockup's fadeIn): the whole composite toward black, on the panel's columns, but for
 * the lid's opaque runs, which lid_draw wrote already faded (the same bytes: the stored channel, then the fade). */
static void fade_draw(const ctx_t *c, const lband_t *lb, uint16_t *fb, const omni_rect_t *b)
{
    if (!c->fade8) return;
    for (int y = b->y0; y < b->y1; y++) {
        uint16_t *d = fb + (size_t)y * SW;
        const int16_t *pv = lb->pv[y - lb->y0];
        const int pa = imax(pv[0], b->x0), pb = imin(pv[1], b->x1);
        int xa, xb, oa = pa, ob = pa;
        if (!lid_row_spans(lb, b, y, &xa, &xb, &oa, &ob)) oa = ob = pa;
        for (int x = pa; x < oa; x++) d[x] = omni_blend565(d[x], 0, c->fade8);
        for (int x = ob; x < pb; x++) d[x] = omni_blend565(d[x], 0, c->fade8);
    }
}

static void dim_rect(uint16_t *fb, const omni_rect_t *r, int a8)
{
    for (int y = r->y0; y < r->y1; y++) {
        uint16_t *d = fb + (size_t)y * SW;
        for (int x = r->x0; x < r->x1; x++) d[x] = omni_blend565(d[x], 0, a8);
    }
}

static void dim_draw(const ctx_t *c, uint16_t *fb, const omni_rect_t *r)
{
    if (c->dim8) dim_rect(fb, r, c->dim8);
}

/* A lid pixel of alpha a at source column ui, over frame pixel q (the general rule, omni_dial.h). */
static inline uint16_t lid_px_a(const ctx_t *c, uint16_t s, uint16_t q, int a, int dark, int sa, int ui)
{
    const uint8_t *sc = c->lsc[ui];
    const int ka = omni_amul((dark * a + 127) / 255), aa = omni_amul(a);
    const int p0 = imin(omni_blend8(0, omni_ex_r(s), ka) + ((sc[0] * sa * aa) >> 16), 255);
    const int p1 = imin(omni_blend8(0, omni_ex_g(s), ka) + ((sc[1] * sa * aa) >> 16), 255);
    const int p2 = imin(omni_blend8(0, omni_ex_b(s), ka) + ((sc[2] * sa * aa) >> 16), 255);
    return omni_pack565(omni_blend8(p0, omni_ex_r(q), aa), omni_blend8(p1, omni_ex_g(q), aa),
                        omni_blend8(p2, omni_ex_b(q), aa));
}

/* The fade on one 8-bit channel of 5 or 6 bits, as fade_draw's omni_blend565 to black would apply it to the stored
 * pixel: the channel truncated and widened as stored, then times kf = 256 - amul(fade8) (kf 256: the channel as is). */
static inline int fade5(int p, int kf) { return kf == 256 ? p : (omni_ex5((unsigned)p >> 3) * kf) >> 8; }
static inline int fade6(int p, int kf) { return kf == 256 ? p : (omni_ex6((unsigned)p >> 2) * kf) >> 8; }

/* A row's darkening at kd, by 565 field: the 8-bit result (r8 ..) and, with no spec to add, its packed bits with the
 * fade kf applied (r ..). */
typedef struct { int kd; uint8_t r8[32], g8[64], b8[32]; uint16_t r[32], g[64], b[32]; } dark_lut_t;
static void dark_lut(dark_lut_t *t, int kd, int kf)
{
    t->kd = kd;
    for (int i = 0; i < 64; i++) {
        const int g = (omni_ex6((unsigned)i) * (256 - kd)) >> 8, gf = fade6(g, kf);
        t->g8[i] = (uint8_t)g;
        t->g[i] = (uint16_t)((gf >> 2) << 5);
        if (i >= 32) continue;
        const int rb = (omni_ex5((unsigned)i) * (256 - kd)) >> 8, rbf = fade5(rb, kf);
        t->r8[i] = t->b8[i] = (uint8_t)rb;
        t->r[i] = (uint16_t)((rbf >> 3) << 11);
        t->b[i] = (uint16_t)(rbf >> 3);
    }
}

/* Each lid row: the source row v, scaled about the centre (u stepped by 1 / s in Q16: no division per pixel), on the
 * panel's columns. A pixel of alpha a: its colour lerped toward black by dark x a, then spec x cols[u] x a of (255, 240,
 * 205) added (clamped), then blended over the frame by a (omni_dial.h has the integer rule). Inside the row's opaque
 * run (a = 255, known from the runs) the alpha is not read and the darkening is a table per row. */
static void lid_draw(const ctx_t *c, const lband_t *lb, uint16_t *fb, const omni_rect_t *b)
{
    dark_lut_t t;
    t.kd = -1;
    const int kf = 256 - omni_amul(c->fade8);   /* the fade, applied here inside the opaque runs only (fade_draw) */
    const uint8_t (*const lsc)[3] = c->lsc;
    for (int y = b->y0; y < b->y1; y++) {
        const omni_hinge_line_t *l = &lb->ll[y - lb->y0];
        int xa, xb, oa, ob;
        if (!lid_row_spans(lb, b, y, &xa, &xb, &oa, &ob)) continue;
        const int32_t inv = (int32_t)l->inv_s_q16;
        const uint16_t *src = c->lid_rgb + (size_t)l->v * SW;
        const uint8_t *al = c->lid_a + (size_t)l->v * SW;
        const int sa = omni_amul(l->spec), dark = l->dark;
        uint16_t *d = fb + (size_t)y * SW;
        int32_t u = lid_u0((uint32_t)inv) + xa * inv;
        for (int x = xa; x < oa; x++, u += inv) {
            const int ui = u >> 16, a = al[ui];
            if (a) d[x] = lid_px_a(c, src[ui], d[x], a, dark, sa, ui);
        }
        if (oa < ob) {
            if (t.kd != omni_amul(dark)) dark_lut(&t, omni_amul(dark), kf);
            if (!sa) {
                for (int x = oa; x < ob; x++, u += inv) {
                    const unsigned s = src[u >> 16];
                    d[x] = (uint16_t)(t.r[s >> 11] | t.g[(s >> 5) & 63] | t.b[s & 31]);
                }
            } else if (kf == 256) {
                for (int x = oa; x < ob; x++, u += inv) {
                    const int ui = u >> 16;
                    const unsigned s = src[ui];
                    const uint8_t *sc = lsc[ui];
                    const int p0 = imin(t.r8[s >> 11] + ((sc[0] * sa) >> 8), 255);
                    const int p1 = imin(t.g8[(s >> 5) & 63] + ((sc[1] * sa) >> 8), 255);
                    const int p2 = imin(t.b8[s & 31] + ((sc[2] * sa) >> 8), 255);
                    d[x] = omni_pack565(p0, p1, p2);
                }
            } else {   /* the flip's first 300 ms: the fade too */
                for (int x = oa; x < ob; x++, u += inv) {
                    const int ui = u >> 16;
                    const unsigned s = src[ui];
                    const uint8_t *sc = lsc[ui];
                    const int p0 = fade5(imin(t.r8[s >> 11] + ((sc[0] * sa) >> 8), 255), kf);
                    const int p1 = fade6(imin(t.g8[(s >> 5) & 63] + ((sc[1] * sa) >> 8), 255), kf);
                    const int p2 = fade5(imin(t.b8[s & 31] + ((sc[2] * sa) >> 8), 255), kf);
                    d[x] = omni_pack565(p0, p1, p2);
                }
            }
        }
        for (int x = ob; x < xb; x++, u += inv) {
            const int ui = u >> 16, a = al[ui];
            if (a) d[x] = lid_px_a(c, src[ui], d[x], a, dark, sa, ui);
        }
    }
}

static void ctx_init(ctx_t *c, const omni_dial_frame_t *f)
{
    const omni_dial_res_t *R = f->res;
    c->valid = R && R->base;
    if (!c->valid) return;
    c->f = f;
    c->R = R;
    memset(c->hand_ok, 0, sizeof c->hand_ok);   /* a reused context keeps no hand of an earlier frame */
    marks_init(c);
    c->zdial = (zoom_t){ &R->dial_map, R->base, SCREEN, 1, 0, 0, 0, 0, 0 };
    hands_build(c, &c->zdial, 0);
    c->mag = R->mag_base != NULL;
    if (c->mag) {
        c->zmag = zoom_of(&R->mag_map, R->mag_base);
        hands_build(c, &c->zmag, 1);
    }
    c->loupe = f->st.loupe > 0;
    c->loupe_content = c->loupe && omni_dial_loupe_ready(R, f->st.loupe);   /* a stale map: the frame, no content */
    if (c->loupe) {
        omni_dial_loupe_geom(f->st.loupe, &c->lg);
        float fr[4];
        loupe_frame(&c->lg, fr);
        omni_rrect_prepare(&c->lframe, fr[0], fr[1], fr[2], fr[3], 8);
        omni_dial_loupe_bbox(f->st.loupe, &c->lbb);
        const float k = 1.0f / (LOUPE_SIGMA * 1.41421356237f), ga = c->lg.ga;
        for (int x = c->lbb.x0; x < c->lbb.x1; x++) {
            const float p = (float)x + 0.5f;
            c->lsx[x - c->lbb.x0] = 0.5f * (erff((fr[2] - p) * k) - erff((fr[0] - p) * k));
        }
        for (int y = c->lbb.y0; y < c->lbb.y1; y++) {
            const float p = (float)y + 0.5f - LOUPE_SHADOW_DY;
            c->lsy[y - c->lbb.y0] = 0.5f * (erff((fr[3] - p) * k) - erff((fr[1] - p) * k)) * 0.9f * ga * 255;
        }
    }
    if (c->loupe_content) {
        c->zloupe = zoom_of(R->loupe_map, R->loupe_base);
        hands_build(c, &c->zloupe, 2);
    }
    c->globe = R->clut != NULL;
    if (c->globe) {
        c->gf = R->globe;
        c->gf.clut = R->clut;
        c->gf.lon0_deg = f->st.lon_deg;
        c->gf.under = NULL;
        c->gf.over = NULL;
        omni_globe_cterms(&c->gt, R->clut, &c->gf.sun, c->gf.lon0_deg);
        c->gx = c->gy = (int)C - R->clut->cfg.size / 2;
    }
    c->marker = marker_at(f, &c->mx, &c->my, &c->mrad, &c->ma_ring, &c->ma_dot);
    c->flash = f->st.flash > 0;
    if (c->flash) ftab_build(&c->ft, f->st.flash);
    c->lid = f->st.lid && R->lid && R->lid_spec;
    if (c->lid) lid_init(c, f);
    c->fade8 = (int)floorf(clampf(f->st.fade, 0, 1) * 255 + 0.5f);
    c->iris = !c->lid && f->st.iris &&
              omni_iris_prepare(&c->iris_p, f->st.iris_ap, f->st.iris_phi, f->st.iris_s, R->iris_grain);
}

void omni_dial_prepare(const omni_dial_frame_t *f, omni_dial_prep_t *p)
{
    ctx_init((ctx_t *)(void *)p->b, f);
}

/* The two stages of a band: under (base, rings, V marks, hands, top), then over (globe .. loupe, the lid's dim; then
 * over the whole band the lid and the flash). Every step of either writes each pixel of b from that pixel's value
 * alone, so a pixel's result never depends on the band. Under an opaque lid the dial is not drawn at all: the band's
 * columns the lid covers opaquely in every one of its rows (lid_core) are left out of both stages, and the lid then
 * writes each of those pixels from the lid alone. */
static void under_band(const ctx_t *c, uint16_t *fb, const omni_rect_t *b)
{
    zoom_draw(c, &c->zdial, fb + (size_t)b->y0 * SW + b->x0, SW, b);   /* base, rings, V marks, hands */
    layer_draw(fb, &c->R->top, b, 255, c->R->over_run);
}

static void over_band(const ctx_t *c, uint16_t *fb, const omni_rect_t *b)
{
    const omni_dial_res_t *R = c->R;
    if (c->globe) globe_draw(c, fb, b);
    if (c->marker) marker_draw(c, fb, b);
    layer_draw(fb, &R->dome, b, 255, R->over_run);
    layer_draw(fb, &R->over, b, 255, R->over_run);
    if (c->mag) magnifier_draw(c, fb, b);
    lenses_draw(c, fb, b);
    if (c->loupe) loupe_draw(c, fb, b);
    if (c->lid) dim_draw(c, fb, b);
}

typedef void (*band_fn)(const ctx_t *c, uint16_t *fb, const omni_rect_t *b);
static void bands(const omni_dial_prep_t *p, uint16_t *fb, const omni_rect_t *r, band_fn f1, band_fn f2)
{
    const ctx_t *c = (const ctx_t *)(const void *)p->b;
    const omni_rect_t clip = isect(*r, SCREEN);
    if (empty(&clip) || !c->valid) return;
    for (int y = clip.y0; y < clip.y1; y += BAND) {
        const omni_rect_t b = { clip.x0, (int16_t)y, clip.x1, (int16_t)imin(y + BAND, clip.y1) };
        /* the dial's: the band, or its columns either side of the lid's core, or the iris's opaque columns' sides and
         * its hole */
        omni_rect_t piece[3] = { b, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
        lband_t lb;
        if (c->iris) {
            int16_t o[4];
            omni_iris_opaque(&c->iris_p, b.y0, b.y1, b.x0, b.x1, o);
            if (o[0] < o[1]) {
                piece[0].x1 = o[0];
                piece[1] = (omni_rect_t){ o[2], b.y0, o[3], b.y1 };
                piece[2] = (omni_rect_t){ o[1], b.y0, b.x1, b.y1 };
            }
        }
        if (c->lid) {
            int p0, p1, x0, x1;
            lid_band(c, &b, &lb);
            lid_core(&lb, &b, &p0, &p1, &x0, &x1);
            piece[0].x0 = (int16_t)p0;
            piece[0].x1 = (int16_t)p1;
            if (x0 < x1) {
                piece[0].x1 = (int16_t)imin(x0, p1);
                piece[1] = (omni_rect_t){ (int16_t)imax(x1, p0), b.y0, (int16_t)p1, b.y1 };
            }
        }
        for (int k = 0; k < 3; k++) {
            if (empty(&piece[k])) continue;
            if (f1) f1(c, fb, &piece[k]);
            if (f2) f2(c, fb, &piece[k]);
        }
        if (!f2) continue;
        if (c->lid) lid_draw(c, &lb, fb, &b);
        if (c->flash) flash_apply(&c->ft, fb, SW, &b);
        if (c->iris) omni_iris_draw_prepared(&c->iris_p, fb + (size_t)b.y0 * SW + b.x0, SW, &b);
        if (c->lid) {
            fade_draw(c, &lb, fb, &b);
            panel_black(&lb, fb, &b);
        } else if (c->fade8) {
            dim_rect(fb, &b, c->fade8);
        }
    }
}

void omni_dial_render_prepared(const omni_dial_prep_t *p, uint16_t *fb, const omni_rect_t *r)
{
    bands(p, fb, r, under_band, over_band);
}

void omni_dial_render_under(const omni_dial_prep_t *p, uint16_t *fb, const omni_rect_t *r)
{
    bands(p, fb, r, under_band, NULL);
}

void omni_dial_render_over(const omni_dial_prep_t *p, uint16_t *fb, const omni_rect_t *r)
{
    bands(p, fb, r, NULL, over_band);
}

void omni_dial_render(const omni_dial_frame_t *f, uint16_t *fb, const omni_rect_t *r)
{
    const omni_rect_t clip = isect(*r, SCREEN);
    if (empty(&clip) || !f->res || !f->res->base) return;
    omni_dial_prep_t p;
    omni_dial_prepare(f, &p);
    omni_dial_render_prepared(&p, fb, &clip);
}

/* ---- Dirty rects */

typedef struct { omni_rect_t r[32]; int n; } rlist_t;

static void add(rlist_t *L, omni_rect_t r)
{
    r = isect(r, SCREEN);
    if (empty(&r)) return;
    if (L->n < 32) L->r[L->n++] = r;
    else L->r[31] = unite(L->r[31], r);
}

static omni_rect_t ring_box(int i)
{
    const int r = (int)ceilf(OMNI_RING_GEOM[i].r1);
    return (omni_rect_t){ (int16_t)(233 - r), (int16_t)(233 - r), (int16_t)(233 + r), (int16_t)(233 + r) };
}

static bool mark_eq(float sa, float aa, float sb, float ab)
{
    const bool va = aa >= 0.02f, vb = ab >= 0.02f;
    return va == vb && (!va || (sa == sb && clampf(aa, 0, 1) == clampf(ab, 0, 1)));
}

static omni_rect_t marker_box(const omni_dial_frame_t *f)
{
    float x, y, rad;
    uint8_t a, b;
    if (!marker_at(f, &x, &y, &rad, &a, &b)) return (omni_rect_t){ 0, 0, 0, 0 };
    return fbox(x - rad - 1, y - rad - 1, x + rad + 1, y + rad + 1);
}

static float lens_level(float v) { v = clampf(v, 0, 1); return v > 0.01f ? v : 0; }

void omni_dial_marker_box(const omni_dial_frame_t *f, omni_rect_t *out)
{
    *out = f->res ? isect(marker_box(f), SCREEN) : (omni_rect_t){ 0, 0, 0, 0 };
}

int omni_dial_dirty(const omni_dial_frame_t *prev, const omni_dial_frame_t *cur, omni_rect_t out[OMNI_DIRTY_MAX])
{
    if (!prev || prev->res != cur->res || !cur->res) {
        out[0] = SCREEN;
        return 1;
    }
    const omni_dial_res_t *R = cur->res;
    const omni_dial_state_t *a = &prev->st, *b = &cur->st;
    if (R->lid && (a->lid || b->lid)) {   /* the lid moves (and dims the dial) every frame; and the frame it leaves */
        const bool same = a->lid == b->lid && memcmp(a, b, sizeof *a) == 0 && prev->fast == cur->fast &&
                          memcmp(&prev->pose, &cur->pose, sizeof prev->pose) == 0;
        if (same) return 0;   /* a lid held still over a still dial (`dial lid` with `dial hold`) */
        out[0] = SCREEN;
        return 1;
    }
    /* the iris (Task 10, C12): it changes only the disc's box; the fade under it, the whole screen */
    const bool iris = a->iris || b->iris;
    if (iris) {
        if (memcmp(a, b, sizeof *a) == 0 && prev->fast == cur->fast && memcmp(&prev->pose, &cur->pose, sizeof prev->pose) == 0)
            return 0;   /* an iris held still over a still dial (`dial iris` with `dial hold`) */
        if (a->fade > 0 || b->fade > 0) {
            out[0] = SCREEN;
            return 1;
        }
    }
    rlist_t L = { .n = 0 };
    omni_rect_t comp[OMNI_RINGS + 4];   /* what changed in the composite the magnifier and the loupe zoom */
    int nc = 0;
    const bool flash = a->flash > 0 || b->flash > 0;
    for (int i = 0; i < OMNI_RINGS; i++) {
        if (prev->pose.val[i] == cur->pose.val[i] && prev->pose.level[i] == cur->pose.level[i] &&
            mark_eq(a->sel_old[i], a->mark_old, b->sel_old[i], b->mark_old) &&
            mark_eq(a->sel_new[i], a->mark_new, b->sel_new[i], b->mark_new))
            continue;
        add(&L, ring_box(i));
        comp[nc++] = ring_box(i);
    }
    const bool fast = prev->fast != cur->fast;
    for (int h = 0; h < 2; h++) {
        const float pa = h ? a->min_rad : a->hour_rad, pb = h ? b->min_rad : b->hour_rad;
        if (pa == pb && !fast) continue;
        omni_rect_t r0, r1;
        omni_hand_bbox(pa, h == 0, &r0);
        omni_hand_bbox(pb, h == 0, &r1);
        add(&L, r0);
        add(&L, r1);
        comp[nc++] = r0;
        comp[nc++] = r1;
    }
    if (R->clut && a->lon_deg != b->lon_deg) {
        const int S = R->clut->cfg.size, o = 233 - S / 2;
        add(&L, (omni_rect_t){ (int16_t)o, (int16_t)o, (int16_t)(o + S), (int16_t)(o + S) });
    }
    if (a->marker != b->marker || a->marker_lat != b->marker_lat || a->marker_lon != b->marker_lon ||
        a->marker_a != b->marker_a || a->marker_pulse != b->marker_pulse || a->lon_deg != b->lon_deg) {
        add(&L, marker_box(prev));
        add(&L, marker_box(cur));
    }
    for (int k = 0; k < 2; k++) {
        if (lens_level(k ? a->green : a->red) == lens_level(k ? b->green : b->red)) continue;
        const omni_dial_a8_t *B = &R->bloom[k];
        add(&L, unite(unite(layer_box(&R->lens[2 * k]), layer_box(&R->lens[2 * k + 1])),
                      B->a ? (omni_rect_t){ B->x, B->y, (int16_t)(B->x + B->w), (int16_t)(B->y + B->h) }
                           : (omni_rect_t){ 0, 0, 0, 0 }));
    }
    const uint16_t n_theta = R->dial_map.cfg.n_theta;
    if (R->mag_base && nc) {
        const omni_rect_t src = map_source(&R->mag_map.cfg);
        bool hit = fast;
        for (int i = 0; i < nc && !hit; i++) hit = meets(comp[i], src);
        if (hit) add(&L, R->mag_map.cfg.rect);
    }
    if (a->loupe != b->loupe) {
        omni_rect_t r0, r1;
        omni_dial_loupe_bbox(a->loupe, &r0);
        omni_dial_loupe_bbox(b->loupe, &r1);
        add(&L, r0);
        add(&L, r1);
    } else if (b->loupe > 0 && nc) {
        const omni_pmap_cfg_t lc = omni_dial_loupe_cfg(b->loupe, n_theta);
        const omni_rect_t src = map_source(&lc);
        bool hit = fast;
        for (int i = 0; i < nc && !hit; i++) hit = meets(comp[i], src);
        if (hit) add(&L, lc.rect);
    }
    if (iris) {   /* the disc's box first and whole; then what else changed, but for what lies inside the box */
        const omni_rect_t box = OMNI_IRIS_BOX;
        int k = 0;
        for (int i = 0; i < L.n; i++)
            if (!(L.r[i].x0 >= box.x0 && L.r[i].y0 >= box.y0 && L.r[i].x1 <= box.x1 && L.r[i].y1 <= box.y1)) L.r[k++] = L.r[i];
        L.n = k;
    }
    if (L.n == 0 && a->flash == b->flash && !iris) return 0;   /* nothing changed (a flash held is the same frame) */
    if (flash) {
        out[0] = SCREEN;
        return 1;
    }
    /* merge: any pair whose union costs no more than the two; then the cheapest pairs until OMNI_DIRTY_MAX are left */
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; i < L.n && !again; i++)
            for (int j = i + 1; j < L.n && !again; j++) {
                const omni_rect_t u = unite(L.r[i], L.r[j]);
                if (area(&u) <= area(&L.r[i]) + area(&L.r[j])) {
                    L.r[i] = u;
                    L.r[j] = L.r[--L.n];
                    again = true;
                }
            }
    }
    const int max = iris ? OMNI_DIRTY_MAX - 1 : OMNI_DIRTY_MAX;
    while (L.n > max) {
        int bi = 0, bj = 1;
        int32_t best = INT32_MAX;
        for (int i = 0; i < L.n; i++)
            for (int j = i + 1; j < L.n; j++) {
                const omni_rect_t u = unite(L.r[i], L.r[j]);
                const int32_t cost = area(&u) - area(&L.r[i]) - area(&L.r[j]);
                if (cost < best) {
                    best = cost;
                    bi = i;
                    bj = j;
                }
            }
        L.r[bi] = unite(L.r[bi], L.r[bj]);
        L.r[bj] = L.r[--L.n];
    }
    if (iris) {
        out[0] = OMNI_IRIS_BOX;
        memcpy(out + 1, L.r, (size_t)L.n * sizeof *out);
        return L.n + 1;
    }
    memcpy(out, L.r, (size_t)L.n * sizeof *out);
    return L.n;
}
