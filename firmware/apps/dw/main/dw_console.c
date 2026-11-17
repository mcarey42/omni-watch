/* The `dw` console command (dw wave 1 Task 11; R101: built only with CONFIG_OMNI_DEBUG_CONSOLE, registered by log and
 * continue through debug_console_reg).
 *   dw doors bench [n] [copy]   n swings (default 5) through the real path on the face shown (a doors face): fps,
 *                               ms a frame, the bands lost, the PSRAM and internal minimum; copy: the door from a
 *                               434 KB PSRAM leaf copy instead of the flash mapping (spec section 10); the face's
 *                               own doors (its asset and kind, wave 2 Task 3), named first
 *   dw verify                   every registered face's still frames (dw_still) against this build's art set's CRC
 *                               table: MATCH, or DIFF <face> <crc> (W4)
 *   dw prefs                    the theme's preferences (the published snapshot), the offered mask and the carousel
 *   dw offer reset              R147 PR2(b): clear the offered bit of every built face the carousel lacks (on the
 *                               event loop, R26/R27), so the next boot appends those faces again: the one recovery
 *                               after a rollback to a build with fewer faces dropped them. Refused on a no-write
 *                               boot or a read-only record; prints the event loop's own verdict
 *   dw chime                    the chime service (Task 16): the state, the next boundary, the last verdict, the
 *                               esp_timer task's stack high-water mark (C10, W7)
 *   dw chime at +N | at off     the real path against a boundary N s ahead (a scheduler offset, H7); cleared by
 *                               `at off` and on reboot
 *   dw vortex bench [n] [q] [direct|canvas] [rgb|pal|psram]
 *                               wave 2 Task 13 (the gate, WD38): a temporary PSRAM canvas view over the face shown,
 *                               the Vortex tunnel (dw/vortex.h, q 1 half or q 2 quarter resolution) under a static time
 *                               box, drawn for n s (default 10) of whole frames through the direct path (the doors':
 *                               two cores, 8-row bands; the default) or the canvas (one core, lv_refr_now a frame). It
 *                               prints the path that ran (a refused claim falls back to the canvas and says so), fps,
 *                               render ms (average, maximum), frame ms, the PSRAM and internal minimum, then frees
 *                               everything. Texture forms: rgb (dw_v12_tx0.bin, flash-mapped; the shipped form), psram
 *                               (that texture copied to PSRAM), pal (R146's palette-indexed question: an 8-bit 256 x 256
 *                               index and a 256-entry RGB565 palette in PSRAM, quantised 3-3-2 from the texture here;
 *                               no asset ships). No carousel, NVS or face change. Wave 2 Task 14 (the Task 13
 *                               review): it refuses to start while the doors run (M1), and brackets its run
 *                               (fk_view_bench_begin/end): no doors start and no face's rest motion claims the path
 *                               until it ends, and its claim goes through the view's one-holder rule. n past the
 *                               sleep time needs an `awake hold` first (M2): the sleep cuts the run otherwise. */
#include "dw_console.h"
#include "sdkconfig.h"
#if CONFIG_OMNI_DEBUG_CONSOLE
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "assets.h"
#include "debug_console.h"
#include "disp_direct.h"
#include "shell.h"
#include "store.h"
#include "theme.h"
#include "omni/nav.h"
#include "dw_chime.h"
#include "dw_prefs_svc.h"
#include "fk_view.h"
#include "dw/assets_meta.h"
#include "dw/chime.h"
#include "dw/faces.h"
#include "dw/vortex.h"
/* The CRC tables of the art set this build packs (dw_art.txt). The generic build: dw/verify_crc.h. An overlay build:
 * the overlay's verify_crc_overlay.h (main/CMakeLists puts it on the include path only when the overlay has it, G9),
 * which supplements the generic table (R147 PR3, wave 2 Task 4): rows only for the faces whose bind reads a swap asset,
 * looked up first, then the generic rows. An overlay build without it has no table. */
#if defined(DW_ART_OVERLAY_CRC)
#include "verify_crc_overlay.h"
#include "dw/verify_crc.h"
#define DW_VERIFY_TABS {{DW_VERIFY_OVERLAY, sizeof DW_VERIFY_OVERLAY / sizeof DW_VERIFY_OVERLAY[0]}, \
                        {DW_VERIFY, sizeof DW_VERIFY / sizeof DW_VERIFY[0]}}
#define DW_VERIFY_LABEL "the " DW_VERIFY_OVERLAY_SET " art set's table, then the " DW_VERIFY_SET " one"
#elif !defined(DW_ART_OVERLAY_BUILD)
#include "dw/verify_crc.h"
#define DW_VERIFY_TABS {{DW_VERIFY, sizeof DW_VERIFY / sizeof DW_VERIFY[0]}}
#define DW_VERIFY_LABEL "the " DW_VERIFY_SET " art set's table"
#endif

#define W 466

static int bench(int n, bool copy)
{
    bsp_display_lock(-1);
    fk_view_t *v = fk_view_current();
    const fk_face_ops_t *ops = fk_view_ops(v);
    const bool ok = v && ops && ops->reveal && fk_view_reveal_bench(v, n, copy);
    bsp_display_unlock();
    if (!ok) {
        printf("DW DOORS BENCH: needs a doors face shown and idle (face crystal), its assets and %s of PSRAM\n",
               copy ? "868 KB" : "434 KB");
        return 1;
    }
    const fk_cols_kind_t *k = ops->reveal_arg ? ops->reveal_arg : &DW_DOORS_OUT;
    printf("DW DOORS BENCH face %s: doors %s, %s to %.0f deg%s\n", ops->key ? ops->key : "?",
           ops->reveal_asset ? ops->reveal_asset : ASSET_DW_DOORS_M, k->inward ? "inward" : "outward",
           (double)k->open_deg, k->keep ? ", kept" : "");
    const int64_t t0 = esp_timer_get_time();
    fk_reveal_stats_t s;
    bool done = false;
    for (int i = 0; i < n * 60 && !done; i++) {   /* at most 6 s a swing */
        vTaskDelay(pdMS_TO_TICKS(100));
        bsp_display_lock(-1);
        done = fk_view_current() == v && fk_view_reveal_bench_result(v, &s);
        const bool gone = fk_view_current() != v;
        bsp_display_unlock();
        if (gone) break;
    }
    const uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (!done) {
        printf("DW DOORS BENCH: no result (the face went, or it timed out after %" PRIu32 " ms)\n", ms);
        return 1;
    }
    const uint32_t avg = s.frames ? s.us_sum / s.frames : 0;
    printf("DW DOORS BENCH %" PRIu32 " swings%s (%" PRIu32 " direct, %" PRIu32 " through the canvas)%s: %" PRIu32
           " frames in %" PRIu32 " ms, %.1f fps; %" PRIu32 ".%03" PRIu32 " ms a frame (max %" PRIu32 ".%03" PRIu32
           "); lost %" PRIu32 " bands; PSRAM min %" PRIu32 " B, internal min %" PRIu32 " B; worker stack free %" PRIu32
           " B; interior %" PRIu32 ".%03" PRIu32 " ms a frame over both cores (max %" PRIu32 ".%03" PRIu32 ")\n",
           s.swings, copy ? " copy" : "", s.direct, s.refused, s.cut ? " CUT" : "", s.frames, ms,
           ms ? (double)s.frames * 1000.0 / (double)ms : 0.0, avg / 1000, avg % 1000, s.us_max / 1000, s.us_max % 1000,
           s.lost, s.psram_min, s.int_min, s.stack_hwm, (s.frames ? s.prep_sum / s.frames : 0) / 1000,
           (s.frames ? s.prep_sum / s.frames : 0) % 1000, s.prep_max / 1000, s.prep_max % 1000);
    return s.cut ? 1 : 0;
}

static int verify(void)
{
#if defined(DW_VERIFY_TABS)
    static const fk_verify_tab_t tabs[] = DW_VERIFY_TABS;
    const unsigned ntabs = (unsigned)(sizeof tabs / sizeof tabs[0]);
    uint16_t *fb = heap_caps_malloc((size_t)W * W * 2, MALLOC_CAP_SPIRAM);
    if (!fb) {
        printf("DW VERIFY: no PSRAM for a frame\n");
        return 1;
    }
    unsigned diffs = 0, matched = 0;
    for (unsigned f = 0; f < DW_FACE_COUNT; f++) {
        const fk_face_ops_t *ops = DW_FACES[f];
        void *ctx = heap_caps_calloc(1, ops->ctx_bytes ? ops->ctx_bytes : 1, MALLOC_CAP_SPIRAM);
        if (!ctx || !ops->bind(ctx, assets_raw)) {
            printf("DW VERIFY %s: no PSRAM or assets\n", ops->key);
            heap_caps_free(ctx);
            diffs++;
            continue;
        }
        for (unsigned k = 0; k < dw_still_count(ops); k++) {
            const int64_t t0 = esp_timer_get_time();
            dw_still_frame(ops, ctx, k, fb);
            const uint32_t crc = fk_crc32(0, fb, (size_t)W * W * 2);
            const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
            unsigned dups = 0;
            const fk_verify_t *want = fk_verify_lookup(tabs, ntabs, ops->key, k, &dups);
            dw_still_t st;
            dw_still(k % DW_STILL_N, &st);
            /* the table the row came from (Task 4 review M1): the overlay's, or the generic one a still falls back
             * to */
            const char *from = want && ntabs > 1 && want >= tabs[0].rows && want < tabs[0].rows + tabs[0].n
                                   ? "overlay" : "generic";
            if (want && dups == 1 && want->crc == crc) {
                printf("MATCH %s %u (%s) %08" PRIx32 " %" PRIu32 " us [%s row]\n", ops->key, k, st.what, crc, us,
                       from);
                matched++;
            } else {
                printf("DIFF %s %08" PRIx32 " (still %u, %s; the table: %s)%s%s%s\n", ops->key, crc, k, st.what,
                       !want ? "no row" : dups != 1 ? "two rows" : "another CRC", want ? " [" : "", want ? from : "",
                       want ? " row]" : "");
                diffs++;
            }
            vTaskDelay(1);   /* Task 17: about 80 frames back to back starved IDLE0 past the task watchdog */
        }
        heap_caps_free(ctx);
    }
    heap_caps_free(fb);
    printf("DW VERIFY %s: %u still frames match, %u differ (%s)\n", diffs ? "DIFF" : "MATCH",
           matched, diffs, DW_VERIFY_LABEL);
    return diffs ? 1 : 0;
#else
    printf("DW VERIFY: this overlay build has no CRC table (the overlay has no verify_crc_overlay.h yet)\n");
    return 1;
#endif
}

/* the carousel as the console task can read it (as the shared `face` command does) */
static void prefs_print(void)
{
    const omni_settings_t *s = settings();
    printf("DW PREFS mode %s, chime %s, quiet %02u:00-%02u:00; offered %04x; carousel",
           dw_prefs_now(DW_PF_MODE) == DW_MODE_FLIP_OPEN ? "flip-open" : "always-on",
           dw_prefs_now(DW_PF_CHIME) ? "on" : "off", dw_prefs_now(DW_PF_QUIET_FROM), dw_prefs_now(DW_PF_QUIET_TO),
           dw_prefs_svc_offered());
    for (uint8_t i = 0; i < s->carousel_len && i < OMNI_CAROUSEL_MAX; i++) printf(" %u", s->carousel[i]);
    printf("\n");
}

/* the event loop's verdict (dw_prefs_svc_offer_verdict), printed as it published it: never inferred from the mask */
#define OFFER_WAIT_MS 5000
static int offer_reset(void)
{
    const uint16_t was = dw_prefs_svc_offered();
    const uint32_t before = dw_prefs_svc_offer_verdict();
    if (!shell_post(UI_THEME_SET, omni_theme_set_pack(DW_ROW_OFFER_RESET, DW_OFFER_RESET_GO))) {
        printf("DW OFFER RESET: the event queue is full; try again\n");
        return 1;
    }
    uint32_t v = before;
    for (int i = 0; i < OFFER_WAIT_MS / 50 && (v = dw_prefs_svc_offer_verdict()) >> 8 == before >> 8; i++)
        vTaskDelay(pdMS_TO_TICKS(50));
    if (v >> 8 == before >> 8) {
        printf("DW OFFER RESET: no verdict from the event loop within %d ms (still queued); check `dw prefs` and the "
               "log\n", OFFER_WAIT_MS);
        return 1;
    }
    const uint16_t now = dw_prefs_svc_offered();
    const dw_offer_verdict_t vd = (dw_offer_verdict_t)(v & 0xFFu);
    switch (vd) {
    case DW_OFFER_V_RESET:
        printf("DW OFFER RESET: offered %04x -> %04x; the next boot appends the missing faces\n", was, now);
        break;
    case DW_OFFER_V_NOTHING:
        printf("DW OFFER RESET: nothing to reset (offered %04x; every built face is in the carousel or not yet "
               "offered)\n", now);
        break;
    case DW_OFFER_V_READ_ONLY:
        printf("DW OFFER RESET: refused: a no-write boot or a read-only record; nothing changed (offered %04x)\n", now);
        break;
    default:
        printf("DW OFFER RESET: refused: the commit failed; nothing changed (offered %04x)\n", now);
        break;
    }
    prefs_print();
    return vd == DW_OFFER_V_RESET || vd == DW_OFFER_V_NOTHING ? 0 : 1;
}

/* ---- dw vortex bench (wave 2 Task 13) */
#define VB_STACK 4096   /* PSRAM, as the doors' worker */
#define VB_PRIO 2       /* under LVGL */
#define VB_CHUNKS ((W + DISP_DIRECT_ROWS - 1) / DISP_DIRECT_ROWS)
typedef enum { VB_RGB, VB_PSRAM, VB_PAL } vb_form_t;
static const char *const VB_FORM[] = {"rgb (flash-mapped texture)", "psram (texture copied to PSRAM)",
                                      "pal (8-bit index + RGB565 palette, PSRAM)"};
typedef struct {
    dw_vortex_t vx;
    uint8_t q, rot, rush;
    vb_form_t form;
    uint8_t *idx;
    uint16_t *pal, *tex_copy;
    uint16_t *box;                 /* the static time box (PSRAM), box_r on the screen */
    omni_rect_t box_r;
    int next;                      /* the frame's next chunk (s_vb_mux) */
    bool fail, quit;
    uint32_t draw_us;              /* this frame's render us, both cores (s_vb_mux) */
    SemaphoreHandle_t go, done, h_exit, w_exit;
    TaskHandle_t helper, worker;
    lv_obj_t *canvas;
    lv_draw_buf_t *buf;
    /* the run */
    int64_t t0;
    uint32_t n_ms, frames, seq, us_sum, us_max, draw_sum, draw_max, psram_min, int_min, lost0;
    bool finished, cut;
} vb_t;
static portMUX_TYPE s_vb_mux = portMUX_INITIALIZER_UNLOCKED;

static void vb_heap(vb_t *b)
{
    const uint32_t ps = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const uint32_t in = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (ps < b->psram_min) b->psram_min = ps;
    if (in < b->int_min) b->int_min = in;
}

/* c's rows of the frame (the tunnel, then the box's rows over it) into fb (pixel (c.x0, c.y0), stride W) */
static void vb_draw(vb_t *b, omni_rect_t c, uint16_t *fb)
{
    const int64_t t0 = esp_timer_get_time();
    if (b->form == VB_PAL) dw_vortex_draw_idx(&b->vx, b->idx, b->pal, b->rot, b->rush, b->q, c, fb, W);
    else dw_vortex_draw(&b->vx, b->rot, b->rush, b->q, c, fb, W);
    const omni_rect_t r = b->box_r;
    const int bw = r.x1 - r.x0;
    for (int y = c.y0 > r.y0 ? c.y0 : r.y0; y < c.y1 && y < r.y1; y++)
        memcpy(fb + (size_t)(y - c.y0) * W + (r.x0 - c.x0), b->box + (size_t)(y - r.y0) * bw, (size_t)bw * 2u);
    const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    portENTER_CRITICAL(&s_vb_mux);
    b->draw_us += us;
    portEXIT_CRITICAL(&s_vb_mux);
}

static void vb_fill(void *ctx, uint16_t *band, omni_rect_t c) { vb_draw(ctx, c, band); }   /* band: (0, c.y0), stride W */

/* a frame's chunks, shared by both cores: false once the path aborted */
static void vb_chunks(vb_t *b)
{
    for (;;) {
        portENTER_CRITICAL(&s_vb_mux);
        const int k = b->fail ? VB_CHUNKS : b->next++;
        portEXIT_CRITICAL(&s_vb_mux);
        if (k >= VB_CHUNKS) return;
        const int y0 = k * DISP_DIRECT_ROWS, y1 = y0 + DISP_DIRECT_ROWS < W ? y0 + DISP_DIRECT_ROWS : W;
        const omni_rect_t c = {0, (int16_t)y0, W, (int16_t)y1};
        if (!disp_direct_send_chunk(c, vb_fill, b)) {
            portENTER_CRITICAL(&s_vb_mux);
            b->fail = true;
            portEXIT_CRITICAL(&s_vb_mux);
            return;
        }
    }
}

static void vb_helper(void *arg)
{
    vb_t *b = arg;
    for (;;) {
        xSemaphoreTake(b->go, portMAX_DELAY);
        if (b->quit) break;
        vb_chunks(b);
        xSemaphoreGive(b->done);
    }
    xSemaphoreGive(b->h_exit);
    vTaskSuspend(NULL);
}

static void vb_frame_params(vb_t *b)
{
    const uint32_t t = (uint32_t)((esp_timer_get_time() - b->t0) / 1000);
    b->rot = dw_vortex_rot(t);
    b->rush = dw_vortex_rush(t);
}

static void vb_account(vb_t *b, int64_t f0)
{
    const uint32_t us = (uint32_t)(esp_timer_get_time() - f0);
    b->frames++;
    b->us_sum += us;
    if (us > b->us_max) b->us_max = us;
    b->draw_sum += b->draw_us;
    if (b->draw_us > b->draw_max) b->draw_max = b->draw_us;
    vb_heap(b);
}

/* the direct path's worker (core 1): whole frames until n ms have passed, each shared with the helper (core 0) */
static void vb_worker(void *arg)
{
    vb_t *b = arg;
    while (!b->quit && (uint32_t)((esp_timer_get_time() - b->t0) / 1000) < b->n_ms) {
        if (!disp_direct_active()) {
            b->cut = true;
            break;
        }
        const int64_t f0 = esp_timer_get_time();
        vb_frame_params(b);
        portENTER_CRITICAL(&s_vb_mux);
        b->next = 0;
        b->draw_us = 0;
        portEXIT_CRITICAL(&s_vb_mux);
        xSemaphoreGive(b->go);
        vb_chunks(b);
        xSemaphoreTake(b->done, portMAX_DELAY);
        if (b->fail) {
            b->cut = true;
            break;
        }
        disp_direct_frame_end(++b->seq);
        vb_account(b, f0);
        vTaskDelay(1);   /* IDLE's turn (the task watchdog, as `dw verify` learned) */
    }
    b->finished = true;
    xSemaphoreGive(b->w_exit);
    vTaskSuspend(NULL);
}

/* the static time box: "10:09" in dw_b8_dig over black, centred at (233, 233), drawn once */
static bool vb_box(vb_t *b)
{
    fk_img_t im;
    size_t n = 0;
    const void *p = assets_raw(ASSET_DW_B8_DIG, &n);
    if (!p || !fk_img_from_bin(&im, p, n)) return false;
    const fk_atlas_t at = {&im, DW_CELLS_DW_B8_DIG, DW_GLYPHS_DW_B8_DIG, ASSET_DW_B8_DIG_LINE_H};
    const int tw = fk_atlas_width(&at, "10:09", 0), bw = (tw + 16 + 1) & ~1, bh = ASSET_DW_B8_DIG_LINE_H + 16;
    b->box_r = (omni_rect_t){(int16_t)(233 - bw / 2), (int16_t)(233 - bh / 2), (int16_t)(233 - bw / 2 + bw),
                             (int16_t)(233 - bh / 2 + bh)};
    b->box = heap_caps_calloc((size_t)bw * bh, 2, MALLOC_CAP_SPIRAM);
    if (!b->box) return false;
    const omni_rect_t clip = {0, 0, (int16_t)bw, (int16_t)bh};
    fk_atlas_text_rgb(b->box, bw, clip, &at, "10:09", 8, 8 + ASSET_DW_B8_DIG_LINE_H * 3 / 4, 0, 0xFF9C, 255, 0);
    return true;
}

static void vb_free(vb_t *b)
{
    heap_caps_free(b->box);
    heap_caps_free(b->idx);
    heap_caps_free(b->pal);
    heap_caps_free(b->tex_copy);
    if (b->go) vSemaphoreDeleteWithCaps(b->go);
    if (b->done) vSemaphoreDeleteWithCaps(b->done);
    if (b->h_exit) vSemaphoreDeleteWithCaps(b->h_exit);
    if (b->w_exit) vSemaphoreDeleteWithCaps(b->w_exit);
    heap_caps_free(b);
}

static int vortex_bench_run(int n_s, uint8_t q, bool want_direct, vb_form_t form);

static int vortex_bench(int n_s, uint8_t q, bool want_direct, vb_form_t form)
{
    /* M1 (the Task 13 review): never over the doors; then the bracket holds the doors and every rest motion off */
    bsp_display_lock(-1);
    const bool doors = fk_view_reveal_active(fk_view_current());
    const bool begun = !doors && fk_view_bench_begin();
    bsp_display_unlock();
    if (!begun) {
        printf("DW VORTEX BENCH: refused (%s)\n", doors ? "the doors are running" : "a bench is already on");
        return 1;
    }
    const int rc = vortex_bench_run(n_s, q, want_direct, form);
    bsp_display_lock(-1);
    fk_view_bench_end();
    bsp_display_unlock();
    return rc;
}

static int vortex_bench_run(int n_s, uint8_t q, bool want_direct, vb_form_t form)
{
    vb_t *b = heap_caps_calloc(1, sizeof *b, MALLOC_CAP_SPIRAM);
    if (!b) {
        printf("DW VORTEX BENCH: no PSRAM\n");
        return 1;
    }
    b->q = q;
    b->form = form;
    b->n_ms = (uint32_t)n_s * 1000u;
    b->psram_min = b->int_min = UINT32_MAX;
    if (!dw_vortex_bind(&b->vx, assets_raw, 0) || !vb_box(b)) {
        printf("DW VORTEX BENCH: the assets (%s, %s, %s) did not bind\n", ASSET_DW_V12_LUT, ASSET_DW_V12_TX0,
               ASSET_DW_B8_DIG);
        vb_free(b);
        return 1;
    }
    if (form == VB_PSRAM || form == VB_PAL) {
        const size_t texels = (size_t)DW_V12_TEX * DW_V12_TEX;
        if (form == VB_PSRAM) {
            b->tex_copy = heap_caps_malloc(texels * 2, MALLOC_CAP_SPIRAM);
            if (b->tex_copy) {
                memcpy(b->tex_copy, b->vx.tex, texels * 2);
                b->vx.tex = b->tex_copy;
            }
        } else {
            b->idx = heap_caps_malloc(texels, MALLOC_CAP_SPIRAM);
            b->pal = heap_caps_malloc(256 * 2, MALLOC_CAP_SPIRAM);
            if (b->idx && b->pal) {
                for (size_t i = 0; i < texels; i++) {   /* 3-3-2: r5 >> 2, g6 >> 3, b5 >> 3 */
                    const uint16_t t = b->vx.tex[i];
                    b->idx[i] = (uint8_t)(((t >> 13) << 5) | (((t >> 8) & 7u) << 2) | ((t >> 3) & 3u));
                }
                for (int k = 0; k < 256; k++) {
                    const unsigned r = (unsigned)(k >> 5) * 31u / 7u, g = (unsigned)((k >> 2) & 7) * 63u / 7u,
                                   bl = (unsigned)(k & 3) * 31u / 3u;
                    b->pal[k] = (uint16_t)(r << 11 | g << 5 | bl);
                }
            }
        }
        if ((form == VB_PSRAM && !b->tex_copy) || (form == VB_PAL && (!b->idx || !b->pal))) {
            printf("DW VORTEX BENCH: no PSRAM for the %s texture\n", form == VB_PAL ? "indexed" : "copied");
            vb_free(b);
            return 1;
        }
    }
    b->go = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
    b->done = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
    b->h_exit = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
    b->w_exit = xSemaphoreCreateBinaryWithCaps(MALLOC_CAP_SPIRAM);
    if (!b->go || !b->done || !b->h_exit || !b->w_exit) {
        printf("DW VORTEX BENCH: no PSRAM for the semaphores\n");
        vb_free(b);
        return 1;
    }
    /* the view: a PSRAM canvas over the face, black until the first frame */
    bsp_display_lock(-1);
    b->buf = theme_buf_create(W, W, LV_COLOR_FORMAT_RGB565);
    if (b->buf) {
        memset(b->buf->data, 0, b->buf->data_size);
        b->canvas = lv_canvas_create(lv_screen_active());
        lv_canvas_set_draw_buf(b->canvas, b->buf);
        lv_obj_set_pos(b->canvas, 0, 0);
        lv_obj_move_foreground(b->canvas);
    }
    bsp_display_unlock();
    if (!b->buf || (int)(b->buf->header.stride / 2) != W) {
        printf("DW VORTEX BENCH: no PSRAM for the canvas\n");
        bsp_display_lock(-1);
        if (b->canvas) lv_obj_delete(b->canvas);
        theme_buf_destroy(b->buf);
        bsp_display_unlock();
        vb_free(b);
        return 1;
    }
    vTaskDelay(pdMS_TO_TICKS(100));   /* LVGL shows it once */
    omni_dd_why_t why = OMNI_DD_OFF;
    bool direct = false;
    if (want_direct) {
        for (int i = 0; i < 30; i++) {   /* past an earlier exit's back-off (500 ms) */
            bsp_display_lock(-1);
            why = fk_view_bench_claim(b->canvas);   /* the one-holder rule: a face's rest motion may still hold it */
            bsp_display_unlock();
            if (why == OMNI_DD_OK || (why != OMNI_DD_BACKOFF && why != OMNI_DD_DISPLAY)) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        direct = why == OMNI_DD_OK;
    }
    vb_heap(b);
    b->t0 = esp_timer_get_time();
    if (direct) {
        b->lost0 = disp_direct_lost();
        bool ok = xTaskCreatePinnedToCoreWithCaps(vb_helper, "dwvx2", VB_STACK, b, VB_PRIO, &b->helper, 0,
                                                  MALLOC_CAP_SPIRAM) == pdPASS;
        ok = ok && xTaskCreatePinnedToCoreWithCaps(vb_worker, "dwvx", VB_STACK, b, VB_PRIO, &b->worker, 1,
                                                   MALLOC_CAP_SPIRAM) == pdPASS;
        if (!ok) b->cut = true;
        uint32_t shown = disp_direct_shown();
        /* LVGL ctx's part: the admission check once per frame shown, until the worker ends */
        while (ok && xSemaphoreTake(b->w_exit, pdMS_TO_TICKS(20)) != pdTRUE) {
            bsp_display_lock(-1);
            const uint32_t now = disp_direct_shown();
            if (now != shown) {
                shown = now;
                const omni_dd_why_t w = disp_direct_check(b->canvas, true);
                if (w != OMNI_DD_OK) {
                    printf("DW VORTEX BENCH: the admission check refused (%s): cut\n", disp_direct_why_name(w));
                    disp_direct_abort();
                }
            }
            bsp_display_unlock();
        }
        b->quit = true;
        if (b->helper) {
            xSemaphoreGive(b->go);
            xSemaphoreTake(b->h_exit, portMAX_DELAY);
            vTaskDeleteWithCaps(b->helper);
        }
        if (b->worker) vTaskDeleteWithCaps(b->worker);
    } else {
        /* the canvas: a whole frame into the canvas on this core, then LVGL renders and flushes it (lv_refr_now) */
        uint16_t *px = (uint16_t *)b->buf->data;
        const omni_rect_t all = {0, 0, W, W};
        while ((uint32_t)((esp_timer_get_time() - b->t0) / 1000) < b->n_ms) {
            const int64_t f0 = esp_timer_get_time();
            vb_frame_params(b);
            b->draw_us = 0;
            vb_draw(b, all, px);
            bsp_display_lock(-1);
            lv_obj_invalidate(b->canvas);
            lv_refr_now(NULL);
            bsp_display_unlock();
            vb_account(b, f0);
            vTaskDelay(1);
        }
    }
    const uint32_t ms = (uint32_t)((esp_timer_get_time() - b->t0) / 1000);
    const uint32_t lost = direct ? disp_direct_lost() - b->lost0 : 0;
    bsp_display_lock(-1);
    if (direct) fk_view_bench_release();   /* hands the panel back; LVGL redraws what it dropped */
    lv_obj_delete(b->canvas);
    theme_buf_destroy(b->buf);
    bsp_display_unlock();
    const uint32_t f = b->frames ? b->frames : 1;
    printf("DW VORTEX BENCH q %u (%s), %s, path %s%s%s%s: %" PRIu32 " frames in %" PRIu32 " ms, %.1f fps; render %"
           PRIu32 ".%03" PRIu32 " ms a frame (max %" PRIu32 ".%03" PRIu32 ")%s; frame %" PRIu32 ".%03" PRIu32
           " ms (max %" PRIu32 ".%03" PRIu32 "); PSRAM min %" PRIu32 " B, internal min %" PRIu32 " B; lost %" PRIu32
           " bands%s\n",
           q, q == 2 ? "quarter resolution" : "half resolution", VB_FORM[form], direct ? "direct" : "canvas",
           want_direct && !direct ? " (the direct claim refused: " : "",
           want_direct && !direct ? disp_direct_why_name(why) : "", want_direct && !direct ? ")" : "", b->frames, ms,
           ms ? (double)b->frames * 1000.0 / (double)ms : 0.0, b->draw_sum / f / 1000, b->draw_sum / f % 1000,
           b->draw_max / 1000, b->draw_max % 1000, direct ? " over both cores" : " on one core",
           b->us_sum / f / 1000, b->us_sum / f % 1000, b->us_max / 1000, b->us_max % 1000, b->psram_min,
           b->int_min, lost, b->cut ? " CUT" : "");
    const int rc = b->cut || !b->frames ? 1 : 0;
    vb_free(b);
    return rc;
}

static int cmd_dw(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "doors") == 0 && argc >= 3 && strcmp(argv[2], "bench") == 0) {
        int n = 5;
        bool copy = false;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "copy") == 0) copy = true;
            else n = atoi(argv[i]);
        }
        if (n < 1 || n > 50) {
            printf("usage: dw doors bench [n 1..50] [copy]\n");
            return 1;
        }
        return bench(n, copy);
    }
    if (strcmp(sub, "vortex") == 0 && argc >= 3 && strcmp(argv[2], "bench") == 0) {
        int n = 10, q = 1, nums = 0;
        bool direct = true, bad = false;
        vb_form_t form = VB_RGB;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "direct") == 0) direct = true;
            else if (strcmp(argv[i], "canvas") == 0) direct = false;
            else if (strcmp(argv[i], "rgb") == 0) form = VB_RGB;
            else if (strcmp(argv[i], "psram") == 0) form = VB_PSRAM;
            else if (strcmp(argv[i], "pal") == 0) form = VB_PAL;
            else if (nums == 0) n = atoi(argv[i]), nums++;
            else if (nums == 1) q = atoi(argv[i]), nums++;
            else bad = true;
        }
        if (bad || n < 1 || n > 60 || (q != 1 && q != 2)) {
            printf("usage: dw vortex bench [n 1..60] [q 1|2] [direct|canvas] [rgb|pal|psram]\n"
                   "  n s past the sleep time needs `awake hold` first (the sleep cuts the run); not while the doors run\n");
            return 1;
        }
        return vortex_bench(n, (uint8_t)q, direct, form);
    }
    if (strcmp(sub, "verify") == 0 && argc == 2) return verify();
    if (strcmp(sub, "prefs") == 0 && argc == 2) {
        prefs_print();
        return 0;
    }
    if (strcmp(sub, "offer") == 0 && argc == 3 && strcmp(argv[2], "reset") == 0) return offer_reset();
    if (strcmp(sub, "chime") == 0 && argc == 2) {
        dw_chime_print();
        return 0;
    }
    if (strcmp(sub, "chime") == 0 && argc == 4 && strcmp(argv[2], "at") == 0) {
        /* H7: the real path against a boundary N s ahead (the offset is RAM only: cleared by `at off` and a reboot) */
        const char *a = argv[3];
        const bool off = strcmp(a, "off") == 0;
        char *end = NULL;
        const long n = off ? 0 : strtol(a[0] == '+' ? a + 1 : a, &end, 10);
        if (!off && (a[0] != '+' || !end || *end || n < 1 || n > FK_CHIME_AT_MAX_S)) {
            printf("usage: dw chime at +N (N s, 1..%d) | dw chime at off\n", FK_CHIME_AT_MAX_S);
            return 1;
        }
        if (!fk_chime_svc_at((uint32_t)n)) {
            printf("DW CHIME AT: refused (no boundary, or the service is not started)\n");
            return 1;
        }
        if (!off && !dw_prefs_now(DW_PF_CHIME)) printf("DW CHIME AT: the chime is off: nothing will play\n");
        dw_chime_print();
        return 0;
    }
    printf("usage: dw doors bench [n] [copy] | dw vortex bench [n] [q] [direct|canvas] [rgb|pal|psram] | dw verify | "
           "dw prefs | dw offer reset | dw chime [at +N | at off]\n");
    return 1;
}

void dw_console_register(void)
{
    debug_console_reg("dw", "dw doors bench [n] [copy] | dw vortex bench [n] [q] [direct|canvas] [rgb|pal|psram] | dw "
                      "verify | dw prefs | dw offer reset | dw chime [at +N | at off]: the dw theme's doors and "
                      "Vortex benches, still-frame CRCs, preferences, carousel offer and chime",
                      &cmd_dw);
}
#endif
