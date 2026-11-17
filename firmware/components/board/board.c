#include "board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "axp2101.h"
#include "imu.h"
#include "input.h"
#include "omni_events.h"
#include "power.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_display.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "freertos/semphr.h"
#include "omni/band.h"

static const char *TAG = "board";

/* Display memory. The BSP registers the display with 50-line draw buffers in PSRAM. spi_master cannot DMA from that
 * PSRAM, so each flush allocated an internal DMA bounce buffer of the same size at the moment of the flush: 46.6 KB
 * contiguous, then 14.9 KB once the board component shrank the buffers to 16 lines. While Wi-Fi + BLE provisioning
 * run the largest free internal block is about 22 KB, so frames depended on fragmentation ("Failed to allocate priv
 * TX buffer": the panel stops updating).
 *  - -Wl,--wrap=esp_lv_adapter_register_display (CMakeLists.txt) sets the 16-line draw buffers (PSRAM, double).
 *  - One 16-line staging buffer in internal DMA RAM is allocated at boot (s_stage), and the adapter's draw-bitmap hook
 *    copies each flush into it and sends from there; spi_master then sends in place and allocates nothing.
 *  - Early release (Task 1 step 4, measured): the band is free once copied, so the hook gives flush_ready itself
 *    instead of waiting for the transfer-done ISR. LVGL renders the next band at once, and the wait for the panel moves
 *    out of LVGL's flush wait (a busy loop in LVGL 9.5: the adapter sets no flush_wait_cb) into blocking SPI calls:
 *    the next copy into the stage first waits for the transfer still reading it (tx_wait), and the CO5300's
 *    draw_bitmap sends CASET/RASET as polling transactions, which wait for every queued colour transfer
 *    (esp_lcd_panel_io_spi.c tx_param). The ISR's own flush_ready still comes later and only clears flushing again.
 *    `disp bench`: full screen 25.5 -> 26.6 fps, the 350 px box 36.6 -> 39.2 fps.
 * Measured and rejected (Task 1): taller PSRAM bands (32/48 lines: the band no longer stays in the
 * 64 KB data cache, full screen 21.5/19.2 fps), a second ping-pong stage (no gain: the copy is about 1 ms a refresh),
 * the byte swap fused into the copy (the adapter's in-place swap on a cache-hot band is cheaper). 8-line internal draw
 * buffers cost ~45 ms more per full redraw (the big compressed digits are decoded again per chunk). */
#define DRAW_BUF_LINES 16
#define STAGE_BYTES ((size_t)BSP_LCD_H_RES * DRAW_BUF_LINES * 2) /* RGB565 */
static bool s_display_wrapped;
static bool s_lvgl_init_wrapped;
static uint8_t *s_stage;
static bool s_tx_pending;                  /* LVGL task: a colour transfer may still be reading the stage */
static volatile bool s_early = true;       /* flush_ready at the end of the hook (board_display_flush_early) */
static volatile unsigned s_unstaged; /* flushes sent from PSRAM (no stage, or too big): each needs a bounce */
static portMUX_TYPE s_fl_lock = portMUX_INITIALIZER_UNLOCKED;
static board_flush_stats_t s_fl;           /* per window, under s_fl_lock */

lv_display_t *__real_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config);
lv_display_t *__wrap_esp_lv_adapter_register_display(const esp_lv_adapter_display_config_t *config)
{
    esp_lv_adapter_display_config_t cfg = *config;
    cfg.profile.buffer_height = DRAW_BUF_LINES;
    s_display_wrapped = true;
    return __real_esp_lv_adapter_register_display(&cfg);
}

/* The LVGL task's core (CONFIG_OMNI_LVGL_CORE; the BSP's default config has no affinity): core 0 (R108), away from the
 * render workers on core 1. On core 1 a full-screen refresh cost about 5 ms more (disp bench full 24.5 against 28.0-28.5
 * fps; measured in Stage 3: Omni rest 27.1-27.8 -> 30.2-31.0 fps). Stage 3 Task 7 (T2 m10) had pinned it to core 1,
 * measured neutral only against the unpinned task, which also landed there. LVGL's draw thread (LV_OS_FREERTOS,
 * priority 3, an unpinned xTaskCreate in lv_freertos.c) is not pinned. */
esp_err_t __real_esp_lv_adapter_init(const esp_lv_adapter_config_t *config);
esp_err_t __wrap_esp_lv_adapter_init(const esp_lv_adapter_config_t *config)
{
    esp_lv_adapter_config_t cfg = *config;
    cfg.task_core_id = CONFIG_OMNI_LVGL_CORE;
    s_lvgl_init_wrapped = true;
    return __real_esp_lv_adapter_init(&cfg);
}

/* Waits (blocking) until no colour transfer is queued; sends nothing (lcd_cmd -1, no parameters). */
static void tx_wait(void)
{
    esp_lcd_panel_io_handle_t io = power_panel_io();
    if (io != NULL) esp_lcd_panel_io_tx_param(io, -1, NULL, 0);
    s_tx_pending = false;
}

/* ------------------------------------------------------------ direct mode (lever B, board.h) */
static SemaphoreHandle_t s_panel_mx;       /* the panel mutex (D5): direct draws, waits, power's hold, enable */
static volatile bool s_direct;             /* drop mode: LVGL's flushes are dropped (begin .. end or force-end) */
static volatile bool s_direct_on = true;   /* board_direct_enable: the panel is on (bsp_display_start leaves it on) */
static volatile bool s_forced;             /* force-ended by board_direct_enable(false) since the last begin */
static volatile bool s_stage_out;          /* the stage is lent: begin .. end (a force-end does not return it, M1) */
static omni_rect_t s_drop[BOARD_DIRECT_DROP_MAX];   /* the dropped flushes' areas, half-open (LVGL task; read at end) */
static int s_drop_n;
static volatile uint32_t s_dropped;        /* dropped flushes since boot */

static uint32_t s_mx_want;                /* callers other than the sender waiting for the panel mutex */

/* A FreeRTOS mutex does not pass to a waiter when it is given: the sender (priority 6, a band every ~0.5 ms) would take
 * it back before a waiting event loop or LVGL task runs, and starve it (measured: PA_TO_SLEEP held 5.7 s in
 * board_direct_enable until the bench was stopped). So everyone but the sender announces itself, and the sender
 * yields while anyone waits: a waiter gets the mutex after at most one more band. */
static void panel_lock(void)
{
    __atomic_add_fetch(&s_mx_want, 1, __ATOMIC_SEQ_CST);
    xSemaphoreTake(s_panel_mx, portMAX_DELAY);
    __atomic_sub_fetch(&s_mx_want, 1, __ATOMIC_SEQ_CST);
}

static void panel_lock_sender(void)
{
    while (__atomic_load_n(&s_mx_want, __ATOMIC_SEQ_CST) != 0) vTaskDelay(1);
    xSemaphoreTake(s_panel_mx, portMAX_DELAY);
}

static void panel_unlock(void) { xSemaphoreGive(s_panel_mx); }

bool board_direct_begin(void)
{
    if (s_panel_mx == NULL) return false;
    panel_lock();
    const bool on = s_direct_on;
    if (on) {
        if (s_tx_pending) tx_wait();   /* LVGL's last staged band is on the wire: the stage is free to lend */
        s_drop_n = 0;
        s_forced = false;
        s_stage_out = true;
        s_direct = true;
    }
    panel_unlock();
    return on;
}

esp_err_t board_direct_draw(int x0, int y0, int x1, int y1, const void *px)
{
    panel_lock_sender();
    esp_err_t err = ESP_ERR_INVALID_STATE;
    esp_lcd_panel_handle_t panel = power_panel();
    if (s_direct_on && s_direct && !s_forced && panel != NULL)
        err = esp_lcd_panel_draw_bitmap(panel, x0, y0, x1, y1, px);
    panel_unlock();
    return err;
}

/* The panel mutex held: every queued colour transfer has gone out (tx_param drains the queue first, inv. section 2). */
static void wait_locked(void)
{
    esp_lcd_panel_io_handle_t io = power_panel_io();
    if (io != NULL) esp_lcd_panel_io_tx_param(io, -1, NULL, 0);
}

void board_direct_wait(void)
{
    panel_lock_sender();
    wait_locked();
    panel_unlock();
}

int board_direct_end(lv_area_t out[BOARD_DIRECT_DROP_MAX])
{
    if (s_panel_mx == NULL) return 0;
    panel_lock();
    if (s_direct) wait_locked();   /* the sender has drained; nothing of it may still read a band */
    s_direct = false;
    s_stage_out = false;           /* LVGL may stage its flushes again */
    const int n = s_drop_n;
    for (int i = 0; i < n; i++)
        out[i] = (lv_area_t){s_drop[i].x0, s_drop[i].y0, s_drop[i].x1 - 1, s_drop[i].y1 - 1};
    s_drop_n = 0;
    panel_unlock();
    return n;
}

bool board_direct_ended(void) { return s_forced; }

bool board_direct_enabled(void) { return s_direct_on; }

void board_direct_hold(bool on)
{
    if (s_panel_mx == NULL) return;
    if (on) {
        panel_lock();
        wait_locked();   /* no direct band on the wire when power's command goes out */
    } else {
        panel_unlock();
    }
}

/* No SPI here (Task 5): both callers run with LVGL live (display_off before panel_off's pause, panel_on after its
 * release), and esp_lcd's SPI IO keeps its in-flight count unsynchronised, so a wait here racing one of LVGL's flushes
 * can leave either task waiting forever for a transfer result the other took (seen as a hung event loop or LVGL task
 * after sleeps and wakes). Taking the mutex is enough: the sender draws only under it, so none of its draws is under
 * way, and D5's "the sender's last band is out before DISPOFF" is panel_off's board_direct_hold, which waits for the
 * wire with LVGL paused. Turning on, the path has drawn nothing since it was turned off. */
void board_direct_enable(bool on)
{
    if (s_panel_mx == NULL) return;
    panel_lock();
    s_direct_on = on;
    if (!on && s_direct) {
        /* D4: force-end: no sender band reaches the panel from now on, and the client sees board_direct_ended() and
         * treats the panel as unknown. LVGL's flushes stay dropped (and kept) until board_direct_end returns the stage:
         * a render task may still be writing a band of it (M1). The event loop releases the path right after
         * display_off (disp_direct_display_off), so display_on's invalidate and refresh reach the panel. */
        s_forced = true;
        s_direct = false;
    }
    panel_unlock();
}

uint8_t *board_stage_lend(size_t *bytes)
{
    *bytes = s_stage ? STAGE_BYTES : 0;
    return s_stage;
}

uint32_t board_direct_dropped(void) { return s_dropped; }

/* LVGL task, one call per flush (the adapter has already byte-swapped color_map in place).
 *
 * Direct mode: the flush is dropped. flush_ready is given here, whatever s_early says, and the area is kept for the
 * hand-back. The adapter's transfer-done ISR still fires for the sender's transfers (they go through the same panel
 * IO): it only calls lv_display_flush_ready, which clears LVGL's flushing flag, and drop mode has already cleared it
 * for every flush it took, as early release does (the note above). So a sender band's ISR never completes one of LVGL's
 * flushes early, and none of LVGL's flushes is on the wire meanwhile (begin waited for the last one). */
static esp_err_t draw_staged(lv_display_t *disp, esp_lcd_panel_handle_t panel, int x_start, int y_start, int x_end,
                             int y_end, const void *color_map, void *ctx)
{
    (void)ctx;
    if (s_direct || s_stage_out) {
        const omni_rect_t a = {(int16_t)x_start, (int16_t)y_start, (int16_t)x_end, (int16_t)y_end};
        omni_band_drop_keep(s_drop, &s_drop_n, BOARD_DIRECT_DROP_MAX, a);
        s_dropped++;
        lv_display_flush_ready(disp);
        return ESP_OK;
    }
    const int64_t t0 = esp_timer_get_time();
    const size_t n = (size_t)(x_end - x_start) * (size_t)(y_end - y_start) * 2;
    if (s_stage == NULL || n > STAGE_BYTES) {
        s_unstaged++;
        if (s_tx_pending) tx_wait();   /* early release: the bounce must not overtake the stage's transfer */
        return esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, color_map);   /* ISR: flush_ready */
    }
    if (s_tx_pending) tx_wait();       /* the stage may still be on its way out */
    memcpy(s_stage, color_map, n);
    const int64_t t1 = esp_timer_get_time();
    const esp_err_t err = esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, s_stage);
    const int64_t t2 = esp_timer_get_time();
    s_tx_pending = true;
    taskENTER_CRITICAL(&s_fl_lock);
    s_fl.flushes++;
    s_fl.copy_us += (uint32_t)(t1 - t0);
    s_fl.send_us += (uint32_t)(t2 - t1);
    if ((uint32_t)(t2 - t0) > s_fl.hook_us_max) s_fl.hook_us_max = (uint32_t)(t2 - t0);
    taskEXIT_CRITICAL(&s_fl_lock);
    if (err == ESP_OK && s_early) lv_display_flush_ready(disp);   /* the band is copied: LVGL may reuse it */
    return err;                                                   /* an error: the adapter gives flush_ready */
}

unsigned board_unstaged_flushes(void) { return s_unstaged; }

void board_flush_stats(board_flush_stats_t *s, bool reset)
{
    taskENTER_CRITICAL(&s_fl_lock);
    *s = s_fl;
    if (reset) memset(&s_fl, 0, sizeof s_fl);
    taskEXIT_CRITICAL(&s_fl_lock);
    s->early = s_early;
}

bool board_display_flush_early(bool on)
{
    if (!board_lvgl_pause(true)) return false;
    tx_wait();
    s_early = on;
    board_lvgl_pause(false);
    return true;
}

bool board_lvgl_pause(bool pause) { return pause ? power_lvgl_pause_begin() : (power_lvgl_pause_end(), true); }

/* ------------------------------------------------------------ internal-RAM telemetry */
#define HEAP_FLUSH_SAMPLE_US 200000
#define DMA_INTERNAL (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)

static portMUX_TYPE s_heap_lock = portMUX_INITIALIZER_UNLOCKED;
static board_heap_t s_heap = {.dma_largest_min = SIZE_MAX};

void board_heap_sample(void)
{
    size_t v = heap_caps_get_largest_free_block(DMA_INTERNAL);
    taskENTER_CRITICAL(&s_heap_lock);
    s_heap.dma_largest = v;
    if (v < s_heap.dma_largest_min) s_heap.dma_largest_min = v;
    taskEXIT_CRITICAL(&s_heap_lock);
}

board_heap_t board_heap(void)
{
    taskENTER_CRITICAL(&s_heap_lock);
    board_heap_t h = s_heap;
    taskEXIT_CRITICAL(&s_heap_lock);
    return h;
}

/* Runs in the failing allocation's task (never an ISR here); no logging (it may be the logger that failed). */
static void on_alloc_failed(size_t size, uint32_t caps, const char *fn)
{
    (void)fn;
    taskENTER_CRITICAL(&s_heap_lock);
    s_heap.alloc_failures++;
    s_heap.last_fail_size = size;
    s_heap.last_fail_caps = caps;
    taskEXIT_CRITICAL(&s_heap_lock);
}

/* LVGL task, at the start of each flush (several per frame): a reading at most every HEAP_FLUSH_SAMPLE_US, since
 * the largest-block query walks the heap. */
static void on_flush_start(lv_event_t *e)
{
    (void)e;
    static int64_t last;
    int64_t now = esp_timer_get_time();
    if (now - last < HEAP_FLUSH_SAMPLE_US) return;
    last = now;
    board_heap_sample();
}

#define EVENT_QUEUE_LEN 16
#define EVENT_HISTORY 5

static QueueHandle_t s_events;
static portMUX_TYPE s_hist_lock = portMUX_INITIALIZER_UNLOCKED;
static omni_event_t s_hist[EVENT_HISTORY];
static unsigned s_hist_n; /* total posted; ring index = n % EVENT_HISTORY */

QueueHandle_t omni_events(void) { return s_events; }

bool omni_events_post(const omni_event_t *e)
{
    if (e->kind != EV_TICK_1S) { /* ticks would flush the history `state` shows */
        taskENTER_CRITICAL(&s_hist_lock);
        s_hist[s_hist_n++ % EVENT_HISTORY] = *e;
        taskEXIT_CRITICAL(&s_hist_lock);
    }
    if (s_events == NULL || xQueueSend(s_events, e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropped");
        return false;
    }
    return true;
}

int omni_events_recent(omni_event_t *out, int max)
{
    taskENTER_CRITICAL(&s_hist_lock);
    unsigned total = s_hist_n;
    unsigned n = total < EVENT_HISTORY ? total : EVENT_HISTORY;
    if ((unsigned)max < n) n = (unsigned)max;
    for (unsigned i = 0; i < n; i++) out[i] = s_hist[(total - n + i) % EVENT_HISTORY];
    taskEXIT_CRITICAL(&s_hist_lock);
    return (int)n;
}

const char *omni_event_format(const omni_event_t *e, char *buf, size_t n)
{
    static const char *const KIND[] = {"EV_GESTURE", "EV_BOOT", "EV_PWR", "EV_LIFT", "EV_TICK_1S", "EV_NET", "EV_UI", "EV_VBUS", "EV_SETTIME", "EV_SAVE"};
    static const char *const GEST[] = {"G_NONE", "G_TAP", "G_LONG_PRESS", "G_SWIPE_L", "G_SWIPE_R",
                                       "G_SWIPE_UP", "G_SWIPE_DOWN_FROM_TOP", "G_DRAG_START", "G_DRAG_MOVE",
                                       "G_DRAG_END", "G_DOWN", "G_HOLD", "G_LIFT_STILL"};
    static const char *const PRESS[] = {"B_NONE", "B_SHORT", "B_DOUBLE", "B_LONG"};
    const char *kind = (unsigned)e->kind < sizeof KIND / sizeof KIND[0] ? KIND[e->kind] : "EV_?";
    switch (e->kind) {
    case EV_GESTURE:
        snprintf(buf, n, "%s %s %d,%d%s", kind,
                 (unsigned)e->g.kind < sizeof GEST / sizeof GEST[0] ? GEST[e->g.kind] : "G_?", e->g.x, e->g.y,
                 e->g.edge_start ? " edge" : "");
        break;
    case EV_BOOT:
        snprintf(buf, n, "%s %s", kind, (unsigned)e->press < sizeof PRESS / sizeof PRESS[0] ? PRESS[e->press] : "B_?");
        break;
    case EV_NET:
        snprintf(buf, n, "%s 0x%x", kind, (unsigned)e->net);
        break;
    case EV_UI:
        snprintf(buf, n, "%s %d/%d", kind, e->ui & 0xff, e->ui >> 8); /* code / argument */
        break;
    case EV_VBUS:
        snprintf(buf, n, "%s %s", kind, e->vbus ? "plugged" : "unplugged");
        break;
    case EV_SETTIME:
        snprintf(buf, n, "%s %lld", kind, (long long)e->unix_s);
        break;
    default:
        snprintf(buf, n, "%s", kind);
        break;
    }
    return buf;
}

static void tick_1s(void *arg)
{
    (void)arg;
    omni_event_t e = {.kind = EV_TICK_1S, .t_ms = (uint32_t)(esp_timer_get_time() / 1000)};
    omni_events_post(&e);
}

static esp_timer_handle_t s_tick_timer;

void board_tick_run(bool on)
{
    if (s_tick_timer == NULL) return;
    if (on) esp_timer_start_periodic(s_tick_timer, 1000000); /* ESP_ERR_INVALID_STATE if already running: fine */
    else esp_timer_stop(s_tick_timer);                       /* likewise if stopped */
}

esp_err_t board_init(void)
{
    s_events = xQueueCreate(EVENT_QUEUE_LEN, sizeof(omni_event_t));
    if (s_events == NULL) return ESP_ERR_NO_MEM;
    power_init();

    const esp_timer_create_args_t tick = {.callback = tick_1s, .name = "tick_1s"};
    if (esp_timer_create(&tick, &s_tick_timer) != ESP_OK || esp_timer_start_periodic(s_tick_timer, 1000000) != ESP_OK)
        return ESP_ERR_NO_MEM;

    s_panel_mx = xSemaphoreCreateMutex();   /* permanent, internal (about 80 B) */
    if (s_panel_mx == NULL) return ESP_ERR_NO_MEM;
    heap_caps_register_failed_alloc_callback(on_alloc_failed);
    /* first, while internal RAM is still in one piece */
    s_stage = heap_caps_malloc(STAGE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_stage == NULL) ESP_LOGE(TAG, "display staging buffer: no internal DMA RAM; flushes bounce");
    board_heap_sample();
    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start failed");
        return ESP_FAIL;
    }
    /* The link wraps into the BSP (CMakeLists.txt) fail silently if a BSP update renames, inlines or moves the
     * calls: then sleep/wake cannot reach the panel and every flush needs internal RAM (boot loops before). The BSP
     * is pinned (idf_component.yml); a dev bump that breaks them stops here, loudly, on the first boot. */
    if (!s_display_wrapped || !s_lvgl_init_wrapped || !power_panel_captured()) {
        ESP_LOGE(TAG, "BSP link wraps did not run (register_display %d, lv_adapter_init %d, co5300 %d): check the BSP "
                 "version", s_display_wrapped, s_lvgl_init_wrapped, power_panel_captured());
        abort();
    }
    const esp_lv_adapter_draw_bitmap_callbacks_t cbs = {.custom_draw_bitmap = draw_staged};
    if (esp_lv_adapter_set_draw_bitmap_callbacks(disp, &cbs, NULL) != ESP_OK)
        ESP_LOGE(TAG, "draw-bitmap hook not set: flushes bounce");
    if (bsp_display_lock(-1) == ESP_OK) {
        lv_display_add_event_cb(disp, on_flush_start, LV_EVENT_FLUSH_START, NULL);
        bsp_display_unlock();
    }

    esp_err_t first = ESP_OK, ret;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if ((ret = axp2101_init(bus)) != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101: %s", esp_err_to_name(ret));
        if (first == ESP_OK) first = ret;
    }
    if ((ret = imu_init(bus)) != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658: %s", esp_err_to_name(ret));
        if (first == ESP_OK) first = ret;
    }
    if ((ret = input_init()) != ESP_OK) {
        ESP_LOGE(TAG, "input: %s", esp_err_to_name(ret));
        if (first == ESP_OK) first = ret;
    }
    return first;
}

int board_battery_percent(void) { return axp2101_battery_percent(); }

bool board_charging(void) { return axp2101_charging(); }

int board_vbat_mv(void) { return axp2101_vbat_mv(); }

bool board_vbus_present(void)
{
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (power_debug_vbus_all_get() >= 0) return power_debug_vbus_all_get() != 0; /* console `usbsim ... 3` */
#endif
    return axp2101_vbus_confirmed();
}

bool board_vbus_assumed(void) { return axp2101_vbus_failsafe(); }

unsigned board_vbus_read_failures(void) { return axp2101_vbus_read_failures(); }

bool board_touch_pressed(void) { return input_touch_pressed(); }

omni_pose_t board_pose(void) { return imu_pose(); }

unsigned board_imu_read_failures(void) { return imu_read_failures(); }

void board_set_touch_claim_w(bool (*claim)(int16_t, int16_t, void *), void *ctx, uint8_t want)
{
    input_set_touch_claim(claim, ctx, want);
}
void board_set_touch_claim(bool (*claim)(int16_t, int16_t, void *), void *ctx) { board_set_touch_claim_w(claim, ctx, 0); }

void board_debug_touch(bool pressed, int16_t x, int16_t y) { input_debug_touch(pressed, x, y); }

void board_trace(bool on) { imu_trace(on); }

esp_err_t board_trace_rec(unsigned seconds, unsigned delay_s) { return imu_trace_rec(seconds, delay_s); }

void board_trace_dump(void) { imu_trace_dump(); }
