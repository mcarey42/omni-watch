#include "omni/globe_check.h"
#include <stdio.h>
#include <string.h>

uint64_t omni_fnv1a64(uint64_t h, const void *p, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
    return h;
}

bool omni_globe_kernel_bitexact(omni_globe_span_fn fn, const omni_globe_frame_t *f, int32_t uoff_q4, uint16_t *scratch,
                                const char *tag, char *why, size_t why_n)
{
    const omni_globe_lut_t *lut = f->lut;
    const int S = lut->cfg.size, n = S + OMNI_KCHECK_GUARD;
    uint16_t *ra = scratch, *rb = scratch + n;
    for (unsigned si = 0; si < lut->nspans; si++) {
        const omni_globe_span_t *sp = &lut->span[si];
        for (int x = 0; x < n; x++) ra[x] = x >= S ? 0xa5a5 : f->under ? f->under[(size_t)sp->y * S + x] : 0;
        memcpy(rb, ra, (size_t)n * sizeof *ra);
        omni_globe_span_ref(f, sp, uoff_q4, ra + sp->x0);
        fn(f, sp, uoff_q4, rb + sp->x0);
        for (int x = 0; x < n; x++)
            if (ra[x] != rb[x]) {
                if (why) snprintf(why, why_n, "%s: row %d x %d: ref %04x, kernel %04x", tag, sp->y, x, ra[x], rb[x]);
                return false;
            }
    }
    return true;
}

/* ---- Synthetic frame: a 64 px box whose spans have ragged starts and lengths, every plane value drawn at random
 * from its valid range (u within +-8 tex_w, so u + uoff_q4 > 0; Q14 terms within +-1; about a third of the
 * alpha 0 and a third 255), shade fields within 0..4 (Q8) so no sum can overflow, and a sun whose mu reaches past
 * both ends of the shade table. */

#define SY_S 64
#define SY_TW 64
#define SY_TH (SY_TW / 2)
#define SY_MAXN (SY_S * (SY_S + 8))   /* plane entries, padding included */

static uint32_t xs32(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

static uint8_t *align16(uint8_t *p) { return p + ((16 - ((uintptr_t)p & 15)) & 15); }

size_t omni_globe_synth_bytes(void)
{
    return 15 + 16 * 12 + SY_S * sizeof(omni_globe_span_t) + (size_t)SY_MAXN * (5 * 2 + 3) +
           sizeof(omni_globe_shade_t) + (size_t)SY_TW * SY_TH * 4 + (SY_S + OMNI_KCHECK_GUARD) * 2;
}

uint64_t omni_globe_synth_digest(omni_globe_span_fn fn, void *mem, size_t bytes)
{
    if (!mem || bytes < omni_globe_synth_bytes()) return 0;
    uint8_t *p = align16(mem);
    omni_globe_lut_t lut = { .cfg = { .size = SY_S, .diam = SY_S, .proj = OMNI_PROJ_ORTHO, .tex_w = SY_TW } };
    lut.span = (omni_globe_span_t *)p;
    p = align16(p + SY_S * sizeof(omni_globe_span_t));
    uint32_t off = 0;
    for (int y = 0; y < SY_S; y++) {
        omni_globe_span_t *s = &lut.span[y];
        s->y = (int16_t)y;
        s->x0 = (int16_t)((y * 5) % 9);
        s->n = (int16_t)(SY_S - s->x0 - (y * 3) % 7);
        s->off = off;
        off += ((uint32_t)s->n + 7) & ~7u;
    }
    lut.nspans = SY_S;
    lut.n = off;
    lut.u = (int16_t *)p;   p = align16(p + SY_MAXN * 2);
    lut.row = (uint16_t *)p; p = align16(p + SY_MAXN * 2);
    lut.a = (int16_t *)p;   p = align16(p + SY_MAXN * 2);
    lut.b = (int16_t *)p;   p = align16(p + SY_MAXN * 2);
    lut.c = (int16_t *)p;   p = align16(p + SY_MAXN * 2);
    lut.limb = p;           p = align16(p + SY_MAXN);
    lut.rim = p;            p = align16(p + SY_MAXN);
    lut.alpha = p;          p = align16(p + SY_MAXN);
    omni_globe_shade_t *sh = (omni_globe_shade_t *)p;
    p = align16(p + sizeof *sh);
    uint32_t *tex = (uint32_t *)p;
    p += (size_t)SY_TW * SY_TH * 4;
    uint16_t *row = (uint16_t *)p;

    uint32_t r = 0x9e3779b9u;
    memset(lut.u, 0, (size_t)(lut.limb - (uint8_t *)lut.u));   /* padding entries stay zero (alpha 0) */
    memset(lut.limb, 0, (size_t)((uint8_t *)sh - lut.limb));
    for (int y = 0; y < SY_S; y++) {
        const omni_globe_span_t *s = &lut.span[y];
        for (uint32_t k = s->off; k < s->off + (uint32_t)s->n; k++) {
            lut.u[k] = (int16_t)((int)(xs32(&r) % (16 * SY_TW)) - 8 * SY_TW);
            lut.row[k] = (uint16_t)(xs32(&r) % SY_TH);
            lut.a[k] = (int16_t)((int)(xs32(&r) % 32769) - 16384);
            lut.b[k] = (int16_t)((int)(xs32(&r) % 32769) - 16384);
            lut.c[k] = (int16_t)((int)(xs32(&r) % 32769) - 16384);
            lut.limb[k] = (uint8_t)xs32(&r);
            lut.rim[k] = (uint8_t)xs32(&r);
            const uint32_t v = xs32(&r);
            lut.alpha[k] = (uint8_t)((v >> 8) % 3 == 0 ? 0 : (v >> 8) % 3 == 1 ? 255 : v & 255);
        }
    }
    for (int i = 0; i < OMNI_SHADE_N; i++) {
        omni_shade_e_t *e = &sh->e[i];
        e->dr = (uint16_t)(xs32(&r) % 1024);
        e->dg = (uint16_t)(xs32(&r) % 1024);
        e->db = (uint16_t)(xs32(&r) % 1024);
        e->nr = (uint16_t)(xs32(&r) % 1024);
        e->ng = (uint16_t)(xs32(&r) % 1024);
        e->nb = (uint16_t)(xs32(&r) % 1024);
        e->at = (uint16_t)(xs32(&r) % 1024);
        e->pad = 0;
    }
    for (int i = 0; i < SY_TW * SY_TH; i++) tex[i] = xs32(&r);
    const omni_globe_tex_t t = { SY_TW, SY_TH, tex };
    omni_globe_frame_t f = { .lut = &lut, .tex = &t, .shade = sh, .fade = 255 };
    f.sun.sd = (int16_t)((int)(xs32(&r) % 32769) - 16384);
    f.sun.k1 = (int16_t)((int)(xs32(&r) % 32769) - 16384);
    f.sun.k2 = (int16_t)((int)(xs32(&r) % 32769) - 16384);

    static const int32_t UOFF[] = { 16 * SY_TW, 16 * SY_TW + 37, 32 * SY_TW - 1 };
    uint64_t h = OMNI_FNV1A_INIT;
    for (unsigned ui = 0; ui < sizeof UOFF / sizeof UOFF[0]; ui++)
        for (int y = 0; y < SY_S; y++) {
            const omni_globe_span_t *s = &lut.span[y];
            for (int x = 0; x < SY_S + OMNI_KCHECK_GUARD; x++)
                row[x] = x >= SY_S ? 0xa5a5 : (uint16_t)(x * 0x0841u + y * 0x1003u + ui * 0x2109u);
            fn(&f, s, UOFF[ui], row + s->x0);
            h = omni_fnv1a64(h, row, (SY_S + OMNI_KCHECK_GUARD) * 2);
        }
    return h;
}

uint64_t omni_globe_lut_digest(const omni_globe_lut_t *lut)
{
    uint64_t h = OMNI_FNV1A_INIT;
    for (unsigned i = 0; i < lut->nspans; i++) {
        const omni_globe_span_t *s = &lut->span[i];
        const int32_t v[4] = { s->y, s->x0, s->n, (int32_t)s->off };
        h = omni_fnv1a64(h, v, sizeof v);
    }
    h = omni_fnv1a64(h, lut->u, lut->n * 2);
    h = omni_fnv1a64(h, lut->row, lut->n * 2);
    h = omni_fnv1a64(h, lut->a, lut->n * 2);
    h = omni_fnv1a64(h, lut->b, lut->n * 2);
    h = omni_fnv1a64(h, lut->c, lut->n * 2);
    h = omni_fnv1a64(h, lut->limb, lut->n);
    h = omni_fnv1a64(h, lut->rim, lut->n);
    return omni_fnv1a64(h, lut->alpha, lut->n);
}

uint64_t omni_globe_shade_digest(const omni_globe_shade_t *sh) { return omni_fnv1a64(OMNI_FNV1A_INIT, sh, sizeof *sh); }

/* ---- Compact kernels */

bool omni_globe_ckernel_bitexact(omni_globe_cspan_fn fn, const omni_globe_frame_t *f, uint16_t *scratch, const char *tag,
                                 char *why, size_t why_n)
{
    const omni_globe_clut_t *c = f->clut;
    const int S = c->cfg.size, n = S + OMNI_KCHECK_GUARD;
    uint16_t *ra = scratch, *rb = scratch + n;
    omni_globe_cterms_t t;
    omni_globe_cterms(&t, c, &f->sun, f->lon0_deg);
    for (unsigned si = 0; si < c->nspans; si++) {
        const omni_globe_cspan_t *sp = &c->span[si];
        for (int x = 0; x < n; x++) ra[x] = x >= S ? 0xa5a5 : f->under ? f->under[(size_t)sp->y * S + x] : 0;
        memcpy(rb, ra, (size_t)n * sizeof *ra);
        omni_globe_cspan_ref(f, sp, &t, ra + sp->x0);
        fn(f, sp, &t, rb + sp->x0);
        for (int x = 0; x < n; x++)
            if (ra[x] != rb[x]) {
                if (why) snprintf(why, why_n, "%s: row %d x %d: ref %04x, kernel %04x", tag, sp->y, x, ra[x], rb[x]);
                return false;
            }
    }
    return true;
}

#define CS_BLKS (SY_S * (SY_S + 8) / 8)

size_t omni_globe_csynth_bytes(void)
{
    return 63 + (size_t)CS_BLKS * sizeof(omni_globe_blk_t) + SY_S * sizeof(omni_globe_cspan_t) +
           sizeof(omni_globe_shade_t) + (size_t)SY_TW * SY_TH * 4 + (SY_S + OMNI_KCHECK_GUARD) * 2 + 16;
}

uint64_t omni_globe_csynth_digest(omni_globe_cspan_fn fn, void *mem, size_t bytes)
{
    if (!mem || bytes < omni_globe_csynth_bytes()) return 0;
    uint8_t *p = (uint8_t *)mem + ((64 - ((uintptr_t)mem & 63)) & 63);
    omni_globe_clut_t c = { .cfg = { .size = SY_S, .diam = SY_S, .proj = OMNI_PROJ_ORTHO, .tex_w = SY_TW } };
    c.blk = (omni_globe_blk_t *)p;
    p += (size_t)CS_BLKS * sizeof(omni_globe_blk_t);
    c.span = (omni_globe_cspan_t *)p;
    p += SY_S * sizeof(omni_globe_cspan_t);
    omni_globe_shade_t *sh = (omni_globe_shade_t *)align16(p);
    p = align16(p) + sizeof *sh;
    uint32_t *tex = (uint32_t *)p;
    p += (size_t)SY_TW * SY_TH * 4;
    uint16_t *row = (uint16_t *)p;
    c.k = 1048576 / SY_S;
    uint32_t blk = 0;
    for (int y = 0; y < SY_S; y++) {
        omni_globe_cspan_t *s = &c.span[y];
        s->y = (int16_t)y;
        s->x0 = (int16_t)((y * 5) % 9);
        s->n = (int16_t)(SY_S - s->x0 - (y * 3) % 7);
        s->ia = s->ib = s->pad = 0;
        if (!(y & 1)) {   /* an interior run (alpha 255) on even rows: vector kernels' fast blocks */
            s->ia = (int16_t)(s->n / 4 - (y % 5));
            s->ib = (int16_t)(s->n - s->n / 5 + (y % 3));
        }
        s->blk = blk;
        blk += ((uint32_t)s->n + 7) / 8;
    }
    c.nspans = SY_S;
    c.nblk = blk;
    memset(c.blk, 0, (size_t)CS_BLKS * sizeof(omni_globe_blk_t));
    uint32_t r = 0x7f4a7c15u;
    for (int y = 0; y < SY_S; y++) {
        const omni_globe_cspan_t *s = &c.span[y];
        for (int j = 0; j < s->n; j++) {
            omni_globe_blk_t *b = &c.blk[s->blk + (uint32_t)j / 8];
            const int l = j & 7;
            b->u[l] = (int16_t)((int)(xs32(&r) % (16 * SY_TW)) - 8 * SY_TW);
            b->z[l] = (int16_t)(xs32(&r) % 16385);
            b->row[l] = (uint8_t)(xs32(&r) % SY_TH);
            b->limb[l] = (uint8_t)xs32(&r);
            b->rim[l] = (uint8_t)xs32(&r);
            const uint32_t v = xs32(&r);
            b->alpha[l] = (uint8_t)((v >> 8) % 3 == 0 ? 0 : (v >> 8) % 3 == 1 ? 255 : v & 255);
            if (j >= s->ia && j < s->ib) b->alpha[l] = 255;
        }
    }
    for (int i = 0; i < OMNI_SHADE_N; i++) {
        omni_shade_e_t *e = &sh->e[i];
        e->dr = (uint16_t)(xs32(&r) % 1024);
        e->dg = (uint16_t)(xs32(&r) % 1024);
        e->db = (uint16_t)(xs32(&r) % 1024);
        e->nr = (uint16_t)(xs32(&r) % 1024);
        e->ng = (uint16_t)(xs32(&r) % 1024);
        e->nb = (uint16_t)(xs32(&r) % 1024);
        e->at = (uint16_t)(xs32(&r) % 1024);
        e->pad = 0;
    }
    for (int i = 0; i < SY_TW * SY_TH; i++) tex[i] = xs32(&r);
    const omni_globe_tex_t tx = { SY_TW, SY_TH, tex };
    omni_globe_frame_t f = { .tex = &tx, .shade = sh, .clut = &c, .fade = 255 };
    uint64_t h = OMNI_FNV1A_INIT;
    for (int ti = 0; ti < 4; ti++) {
        /* terms past the physical range too (|mu| beyond 1: the shade index clamps at both ends) */
        const omni_globe_cterms_t t = { .uoff_q4 = 16 * SY_TW + (int32_t)(xs32(&r) % (16 * SY_TW)),
                                        .ex = (int16_t)((int)(xs32(&r) % 32769) - 16384),
                                        .ey = (int16_t)((int)(xs32(&r) % 32769) - 16384),
                                        .lz = (int16_t)((int)(xs32(&r) % 32769) - 16384) };
        for (int y = 0; y < SY_S; y++) {
            const omni_globe_cspan_t *s = &c.span[y];
            for (int x = 0; x < SY_S + OMNI_KCHECK_GUARD; x++)
                row[x] = x >= SY_S ? 0xa5a5 : (uint16_t)(x * 0x0841u + y * 0x1003u + (unsigned)ti * 0x2109u);
            fn(&f, s, &t, row + s->x0);
            h = omni_fnv1a64(h, row, (SY_S + OMNI_KCHECK_GUARD) * 2);
        }
    }
    return h;
}

uint64_t omni_globe_clut_digest(const omni_globe_clut_t *c)
{
    uint64_t h = OMNI_FNV1A_INIT;
    for (unsigned i = 0; i < c->nspans; i++) {
        const omni_globe_cspan_t *s = &c->span[i];
        const int32_t v[6] = { s->y, s->x0, s->n, s->ia, s->ib, (int32_t)s->blk };
        h = omni_fnv1a64(h, v, sizeof v);
    }
    return omni_fnv1a64(h, c->blk, (size_t)c->nblk * sizeof *c->blk);
}
