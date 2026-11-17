/* rd_shot: a Mining Ship face frame on the host (rd wave 1 Task 9), from the same rd_core code and asset files as the
 * device (rd_assets_host over the committed set; rd has no overlay). A host tool, not a test.
 *
 *   rd_shot --face K [--at "YYYY-MM-DD HH:MM:SS[.mmm]"] [--glance] [--unset | --stale] [--reduced] [--odx N --ody N]
 *           [--h24] [--slept S] [--thrum T] [--beep B] [--variant N] [--yday N] --out f.ppm
 *       one frame as a binary PPM (no --at: unset). --slept S: the time asleep in s (-1, the default: none).
 *       --reveal T (rd wave 1 Task 10): the face's reveal (Stasis Release, RD_REVEAL_STASIS; wave 2 Task 4: Bay
 *       Mouth's airlock, RD_REVEAL_AIRLOCK) T ms after its start, as fk_view draws it (frame, the interior at the
 *       plan's wake_ms, 8-row bands), over the same input; with --reduced
 *       the reduced cross-fade; --skip a tap's end frame. A frame in motion: never a CRC.
 *       --wake T, --pulse T (Task 11): a face's own wake, or a tap's pulse, T ms in; --mono MS: in->mono_ms (the
 *       free-running count a face's rest motion reads; 0 by default). --fresh MS (Task 12): a revealing wake MS ms
 *       ago (in->fresh, fresh_ms); --click C: the chime's click channel. --battery P (wave 2 Task 6): in->battery
 *       (-1, the default: unknown); --sync S: in->sync_age_s (0 by default). --step MS (wave 2 Task 7): Nav Scope's
 *       sweep mode for the frame (rd_sc_set_step_ms: 0 or 250; a frame in motion, never a CRC).
 *   rd_shot --face K --frame I --out f.ppm
 *       still I of the face (rd_still_frame), exactly the frame rd/verify_crc.h pins.
 *   rd_shot --crc
 *       every registered face's still frames (rd_still, rd/faces.h) as the C header rd/verify_crc.h (stdout). Still
 *       frames only: never a frame in motion.
 *   rd_shot --glow-lut
 *       the faces' glow tables (fk_glow_lut_build for each of RD_GLOW_LUT_RADII, then rd_soft_lut_build for each of
 *       RD_SOFT_LUT_RADII, Task 10, then rd_lin_lut_build for each of RD_LIN_LUT_RADII, Task 12, then
 *       rd_qtr_lut_build for each of RD_QTR_LUT_RADII, Task 14) as rd_core's glow_lut.h (stdout; lesson 3). The host
 *       tests check the committed header against the builders.
 *   rd_shot --spray-lut
 *       Deck Sign's spray mask (rd_spray_lut_build, rd/soft_lut.h; Task 12) as rd_core's spray_lut.h (stdout).
 *   rd_shot --void PNG
 *       the share of the disc that is void, by the harness's rule (test_rd_faces.c void_pct: pixel centres within
 *       233 px of the centre of a 466 x 466 image, every channel <= 40 after the panel's RGB565 quantisation), of a
 *       mockup's shot (8-bit RGB, RGBA, grey or palette PNG, not interlaced): PR10's per-face floor is this minus 2.
 *   --assets DIR (default the committed set). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "rd_assets_host.h"
#include "fk/face.h"
#include "fk/shapes.h"
#include "rd/faces.h"
#include "rd/release.h"
#include "rd/soft_lut.h"

#define W 466
static uint16_t s_fb[W * W];

static int usage(void)
{
    fprintf(stderr, "usage: rd_shot --face K [--at \"YYYY-MM-DD HH:MM:SS[.mmm]\"] [--glance] [--unset|--stale] "
                    "[--reduced] [--odx N --ody N] [--h24] [--slept S] [--thrum T] [--beep B] [--variant N] "
                    "[--yday N] [--reveal T [--skip]] [--wake T] [--pulse T] [--mono MS] [--fresh MS] [--click C] [--battery P] [--sync S] [--step MS] --out f.ppm | --face K --frame I --out f.ppm | --crc | --glow-lut | --spray-lut | --void f.png "
                    "[--assets DIR]\n");
    return 2;
}

static bool parse_at(const char *s, fk_clock_t *c)
{
    int Y, M, D, h, m, sec, ms = 0;
    const int n = sscanf(s, "%d-%d-%d %d:%d:%d.%d", &Y, &M, &D, &h, &m, &sec, &ms);
    if (n < 6 || M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59 ||
        ms < 0 || ms > 999)
        return false;
    static const int T[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};   /* Sakamoto's weekday; TZ-free wall time */
    static const int CUM[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const int y = Y - (M < 3);
    const bool leap = (Y % 4 == 0 && Y % 100 != 0) || Y % 400 == 0;
    memset(c, 0, sizeof *c);
    c->set = true;
    c->h = (uint8_t)h, c->m = (uint8_t)m, c->s = (uint8_t)sec, c->ms = (uint16_t)ms;
    c->mday = (uint8_t)D, c->mon = (uint8_t)(M - 1);
    c->wday = (uint8_t)((y + y / 4 - y / 100 + y / 400 + T[M - 1] + D) % 7);
    c->yday = (uint16_t)(CUM[M - 1] + D + (leap && M > 2));
    return true;
}

static bool write_ppm(const char *path, const uint16_t *fb)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", W, W);
    for (int i = 0; i < W * W; i++) {
        const uint16_t p = fb[i];
        const unsigned r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        const unsigned char px[3] = {(unsigned char)(r << 3 | r >> 2), (unsigned char)(g << 2 | g >> 4),
                                     (unsigned char)(b << 3 | b >> 2)};
        fwrite(px, 1, 3, f);
    }
    return fclose(f) == 0;
}

static void *bind_face(const fk_face_ops_t *ops)
{
    void *ctx = calloc(1, ops->ctx_bytes ? ops->ctx_bytes : 1);
    if (!ctx || !ops->bind(ctx, rd_assets_host_get)) {
        fprintf(stderr, "rd_shot: %s: bind failed (assets?)\n", ops->key);
        exit(1);
    }
    return ctx;
}

/* ---- --void: a minimal PNG reader (8-bit depth, colour types 0, 2, 3, 4 and 6, not interlaced) over zlib */
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int paeth(int a, int b, int c)
{
    const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* the image as 8-bit RGB (w x h x 3, malloc'd), or NULL with a reason on stderr */
static uint8_t *png_rgb(const char *path, int *w_out, int *h_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = n > 0 ? malloc((size_t)n) : NULL;
    if (!file || fread(file, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(file);
        return NULL;
    }
    fclose(f);
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (n < 33 || memcmp(file, SIG, 8) != 0) {
        fprintf(stderr, "rd_shot: %s is not a PNG\n", path);
        free(file);
        return NULL;
    }
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1, inter = 0;
    uint8_t pal[256 * 3] = {0};
    uint8_t *idat = NULL;
    size_t idat_n = 0;
    for (long p = 8; p + 12 <= n;) {
        const uint32_t len = be32(file + p);
        const uint8_t *tag = file + p + 4, *data = file + p + 8;
        if ((long)len > n - p - 12) break;
        if (!memcmp(tag, "IHDR", 4) && len >= 13) {
            w = be32(data), h = be32(data + 4), depth = data[8], ctype = data[9], inter = data[12];
        } else if (!memcmp(tag, "PLTE", 4)) {
            memcpy(pal, data, len < sizeof pal ? len : sizeof pal);
        } else if (!memcmp(tag, "IDAT", 4)) {
            uint8_t *m = realloc(idat, idat_n + len);
            if (!m) break;
            idat = m;
            memcpy(idat + idat_n, data, len);
            idat_n += len;
        } else if (!memcmp(tag, "IEND", 4)) {
            break;
        }
        p += 12 + (long)len;
    }
    free(file);
    const int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!idat || !w || !h || w > 4096 || h > 4096 || depth != 8 || !ch || inter) {
        fprintf(stderr, "rd_shot: %s: only 8-bit, non-interlaced PNGs (depth %d, colour type %d)\n", path, depth, ctype);
        free(idat);
        return NULL;
    }
    const size_t stride = (size_t)w * (size_t)ch;
    uLongf raw_n = (uLongf)((stride + 1) * h);
    uint8_t *raw = malloc(raw_n), *rgb = malloc((size_t)w * h * 3);
    if (!raw || !rgb || uncompress(raw, &raw_n, idat, (uLong)idat_n) != Z_OK || raw_n != (stride + 1) * h) {
        fprintf(stderr, "rd_shot: %s: the image data does not inflate to %u x %u\n", path, w, h);
        free(idat), free(raw), free(rgb);
        return NULL;
    }
    free(idat);
    for (uint32_t y = 0; y < h; y++) {   /* unfilter in place */
        uint8_t *row = raw + y * (stride + 1) + 1;
        const uint8_t *up = y ? raw + (y - 1) * (stride + 1) + 1 : NULL;
        const int ft = row[-1];
        for (size_t i = 0; i < stride; i++) {
            const int a = i >= (size_t)ch ? row[i - (size_t)ch] : 0, b = up ? up[i] : 0,
                      c = up && i >= (size_t)ch ? up[i - (size_t)ch] : 0;
            const int add = ft == 1 ? a : ft == 2 ? b : ft == 3 ? (a + b) / 2 : ft == 4 ? paeth(a, b, c) : 0;
            row[i] = (uint8_t)(row[i] + add);
        }
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *s = row + (size_t)x * (size_t)ch;
            uint8_t *d = rgb + ((size_t)y * w + x) * 3;
            if (ctype == 3) memcpy(d, pal + 3 * s[0], 3);
            else if (ch <= 2) d[0] = d[1] = d[2] = s[0];
            else memcpy(d, s, 3);   /* an alpha channel is ignored: the shots are opaque */
        }
    }
    free(raw);
    *w_out = (int)w, *h_out = (int)h;
    return rgb;
}

static int measure_void(const char *path)
{
    int w = 0, h = 0;
    uint8_t *rgb = png_rgb(path, &w, &h);
    if (!rgb) return 1;
    if (w != W || h != W) {
        fprintf(stderr, "rd_shot: %s is %d x %d, not %d x %d\n", path, w, h, W, W);
        free(rgb);
        return 1;
    }
    long v = 0, disc = 0;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            const float dx = (float)x + 0.5f - 233.f, dy = (float)y + 0.5f - 233.f;
            if (dx * dx + dy * dy > 233.f * 233.f) continue;
            disc++;
            const uint8_t *p = rgb + ((size_t)y * W + x) * 3;
            const uint16_t q = RD_RGB(p[0], p[1], p[2]);   /* the panel's RGB565, expanded as the harness does */
            const int R = ((q >> 11) & 31) * 255 / 31, G = ((q >> 5) & 63) * 255 / 63, B = (q & 31) * 255 / 31;
            v += R <= 40 && G <= 40 && B <= 40;
        }
    free(rgb);
    const double pct = 100.0 * (double)v / (double)disc;
    printf("%s: %.2f %% void (%ld of %ld disc px with every channel <= 40); PR10 floor %.2f %%\n", path, pct, v, disc,
           pct - 2.0);
    return 0;
}

int main(int argc, char **argv)
{
    const char *face = NULL, *at = NULL, *out = NULL, *assets = RD_ASSETS_DIR, *void_png = NULL;
    bool glance = false, unset = false, stale = false, crc = false, reduced = false, h24 = false, glow_lut = false;
    bool spray_lut = false;
    long fresh = -1;
    double click = 0;
    long odx = 0, ody = 0, slept = -1, variant = 0, still = -1, yday = -1, reveal_t = -1, wake = -1, pulse = -1, mono = 0;
    long battery = -1, sync = 0, step = -1;
    bool skip = false;
    double thrum = 0, beep = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const bool more = i + 1 < argc;
        if (!strcmp(a, "--face") && more) face = argv[++i];
        else if (!strcmp(a, "--at") && more) at = argv[++i];
        else if (!strcmp(a, "--out") && more) out = argv[++i];
        else if (!strcmp(a, "--assets") && more) assets = argv[++i];
        else if (!strcmp(a, "--void") && more) void_png = argv[++i];
        else if (!strcmp(a, "--odx") && more) odx = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--ody") && more) ody = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--slept") && more) slept = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--variant") && more) variant = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--frame") && more) still = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--yday") && more) yday = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--thrum") && more) thrum = strtod(argv[++i], NULL);
        else if (!strcmp(a, "--beep") && more) beep = strtod(argv[++i], NULL);
        else if (!strcmp(a, "--reveal") && more) reveal_t = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--wake") && more) wake = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--pulse") && more) pulse = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--mono") && more) mono = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--fresh") && more) fresh = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--click") && more) click = strtod(argv[++i], NULL);
        else if (!strcmp(a, "--battery") && more) battery = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--sync") && more) sync = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--step") && more) step = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--skip")) skip = true;
        else if (!strcmp(a, "--glance")) glance = true;
        else if (!strcmp(a, "--unset")) unset = true;
        else if (!strcmp(a, "--stale")) stale = true;
        else if (!strcmp(a, "--reduced")) reduced = true;
        else if (!strcmp(a, "--h24")) h24 = true;
        else if (!strcmp(a, "--crc")) crc = true;
        else if (!strcmp(a, "--glow-lut")) glow_lut = true;
        else if (!strcmp(a, "--spray-lut")) spray_lut = true;
        else return usage();
    }
    if (void_png) return measure_void(void_png);
    if (spray_lut) {   /* Task 12: Deck Sign's spray mask (no assets needed) */
        static uint8_t lut[RD_DK_SPRAY_TILE * RD_DK_SPRAY_TILE];
        rd_spray_lut_build(lut);
        printf("#pragma once\n"
               "/* generated by rd_shot --spray-lut (firmware/test/host/rd_shot.c) -- do not edit.\n"
               " * Deck Sign's spray mask (rd/faces.h RD_DK_SPRAY_TILE): rd_spray_lut_build's bytes\n"
               " * (rd/soft_lut.h), the mockup's spray() discs' thresholds as levels on a 128 x 128 toroidal\n"
               " * tile, as const data (lesson 3; test_rd_dk_tables checks it). */\n"
               "#include <stdint.h>\n\n"
               "static const uint8_t RD_DK_SPRAY_LUT[%d] = {", RD_DK_SPRAY_TILE * RD_DK_SPRAY_TILE);
        for (int k = 0; k < RD_DK_SPRAY_TILE * RD_DK_SPRAY_TILE; k++)
            printf("%s%s%u", k ? "," : "", k % 32 ? "" : "\n    ", lut[k]);
        printf("\n};\n");
        return 0;
    }
    if (glow_lut) {   /* no assets needed */
        static const int R[] = RD_GLOW_LUT_RADII;
        static uint8_t lut[FK_GLOW_LUT_MAX];
        printf("#pragma once\n"
               "/* generated by rd_shot --glow-lut (firmware/test/host/rd_shot.c) -- do not edit.\n"
               " * The faces' glow tables as const data (rd/faces.h rd_glow_lut; lesson 3): no face builds a table at\n"
               " * bind. fk_glow_lut_build's bytes for each radius of RD_GLOW_LUT_RADII (test_rd_glow_lut_tables checks\n"
               " * them), then the soft glows', rd_soft_lut_build's (rd/soft_lut.h), for each of RD_SOFT_LUT_RADII\n"
               " * (test_rd_release_glow_luts_const checks them), then the linear glows', rd_lin_lut_build's, for each\n"
               " * of RD_LIN_LUT_RADII (test_rd_dk_tables checks them), then the quarters glows', rd_qtr_lut_build's,\n"
               " * for each of RD_QTR_LUT_RADII (test_rd_bk_face checks them). */\n"
               "#include <stdint.h>\n\n");
        static const int SR[] = RD_SOFT_LUT_RADII;   /* Task 10: the soft glows (rd/soft_lut.h) */
        const unsigned nr = sizeof R / sizeof R[0], ns = sizeof SR / sizeof SR[0];
        for (unsigned i = 0; i < nr + ns; i++) {
            const int r = i < nr ? R[i] : SR[i - nr];
            const size_t n = FK_GLOW_LUT_N(r);
            if (n > sizeof lut) return 1;   /* a soft radius of 128 or more takes the coarse layout (wave 2 Task 3) */
            if (i == nr) printf("/* the soft glows (RD_SOFT_LUT_RADII): rd_soft_lut_build's bytes, softSprite()'s profile */\n");
            if (i < nr) fk_glow_lut_build(lut, r);
            else rd_soft_lut_build(lut, r);
            /* wave 2 Task 4: a soft radius another list also has (16, 26) is named RD_SOFT_LUT_ (rd_soft_lut) */
            bool shared = false;
            if (i >= nr) {
                static const int O[] = RD_GLOW_LUT_RADII, OL[] = RD_LIN_LUT_RADII, OQ[] = RD_QTR_LUT_RADII;
                for (unsigned j = 0; j < sizeof O / sizeof O[0]; j++) shared |= O[j] == r;
                for (unsigned j = 0; j < sizeof OL / sizeof OL[0]; j++) shared |= OL[j] == r;
                for (unsigned j = 0; j < sizeof OQ / sizeof OQ[0]; j++) shared |= OQ[j] == r;
            }
            printf("static const uint8_t RD_%s_LUT_%d[%u] = {", shared ? "SOFT" : "GLOW", r, (unsigned)n);
            for (size_t k = 0; k < n; k++) printf("%s%s%u", k ? "," : "", k % 24 ? "" : "\n    ", lut[k]);
            printf("\n};\n");
        }
        static const int LR[] = RD_LIN_LUT_RADII;   /* Task 12: the linear glows (rd/soft_lut.h) */
        printf("/* the linear glows (RD_LIN_LUT_RADII): rd_lin_lut_build's bytes, a two-stop radial gradient */\n");
        for (unsigned i = 0; i < sizeof LR / sizeof LR[0]; i++) {
            const int r = LR[i];
            const size_t n = FK_GLOW_LUT_N(r);
            if (n > sizeof lut || r >= FK_GLOW_COARSE_R) return 1;
            rd_lin_lut_build(lut, r);
            printf("static const uint8_t RD_LIN_LUT_%d[%u] = {", r, (unsigned)n);
            for (size_t k = 0; k < n; k++) printf("%s%s%u", k ? "," : "", k % 24 ? "" : "\n    ", lut[k]);
            printf("\n};\n");
        }
        static const int QR[] = RD_QTR_LUT_RADII;   /* Task 14: the quarters glows (rd/soft_lut.h) */
        printf("/* the quarters glows (RD_QTR_LUT_RADII): rd_qtr_lut_build's bytes, glowSprite()'s three stops */\n");
        for (unsigned i = 0; i < sizeof QR / sizeof QR[0]; i++) {
            const int r = QR[i];
            const size_t n = FK_GLOW_LUT_N(r);
            if (n > sizeof lut || r >= FK_GLOW_COARSE_R) return 1;
            rd_qtr_lut_build(lut, r);
            printf("static const uint8_t RD_QTR_LUT_%d[%u] = {", r, (unsigned)n);
            for (size_t k = 0; k < n; k++) printf("%s%s%u", k ? "," : "", k % 24 ? "" : "\n    ", lut[k]);
            printf("\n};\n");
        }
        return 0;
    }
    if (!rd_assets_host_open(assets)) return usage();
    if (crc) {
        printf("#pragma once\n"
               "/* generated by rd_shot --crc (firmware/test/host/rd_shot.c) over the committed assets -- do not edit.\n"
               " * Every registered face's still frames (rd_still, rd/faces.h): CRC-32 of the 466 x 466 RGB565 frame. The\n"
               " * host's test_verify_crc_current checks it; the device's `rd verify` compares its own frames. Still frames\n"
               " * only: never a frame in motion. */\n"
               "#include \"fk/face.h\"\n\n"
               "#define RD_VERIFY_SET \"generic\"\n"
               "static const fk_verify_t RD_VERIFY[] = {\n");
        unsigned rows = 0;
        for (unsigned i = 0; i < RD_FACE_COUNT; i++) {
            void *ctx = bind_face(RD_FACES[i]);
            for (unsigned k = 0; k < rd_still_count(RD_FACES[i]); k++) {
                rd_still_t st;
                rd_still(k % RD_STILL_N, &st);
                rd_still_frame(RD_FACES[i], ctx, k, s_fb);
                printf("    {\"%s\", %u, 0x%08xu},   /* %s%s */\n", RD_FACES[i]->key, k,
                       (unsigned)fk_crc32(0, s_fb, sizeof s_fb), st.what, k >= RD_STILL_N ? ", tapped" : "");
                rows++;
            }
            free(ctx);
        }
        printf("};\n");
        rd_assets_host_close();
        return rows ? 0 : 1;   /* an empty array is no C */
    }
    if (!face || !out) return usage();
    const fk_face_ops_t *ops = rd_face_by_key(face);
    if (!ops) {
        fprintf(stderr, "rd_shot: no face %s\n", face);
        return 2;
    }
    void *ctx = bind_face(ops);
    if (still >= 0) {
        if ((unsigned long)still >= rd_still_count(ops)) {
            fprintf(stderr, "rd_shot: %s has %u stills\n", face, rd_still_count(ops));
            return 2;
        }
        rd_still_frame(ops, ctx, (unsigned)still, s_fb);
    } else {
        fk_face_in_t in;
        memset(&in, 0, sizeof in);
        in.wake_ms = UINT32_MAX;
        in.battery = (int8_t)(battery < -1 ? -1 : battery > 100 ? 100 : battery);
        in.sync_age_s = (int32_t)sync;
        if (step >= 0) rd_sc_set_step_ms((uint16_t)step);   /* wave 2 Task 7: Nav Scope's sweep mode for this frame */
        in.h24 = h24;
        in.reduced = reduced;
        in.glance = glance;
        in.odx = (int8_t)odx, in.ody = (int8_t)ody;
        in.slept_s = (int32_t)slept;
        in.thrum = (float)thrum, in.beep = (float)beep;
        in.variant = (uint16_t)(variant < 0 ? 0 : variant > 65535 ? 65535 : variant);
        in.sun_rise_m = in.sun_set_m = -1;
        if (wake >= 0) in.wake_ms = (uint32_t)wake;   /* a face's own wake at T ms (Task 11) */
        if (pulse >= 0) in.pulsing = true, in.pulse_ms = (uint16_t)(pulse > 65535 ? 65535 : pulse);
        in.mono_ms = (uint32_t)mono;
        if (fresh >= 0) in.fresh = true, in.fresh_ms = (uint16_t)(fresh > 59999 ? 59999 : fresh);
        in.click = (float)click;
        if (at && !parse_at(at, &in.clk)) return usage();
        if (!at || unset) memset(&in.clk, 0, sizeof in.clk);
        in.clk.stale = stale && in.clk.set;
        if (yday >= 0 && in.clk.set) in.clk.yday = (uint16_t)yday;
        if (reveal_t >= 0) {   /* the reveal at T, as fk_view draws it */
            if (!ops->reveal || !ops->reveal_ops) {
                fprintf(stderr, "rd_shot: %s has no reveal\n", face);
                return 2;
            }
            const fk_reveal_ops_t *ro = ops->reveal_ops;
            void *rctx = calloc(1, ro->rctx_bytes);
            static uint16_t interior[W * W];
            rd_rel_set_clock(in.clk.set);
            if (!rctx || !ro->bind(ops->reveal_arg, ops->reveal_asset, rd_assets_host_get, rctx)) {
                fprintf(stderr, "rd_shot: %s: the reveal's bind failed\n", face);
                return 1;
            }
            fk_rframe_t fr;
            ro->frame(ops->reveal_arg, rctx, (uint32_t)reveal_t, skip, reduced, &fr);
            in.wake_ms = fr.wake_ms;
            in.reveal_live = fr.live;
            ops->draw(ctx, &in, (omni_rect_t){0, 0, W, W}, interior, W);
            for (int y0 = 0; y0 < W; y0 += 8)
                ro->band(ops->reveal_arg, rctx, interior, (omni_rect_t){0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)},
                         s_fb + (size_t)y0 * W);
            free(rctx);
        } else {
            (glance ? ops->glance : ops->draw)(ctx, &in, (omni_rect_t){0, 0, W, W}, s_fb, W);
        }
    }
    if (!write_ppm(out, s_fb)) {
        fprintf(stderr, "rd_shot: cannot write %s\n", out);
        return 1;
    }
    free(ctx);
    rd_assets_host_close();
    return 0;
}
