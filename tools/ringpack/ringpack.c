/* ringpack: the Omni ring engravings (tools/export/out/engr_<k>.rgba, 932 x 932 unpremultiplied RGBA, the mockup's
 * buildEngrave(k) at 2x) -> the radius-indexed polar strips omni_strip<L>.bin (L 0..2) for the assets partition,
 * through omni_core's own omni_strip_from_rgba, plus <in>/ringpack.json: their manifest entries (tools/assets.sh merges
 * them into firmware/apps/omni/assets/manifest.json), and the prebuilt dial map omni_dialmap.bin (R80: variant B at the same N, by
 * omni_pmap_build, in omni_pmap_file_write's format).
 *
 *   ringpack <in_dir> <assets_dir> [--n N]          N = level-0 entries per row (default 2048, a power of two)
 *   ringpack --tmr-lut <assets_dir> [<json>]        R139: only the timer's ring LUT tmr_lut.bin (and its manifest
 *                                                   entry, as a JSON object, to <json> if given); the full run above
 *                                                   writes it too, after the dial map
 *   ringpack <in_dir> --rle <out_dir>               R69/C6: the month ring's RLE frames at a step of 1/4 slot
 *                                                   (omni_rle_0_<i>.bin, never into firmware/apps/omni/assets), their bytes,
 *                                                   and the bytes all four rings would take, extrapolated
 *   ringpack <in_dir> --iris <out_dir>              Task 10 (R69/C6): the iris's RLE subset, the 8 frames export.js
 *                                                   draws with OMNI_EXPORT_IRIS=1 (iris_<i>.rgba, 466 x 466, drawIris
 *                                                   over transparent) cropped
 *                                                   to the disc's box, as omni_rle_iris_<i>.bin (never into
 *                                                   firmware/apps/omni/assets) and one file of all 8 (omni_rle_iris.bin: u32
 *                                                   count, u32 offsets, the frames), their bytes, and the bytes the
 *                                                   35-frame set would take, extrapolated
 * Built by tools/ringpack/CMakeLists.txt (a host build of the omni_core sources). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "omni/rle.h"
#include "omni/ring.h"
#include "omni/tmr_ring.h"

#define L2X 932
#define PI 3.14159265358979323846

static uint8_t *g_layer[OMNI_RINGS];

static int load(const char *dir)
{
    for (int k = 0; k < OMNI_RINGS; k++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/engr_%d.rgba", dir, k);
        FILE *f = fopen(path, "rb");
        g_layer[k] = malloc((size_t)L2X * L2X * 4);
        if (!f || !g_layer[k] || fread(g_layer[k], 1, (size_t)L2X * L2X * 4, f) != (size_t)L2X * L2X * 4) {
            fprintf(stderr, "ringpack: cannot read %s (932 x 932 RGBA)\n", path);
            if (f) fclose(f);
            return 1;
        }
        fclose(f);
    }
    return 0;
}

static int write_file(const char *path, const void *a, size_t na, const void *b, size_t nb, const void *c, size_t nc)
{
    FILE *f = fopen(path, "wb");
    const int ok = f && fwrite(a, 1, na, f) == na && (!nb || fwrite(b, 1, nb, f) == nb) && (!nc || fwrite(c, 1, nc, f) == nc);
    if (f) fclose(f);
    if (!ok) fprintf(stderr, "ringpack: cannot write %s\n", path);
    return ok ? 0 : 1;
}

/* R139: the timer's ring LUT (omni_tmr_lut_build, the device's own code path, in omni_tmr_lut_file_write's format),
 * so the face loads it instead of building it at create (2.97-4.09 s on the device). Its manifest entry goes to man
 * (if not NULL), led by sep. Deterministic: the LUT is the host libm's double sqrt/atan2 rounded to 1/4 px and
 * 2^-16 turn; test_tmr_lut_file pins the committed bytes to this build, and the ring digest to them. */
static int tmr_lut(const char *assets, FILE *man, const char *sep)
{
    const size_t lb = omni_tmr_lut_bytes();
    void *mem = malloc(lb);
    omni_tmr_lut_t l;
    if (!mem || !omni_tmr_lut_build(&l, mem, lb)) {
        fprintf(stderr, "ringpack: timer LUT build failed\n");
        return 1;
    }
    const size_t fb = omni_tmr_lut_file_bytes(&l);
    uint8_t *file = malloc(fb);
    if (!file || omni_tmr_lut_file_write(&l, file, fb) != fb) return 1;
    uint64_t h = 1469598103934665603ull;   /* FNV-1a 64 of the payload */
    for (size_t i = OMNI_TMR_LUT_HDR; i < fb; i++) h = (h ^ file[i]) * 1099511628211ull;
    char path[1024];
    snprintf(path, sizeof path, "%s/tmr_lut.bin", assets);
    if (write_file(path, file, fb, NULL, 0, NULL, 0)) return 1;
    printf("  tmr_lut.bin      TLUT      %u px, %zu bytes, digest %016llx\n", (unsigned)l.n, fb, (unsigned long long)h);
    if (man)
        fprintf(man,
                "%s {\"file\": \"tmr_lut.bin\", \"name\": \"tmr_lut\", \"cf\": \"TLUT\", \"w\": %d, \"h\": %d, \"n\": %u, "
                "\"digest\": \"%016llx\", \"face\": \"timer\", \"format\": \"OTL1 header (16 B), idx u32[n], ang u16[n], rq "
                "u8[n] LE: the timer ring's pixels 160 <= r <= 220 in box order (tmr_ring.h omni_tmr_lut_load)\"}",
                sep, OMNI_TMR_BOX, OMNI_TMR_BOX, (unsigned)l.n, (unsigned long long)h);
    free(file);
    free(mem);
    return 0;
}

static int strips(const char *in, const char *assets, uint16_t n)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/ringpack.json", in);
    FILE *man = fopen(path, "w");
    if (!man) return 1;
    fprintf(man, "[");
    size_t total = 0;
    for (uint8_t lev = 0; lev < 3; lev++) {
        omni_strip_t st = { .n_theta = n, .r_min = 126, .n_r = 71 };
        const size_t pb = omni_strip_bytes((uint16_t)(n >> lev), st.n_r);
        void *mem = malloc(pb);
        const uint8_t *const layers[OMNI_RINGS] = { g_layer[0], g_layer[1], g_layer[2], g_layer[3] };
        if (!mem || !omni_strip_from_rgba(&st, layers, lev, mem, pb)) {
            fprintf(stderr, "ringpack: strip level %u failed\n", lev);
            return 1;
        }
        uint8_t hdr[OMNI_STRIP_HDR];
        omni_strip_file_header(&st, hdr);
        const size_t ne = (size_t)st.stride * st.n_r;
        snprintf(path, sizeof path, "%s/omni_strip%u.bin", assets, lev);
        if (write_file(path, hdr, sizeof hdr, st.rgb, ne * 2, st.a, ne)) return 1;
        const size_t bytes = sizeof hdr + pb;
        total += bytes;
        printf("  omni_strip%u.bin  STRIP     %u (stride %u) x %u (level %u, blur %d), %zu bytes\n", lev, st.n_theta, st.stride, st.n_r, lev,
               lev == 0 ? 0 : lev == 1 ? OMNI_BLUR_L1 : OMNI_BLUR_L2, bytes);
        fprintf(man,
                "%s\n {\"file\": \"omni_strip%u.bin\", \"name\": \"omni_strip%u\", \"cf\": \"STRIP\", \"w\": %u, \"h\": %u, "
                "\"level\": %u, \"r_min\": %u, \"stride\": %u, \"face\": \"omni-dial\", \"format\": \"ORS1 header (16 B), "
                "rgb565[h][stride] LE, a8[h][stride]: rows by radius from r_min, w entries clockwise from 12 (the rest "
                "of each row pads the stride)\"}",
                lev ? "," : "", lev, lev, st.n_theta, st.n_r, lev, st.r_min, st.stride);
        free(mem);
    }
    /* R80: the dial map (variant B, the delta-coded stream), built by the device's own omni_pmap_build, so the view
     * loads it instead of building it at create (181 ms) */
    const omni_pmap_cfg_t cfg = omni_dial_pmap_cfg(n);
    const size_t mb = omni_pmap_bytes(&cfg, false, true);
    void *mem = malloc(mb);
    omni_pmap_t m;
    if (!mem || !omni_pmap_build(&m, &cfg, false, true, mem, mb)) {
        fprintf(stderr, "ringpack: dial map build failed\n");
        return 1;
    }
    const size_t fb = omni_pmap_file_bytes(&m);
    uint8_t *file = malloc(fb);
    if (!file || omni_pmap_file_write(&m, file, fb) != fb) return 1;
    snprintf(path, sizeof path, "%s/omni_dialmap.bin", assets);
    if (write_file(path, file, fb, NULL, 0, NULL, 0)) return 1;
    printf("  omni_dialmap.bin PMAP      N %u, %u spans, %u px, %zu bytes, digest %016llx\n", n, (unsigned)m.nspans,
           (unsigned)m.npx, fb, (unsigned long long)omni_pmap_digest(&m));
    fprintf(man,
            ",\n {\"file\": \"omni_dialmap.bin\", \"name\": \"omni_dialmap\", \"cf\": \"PMAP\", \"w\": %d, \"h\": %d, "
            "\"origin\": [%d, %d], \"n_theta\": %u, \"nspans\": %u, \"digest\": \"%016llx\", \"face\": \"omni-dial\", "
            "\"format\": \"OPM1 header (72 B), spans (20 B each), 4-bit codes: the dial's polar map, variant B (ring.h "
            "omni_pmap_load)\"}",
            cfg.rect.x1 - cfg.rect.x0, cfg.rect.y1 - cfg.rect.y0, cfg.rect.x0, cfg.rect.y0, n, (unsigned)m.nspans,
            (unsigned long long)omni_pmap_digest(&m));
    free(file);
    free(mem);
    if (tmr_lut(assets, man, ",\n")) return 1;
    fprintf(man, "\n]\n");
    fclose(man);
    printf("  strips: %zu bytes at N %u\n", total, n);
    return 0;
}

/* Ring k at val, as the mockup draws it (drawRot: the 2x layer rotated by -val/n 2pi, sampled bilinearly), over its
 * annulus' bounding box: unpremultiplied RGBA. */
static void ring_frame(int k, double val, uint8_t *rgba, int *ox, int *oy, int *w)
{
    const omni_ring_geom_t *g = &OMNI_RING_GEOM[k];
    const int x0 = (int)floor(233 - g->r1), x1 = (int)ceil(233 + g->r1);
    *ox = *oy = x0;
    *w = x1 - x0;
    const double rot = val / g->slots * 2 * PI, c = cos(rot), s = sin(rot);
    for (int y = x0; y < x1; y++)
        for (int x = x0; x < x1; x++) {
            uint8_t *o = rgba + ((size_t)(y - x0) * *w + (x - x0)) * 4;
            memset(o, 0, 4);
            const double X = x + 0.5 - 233, Y = 233 - (y + 0.5), r = hypot(X, Y);
            if (r < g->r0 || r >= g->r1) continue;
            const double sx = X * c + Y * s, sy = -X * s + Y * c;
            const double u = (233 + sx) * 2 - 0.5, v = (233 - sy) * 2 - 0.5;
            const int u0 = (int)floor(u), v0 = (int)floor(v);
            const double fu = u - u0, fv = v - v0;
            double acc[4] = { 0, 0, 0, 0 };
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    const int uu = u0 + i, vv = v0 + j;
                    if (uu < 0 || vv < 0 || uu >= L2X || vv >= L2X) continue;
                    const double wt = (i ? fu : 1 - fu) * (j ? fv : 1 - fv);
                    const uint8_t *p = g_layer[k] + ((size_t)vv * L2X + uu) * 4;
                    const double a = p[3] / 255.0 * wt;
                    acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a;
                }
            const long a8 = lround(acc[3] * 255);
            if (a8 <= 0) continue;
            for (int ch = 0; ch < 3; ch++) {
                const long v8 = lround(acc[ch] / acc[3]);
                o[ch] = (uint8_t)(v8 > 255 ? 255 : v8);
            }
            o[3] = (uint8_t)(a8 > 255 ? 255 : a8);
        }
}

static int rle(const char *out)
{
    const int W = 2 * 197 + 2;
    uint8_t *rgba = malloc((size_t)W * W * 4), *buf = malloc((size_t)W * W * 7 + 8);
    if (!rgba || !buf) return 1;
    size_t month = 0;
    const int nf = OMNI_RING_GEOM[0].slots * 4;
    for (int i = 0; i < nf; i++) {
        int ox, oy, w;
        ring_frame(0, i / 4.0, rgba, &ox, &oy, &w);
        const size_t n = omni_rle_encode(rgba, w, w, ox, oy, buf, (size_t)W * W * 7 + 8);
        char path[1024];
        snprintf(path, sizeof path, "%s/omni_rle_0_%d.bin", out, i);
        if (!n || write_file(path, buf, n, NULL, 0, NULL, 0)) return 1;
        month += n;
    }
    printf("RLE month ring: %d frames at 1/4 slot, %zu bytes (%.1f KB a frame) -> %s\n", nf, month, month / 1024.0 / nf, out);
    /* the other rings: 8 frames each, evenly spread, scaled to 4 frames a slot */
    double all = (double)month;
    for (int k = 1; k < OMNI_RINGS; k++) {
        size_t s = 0;
        const int frames = OMNI_RING_GEOM[k].slots * 4;
        for (int j = 0; j < 8; j++) {
            int ox, oy, w;
            ring_frame(k, (double)frames / 8 * j / 4.0 + 0.25 * j, rgba, &ox, &oy, &w);
            s += omni_rle_encode(rgba, w, w, ox, oy, buf, (size_t)W * W * 7 + 8);
        }
        const double est = (double)s / 8 * frames;
        printf("RLE ring %d: %d frames, about %.0f bytes (%.1f KB a frame, from 8)\n", k, frames, est, s / 8.0 / 1024);
        all += est;
    }
    printf("RLE all four rings at 1/4 slot: about %.0f bytes (%.2f MB)\n", all, all / 1e6);
    free(rgba);
    free(buf);
    return 0;
}

/* The iris's RLE subset (Task 10): 8 frames at the middles of 8 equal steps of the opening; the 35-frame set (one frame
 * a 27 ms step of the 940 ms opening) extrapolated from them, each of its frames sized as the subset frame nearest in
 * time (the size falls steadily as the aperture opens). */
static int iris(const char *in, const char *out)
{
    const int W = 466, B0 = 34, BW = 398;
    uint8_t *rgba = malloc((size_t)W * W * 4), *crop = malloc((size_t)BW * BW * 4), *buf = malloc((size_t)BW * BW * 7 + 8);
    uint8_t *all = malloc((size_t)8 * BW * BW * 7 + 64);
    int rc = 1;
    size_t sz[8], off = 4 + 8 * 4, total = 0;
    char path[1024];
    if (!rgba || !crop || !buf || !all) goto done;
    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof path, "%s/iris_%d.rgba", in, i);
        FILE *f = fopen(path, "rb");
        if (!f || fread(rgba, 1, (size_t)W * W * 4, f) != (size_t)W * W * 4) {
            fprintf(stderr, "ringpack: cannot read %s (466 x 466 RGBA: export with OMNI_EXPORT_IRIS=1)\n", path);
            if (f) fclose(f);
            goto done;
        }
        fclose(f);
        for (int y = 0; y < BW; y++) memcpy(crop + (size_t)y * BW * 4, rgba + ((size_t)(y + B0) * W + B0) * 4, (size_t)BW * 4);
        sz[i] = omni_rle_encode(crop, BW, BW, B0, B0, buf, (size_t)BW * BW * 7 + 8);
        if (!sz[i]) goto done;
        snprintf(path, sizeof path, "%s/omni_rle_iris_%d.bin", out, i);
        if (write_file(path, buf, sz[i], NULL, 0, NULL, 0)) goto done;
        const uint32_t o = (uint32_t)off;
        memcpy(all + 4 + 4 * i, &o, 4);
        memcpy(all + off, buf, sz[i]);
        off += sz[i];
        total += sz[i];
        printf("RLE iris frame %d (t %.1f ms): %zu bytes\n", i, 480 + 940 * (i + 0.5) / 8, sz[i]);
    }
    const uint32_t n8 = 8;
    memcpy(all, &n8, 4);
    snprintf(path, sizeof path, "%s/omni_rle_iris.bin", out);
    if (write_file(path, all, off, NULL, 0, NULL, 0)) goto done;
    double est = 0;
    for (int j = 0; j < 35; j++) est += (double)sz[(j * 8) / 35];
    printf("RLE iris subset: 8 frames, %zu bytes (%.1f KB a frame); %s holds them all (%zu bytes)\n", total,
           total / 8.0 / 1024, path, off);
    printf("RLE iris, the 35-frame set: about %.0f bytes (%.2f MB)\n", est, est / 1e6);
    rc = 0;
done:
    free(rgba);
    free(crop);
    free(buf);
    free(all);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: ringpack <in_dir> <assets_dir> [--n N] | ringpack <in_dir> --rle|--iris <out_dir> | "
                        "ringpack --tmr-lut <assets_dir> [<json>]\n");
        return 2;
    }
    if (strcmp(argv[1], "--tmr-lut") == 0) {
        if (argc != 3 && argc != 4) return 2;
        FILE *j = argc == 4 ? fopen(argv[3], "w") : NULL;
        if (argc == 4 && !j) return 1;
        const int rc = tmr_lut(argv[2], j, "");
        if (j) {
            fprintf(j, "\n");
            fclose(j);
        }
        return rc;
    }
    if (strcmp(argv[2], "--iris") == 0) return argc == 4 ? iris(argv[1], argv[3]) : 2;
    if (load(argv[1])) return 1;
    if (strcmp(argv[2], "--rle") == 0) return argc == 4 ? rle(argv[3]) : 2;
    uint16_t n = 2048;
    if (argc == 5 && strcmp(argv[3], "--n") == 0) n = (uint16_t)atoi(argv[4]);
    if (n < 64 || (n & (n - 1))) {
        fprintf(stderr, "ringpack: N must be a power of two >= 64\n");
        return 2;
    }
    return strips(argv[1], argv[2], n);
}
