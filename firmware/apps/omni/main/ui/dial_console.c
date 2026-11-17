/* The Omni dial view's console jobs (Stage 3 Task 7): `dial` (print), `dial perf`, `dial scen`, and the worker's jobs
 * `dial bench`, `dial verify`, `dial selftest` / `ring selftest`, and the `dial` and `ring` commands themselves
 * (dial_console_register). Only with CONFIG_OMNI_DEBUG_CONSOLE. */
#include "sdkconfig.h"
#if CONFIG_OMNI_DEBUG_CONSOLE
#include "ui/dial_view_int.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "omni/globe_check.h"
#include "omni/blend565.h"
#include "omni/iris.h"
#include "omni/raster.h"
#include "ui/assets_meta.h"
#include "ui/globe_pie.h"
#include "bsp/esp-bsp.h"
#include "debug_console.h"

#define JOB_TIMEOUT_MS 120000

/* TEX and BASE are the next view's (dial_view.c takes them at create, no repaint); DRIFT and LOSSY are v's.
 * bit -1: just read. */
unsigned dial_view_perf(dial_view_t *v, int bit, int on)
{
    if (bit >= 0 && (1u << bit) == PERF_TEX) dv_set_next_tex(on);
    else if (bit >= 0 && (1u << bit) == PERF_BASE) dv_set_next_base(on);
    else if (bit >= 0 && v) {
        portENTER_CRITICAL(&dial_mux);
        v->perf = on ? (v->perf | (1u << bit)) : (v->perf & ~(1u << bit));
        v->perf_epoch++;   /* the buffers were drawn the other way: both are rendered whole again */
        portEXIT_CRITICAL(&dial_mux);
    }
    portENTER_CRITICAL(&dial_mux);
    const unsigned r = (v ? v->perf & ~(unsigned)PERF_NEXT_VIEW : 0) | (dv_next_tex() ? PERF_TEX : 0)
                     | (dv_next_base() ? PERF_BASE : 0);
    portEXIT_CRITICAL(&dial_mux);
    return r;
}

void dial_view_scenario(dial_view_t *v, dial_scen_t s)
{
    v->scen = s;
    memset(v->scen_val, 0, sizeof v->scen_val);
    v->scen_lon = 0;
}

void dial_view_print(dial_view_t *v)
{
    dial_view_stats_t s;
    portENTER_CRITICAL(&dial_mux);
    const uint32_t late = v->st_late, late_drift = v->st_late_drift;
    const unsigned perf = v->perf;
    portEXIT_CRITICAL(&dial_mux);
    dial_view_stats(v, &s, true);   /* a window per `dial` */
    portENTER_CRITICAL(&dial_mux);
    const uint32_t lf = v->reveal_frames, lmax = v->reveal_us_max, lps = v->reveal_psram_low;
    const uint64_t lsum = v->reveal_us_sum;
    __typeof__(v->reveal_log) log;
    memcpy(log, v->reveal_log, sizeof log);
    uint32_t lvl[OMNI_RINGS][3];
    memcpy(lvl, v->lvl_n, sizeof lvl);
    memset(v->lvl_n, 0, sizeof v->lvl_n);
    const uint32_t ff = v->flash_frames, fmax = v->flash_us_max;
    const uint64_t fsum = v->flash_us_sum;
    v->flash_frames = v->flash_us_max = 0;
    v->flash_us_sum = 0;
    v->reveal_frames = v->reveal_us_max = 0;
    v->reveal_us_sum = 0;
    v->reveal_psram_low = UINT32_MAX;
    portEXIT_CRITICAL(&dial_mux);
    if (lf)
        printf("DIAL reveal: %u frames with the lid or iris on, render avg %.2f ms max %.2f ms, PSRAM free low %u (the "
               "reveal's peak)\n",
               (unsigned)lf, lsum / 1000.0 / lf, lmax / 1000.0, (unsigned)lps);
    if (lf) {
        printf("DIAL reveal frames (lid deg or iris ap px / ms):");
        for (uint32_t i = 0; i < lf && i < 24; i++)
            printf(" %s%.1f/%.1f", log[i].iris ? "ap" : "", (double)log[i].v, log[i].us / 1000.0);
        printf("\n");
    }
    else printf("DIAL reveal: no lid or iris frames in this window\n");
    printf("DIAL ring levels (frames at L0/L1/L2): month %u/%u/%u, day %u/%u/%u, year %u/%u/%u, century %u/%u/%u\n",
           (unsigned)lvl[0][0], (unsigned)lvl[0][1], (unsigned)lvl[0][2], (unsigned)lvl[1][0], (unsigned)lvl[1][1],
           (unsigned)lvl[1][2], (unsigned)lvl[2][0], (unsigned)lvl[2][1], (unsigned)lvl[2][2], (unsigned)lvl[3][0],
           (unsigned)lvl[3][1], (unsigned)lvl[3][2]);
    if (ff) printf("DIAL flash: %u frames, render avg %.2f ms max %.2f ms\n", (unsigned)ff, fsum / 1000.0 / ff, fmax / 1000.0);
    printf("DIAL iris capacity overruns since boot: %u (0 expected)\n", (unsigned)omni_iris_overflows());
    /* the blank's fields are LVGL-context only: the caller's LVGL lock (cmd_dial) makes this one consistent snapshot */
    const uint32_t bn = v->blank_n, blast = v->blank_last_us, bmax = v->blank_max_us;
    const bool bwait = v->blanked;
    printf("DIAL wake blanks: %u ended, last %.1f ms, max %.1f ms (wake to the first fresh frame swapped in)%s\n",
           (unsigned)bn, blast / 1000.0, bmax / 1000.0, bwait ? ", one waiting" : "");
    printf("DIAL window: %u frames, %u over 30 ms (%u of them drift-path frames)\n", (unsigned)s.frames, (unsigned)late,
           (unsigned)late_drift);
    printf("DIAL %s %s: %u frames, render avg %.2f ms max %.2f ms, dirty avg %u px, build %u ms, %u B PSRAM; worker "
           "stack free %u of %u, helper %u; loupe sizes %d built (avg %.2f ms, max %.2f ms); unseen %u, band conflicts %u; "
           "drift frames %u (under rebuilt %u), loupe pieces %d / %d of %d, overflows %u; "
           "perf 0x%x, texture %s, base %s (%u B)\n",
           v->path == DISP_PATH_IMAGE ? "IMAGE" : "BAND",
           dial_view_failed(v) ? "FAILED" : dial_view_ready(v) ? "ready" : "building", (unsigned)s.frames,
           s.render_us_avg / 1000.0, s.render_us_max / 1000.0, (unsigned)s.dirty_px_avg, (unsigned)s.build_ms,
           (unsigned)s.bytes, (unsigned)uxTaskGetStackHighWaterMark(v->task), (unsigned)STACK,
           (unsigned)uxTaskGetStackHighWaterMark(v->helper), LOUPE_STEPS,
           v->loupe_us_sum / 1000.0 / LOUPE_STEPS, v->loupe_us_max / 1000.0,
           (unsigned)v->swap.unseen, (unsigned)v->band_conflicts, (unsigned)v->drift_frames, (unsigned)v->u_builds, v->disc_pieces,
           v->chord_pieces, OMNI_RECTS_MAX, (unsigned)__atomic_load_n(&v->minus_full, __ATOMIC_RELAXED), perf,
           v->tex_copy ? "PSRAM" : "flash", v->base_copy ? "PSRAM" : "flash",
           v->base_copy ? (unsigned)(SCR * SCR * 2) : 0u);
}

void dial_view_direct_lose(dial_view_t *v) { __atomic_store_n(&v->dd_lose, true, __ATOMIC_RELAXED); }

void dial_view_direct(dial_view_t *v, bool reset)
{
    static const char *const MODE[] = { "IMAGE", "DIRECT", "EXIT" };
    portENTER_CRITICAL(&dial_mux);
    const dv_mode_t mode = v->mode;
    uint32_t refused[OMNI_DD__COUNT], checks[OMNI_DD__COUNT], why[DV_EX__COUNT];
    memcpy(refused, v->dd_refused, sizeof refused);
    memcpy(checks, v->dd_checks, sizeof checks);
    memcpy(why, v->dd_exit_why, sizeof why);
    const uint32_t claims = v->dd_claims, frames = v->dd_frames, waits = v->dd_waits, backoffs = v->dd_backoffs,
                   exits = v->dd_exits, aborts = v->dd_aborts, copies = v->dd_copies, exit_max = v->dd_exit_us_max,
                   frame_max = v->dd_frame_us_max, swaps = v->dd_claim_swaps, last_seq = v->dd_last_seq;
    const uint64_t exit_sum = v->dd_exit_us_sum, frame_sum = v->dd_frame_us_sum;
    if (reset) {
        memset(v->dd_refused, 0, sizeof v->dd_refused);
        memset(v->dd_checks, 0, sizeof v->dd_checks);
        memset(v->dd_exit_why, 0, sizeof v->dd_exit_why);
        v->dd_claims = v->dd_frames = v->dd_waits = v->dd_backoffs = v->dd_exits = v->dd_aborts = v->dd_copies = 0;
        v->dd_claim_swaps = 0;
        v->dd_exit_us_max = v->dd_frame_us_max = 0;
        v->dd_exit_us_sum = v->dd_frame_us_sum = 0;
    }
    portEXIT_CRITICAL(&dial_mux);
    disp_direct_stats_t x;
    disp_direct_stats(&x);
    printf("DIAL DIRECT %s: claims %u, frames %u (%u entry copies; render avg %.2f ms max %.2f ms), exits %u "
           "(still %u, below %u, refused %u, perf %u; exit render avg %.2f ms max %.2f ms), aborts %u, "
           "claim swaps %u; last seq %u (shown %u)\n",
           MODE[mode], (unsigned)claims, (unsigned)frames, (unsigned)copies,
           frames ? frame_sum / 1000.0 / frames : 0.0, frame_max / 1000.0, (unsigned)exits, (unsigned)why[DV_EX_STILL],
           (unsigned)why[DV_EX_BELOW], (unsigned)why[DV_EX_REQ], (unsigned)why[DV_EX_EPOCH],
           exits ? exit_sum / 1000.0 / exits : 0.0, exit_max / 1000.0, (unsigned)aborts, (unsigned)swaps,
           (unsigned)last_seq, (unsigned)disp_direct_shown());
    printf("DIAL DIRECT claim refusals OFF %u PROV %u HIDDEN %u OVERLAY %u RAM %u; display waits %u, back-offs %u; "
           "check refusals OFF %u PROV %u DISPLAY %u HIDDEN %u OVERLAY %u RAM %u\n",
           (unsigned)refused[OMNI_DD_OFF], (unsigned)refused[OMNI_DD_PROV], (unsigned)refused[OMNI_DD_HIDDEN],
           (unsigned)refused[OMNI_DD_OVERLAY], (unsigned)refused[OMNI_DD_RAM], (unsigned)waits, (unsigned)backoffs,
           (unsigned)checks[OMNI_DD_OFF], (unsigned)checks[OMNI_DD_PROV], (unsigned)checks[OMNI_DD_DISPLAY],
           (unsigned)checks[OMNI_DD_HIDDEN], (unsigned)checks[OMNI_DD_OVERLAY], (unsigned)checks[OMNI_DD_RAM]);
    printf("DIAL DIRECT transport (since boot): claims %u, frames %u, lost %u, orphan releases %u, drain time-outs %u, "
           "held %u B, %s%s\n",
           (unsigned)x.claims, (unsigned)x.frames, (unsigned)x.lost_total, (unsigned)x.orphans,
           (unsigned)x.drain_timeouts, (unsigned)x.held_bytes,
           x.claimed ? (x.active ? "active" : "claimed, inactive") : x.stuck ? "stuck" : "idle",
           x.shadow ? ", shadow on" : "");
    for (int i = 0; i < DISP_DIRECT_OVERLAYS; i++)   /* Task 6: the registry's sprites */
        if (x.ov[i].used)
            printf("DIAL DIRECT overlay %d: %s, %dx%d at %d,%d; snapshots %u, avg %.2f ms max %.2f ms\n", i,
                   x.ov[i].failed ? "failed" : x.ov[i].shown ? "shown" : "hidden", x.ov[i].r.x1 - x.ov[i].r.x0,
                   x.ov[i].r.y1 - x.ov[i].r.y0, x.ov[i].r.x0, x.ov[i].r.y0, (unsigned)x.ov[i].snaps,
                   x.ov[i].us_avg / 1000.0, x.ov[i].us_max / 1000.0);
}

static bool run_job(dial_view_t *v, job_t job)
{
    portENTER_CRITICAL(&dial_mux);
    const bool ok = v->built;
    if (ok) v->job = job;
    portEXIT_CRITICAL(&dial_mux);
    if (!ok) {
        printf("DIAL job: the view is not built\n");
        return false;
    }
    xSemaphoreTake(v->job_done, 0);
    xTaskNotifyGive(v->task);
    if (xSemaphoreTake(v->job_done, pdMS_TO_TICKS(JOB_TIMEOUT_MS)) != pdTRUE) {
        printf("DIAL job: the worker did not answer\n");
        return false;
    }
    portENTER_CRITICAL(&dial_mux);
    const bool r = v->job_ok;
    portEXIT_CRITICAL(&dial_mux);
    return r;
}

void dial_view_bench(dial_view_t *v) { run_job(v, JOB_BENCH); }
void dial_view_bench_iris(dial_view_t *v) { run_job(v, JOB_BENCH_IRIS); }
bool dial_view_verify(dial_view_t *v)
{
    portENTER_CRITICAL(&dial_mux);
    const bool image = v->mode == DV_IMAGE;
    portEXIT_CRITICAL(&dial_mux);
    /* the overlays on screen now, for the DIRECT line's pipeline part (Task 6) */
    if (image) dv_lov_publish(v, false);
    return run_job(v, JOB_VERIFY);
}

/* The first pixel where a and b differ, skipping the pixels inside skip (may be NULL): its count, *fx, *fy. */
static uint32_t frame_diff(const uint16_t *a, const uint16_t *b, const rects_t *skip, int *fx, int *fy)
{
    uint32_t n = 0;
    *fx = *fy = -1;
    for (int y = 0; y < SCR; y++) {
        uint8_t in[SCR];
        memset(in, 0, sizeof in);
        for (int i = 0; skip && i < skip->n; i++)
            if (y >= skip->r[i].y0 && y < skip->r[i].y1)
                memset(in + skip->r[i].x0, 1, (size_t)(skip->r[i].x1 - skip->r[i].x0));
        for (int x = 0; x < SCR; x++)
            if (!in[x] && a[(size_t)y * SCR + x] != b[(size_t)y * SCR + x]) {
                if (!n) {
                    *fx = x;
                    *fy = y;
                }
                n++;
            }
    }
    return n;
}

/* frame f rendered whole on one core into out (16-row bands) */
static void render_whole(dial_view_t *v, const vframe_t *f, uint16_t *out)
{
    dv_prepare(v, f);
    for (int y0 = 0; y0 < SCR; y0 += 16) {
        const omni_rect_t band = { 0, (int16_t)y0, SCR, (int16_t)dv_imin(y0 + 16, SCR) };
        omni_dial_render_prepared(v->prep, out, &band);
    }
}

/* The DIRECT line (Task 5): (a) the pipeline: the current frame's whole screen through omni_band_chunks, the direct
 * fill, omni_band_pack and an un-swap, on both cores, with no send, against a whole render on one core; (b) panel truth
 * (D3), when the shadow was on through the last motion: the shadow outside what the hand-back redrew (from the exit
 * frame's buffer) against the exit frame rendered whole. */
static bool direct_verify(dial_view_t *v)
{
    const size_t fbytes = (size_t)SCR * SCR * 2, bbytes = (size_t)SCR * DISP_DIRECT_ROWS * 2;
    uint16_t *ref = heap_caps_malloc(fbytes, MALLOC_CAP_SPIRAM), *out = heap_caps_malloc(fbytes, MALLOC_CAP_SPIRAM);
    uint16_t *b0 = heap_caps_malloc(bbytes, MALLOC_CAP_SPIRAM), *b1 = heap_caps_malloc(bbytes, MALLOC_CAP_SPIRAM);
    bool ok = ref && out && b0 && b1;
    if (!ok) printf("DIAL VERIFY DIRECT: no PSRAM for the check\n");
    int x = -1, y = -1;
    uint32_t n = 0;
    const char *part = "pipeline";
    bool shadow = false;
    if (ok) {   /* (a) */
        const vframe_t f = v->prev;
        const rects_t L = { .r = { DV_SCREEN }, .n = 1 };
        v->j_n = omni_band_chunks(&L, DISP_DIRECT_ROWS, SCR, SCR, v->j_chunk, MAX_CHUNKS, NULL);
        v->j_next = 0;
        v->j_fb = NULL;
        v->j_src = NULL;
        v->j_lossy = false;
        v->j_perf = PERF_DEFAULT;
        v->j_dry_out = out;
        v->j_dry_band[0] = b0;
        v->j_dry_band[1] = b1;
        if (!dv_lov_take(v, NULL, true)) printf("DIAL VERIFY DIRECT: no PSRAM for the overlays\n");
        v->j_lov = v->lov;
        v->j_lov_n = v->lov_n;
        dv_prepare(v, &f);
        v->j_dry = true;
        dv_dispatch(v, true);
        v->j_dry = false;
        v->j_lov_n = 0;
        render_whole(v, &f, ref);
        omni_band_overlay(ref, SCR, DV_SCREEN, v->lov, v->lov_n);   /* Task 6: the reference with the overlays */
        n = frame_diff(ref, out, NULL, &x, &y);
    }
    portENTER_CRITICAL(&dial_mux);
    const dv_mode_t mode = v->mode;
    portEXIT_CRITICAL(&dial_mux);
    const uint16_t *sh = disp_direct_shadow_frame();
    bool skip = false;
    if (ok && !n && v->sh_ok && sh && mode == DV_IMAGE) {   /* (b) */
        if (v->sh_whole) {
            skip = true;   /* M3: an abort redraws the whole screen: nothing of the shadow is compared */
        } else {
            shadow = true;
            part = "panel shadow";
            render_whole(v, &v->sh_frame, ref);
            n = frame_diff(ref, sh, &v->sh_patch, &x, &y);
            printf("DIAL VERIFY DIRECT panel: the exit frame; %u px redrawn by the hand-back in %d rects\n",
                   (unsigned)omni_rects_px(&v->sh_patch), v->sh_patch.n);
        }
    }
    if (ok && n)
        printf("DIAL VERIFY DIRECT MISMATCH at (%d, %d) (%s, %u px)\n", x, y, part, (unsigned)n);
    else if (ok && skip)
        printf("DIAL VERIFY DIRECT SKIP (aborted: the last motion ended in an abort, the panel was redrawn whole, no "
               "panel comparison; the pipeline part passed)\n");
    else if (ok)
        printf("DIAL VERIFY DIRECT MATCH%s (overlays %d)\n", shadow ? " (panel shadow)" : "", v->lov_n);
    heap_caps_free(ref);
    heap_caps_free(out);
    heap_caps_free(b0);
    heap_caps_free(b1);
    return ok && !n;
}

/* `dial verify` (R88): each valid buffer (whatever the dirty rects, the drift cache and the half-row frames left in it)
 * against the frame it holds rendered whole, on one core, in 16-row bands. A buffer holding half rows (a flash frame
 * with `perf lossy`) is reported, and excused, since it differs by design. What it cannot see is in dial_view.h. */
static bool job_verify(dial_view_t *v)
{
    if (v->path != DISP_PATH_IMAGE || !v->have_prev) {
        printf("DIAL VERIFY: IMAGE path with a frame only\n");
        return false;
    }
    uint16_t *t = heap_caps_malloc((size_t)SCR * 16 * 2, MALLOC_CAP_SPIRAM);
    if (!t) return false;
    bool all = true;
    for (int b = 0; b < 2; b++) {
        if (!v->buf_valid[b]) continue;
        const vframe_t f = v->bufframe[b];
        dv_prepare(v, &f);
        uint32_t diff = 0;
        int first = -1;
        for (int y0 = 0; y0 < SCR; y0 += 16) {
            const int y1 = dv_imin(y0 + 16, SCR);
            const omni_rect_t band = { 0, (int16_t)y0, SCR, (int16_t)y1 };
            omni_dial_render_prepared(v->prep, (uint16_t *)(void *)((uintptr_t)t - (uintptr_t)y0 * SCR * 2), &band);
            for (int i = 0; i < (y1 - y0) * SCR; i++)
                if (t[i] != v->px[b][(size_t)y0 * SCR + i]) {
                    if (first < 0) first = y0 + i / SCR;
                    if (diff < 64) printf("%s%d,%d", diff ? " " : "DIAL VERIFY px:", i % SCR, y0 + i / SCR);
                    diff++;
                }
        }
        if (diff) printf("\n");
        const bool lossy = omni_rect_area(&v->lossy[b]) > 0;   /* half rows in this buffer: a flash frame, by design */
        printf("DIAL VERIFY %s: buffer %d%s, %u px differ from the whole render (first row %d)%s; lossy box %d,%d..%d,%d\n",
               diff ? "DIFF" : "MATCH", b, b == v->front ? " (shown)" : "", (unsigned)diff, first,
               lossy ? " [half rows: a flash frame, excused]" : "", v->lossy[b].x0, v->lossy[b].y0, v->lossy[b].x1,
               v->lossy[b].y1);
        if (diff && !lossy) all = false;
    }
    if (!all)
        for (uint32_t i = 0; i < 16 && i < v->ntrace; i++) {
            const uint32_t k = (v->ntrace - 1 - i) % 16;
            printf("DIAL TRACE -%u: path %u back %u rects %u rows %d..%d drift %u min %.5f hour %.5f lv %u%u%u%u fast %u\n",
                   (unsigned)i, v->trace[k].path, v->trace[k].back, v->trace[k].nr, v->trace[k].y0, v->trace[k].y1,
                   v->trace[k].dd, (double)v->trace[k].min, (double)v->trace[k].hour, v->trace[k].lv[0], v->trace[k].lv[1],
                   v->trace[k].lv[2], v->trace[k].lv[3], v->trace[k].fast);
        }
    heap_caps_free(t);
    return direct_verify(v) && all;
}
static int ch_diff(uint16_t a, uint16_t b)   /* the largest channel difference, in 8-bit levels */
{
    const int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    const int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    const int dr = abs(((ar << 3) | (ar >> 2)) - ((br << 3) | (br >> 2)));
    const int dg = abs(((ag << 2) | (ag >> 4)) - ((bg << 2) | (bg >> 4)));
    const int db = abs(((ab << 3) | (ab >> 2)) - ((bb << 3) | (bb >> 2)));
    return dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
}

bool dial_view_overlay_check(dial_view_t *v)
{
    portENTER_CRITICAL(&dial_mux);
    /* the front buffer is what the image shows: the worker writes only the back one, and poll (which swaps) waits for
     * the LVGL lock this console job holds */
    const bool rest = v->path == DISP_PATH_IMAGE && v->mode == DV_IMAGE && v->buf_valid[v->front];
    portEXIT_CRITICAL(&dial_mux);
    if (!rest || !v->shown) {
        printf("DIAL OVERLAY: on the IMAGE path at rest only (retry)\n");
        return false;
    }
    if (!dv_lov_publish(v, false)) {
        printf("DIAL OVERLAY: the worker is copying the overlays; retry\n");
        return false;
    }
    if (v->lov_pub_n == 0) {
        printf("DIAL OVERLAY: no registered overlay is visible\n");
        return false;
    }
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);   /* LVGL's render, as shot */
    uint16_t *row = heap_caps_malloc((size_t)SCR * 2, MALLOC_CAP_SPIRAM);
    if (snap == NULL || row == NULL || snap->header.w != SCR || snap->header.h != SCR) {
        printf("DIAL OVERLAY: no PSRAM for the screen's snapshot\n");
        if (snap) lv_draw_buf_destroy(snap);
        heap_caps_free(row);
        return false;
    }
    const uint16_t *px = v->px[v->front];
    for (int i = 0; i < v->lov_pub_n; i++) {
        const omni_rect_t r = omni_band_even(v->lov_pub[i].r, SCR, SCR);
        int dmax = 0, mx = -1, my = -1;
        uint16_t mb = 0, ml = 0;
        uint32_t ndiff = 0;
        for (int y = r.y0; y < r.y1; y++) {
            const omni_rect_t c = { r.x0, (int16_t)y, r.x1, (int16_t)(y + 1) };
            memcpy(row + r.x0, px + (size_t)y * SCR + r.x0, (size_t)(r.x1 - r.x0) * 2);
            omni_band_overlay(row, SCR, c, v->lov_pub, v->lov_pub_n);   /* every overlay, as the bands blend them */
            const uint16_t *lv = (const uint16_t *)(const void *)(snap->data + (size_t)y * snap->header.stride);
            for (int x = r.x0; x < r.x1; x++) {
                const int d = ch_diff(row[x], lv[x]);
                ndiff += d != 0;
                if (d > dmax) {
                    dmax = d;
                    mx = x;
                    my = y;
                    mb = row[x];
                    ml = lv[x];
                }
            }
        }
        printf("DIAL OVERLAY max diff %d in %u px (overlay %d: box %d,%d..%d,%d, %d px compared; max at %d,%d: "
               "bands %04x, LVGL %04x)\n", dmax, (unsigned)ndiff, i, r.x0, r.y0, r.x1, r.y1, (int)omni_rect_area(&r),
               mx, my, mb, ml);
    }
    lv_draw_buf_destroy(snap);
    heap_caps_free(row);
    return true;
}

bool dial_view_selftest(dial_view_t *v, bool ring_only) { return run_job(v, ring_only ? JOB_RINGTEST : JOB_SELFTEST); }


#if CONFIG_OMNI_DIAL_PERF_BOUNDS
static void ring_none(const omni_strip_t *st, const omni_pmap_t *m, const omni_pspan_t *sp, uint32_t off, uint16_t *dst)
{
    (void)st, (void)m, (void)sp, (void)off, (void)dst;
}
#endif

/* n renders of rects L of frame f into fb, on one core or both: average us, *mx the longest. */
static uint32_t time_rects(dial_view_t *v, const vframe_t *f, const rects_t *L, uint16_t *fb, bool both, int n,
                           uint32_t *mx)
{
    uint64_t sum = 0;
    *mx = 0;
    for (int i = 0; i < n; i++) {
        const int64_t t0 = esp_timer_get_time();
        dv_prepare(v, f);
        v->j_fb = fb;
        v->j_stage = 0;   /* T7 M6: a whole render (a drift frame may have left the over stage set) */
        v->j_lossy = false;
        dv_set_chunks(v, L, CHUNK_ROWS);
        dv_dispatch(v, both);
        const uint32_t us = dv_us_since(t0);
        sum += us;
        if (us > *mx) *mx = us;
        vTaskDelay(1);
    }
    return (uint32_t)(sum / (uint64_t)n);
}

static void bench_line(dial_view_t *v, const char *name, const vframe_t *a, const vframe_t *b, uint16_t *fb)
{
    rects_t L;
    bool drift;
    if (a) dv_frame_dirty(v, a, b, &L, &drift);
    else L = (rects_t){ .r = { DV_SCREEN }, .n = 1 };
    uint32_t m1, m2;
    const uint32_t t1 = time_rects(v, b, &L, fb, false, 10, &m1), t2 = time_rects(v, b, &L, fb, true, 10, &m2);
    printf("DIAL BENCH %s: %u px in %d rects; one core %.2f ms (max %.2f), both %.2f ms (max %.2f)\n", name,
           (unsigned)omni_rects_px(&L), L.n, t1 / 1000.0, m1 / 1000.0, t2 / 1000.0, m2 / 1000.0);
}

static void job_bench(dial_view_t *v)
{
    uint16_t *fb = heap_caps_malloc((size_t)SCR * SCR * 2, MALLOC_CAP_SPIRAM);
    if (!fb) {
        printf("DIAL BENCH: no PSRAM for a frame\n");
        return;
    }
    portENTER_CRITICAL(&dial_mux);
    omni_dial_state_t st = v->last;
    const bool have = v->has_last;
    portEXIT_CRITICAL(&dial_mux);
    if (!have) memset(&st, 0, sizeof st);
    memset(st.speed, 0, sizeof st.speed);
    st.lon_speed = 0.0733f;   /* drifting: 2.2 deg/s at 30 fps */
    const vframe_t rest0 = dv_make_frame(v, &st);
    omni_dial_state_t s1 = st;
    s1.lon_deg -= 0.0733f;
    const vframe_t rest1 = dv_make_frame(v, &s1);
    bench_line(v, "full frame (globe in motion)", NULL, &rest0, fb);
    omni_dial_state_t sh = st;
    sh.lon_speed = 0;
    const vframe_t still = dv_make_frame(v, &sh);
    bench_line(v, "full frame (still: linear globe)", NULL, &still, fb);
    bench_line(v, "rest frame (globe drifting 33 ms)", &rest0, &rest1, fb);
    if (v->ucache) {   /* R88 (2): the same step by the drift path (cache restored, over stage, markers) */
        uint64_t sum = 0;
        for (int i = 0; i < 10; i++) {
            const int64_t t0 = esp_timer_get_time();
            dv_prepare(v, &rest1);
            v->j_fb = fb;
            for (int k = 0; k < v->chord.n; k++) {
                const omni_rect_t *b = &v->chord.r[k];
                const int w = b->x1 - b->x0;
                for (int y = b->y0; y < b->y1; y++)
                    memcpy(fb + (size_t)y * SCR + b->x0, dv_ucell(v, b->x0, y), (size_t)w * 2);
            }
            v->j_stage = 2;
            dv_set_chunks(v, &v->chord, CHUNK_ROWS);
            dv_dispatch(v, true);
            rects_t M = { .n = 0 };
            omni_rects_add(&M, rest0.mbox);
            omni_rects_add(&M, rest1.mbox);
            v->j_stage = 0;
            dv_set_chunks(v, &M, CHUNK_ROWS);
            dv_dispatch(v, true);
            sum += dv_us_since(t0);
            vTaskDelay(1);
        }
        printf("DIAL BENCH rest frame by the drift path: both %.2f ms (%u chord px)\n", sum / 10 / 1000.0,
               (unsigned)omni_rects_px(&v->chord));
    }
    /* the rest frame's cost broken down: no globe (and no marker), the C motion kernel, the texture in a PSRAM copy */
    {
        const omni_globe_clut_t *clut = v->res.clut;
        const rects_t L = { .r = { { 121, 121, 345, 345 } }, .n = 1 };
        uint32_t m1, m2, t1, t2;
        v->res.clut = NULL;
        t1 = time_rects(v, &rest1, &L, fb, false, 10, &m1);
        t2 = time_rects(v, &rest1, &L, fb, true, 10, &m2);
        v->res.clut = clut;
        printf("DIAL BENCH rest rect without the globe: one core %.2f ms, both %.2f ms\n", t1 / 1000.0, t2 / 1000.0);
        const omni_globe_cspan_fn km = v->kmove;
        v->kmove = omni_globe_cspan_fast;
        t1 = time_rects(v, &rest1, &L, fb, false, 10, &m1);
        t2 = time_rects(v, &rest1, &L, fb, true, 10, &m2);
        v->kmove = km;
        printf("DIAL BENCH rest rect, C kernel (cspan_fast): one core %.2f ms, both %.2f ms\n", t1 / 1000.0, t2 / 1000.0);
        const size_t tb = (size_t)v->tex.w * v->tex.h * 4;
        uint32_t *copy = heap_caps_malloc(tb, MALLOC_CAP_SPIRAM);
        if (copy) {
            const uint32_t *keep = v->tex.px;
            memcpy(copy, keep, tb);
            v->tex.px = copy;
            t1 = time_rects(v, &rest1, &L, fb, false, 10, &m1);
            t2 = time_rects(v, &rest1, &L, fb, true, 10, &m2);
            v->tex.px = keep;
            heap_caps_free(copy);
            printf("DIAL BENCH rest rect, texture in a PSRAM copy (%u B): one core %.2f ms, both %.2f ms\n", (unsigned)tb,
                   t1 / 1000.0, t2 / 1000.0);
        }
    }
    omni_dial_state_t sl = s1;
    for (int i = 0; i < OMNI_RINGS; i++) sl.val[i] += 0.05f, sl.speed[i] = 0.05f;
    const vframe_t slow = dv_make_frame(v, &sl);
    bench_line(v, "slow ring turn (0.05 slot, drifting)", &rest1, &slow, fb);
#if CONFIG_OMNI_DIAL_PERF_BOUNDS
    {   /* R88/R91: the slow frame's cost broken down, both cores: one part removed at a time (upper bounds) */
        rects_t L;
        bool drift;
        dv_frame_dirty(v, &rest1, &slow, &L, &drift);
        uint32_t m, t;
        omni_dial_res_t keep = v->res;
        t = time_rects(v, &slow, &L, fb, true, 5, &m);
        printf("DIAL BENCH slow parts: all %.2f ms", t / 1000.0);
        v->res.rkernel = ring_none;
        t = time_rects(v, &slow, &L, fb, true, 5, &m);
        printf(", no ring kernel %.2f", t / 1000.0);
        v->res = keep;
        v->res.top.ov.nspans = v->res.over.ov.nspans = v->res.dome.ov.nspans = 0;
        t = time_rects(v, &slow, &L, fb, true, 5, &m);
        printf(", no top/over/dome %.2f", t / 1000.0);
        v->res = keep;
        v->res.clut = NULL;
        t = time_rects(v, &slow, &L, fb, true, 5, &m);
        printf(", no globe %.2f", t / 1000.0);
        v->res = keep;
        v->res.mag_base = NULL;
        t = time_rects(v, &slow, &L, fb, true, 5, &m);
        printf(", no magnifier %.2f", t / 1000.0);
        v->res = keep;
        for (int i = 0; i < 4; i++) v->res.lens[i].ov.nspans = 0;
        v->res.bloom[0].a = v->res.bloom[1].a = NULL;
        t = time_rects(v, &slow, &L, fb, true, 5, &m);
        printf(", no lenses %.2f", t / 1000.0);
        v->res.rkernel = ring_none;
        v->res.top.ov.nspans = v->res.over.ov.nspans = v->res.dome.ov.nspans = 0;
        v->res.clut = NULL;
        v->res.mag_base = NULL;
        t = time_rects(v, &slow, &L, fb, true, 5, &m);
        printf(", none of those %.2f", t / 1000.0);
        v->res = keep;
        printf(" ms\n");
        v->res = keep;
    }
#endif
    omni_dial_state_t w0 = s1, w1 = s1;
    for (int i = 0; i < OMNI_RINGS; i++) w0.speed[i] = w1.speed[i] = 3, w1.val[i] += 3;
    w0.lon_speed = w1.lon_speed = FAST_LON;
    w1.lon_deg = fmodf(w1.lon_deg + FAST_LON + 540.0f, 360.0f) - 180.0f;
    const vframe_t wa = dv_make_frame(v, &w0), wb = dv_make_frame(v, &w1);
    bench_line(v, "whirl (3 slots, L2, fast, globe 20 deg)", &wa, &wb, fb);
    w1.flash = 0.3f;
    const vframe_t wf = dv_make_frame(v, &w1);
    bench_line(v, "whirl with a 0.3 flash", &wa, &wf, fb);
    /* T6 M4: the hands drawn per 16-row band (as omni_dial_render does) against once over their whole box */
    {
        omni_rect_t hb, mb;
        omni_hand_bbox(st.hour_rad, true, &hb);
        omni_hand_bbox(st.min_rad, false, &mb);
        const omni_rect_t u = omni_rect_unite(hb, mb);
        int64_t t0 = esp_timer_get_time();
        int bands = 0;
        for (int k = 0; k < 10; k++)
            for (int y = 0; y < SCR; y += 16, bands += k == 0) {
                const omni_rect_t b = { 0, (int16_t)y, SCR, (int16_t)dv_imin(y + 16, SCR) };
                omni_draw_hand(fb + (size_t)y * SCR, SCR, &b, st.hour_rad, true, true, 1, 0, 0, 0, 0);
                omni_draw_hand(fb + (size_t)y * SCR, SCR, &b, st.min_rad, false, true, 1, 0, 0, 0, 0);
            }
        const uint32_t per_band = dv_us_since(t0) / 10;
        t0 = esp_timer_get_time();
        for (int k = 0; k < 10; k++) {
            omni_draw_hand(fb + (size_t)u.y0 * SCR + u.x0, SCR, &u, st.hour_rad, true, true, 1, 0, 0, 0, 0);
            omni_draw_hand(fb + (size_t)u.y0 * SCR + u.x0, SCR, &u, st.min_rad, false, true, 1, 0, 0, 0, 0);
        }
        const uint32_t once = dv_us_since(t0) / 10;
        printf("DIAL BENCH hands (M4): both hands per 16-row band over the screen (%d bands) %.2f ms, once over their "
               "box %.2f ms (one core)\n", bands, per_band / 1000.0, once / 1000.0);
    }
    /* R85: a loupe map and base built (what a rebuild per frame of the opening would cost), and the loupe open */
    {
        printf("DIAL BENCH loupe: %d sizes built with the view, %.2f ms each on average (max %.2f; one core each)\n",
               LOUPE_STEPS, v->loupe_us_sum / 1000.0 / LOUPE_STEPS, v->loupe_us_max / 1000.0);
        omni_dial_state_t so = s1;
        so.loupe = 1;
        const vframe_t lo = dv_make_frame(v, &so);
        bench_line(v, "loupe open (from the rest frame)", &rest1, &lo, fb);
        omni_dial_state_t so2 = so;
        so2.lon_deg -= 0.0733f;
        const vframe_t lo2 = dv_make_frame(v, &so2);
        bench_line(v, "rest frame with the loupe open (globe drifting 33 ms)", &lo, &lo2, fb);
        so.loupe = 0.5f;
        const vframe_t lh = dv_make_frame(v, &so);
        bench_line(v, "loupe opening (a 0.5, from open)", &lo, &lh, fb);
    }
    /* Task 9: the lid's full frames at 0, 30 and 60 deg (the globe spinning up under it); the prepare alone; with the
     * dial drawn under the opaque lid too (no runs); and the lid read from a PSRAM copy instead of the flash mapping */
    if (v->res.lid) {
        static const float DEG[3] = { 0, 30, 60 };
        for (int i = 0; i < 3; i++) {
            omni_dial_state_t sd = s1;   /* the rings still (the flip has no settle), the globe spinning up */
            sd.lon_speed = FAST_LON;
            sd.lid = true;
            sd.lid_theta = DEG[i] * (float)M_PI / 180.0f;
            sd.fade = 0;
            const vframe_t lf = dv_make_frame(v, &sd);
            char name[48];
            snprintf(name, sizeof name, "lid at %.0f deg (globe fast)", (double)DEG[i]);
            bench_line(v, name, NULL, &lf, fb);
            int64_t t0 = esp_timer_get_time();
            for (int k = 0; k < 10; k++) dv_prepare(v, &lf);
            printf("DIAL BENCH lid at %.0f deg: prepare %.2f ms\n", (double)DEG[i], dv_us_since(t0) / 10 / 1000.0);
            const int16_t *runs = v->res.lid_run;
            v->res.lid_run = NULL;
            snprintf(name, sizeof name, "lid at %.0f deg, dial drawn under it", (double)DEG[i]);
            bench_line(v, name, NULL, &lf, fb);
            v->res.lid_run = runs;
            const uint8_t *keep = v->res.lid;
            uint8_t *copy = heap_caps_malloc((size_t)SCR * SCR * 3, MALLOC_CAP_SPIRAM);
            if (copy) {
                memcpy(copy, keep, (size_t)SCR * SCR * 3);
                v->res.lid = copy;
                snprintf(name, sizeof name, "lid at %.0f deg, lid in a PSRAM copy", (double)DEG[i]);
                bench_line(v, name, NULL, &lf, fb);
                v->res.lid = keep;
                heap_caps_free(copy);
            }
            const omni_globe_clut_t *clut = v->res.clut;
            v->res.clut = NULL;
            snprintf(name, sizeof name, "lid at %.0f deg, no globe", (double)DEG[i]);
            bench_line(v, name, NULL, &lf, fb);
            v->res.clut = clut;
        }
    }
    heap_caps_free(fb);
    printf("DIAL BENCH PSRAM free %u (min %u)\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

/* Task 10: the iris's full-disc frames (OMNI_IRIS_BOX, the rect omni_dial_dirty gives an iris frame) at ap 0, 50 and
 * 150, the globe spinning up under it; the prepare alone; without the globe; with the dial drawn under the blades too
 * (no opaque skip: the iris drawn over a whole dial frame, as a plain overlay would be). */
static void job_bench_iris(dial_view_t *v)
{
    uint16_t *fb = heap_caps_malloc((size_t)SCR * SCR * 2, MALLOC_CAP_SPIRAM);
    omni_iris_t *ip = heap_caps_malloc(sizeof *ip, MALLOC_CAP_SPIRAM);
    if (!fb || !ip) {
        printf("DIAL BENCH iris: no PSRAM\n");
        heap_caps_free(fb);
        heap_caps_free(ip);
        return;
    }
    portENTER_CRITICAL(&dial_mux);
    omni_dial_state_t st = v->last;
    const bool have = v->has_last;
    portEXIT_CRITICAL(&dial_mux);
    if (!have) memset(&st, 0, sizeof st);
    memset(st.speed, 0, sizeof st.speed);
    st.lon_speed = FAST_LON;   /* the spin-up under the opening iris */
    st.lid = false;
    st.fade = 0;
    const rects_t L = { .r = { OMNI_IRIS_BOX }, .n = 1 };
    static const float AP[3] = { 0, 50, 150 };
    for (int i = 0; i < 3; i++) {
        omni_dial_state_t si = st;
        si.iris = true;
        si.iris_ap = AP[i];
        si.iris_phi = AP[i] / OMNI_IRIS_AP_OPEN * 62 * (float)M_PI / 180;
        si.iris_s = AP[i] / OMNI_IRIS_AP_OPEN;
        const vframe_t f = dv_make_frame(v, &si);
        uint32_t m1, m2;
        uint32_t t1 = time_rects(v, &f, &L, fb, false, 10, &m1), t2 = time_rects(v, &f, &L, fb, true, 10, &m2);
        printf("DIAL BENCH iris at ap %.0f (globe fast): %u px; one core %.2f ms (max %.2f), both %.2f ms (max %.2f)\n",
               (double)AP[i], (unsigned)omni_rects_px(&L), t1 / 1000.0, m1 / 1000.0, t2 / 1000.0, m2 / 1000.0);
        int64_t t0 = esp_timer_get_time();
        for (int k = 0; k < 10; k++) omni_iris_prepare(ip, si.iris_ap, si.iris_phi, si.iris_s, v->res.iris_grain);
        printf("DIAL BENCH iris at ap %.0f: iris prepare %.2f ms, ", (double)AP[i], dv_us_since(t0) / 10 / 1000.0);
        t0 = esp_timer_get_time();
        for (int k = 0; k < 10; k++) {   /* the iris alone over the box, one core (the frame holds the last render) */
            const omni_rect_t b = OMNI_IRIS_BOX;
            omni_iris_draw_prepared(ip, fb + (size_t)b.y0 * SCR + b.x0, SCR, &b);
        }
        printf("iris draw alone (one core) %.2f ms\n", dv_us_since(t0) / 10 / 1000.0);
        const omni_globe_clut_t *clut = v->res.clut;
        v->res.clut = NULL;
        t2 = time_rects(v, &f, &L, fb, true, 10, &m2);
        v->res.clut = clut;
        printf("DIAL BENCH iris at ap %.0f, no globe: both %.2f ms (max %.2f)\n", (double)AP[i], t2 / 1000.0, m2 / 1000.0);
        omni_dial_state_t sp = si;   /* the dial under the blades too: the plain frame, then the iris over it */
        sp.iris = false;
        const vframe_t fp = dv_make_frame(v, &sp);
        uint64_t sum = 0;
        for (int k = 0; k < 10; k++) {
            t0 = esp_timer_get_time();
            dv_prepare(v, &fp);
            v->j_fb = fb;
            v->j_stage = 0;
            v->j_lossy = false;
            dv_set_chunks(v, &L, CHUNK_ROWS);
            dv_dispatch(v, true);
            omni_iris_prepare(ip, si.iris_ap, si.iris_phi, si.iris_s, v->res.iris_grain);
            const omni_rect_t b = OMNI_IRIS_BOX;
            omni_iris_draw_prepared(ip, fb + (size_t)b.y0 * SCR + b.x0, SCR, &b);
            sum += dv_us_since(t0);
            vTaskDelay(1);
        }
        printf("DIAL BENCH iris at ap %.0f, dial drawn whole under it (both, then the iris on one): %.2f ms\n",
               (double)AP[i], sum / 10 / 1000.0);
    }
    heap_caps_free(fb);
    heap_caps_free(ip);
    printf("DIAL BENCH PSRAM free %u (min %u)\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

/* R88: the drift path's composition against the whole render, on the device's kernels: a frame X whole, then frame Y
 * (the globe 0.37 deg on, the marker with it) composed as drift_render does (under and over over the chord pieces,
 * the hole kept, both markers whole) must be Y's whole render, band for band; with the loupe shut and open. */
static bool drift_check(dial_view_t *v, char *why, size_t why_n)
{
    uint16_t *a = heap_caps_malloc((size_t)SCR * SCR * 2, MALLOC_CAP_SPIRAM);
    uint16_t *t = heap_caps_malloc((size_t)SCR * 16 * 2, MALLOC_CAP_SPIRAM);
    omni_dial_prep_t *py = heap_caps_malloc(sizeof *py, MALLOC_CAP_SPIRAM);
    bool ok = a && t && py;
    if (!ok) snprintf(why, why_n, "no PSRAM for the drift check");
    portENTER_CRITICAL(&dial_mux);
    omni_dial_state_t st = v->last;
    portEXIT_CRITICAL(&dial_mux);
    memset(st.speed, 0, sizeof st.speed);
    for (int pass = 0; pass < 2 && ok; pass++) {
        st.loupe = (float)pass;
        st.lon_speed = 0.37f;
        const vframe_t x = dv_make_frame(v, &st);
        omni_dial_state_t sy = st;
        sy.lon_deg -= 0.37f;
        const vframe_t y = dv_make_frame(v, &sy);
        const omni_rect_t all = DV_SCREEN;
        dv_prepare(v, &x);   /* the loupe size and kernels are x's, and y's (the same loupe, drifting) */
        omni_dial_render_prepared(v->prep, a, &all);
        omni_dial_prepare(&y.f, py);
        omni_rect_t hole = { 0, 0, 0, 0 };
        if (x.f.st.loupe >= 1) hole = omni_dial_loupe_cfg(1, v->res.dial_map.cfg.n_theta).rect;
        rects_t P = { .n = 0 };
        if (!omni_rects_minus(&v->chord, &hole, &P)) {   /* M7: the view renders such a frame whole */
            snprintf(why, why_n, "chord bands less the loupe overflow %d rects", OMNI_RECTS_MAX);
            ok = false;
            break;
        }
        for (int i = 0; i < P.n; i++) omni_dial_render_under(py, a, &P.r[i]);
        for (int i = 0; i < P.n; i++) omni_dial_render_over(py, a, &P.r[i]);
        omni_dial_render_prepared(py, a, &x.mbox);
        omni_dial_render_prepared(py, a, &y.mbox);
        for (int y0 = 0; y0 < SCR && ok; y0 += 16) {
            const int y1 = y0 + 16 < SCR ? y0 + 16 : SCR;
            const omni_rect_t b = { 0, (int16_t)y0, SCR, (int16_t)y1 };
            omni_dial_render_prepared(py, (uint16_t *)(void *)((uintptr_t)t - (uintptr_t)y0 * SCR * 2), &b);
            if (memcmp(t, a + (size_t)y0 * SCR, (size_t)(y1 - y0) * SCR * 2) != 0) {
                ok = false;
                snprintf(why, why_n, "drift composition differs from the whole render in rows %d..%d (loupe %d)", y0, y1, pass);
            }
        }
        vTaskDelay(1);
    }
    if (ok) printf("DIAL DRIFT MATCH: the drift path's composition equals the whole render (loupe shut and open)\n");
    heap_caps_free(a);
    heap_caps_free(t);
    heap_caps_free(py);
    return ok;
}

static bool job_selftest(dial_view_t *v, bool ring_only)
{
    char why[112] = "no memory";
    bool ok = true;
    /* rings: the view's map (B, loaded) against an A+B build of its cfg, by digest; the codes kernel against the LUT
     * kernel on every span, 3 levels x 16 offsets; the synthetic digest */
    const omni_pmap_cfg_t *mc = &v->res.dial_map.cfg;
    const size_t fbytes = omni_pmap_bytes(mc, true, true);
    void *fmem = heap_caps_malloc(fbytes, MALLOC_CAP_SPIRAM);
    const int w = mc->rect.x1 - mc->rect.x0;
    uint16_t *scratch = heap_caps_malloc((size_t)2 * (SCR + OMNI_KCHECK_GUARD) * 2, MALLOC_CAP_SPIRAM);
    const size_t sb = omni_ring_synth_bytes() > omni_globe_csynth_bytes() ? omni_ring_synth_bytes() : omni_globe_csynth_bytes();
    void *smem = heap_caps_malloc(sb, MALLOC_CAP_SPIRAM);
    omni_pmap_t full;
    ok = fmem && scratch && smem && w <= SCR;
    if (ok) {
        const int64_t t0 = esp_timer_get_time();
        ok = omni_pmap_build(&full, mc, true, true, fmem, fbytes);
        const uint64_t dv = omni_pmap_digest(&v->res.dial_map), db = ok ? omni_pmap_digest(&full) : 0;
        ok = ok && dv == db;
        printf("RING MAP %s: view's map (%s) digest %016llx, built here (%u ms) %016llx\n", ok ? "MATCH" : "MISMATCH",
               v->map_loaded ? "loaded from " ASSET_OMNI_DIALMAP : "built at create", (unsigned long long)dv,
               (unsigned)(dv_us_since(t0) / 1000), (unsigned long long)db);
        if (!ok) snprintf(why, sizeof why, "dial map digest");
        vTaskDelay(1);
    }
    int pass = 0, n = 0;
    uint32_t seed = 0x1234567;
    for (int l = 0; l < 3 && ok; l++)
        for (int k = 0; k < 16 && ok; k++, n++) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            ok = omni_ring_kernel_bitexact(omni_rings_span_codes, v->res.strips[l], &full, seed, scratch, why, sizeof why);
            pass += ok;
            vTaskDelay(1);
        }
    uint64_t hl = 0, hc = 0;
    if (ok) {
        hl = omni_ring_synth_digest(omni_rings_span_lut, smem, sb);
        hc = omni_ring_synth_digest(omni_rings_span_codes, smem, sb);
        ok = hl == OMNI_RING_SYNTH_DIGEST && hc == OMNI_RING_SYNTH_DIGEST;
        if (!ok) snprintf(why, sizeof why, "synth digest lut %016llx codes %016llx (pinned %016llx)",
                          (unsigned long long)hl, (unsigned long long)hc, (unsigned long long)OMNI_RING_SYNTH_DIGEST);
    }
    if (ok) printf("RING SELFTEST PASS %d/%d (3 levels x 16 offsets, synth %016llx, pinned %016llx)\n", pass, n,
                   (unsigned long long)hl, (unsigned long long)OMNI_RING_SYNTH_DIGEST);
    else printf("RING SELFTEST FAIL %d/%d: %s\n", pass, n, why);
    heap_caps_free(fmem);
    if (ok && !ring_only) {   /* the globe: PIE against cspan_ref on this view's compact LUT, digests, the shade range */
        static const double LON[5] = { -179.9, -95.87, 0, 60, 179.9 };
        int gp = 0;
        for (int i = 0; i < 5 && ok; i++) {
            omni_globe_frame_t gf = v->res.globe;
            gf.lon0_deg = LON[i];
            char tag[24];
            snprintf(tag, sizeof tag, "lon0 %.2f", LON[i]);
            ok = omni_globe_ckernel_bitexact(globe_cspan_pie, &gf, scratch, tag, why, sizeof why);
            gp += ok;
            vTaskDelay(1);
        }
        uint64_t cr = 0, cp = 0;
        if (ok) {
            cr = omni_globe_csynth_digest(omni_globe_cspan_ref, smem, sb);
            cp = omni_globe_csynth_digest(globe_cspan_pie, smem, sb);
            ok = cr == OMNI_GLOBE_CSYNTH_DIGEST && cp == OMNI_GLOBE_CSYNTH_DIGEST;
            if (!ok) snprintf(why, sizeof why, "compact synth ref %016llx pie %016llx", (unsigned long long)cr,
                              (unsigned long long)cp);
        }
        const bool shade_ok = globe_pie_shade_ok(v->shade);
        ok = ok && shade_ok;
        if (!shade_ok) snprintf(why, sizeof why, "the Omni shade table is outside the PIE kernel's exact range");
        printf("DIAL globe: PIE vs cspan_ref %d/5, compact synth %016llx %016llx (pinned %016llx), shade %s for PIE, "
               "clut digest %016llx, shade digest %016llx\n",
               gp, (unsigned long long)cr, (unsigned long long)cp, (unsigned long long)OMNI_GLOBE_CSYNTH_DIGEST,
               shade_ok ? "ok" : "NOT ok", (unsigned long long)omni_globe_clut_digest(&v->clut),
               (unsigned long long)omni_globe_shade_digest(v->shade));
        if (ok) ok = drift_check(v, why, sizeof why);
        if (ok) printf("DIAL SELFTEST PASS\n");
        else printf("DIAL SELFTEST FAIL: %s\n", why);
    }
    heap_caps_free(scratch);
    heap_caps_free(smem);
    return ok;
}
bool dv_job(dial_view_t *v, job_t job)
{
    if (job == JOB_BENCH) {
        job_bench(v);
        return true;
    }
    if (job == JOB_BENCH_IRIS) {
        job_bench_iris(v);
        return true;
    }
    if (job == JOB_VERIFY) return job_verify(v);
    return job_selftest(v, job == JOB_RINGTEST);
}

/* ------------------------------------------------------------ the `ring` and `dial` commands (watch_ui's console core
 * held them until Stage 4b Task 5) */
/* ring selftest: the ring kernels bit for bit on the Omni dial view's resources (dial_view_selftest, the ring part:
 * "RING SELFTEST PASS"). Needs the Omni face shown (its view built). */
static int cmd_ring(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "selftest") != 0) {
        printf("usage: ring selftest (on the Omni face)\n");
        return 1;
    }
    bsp_display_lock(-1);
    dial_view_t *v = dial_view_current();
    const bool ok = v && dial_view_selftest(v, true);
    if (!v) printf("RING SELFTEST FAIL: no dial view (face omni first)\n");
    bsp_display_unlock();
    return ok ? 0 : 1;
}

/* dial [hold <lon>|off | lid <deg>|off | bench | selftest | verify | path image|band | scen off|slow|whirl|flash |
 * perf <c> on|off | direct [reset|lose] | overlay check]: the Omni dial view (dial_view_current). Plain `dial`: its
 * stats, the lid frames' since the last `dial`, stack marks and PSRAM free. lid: the lid held at an angle (Task 9's
 * shots). path: the path the next view is made on (then `face digital`, `face omni`; one view only). scen: the
 * measurement overlay on the submitted states. perf: a kept candidate off for a measurement (tex: the next view; drift,
 * lossy: this view). direct: lever B's counters (Task 5). overlay check: the LVGL overlays' blend against LVGL's
 * (Task 6). */
static int cmd_dial(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    int rc = 0;
    bsp_display_lock(-1);
    dial_view_t *v = dial_view_current();
    if (strcmp(sub, "perf") == 0 && argc == 4) {
        static const char *const N[] = { "tex", "drift", "lossy", "render", "base" };   /* bits 0..4 */
        int k = -1;
        for (int i = 0; i < 5; i++)
            if (strcmp(argv[2], N[i]) == 0) k = i;
        if (k < 0 || (strcmp(argv[3], "on") != 0 && strcmp(argv[3], "off") != 0)) {
            printf("usage: dial perf tex|drift|lossy|render|base on|off (tex, base: the next view; the others: this view)\n");
            rc = 1;
        } else if (k > 0 && k < 4 && !v) {
            printf("DIAL no view (face omni first)\n");
            rc = 1;
        } else {
            printf("DIAL perf 0x%x\n", dial_view_perf(v, k, strcmp(argv[3], "on") == 0));
        }
    } else if (strcmp(sub, "path") == 0 && argc == 3 && (strcmp(argv[2], "image") == 0 || strcmp(argv[2], "band") == 0)) {
        dial_view_set_path(argv[2][0] == 'i' ? DISP_PATH_IMAGE : DISP_PATH_BAND);
        printf("DIAL path %s (the next view only)\n", argv[2]);
    } else if (!v) {
        printf("DIAL no view (face omni first)\n");
        rc = 1;
    } else if (argc == 1) {
        dial_view_print(v);
    } else if (strcmp(sub, "hold") == 0 && argc == 3) {
        const bool off = strcmp(argv[2], "off") == 0;
        dial_view_hold(v, off ? NAN : (float)atof(argv[2]));
        printf("DIAL hold %s\n", off ? "off" : argv[2]);
    } else if (strcmp(sub, "lid") == 0 && argc == 3) {   /* Task 9: the lid held at an angle, for shots */
        const bool off = strcmp(argv[2], "off") == 0;
        dial_view_lid(v, off ? NAN : (float)(atof(argv[2]) * M_PI / 180.0));
        printf("DIAL lid %s\n", off ? "off" : argv[2]);
    } else if (strcmp(sub, "iris") == 0 && argc == 3) {   /* Task 10: the iris held at a time, for shots */
        const bool off = strcmp(argv[2], "off") == 0;
        dial_view_iris(v, off ? -1 : (int32_t)atoi(argv[2]));
        printf("DIAL iris %s\n", off ? "off" : argv[2]);
    } else if (strcmp(sub, "bench") == 0 && argc == 3 && strcmp(argv[2], "iris") == 0) {
        dial_view_bench_iris(v);
    } else if (strcmp(sub, "direct") == 0 && argc == 3 && strcmp(argv[2], "lose") == 0) {
        dial_view_direct_lose(v);
        printf("DIAL direct: the next direct chunk is lost\n");
    } else if (strcmp(sub, "direct") == 0 && (argc == 2 || (argc == 3 && strcmp(argv[2], "reset") == 0))) {
        dial_view_direct(v, argc == 3);
    } else if (strcmp(sub, "verify") == 0 && argc == 2) {
        rc = dial_view_verify(v) ? 0 : 1;
    } else if (strcmp(sub, "overlay") == 0 && argc == 3 && strcmp(argv[2], "check") == 0) {   /* Task 6 (C5) */
        rc = dial_view_overlay_check(v) ? 0 : 1;
    } else if (strcmp(sub, "bench") == 0 && argc == 2) {
        dial_view_bench(v);
    } else if (strcmp(sub, "selftest") == 0 && argc == 2) {
        rc = dial_view_selftest(v, false) ? 0 : 1;
    } else if (strcmp(sub, "scen") == 0 && argc == 3) {
        static const char *const N[] = { "off", "slow", "whirl", "flash" };
        int k = -1;
        for (int i = 0; i < 4; i++)
            if (strcmp(argv[2], N[i]) == 0) k = i;
        if (k < 0) {
            printf("usage: dial scen off|slow|whirl|flash\n");
            rc = 1;
        } else {
            dial_view_scenario(v, (dial_scen_t)k);
            printf("DIAL scen %s\n", argv[2]);
        }
    } else {
        printf("usage: dial [hold <lon>|off | lid <deg>|off | bench | selftest | verify | path image|band | "
               "scen off|slow|whirl|flash | "
               "perf tex|drift|lossy|render|base on|off | iris <ms>|off | bench iris | direct [reset|lose] | "
               "overlay check]\n"
               "  verify checks each buffer against its frame rendered whole; it cannot see the panel (pixels never\n"
               "  invalidated), errors common to both sides (same prepare and kernels: see selftest), frames shown before\n"
               "  the check, or a flash frame's half rows (excused); its DIRECT line checks the direct path's\n"
               "  pipeline, and the panel itself after the last motion when `disp direct shadow on` was set\n"
               "  through it\n");
        rc = 1;
    }
    bsp_display_unlock();
    printf("DIAL psram free %u (min %u; internal %u, min %u)\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    return rc;
}

/* The Omni's console hook registers these (omni_theme.c). */
void dial_console_register(void)
{
    debug_console_reg("ring", "ring selftest: the ring kernels bit for bit on the Omni dial view", &cmd_ring);
    debug_console_reg("dial", "dial [hold <lon>|off | lid <deg>|off | iris <ms>|off | bench [iris] | selftest | verify | "
        "path image|band | scen off|slow|whirl|flash | "
        "perf tex|drift|lossy|render|base on|off | direct [reset|lose] | overlay check]: the Omni dial view "
        "(`dial help`: what verify cannot see)",
        &cmd_dial);
}
#endif /* CONFIG_OMNI_DEBUG_CONSOLE */
