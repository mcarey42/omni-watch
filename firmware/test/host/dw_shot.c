/* dw_shot: a dw face frame on the host (dw wave 1 Task 11), from the same dw_core code and asset files as the
 * device. A host tool, not a test.
 *
 *   dw_shot --face K --at "YYYY-MM-DD HH:MM:SS[.mmm]" [--glance] [--unset | --stale] [--wake MS] [--reduced]
 *           [--odx N --ody N] [--h24] [--page N] [--alt] --out f.ppm
 *       one frame as a binary PPM. --wake MS: the wake MS ms in; on a doors face (#1) that is the doors' reveal
 *       frame (dw_doors_at(MS) over the face drawn at wake_ms MS), --reduced its cross-fade.
 *   dw_shot --crc [--set NAME]
 *       every registered face's still frames (dw_still) as the C header dw/verify_crc.h (stdout). Only still
 *       frames: never a frame in motion (doors.h).
 *   dw_shot --crc --overlay DIR [--set NAME]
 *       the overlay's supplement (R147 PR3, wave 2 Task 4) as verify_crc_overlay.h: the still frames of only the faces
 *       whose bind reads a file from DIR (a swap asset), as DW_VERIFY_OVERLAY. A private build and the overlay test
 *       look a still up there first, then in dw/verify_crc.h (fk_verify_lookup).
 *   dw_shot --bench
 *       host times: a whole frame, the script's step, the crystal's glint, a glance frame, a doors frame.
 *   dw_shot --glow-lut
 *       the faces' glow tables (fk_glow_lut_build for each of DW_GLOW_LUT_RADII) as dw_core's glow_lut.h (stdout;
 *       Task 13, the Task 12 review's M3). The host tests check the committed header against fk_glow_lut_build.
 *   --page N: the key's page (Task 15; --wake MS is then the time since the page opened); --alt: the learning
 *   face's twins hidden.
 *   Frames also take [--battery N] [--sun rise|set HH:MM] [--pulse MS] (the call box's plaque and lamp) and
 *   [--day HH:MM HH:MM] (today's sunrise and sunset, #9's SOLAR meter; none without it), [--variant N] (the face's
 *   variant: #12's palette, wave 2 Task 14) and [--mono MS] (the free-running ms count: #12's tunnel, #14's breath).
 *   --assets DIR (default the committed generic set) and --overlay DIR (a directory whose files replace DIR's: the
 *   private overlay's assets/, so the staged private set) choose the art. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dw_assets_host.h"
#include "fk/compose.h"
#include "dw/doors.h"
#include "fk/reveal_sm.h"
#include "dw/faces.h"

#define W 466
static uint16_t s_fb[W * W], s_int[W * W];

static int usage(void)
{
    fprintf(stderr, "usage: dw_shot --face K --at \"YYYY-MM-DD HH:MM:SS\" [--glance] [--unset|--stale] [--wake MS] "
                    "[--reduced] [--odx N --ody N] [--h24] [--page N] [--alt] [--battery N] [--sun rise|set HH:MM] [--pulse MS] "
                    "[--day HH:MM HH:MM] [--variant N] [--mono MS] "
                    "--out f.ppm | --crc [--set NAME] | --bench | --glow-lut "
                    "[--assets DIR] [--overlay DIR]\n");
    return 2;
}

static bool parse_at(const char *s, fk_clock_t *c)
{
    int Y, M, D, h, m, sec, ms = 0;
    const int n = sscanf(s, "%d-%d-%d %d:%d:%d.%d", &Y, &M, &D, &h, &m, &sec, &ms);
    if (n < 6 || M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59 ||
        ms < 0 || ms > 999)
        return false;
    /* the weekday of the date (Sakamoto's method): the fields are local wall time, TZ-free */
    static const int T[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = Y - (M < 3);
    memset(c, 0, sizeof *c);
    c->set = true;
    c->h = (uint8_t)h, c->m = (uint8_t)m, c->s = (uint8_t)sec, c->ms = (uint16_t)ms;
    c->mday = (uint8_t)D, c->mon = (uint8_t)(M - 1);
    c->wday = (uint8_t)((y + y / 4 - y / 100 + y / 400 + T[M - 1] + D) % 7);
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

/* the bind's getter, noting whether any file came from the overlay directory (a swap asset) */
static bool s_from_overlay;
static const void *watch_get(const char *name, size_t *size)
{
    const void *p = dw_assets_host_get(name, size);
    if (p && dw_assets_host_from_overlay(name)) s_from_overlay = true;
    return p;
}

static void *bind_face(const fk_face_ops_t *ops)
{
    void *ctx = calloc(1, ops->ctx_bytes ? ops->ctx_bytes : 1);
    s_from_overlay = false;
    if (!ctx || !ops->bind(ctx, watch_get)) {
        fprintf(stderr, "dw_shot: %s: bind failed (assets?)\n", ops->key);
        exit(1);
    }
    return ctx;
}

static bool doors_assets(const fk_face_ops_t *ops, fk_img_t *door, fk_img_t *glow)
{
    size_t n;
    const void *d = dw_assets_host_get(ops->reveal_asset ? ops->reveal_asset : "dw_doors_m.bin", &n);
    if (!d || !fk_img_from_bin(door, d, n) || door->cf != FK_CF_RGB565 || door->w != W || door->h != W) return false;
    const void *g = dw_assets_host_get("dw_glow.bin", &n);
    return g && fk_img_from_bin(glow, g, n) && glow->a && glow->w == 64 && glow->h == 256;
}

/* the doors' frame at t over the interior, in 8-row bands as the device sends it; the face's own doors
 * (ops->reveal_asset, ops->reveal_arg), and the interior drawn whole with the input fk_view's frame plan gives it
 * (fk_rs_plan, fk_rs_job_in: wake_ms and doors_live; wave 2 Tasks 3 and 11) */
static bool doors_frame(const fk_face_ops_t *ops, const void *ctx, fk_face_in_t in, uint32_t t, bool reduced)
{
    fk_img_t door, glow;
    if (!doors_assets(ops, &door, &glow)) return false;
    const fk_cols_kind_t *k = ops->reveal_arg ? ops->reveal_arg : &DW_DOORS_OUT;
    fk_cols_state_t st;
    dw_doors_at_k(k, t, reduced, &st);
    static fk_col_t cols[W];
    fk_cols_k(k, st.th_l, st.th_r, cols);
    bool settled = false;
    fk_rp_t plan;
    fk_rs_plan(&st, cols, reduced, &settled, &plan);
    /* FK_RP_NONE (the leaves meet): the device draws nothing then and the interior keeps its pixels; this tool has no
     * previous interior, so it draws the tail at t (FK_RP_SWING), none of which shows under the closed leaves (Task 11
     * review M6) */
    if (plan.kind == FK_RP_NONE) plan.kind = FK_RP_SWING;
    fk_rs_job_in(&plan, t, reduced, &in.wake_ms, &in.reveal_live);
    ops->draw(ctx, &in, (omni_rect_t){0, 0, W, W}, s_int, W);
    for (int y0 = 0; y0 < W; y0 += 8) {
        const omni_rect_t b = {0, (int16_t)y0, W, (int16_t)(y0 + 8 < W ? y0 + 8 : W)};
        fk_cols_band(k, &st, cols, door.rgb, s_int, glow.a, b, s_fb + (size_t)y0 * W);
    }
    return true;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void bench(void)
{
    for (unsigned i = 0; i < DW_FACE_COUNT; i++) {
        const fk_face_ops_t *ops = DW_FACES[i];
        void *ctx = bind_face(ops);
        dw_still_t st;
        dw_still(0, &st);
        const int N = 60;
        double t0 = now_ms();
        for (int k = 0; k < N; k++) ops->draw(ctx, &st.in, (omni_rect_t){0, 0, W, W}, s_fb, W);
        const double whole = (now_ms() - t0) / N;
        /* the rest motion: each pair's dirty rects redrawn (the view's work at the cadence) */
        double step = 0, glint = 0;
        for (int pass = 0; pass < 2; pass++) {
            fk_face_in_t a = st.in, b = st.in;
            a.clk.ms = pass ? 500 : 100, b.clk.ms = (uint16_t)(a.clk.ms + 40);
            omni_rect_t r[8];
            const uint8_t n = ops->dirty(ctx, &a, &b, r);
            t0 = now_ms();
            for (int k = 0; k < N; k++)
                for (uint8_t j = 0; j < n; j++)
                    ops->draw(ctx, &b, r[j], s_fb + (size_t)r[j].y0 * W + r[j].x0, W);
            *(pass ? &glint : &step) = (now_ms() - t0) / N;
        }
        dw_still(3, &st);
        t0 = now_ms();
        for (int k = 0; k < N; k++) ops->glance(ctx, &st.in, (omni_rect_t){0, 0, W, W}, s_fb, W);
        const double gl = (now_ms() - t0) / N;
        printf("%s: whole %.3f ms, script step (dirty) %.3f ms, glint (dirty) %.3f ms, glance %.3f ms", ops->key, whole,
               step, glint, gl);
        if (ops->reveal) {
            dw_still(0, &st);
            t0 = now_ms();
            for (int k = 0; k < N; k++) doors_frame(ops, ctx, st.in, (uint32_t)(300 + k * 8), false);
            printf(", doors frame (interior + bands) %.3f ms", (now_ms() - t0) / N);
        }
        printf("\n");
        free(ctx);
    }
}

int main(int argc, char **argv)
{
    const char *face = NULL, *at = NULL, *out = NULL, *assets = DW_ASSETS_DIR, *overlay = NULL, *set = NULL;
    bool glance = false, unset = false, stale = false, crc = false, do_bench = false, reduced = false, h24 = false;
    long wake = -1, odx = 0, ody = 0, battery = -1, pulse = -1, page = 0, variant = 0;
    unsigned long mono = 0;
    bool alt = false;
    bool glow_lut = false;
    const char *sun_kind = NULL, *sun_at = NULL, *day_rise = NULL, *day_set = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const bool more = i + 1 < argc;
        if (!strcmp(a, "--face") && more) face = argv[++i];
        else if (!strcmp(a, "--at") && more) at = argv[++i];
        else if (!strcmp(a, "--out") && more) out = argv[++i];
        else if (!strcmp(a, "--assets") && more) assets = argv[++i];
        else if (!strcmp(a, "--overlay") && more) overlay = argv[++i];
        else if (!strcmp(a, "--set") && more) set = argv[++i];
        else if (!strcmp(a, "--wake") && more) wake = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--odx") && more) odx = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--ody") && more) ody = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--glance")) glance = true;
        else if (!strcmp(a, "--unset")) unset = true;
        else if (!strcmp(a, "--stale")) stale = true;
        else if (!strcmp(a, "--reduced")) reduced = true;
        else if (!strcmp(a, "--h24")) h24 = true;
        else if (!strcmp(a, "--page") && more) page = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--alt")) alt = true;
        else if (!strcmp(a, "--crc")) crc = true;
        else if (!strcmp(a, "--bench")) do_bench = true;
        else if (!strcmp(a, "--glow-lut")) glow_lut = true;
        else if (!strcmp(a, "--battery") && more) battery = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--pulse") && more) pulse = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--variant") && more) variant = strtol(argv[++i], NULL, 10);
        else if (!strcmp(a, "--mono") && more) mono = strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--sun") && i + 2 < argc) sun_kind = argv[++i], sun_at = argv[++i];
        else if (!strcmp(a, "--day") && i + 2 < argc) day_rise = argv[++i], day_set = argv[++i];
        else return usage();
    }
    if (glow_lut) {   /* no assets needed */
        static const int R[] = DW_GLOW_LUT_RADII;
        static uint8_t lut[FK_GLOW_LUT_MAX];
        printf("#pragma once\n"
               "/* generated by dw_shot --glow-lut (firmware/test/host/dw_shot.c) -- do not edit.\n"
               " * fk_glow_lut_build's bytes for each radius the faces glow at, as const data (dw/faces.h\n"
               " * dw_glow_lut; Task 13, the Task 12 review's M3): no face builds a table at bind.\n"
               " * test_glow_lut_tables checks them against fk_glow_lut_build. */\n"
               "#include <stdint.h>\n\n");
        for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
            const size_t n = FK_GLOW_LUT_N(R[i]);
            fk_glow_lut_build(lut, R[i]);
            printf("static const uint8_t DW_GLOW_LUT_%d[%u] = {", R[i], (unsigned)n);
            for (size_t k = 0; k < n; k++) printf("%s%s%u", k ? "," : "", k % 24 ? "" : "\n    ", lut[k]);
            printf("\n};\n");
        }
        return 0;
    }
    if (!dw_assets_host_open(assets, overlay)) return usage();
    if (do_bench) {
        bench();
        return 0;
    }
    if (crc) {
        const char *label = set ? set : overlay ? "overlay" : "generic";
        if (overlay)
            printf("#pragma once\n"
                   "/* generated by dw_shot --crc --overlay (firmware/test/host/dw_shot.c) over the %s art set -- do not\n"
                   " * edit. The supplement (R147 PR3): the still frames (dw_still, dw/faces.h) of only the faces whose bind\n"
                   " * reads a swap asset, CRC-32 of the 466 x 466 RGB565 frame. A private build's `dw verify` and the host's\n"
                   " * overlay test look a still up here first, then in the generic dw/verify_crc.h (fk_verify_lookup). Still\n"
                   " * frames only: never a frame in motion (doors.h). */\n"
                   "#include \"dw/faces.h\"\n\n"
                   "#define DW_VERIFY_OVERLAY_SET \"%s\"\n"
                   "static const dw_verify_t DW_VERIFY_OVERLAY[] = {\n",
                   label, label);
        else
            printf("#pragma once\n"
                   "/* generated by dw_shot --crc (firmware/test/host/dw_shot.c) over the %s art set -- do not edit.\n"
                   " * Every registered face's still frames (dw_still, dw/faces.h): CRC-32 of the 466 x 466 RGB565 frame. The\n"
                   " * host's test_verify_crc_current checks it; the device's `dw verify` compares its own frames (W4). Still\n"
                   " * frames only: never a frame in motion (doors.h). */\n"
                   "#include \"dw/faces.h\"\n\n"
                   "#define DW_VERIFY_SET \"%s\"\n"
                   "static const dw_verify_t DW_VERIFY[] = {\n",
                   label, label);
        unsigned rows = 0;
        for (unsigned i = 0; i < DW_FACE_COUNT; i++) {
            void *ctx = bind_face(DW_FACES[i]);
            if (overlay && !s_from_overlay) {   /* no swap asset: the generic table's rows apply */
                free(ctx);
                continue;
            }
            for (unsigned k = 0; k < dw_still_count(DW_FACES[i]); k++) {
                dw_still_t st;
                dw_still(k % DW_STILL_N, &st);
                dw_still_frame(DW_FACES[i], ctx, k, s_fb);
                char page[32] = "";
                if (DW_FACES[i]->pages > 1) snprintf(page, sizeof page, "page %u, ", k / DW_STILL_N);
                printf("    {\"%s\", %u, 0x%08xu},   /* %s%s */\n", DW_FACES[i]->key, k,
                       (unsigned)fk_crc32(0, s_fb, sizeof s_fb), page, st.what);
                rows++;
            }
            free(ctx);
        }
        printf("};\n");
        if (!rows) {   /* an empty array is no C; and an overlay no face reads replaces nothing a still shows */
            fprintf(stderr, "dw_shot: no face's bind reads a file of %s\n", overlay ? overlay : assets);
            return 1;
        }
        return 0;
    }
    if (!face || !out) return usage();
    const fk_face_ops_t *ops = dw_face_by_key(face);
    if (!ops) {
        fprintf(stderr, "dw_shot: no face %s\n", face);
        return 2;
    }
    fk_face_in_t in;
    memset(&in, 0, sizeof in);
    in.wake_ms = UINT32_MAX;
    in.battery = -1;
    in.h24 = h24;
    in.page = (uint8_t)(page < 0 ? 0 : page > 255 ? 255 : page);
    in.alt = alt;
    in.reduced = reduced;
    in.odx = (int8_t)odx, in.ody = (int8_t)ody;
    if (at && !parse_at(at, &in.clk)) return usage();
    if (!at) unset = true;
    if (unset) memset(&in.clk, 0, sizeof in.clk);
    in.clk.stale = stale && !unset;
    in.battery = (int8_t)(battery < 0 ? -1 : battery > 100 ? 100 : battery);
    int sh, sm;
    if (sun_kind && sun_at && sscanf(sun_at, "%d:%d", &sh, &sm) == 2) {
        in.sun_kind = strcmp(sun_kind, "rise") == 0 ? 1 : 2;
        in.sun_h = (uint8_t)sh, in.sun_m = (uint8_t)sm;
    }
    in.sun_rise_m = in.sun_set_m = -1;
    int rh, rm, th, tm;
    if (day_rise && day_set && sscanf(day_rise, "%d:%d", &rh, &rm) == 2 && sscanf(day_set, "%d:%d", &th, &tm) == 2)
        in.sun_rise_m = (int16_t)(rh * 60 + rm), in.sun_set_m = (int16_t)(th * 60 + tm);
    if (pulse >= 0) in.pulsing = true, in.pulse_ms = (uint16_t)pulse;
    in.variant = (uint8_t)(variant < 0 ? 0 : variant > 255 ? 255 : variant);
    in.mono_ms = (uint32_t)mono;
    void *ctx = bind_face(ops);
    if (wake >= 0 && ops->reveal && !glance) {
        if (!doors_frame(ops, ctx, in, (uint32_t)wake, reduced)) {
            fprintf(stderr, "dw_shot: no doors assets\n");
            return 1;
        }
    } else {
        if (wake >= 0) in.wake_ms = (uint32_t)wake;
        (glance ? ops->glance : ops->draw)(ctx, &in, (omni_rect_t){0, 0, W, W}, s_fb, W);
    }
    if (!write_ppm(out, s_fb)) {
        fprintf(stderr, "dw_shot: cannot write %s\n", out);
        return 1;
    }
    free(ctx);
    dw_assets_host_close();
    return 0;
}
