#include "input.h"
#include <string.h>
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_lv_adapter_input.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "axp2101.h"
#include "omni_events.h"
#include "power.h"

static const char *TAG = "input";

#define INPUT_PERIOD_MS 16  /* ~60 Hz touch; also the BOOT feed (needs <= ~20 ms, idle included) */
#define PKEY_EVERY 3        /* PWR-key IRQ poll every 3rd period, ~20 Hz */
#define ASLEEP_PERIOD_MS 100 /* while asleep only the PWR key (latched by the AXP) is polled, at 10 Hz */
#define TOUCH_STUCK_MS 100  /* pressed but no IRQ this long: read once anyway (a missed release IRQ) */
#define INPUT_TASK_PRIO 4
#define BOOT_GPIO GPIO_NUM_0

typedef struct {
    uint8_t count;
    esp_lcd_touch_point_data_t pt[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
} touch_snap_t;

static lv_indev_t *s_indev;
static esp_lcd_touch_handle_t volatile s_tp;  /* learned from the first touch IRQ */
static volatile uint32_t s_irqs;              /* touch IRQs seen (ISR increments) */
static uint32_t s_irqs_read;
static uint32_t s_t_read;                     /* last controller read */
static volatile bool s_awake = true;          /* false: touch and BOOT are not read (screen off) */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static touch_snap_t s_snap;                   /* latest sample, guarded by s_lock */

typedef struct {
    bool (*fn)(int16_t, int16_t, void *);
    void *ctx;
    uint8_t want; /* OMNI_GW_*: the appended gesture events this claim asks for (0 = none) */
} claim_t;
static claim_t s_claim;                       /* guarded by s_lock: the pair is swapped as one */

typedef struct {
    bool active, pressed;
    int16_t x, y;
} fake_touch_t;
static fake_touch_t s_fake;                   /* debug touch (console), guarded by s_lock */

static omni_gesture_sm_t s_gest;
static int16_t s_last_x, s_last_y; /* last pressed point: the release is reported there */
static omni_button_sm_t s_boot;

/* Touch IRQ (ISR context, via the adapter): a new controller frame is ready. */
static void IRAM_ATTR touch_irq(esp_lcd_touch_handle_t tp, void *ctx)
{
    (void)ctx;
    s_tp = tp;
    s_irqs++;
}

/* Called from the LVGL task when the adapter wants touch data: hand it our latest sample. */
static esp_err_t lvgl_touch_read(esp_lcd_touch_handle_t tp, esp_lcd_touch_point_data_t *points,
                                 uint8_t *count, uint8_t max_count, void *ctx)
{
    (void)tp;
    (void)ctx;
    taskENTER_CRITICAL(&s_lock);
    uint8_t n = s_snap.count < max_count ? s_snap.count : max_count;
    memcpy(points, s_snap.pt, n * sizeof points[0]);
    taskEXIT_CRITICAL(&s_lock);
    *count = n;
    return ESP_OK;
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

#define MOVE_RESERVE 6 /* queue slots a drag move leaves free for everything else */

static void post_gestures(const omni_gesture_t *g, int n, uint32_t t)
{
    for (int i = 0; i < n; i++) {
        /* Moves come at 60 Hz and carry absolute positions: while the event loop is behind (rendering a screen),
         * drop them rather than fill the queue and lose a release, a tap or a button. */
        if (g[i].kind == G_DRAG_MOVE && omni_events() && uxQueueSpacesAvailable(omni_events()) <= MOVE_RESERVE) continue;
        omni_event_t e = {.kind = EV_GESTURE, .t_ms = t, .g = g[i]};
        omni_events_post(&e);
    }
}

/* Polled while untouched, every CST9217 read fails with ESP_ERR_INVALID_RESPONSE (no ACK 0xAB; observed on
 * this board), so a read is only meaningful after the controller raised INT. Read only when an IRQ arrived
 * since the last read, and, as the vendor adapter does, take a failed or empty IRQ read as "released".
 * Without an IRQ the last sample holds, except that a press with no IRQ for TOUCH_STUCK_MS is read once
 * more, so a missed release IRQ cannot leave the press stuck (an untouched read fails -> released). */
static void poll_touch(uint32_t t)
{
    esp_lcd_touch_handle_t tp = s_tp;
    uint32_t irqs = s_irqs;
    touch_snap_t snap = {0};
    bool stuck = s_snap.count > 0 && (t - s_t_read) >= TOUCH_STUCK_MS;
    taskENTER_CRITICAL(&s_lock);
    fake_touch_t fake = s_fake;
    if (fake.active && !fake.pressed) s_fake.active = false; /* a fake release is fed once */
    claim_t claim = s_claim;
    taskEXIT_CRITICAL(&s_lock);
    if (fake.active) { /* the console drives the gesture path; the controller and LVGL are left alone */
        if (fake.pressed) {
            s_last_x = fake.x;
            s_last_y = fake.y;
        }
        omni_gesture_t g[2];
        int n = omni_gesture_feed_w(&s_gest, t, fake.pressed, s_last_x, s_last_y, claim.fn, claim.ctx, claim.want, g);
        post_gestures(g, n, t);
        return;
    }
    if (tp != NULL && (irqs != s_irqs_read || stuck)) {
        s_irqs_read = irqs;
        s_t_read = t;
        if (esp_lcd_touch_read_data(tp) != ESP_OK ||
            esp_lcd_touch_get_data(tp, snap.pt, &snap.count, CONFIG_ESP_LCD_TOUCH_MAX_POINTS) != ESP_OK)
            memset(&snap, 0, sizeof snap);
        taskENTER_CRITICAL(&s_lock);
        bool changed = memcmp(&snap, &s_snap, sizeof snap) != 0;
        s_snap = snap;
        taskEXIT_CRITICAL(&s_lock);
        if (changed) esp_lv_adapter_touch_notify_interrupt(s_indev);
    } else {
        taskENTER_CRITICAL(&s_lock);
        snap = s_snap;
        taskEXIT_CRITICAL(&s_lock);
    }
    if (snap.count > 0) {
        s_last_x = (int16_t)snap.pt[0].x;
        s_last_y = (int16_t)snap.pt[0].y;
    }
    omni_gesture_t g[2];
    int n = omni_gesture_feed_w(&s_gest, t, snap.count > 0, s_last_x, s_last_y, claim.fn, claim.ctx, claim.want, g);
    post_gestures(g, n, t);
}

static void poll_boot(uint32_t t)
{
    omni_press_t p = omni_button_feed(&s_boot, t, gpio_get_level(BOOT_GPIO) == 0);
    if (p != B_NONE) {
        omni_event_t e = {.kind = EV_BOOT, .t_ms = t, .press = p};
        omni_events_post(&e);
    }
}

/* Screen off: drop any held sample (LVGL sees a release), reset the classifiers and skip the IRQs that
 * arrived meanwhile, so nothing from the asleep period leaks into the next wake. */
static void touch_quiesce(void)
{
    taskENTER_CRITICAL(&s_lock);
    bool had = s_snap.count > 0;
    memset(&s_snap, 0, sizeof s_snap);
    taskEXIT_CRITICAL(&s_lock);
    if (had) esp_lv_adapter_touch_notify_interrupt(s_indev);
    s_irqs_read = s_irqs;
    omni_gesture_reset(&s_gest);
    memset(&s_boot, 0, sizeof s_boot);
}

static void input_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    bool was_awake = true;
    for (unsigned i = 0;; i++) {
        bool awake = s_awake;
        if (awake != was_awake) {
            touch_quiesce();
            was_awake = awake;
        }
        if (!awake) {
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(ASLEEP_PERIOD_MS));
            s_irqs_read = s_irqs; /* touches while asleep do nothing (omni_pwr_on_touch) */
            power_asleep_poll();
            if (axp2101_pkey_short()) {
                omni_event_t e = {.kind = EV_PWR, .t_ms = now_ms()};
                omni_events_post(&e);
            }
            continue;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(INPUT_PERIOD_MS));
        uint32_t t = now_ms();
        poll_touch(t);
        poll_boot(t);
        if (i % PKEY_EVERY == 0 && axp2101_pkey_short()) {
            omni_event_t e = {.kind = EV_PWR, .t_ms = t};
            omni_events_post(&e);
        }
    }
}

esp_err_t input_init(void)
{
    const gpio_config_t boot = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t ret = gpio_config(&boot);
    if (ret != ESP_OK) return ret;

    omni_gesture_reset(&s_gest);
    memset(&s_boot, 0, sizeof s_boot);

    s_indev = bsp_display_get_input_dev();
    if (s_indev == NULL) return ESP_ERR_INVALID_STATE;
    const esp_lv_adapter_touch_callbacks_t cbs = {.on_interrupt = touch_irq, .custom_touch_read = lvgl_touch_read};
    ret = esp_lv_adapter_set_touch_callbacks(s_indev, &cbs);
    if (ret != ESP_OK) return ret;

    if (xTaskCreate(input_task, "input", 4096, NULL, INPUT_TASK_PRIO, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

void input_set_touch_claim(bool (*claim)(int16_t, int16_t, void *), void *ctx, uint8_t want)
{
    taskENTER_CRITICAL(&s_lock);
    s_claim = (claim_t){claim, ctx, want};
    taskEXIT_CRITICAL(&s_lock);
}

void input_debug_touch(bool pressed, int16_t x, int16_t y)
{
    taskENTER_CRITICAL(&s_lock);
    s_fake = (fake_touch_t){.active = true, .pressed = pressed, .x = x, .y = y};
    taskEXIT_CRITICAL(&s_lock);
}

void input_set_awake(bool awake) { s_awake = awake; }

/* A console finger (input_debug_touch: `inject drag/arc`) counts as a press too, until its release is fed: a face that
 * watches the press (the timer's lost-release cancel after 500 ms, its hold ring) saw the real controller's "up"
 * during an injected 1.5 s arc and cancelled the wind (Task 9: `inject arc` left the timer IDLE). */
bool input_touch_pressed(void)
{
    taskENTER_CRITICAL(&s_lock);
    bool p = s_snap.count > 0 || (s_fake.active && s_fake.pressed);
    taskEXIT_CRITICAL(&s_lock);
    return p;
}
