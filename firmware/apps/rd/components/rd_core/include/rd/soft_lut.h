#pragma once
/* The soft glows' tables (rd/faces.h RD_SOFT_LUT_RADII; rd wave 1 Task 10): the reveal mockup's softSprite() profile,
 * a radial gradient of alpha 1 at the centre, .45 at .35 of r and 0 at r (faces-reveal.js:202-207), in the layout of
 * fk_glow_lut_build (fk/shapes.h: entry k for N = 8k + 2, N = (2 dx)^2 + (2 dy)^2 of a pixel about a whole-pixel
 * centre, so fk_glow_add_lut draws it; r <= 255, the coarse layout from FK_GLOW_COARSE_R). HOST ONLY: rd_shot
 * --glow-lut writes the const tables into rd_core's glow_lut.h from this, and the tests check them; no rd_core file
 * includes it (its sqrt is libm, lesson 6, and no face builds a table, lesson 3). */
#include <math.h>
#include <stdint.h>
#include "fk/shapes.h"

/* (rd wave 2 Task 3) A radius of FK_GLOW_COARSE_R or more takes fk_glow_lut_build's coarse layout: entry k at N = 64 k
 * (Bay Mouth's chime glow, r 255), which fk_glow_add_lut interpolates. */
static inline void rd_soft_lut_build(uint8_t *lut, int r)
{
    const size_t n = FK_GLOW_LUT_N(r);
    const int coarse = r >= FK_GLOW_COARSE_R;
    for (size_t k = 0; k < n; k++) {
        const double d = sqrt((double)(coarse ? 64 * k : 8 * k + 2)) / 2.0, u = d / (double)r;
        const double a = u < 0.35 ? 1.0 - 0.55 * u / 0.35 : u < 1.0 ? 0.45 * (1.0 - u) / 0.65 : 0.0;
        lut[k] = (uint8_t)(a * 255.0 + 0.5);
    }
}

/* Task 12: the linear radial glows (rd/faces.h RD_LIN_LUT_RADII): alpha 1 - d / r (the bridge mockup's two-stop
 * createRadialGradient, Deck Sign's beacon), in the same layout. */
static inline void rd_lin_lut_build(uint8_t *lut, int r)
{
    const size_t n = FK_GLOW_LUT_N(r);
    for (size_t k = 0; k < n; k++) {
        const double d = sqrt((double)(8 * k + 2)) / 2.0, a = d < (double)r ? 1.0 - d / (double)r : 0.0;
        lut[k] = (uint8_t)(a * 255.0 + 0.5);
    }
}

/* Task 14: the quarters glows (rd/faces.h RD_QTR_LUT_RADII): faces-quarters.js glowSprite()'s three stops, alpha 1 at
 * the centre, .4 at .35 of r and 0 at r, in the same layout (Bunk Clock's lamp). */
static inline void rd_qtr_lut_build(uint8_t *lut, int r)
{
    const size_t n = FK_GLOW_LUT_N(r);
    for (size_t k = 0; k < n; k++) {
        const double d = sqrt((double)(8 * k + 2)) / 2.0, u = d / (double)r;
        const double a = u < 0.35 ? 1.0 - 0.6 * u / 0.35 : u < 1.0 ? 0.4 * (1.0 - u) / 0.65 : 0.0;
        lut[k] = (uint8_t)(a * 255.0 + 0.5);
    }
}

/* Task 12: Deck Sign's spray mask (rd/faces.h RD_DK_SPRAY_TILE; faces-bridge.js:603-612 spray()). The mockup sprays
 * a glyph through the union of n = floor(2400 k) discs of radius 2 + 7 k rho, each disc's centre and rho drawn from
 * mulberry32(seed) in turn over the glyph's sprite, k = easeOut3 of the spray's time. A pixel centre at distance d from
 * disc i is inside it once k >= max((i + 1) / 2400, (d - 2) / (7 rho)): its threshold T is the least of those over
 * the discs, so the whole dissolve is one table. Here the discs are drawn over a 128 x 128 tile at the mockup's density
 * (2400 over a two-digit sprite of 184 x 165 px: 1 295 discs, disc i coming at k = (i + 1) / 1 295, the same share of
 * the whole count), from seed 8, with the distance taken round the tile
 * (toroidal: a face offsets it per number), and T is kept as a level: ceil(253 T) in 1..253 while T <= 1, 254 past
 * it (the mockup's last specks, which only its k = 1 frame fills); the 64 latest pixels are moved to 254 too, so every
 * spray's last frames (rd_dk_spray_level 252 and 253) still leave specks to fill at the end. */
static inline uint32_t rd_mulberry32(uint32_t *a)
{
    uint32_t t = (*a += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

static inline void rd_spray_lut_build(uint8_t *lut)
{
    enum { TW = 128, NDISC = 1295, LAST = 64 };
    static double cx[NDISC], cy[NDISC], rho[NDISC], th[TW * TW];
    uint32_t s = 8u;
    for (int i = 0; i < NDISC; i++) {
        cx[i] = rd_mulberry32(&s) / 4294967296.0 * TW;
        cy[i] = rd_mulberry32(&s) / 4294967296.0 * TW;
        rho[i] = rd_mulberry32(&s) / 4294967296.0;
    }
    for (int y = 0; y < TW; y++)
        for (int x = 0; x < TW; x++) {
            double best = 1e9;
            for (int i = 0; i < NDISC; i++) {
                double dx = fabs(x + 0.5 - cx[i]), dy = fabs(y + 0.5 - cy[i]);
                dx = dx > TW / 2 ? TW - dx : dx, dy = dy > TW / 2 ? TW - dy : dy;
                const double d = sqrt(dx * dx + dy * dy), on = (i + 1) / (double)NDISC;
                const double grow = d <= 2.0 ? 0.0 : rho[i] > 0 ? (d - 2.0) / (7.0 * rho[i]) : 1e9;
                const double t = on > grow ? on : grow;
                best = t < best ? t : best;
            }
            th[y * TW + x] = best;
        }
    for (int k = 0; k < TW * TW; k++) {
        const double q = ceil(253.0 * th[k]);
        lut[k] = (uint8_t)(th[k] > 1.0 ? 254 : q < 1 ? 1 : q > 253 ? 253 : q);
    }
    for (int m = 0; m < LAST; m++) {   /* the latest pixels not yet at 254, one at a time (ties: the first) */
        int w = -1;
        for (int k = 0; k < TW * TW; k++)
            if (lut[k] < 254 && (w < 0 || th[k] > th[w])) w = k;
        if (w >= 0) lut[w] = 254;
    }
}
