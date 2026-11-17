#include "power.h"
#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "omni_events.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "lvgl.h"
#include "axp2101.h"
#include "omni/vbus.h"
#include "input.h"
#include "board.h"

static const char *TAG = "power";

#define BRIGHTNESS_MIN 5
#define BRIGHTNESS_MAX 100
#define CO5300_SLPIN 0x10
#define CO5300_SLPOUT 0x11
#define SLPOUT_SETTLE_MS 60    /* as the CO5300 driver's own init list */
#define LVGL_PAUSE_TIMEOUT_MS 1000
#define CPU_MAX_MHZ 240
#define CPU_IDLE_MIN_MHZ 40
#define USB_RECONNECT_MS 200   /* D+ released this long reads as an unplug to the host */

/* The BSP keeps its CO5300 panel and IO handles private. The board component links with
 * -Wl,--wrap=esp_lcd_new_panel_co5300 (CMakeLists.txt) so the handles are captured as the BSP creates them. */
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;

esp_err_t __real_esp_lcd_new_panel_co5300(const esp_lcd_panel_io_handle_t io,
                                          const esp_lcd_panel_dev_config_t *cfg, esp_lcd_panel_handle_t *ret);
esp_err_t __wrap_esp_lcd_new_panel_co5300(const esp_lcd_panel_io_handle_t io,
                                          const esp_lcd_panel_dev_config_t *cfg, esp_lcd_panel_handle_t *ret)
{
    esp_err_t err = __real_esp_lcd_new_panel_co5300(io, cfg, ret);
    if (err == ESP_OK) {
        s_io = io;
        s_panel = *ret;
    }
    return err;
}

bool power_panel_captured(void) { return s_panel != NULL && s_io != NULL; }

esp_lcd_panel_handle_t power_panel(void) { return s_panel; }

static omni_pwr_state_t s_state = PWR_AWAKE; /* bsp_display_start leaves the panel on */
static uint8_t s_brightness = BRIGHTNESS_MAX;
/* Serialises the event loop (power_apply), the input task (power_asleep_poll) and brightness changes, so
 * esp_pm_configure calls cannot interleave (e.g. a wake ending at 40 MHz with light sleep on). */
static SemaphoreHandle_t s_mutex;
static bool s_vbus_cfg; /* VBUS as seen when light sleep was configured; guarded by s_mutex */
static bool s_cfg_assumed; /* ... and that value was the VBUS-read fallback's assumption (N2); s_mutex */
static bool s_light_cfg;   /* light sleep is configured on now (asleep); s_mutex */
static bool s_idle_held;   /* the effective Wi-Fi hold when light sleep was last set (asleep); s_mutex */
static uint32_t s_asleep_since_ms; /* when the screen last went to sleep (the M1 backstop); s_mutex */
static volatile bool s_wifi_hold; /* net owns Wi-Fi for now (power_wifi_hold) */

void power_wifi_hold(bool on) { s_wifi_hold = on; }

#if CONFIG_OMNI_DEBUG_CONSOLE
static volatile int s_vbus_force = -1; /* console `usbsim`: power's view of VBUS (-1 = the AXP2101's) */
static volatile bool s_vbus_lapses;    /* set by `pm vbus`: the override lapses like `awake hold` (R66, final M6) */
static volatile uint32_t s_vbus_since_ms;
void power_debug_vbus(int v)
{
    s_vbus_lapses = false;
    s_vbus_force = v;
}
void power_debug_vbus_lapsing(int v)
{
    s_vbus_since_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_vbus_lapses = v >= 0;
    s_vbus_force = v;
}
static volatile int s_vbus_force_all = -1; /* the same for board_vbus_present (the event loop, batrec, clock) */
void power_debug_vbus_all(int v) { s_vbus_force_all = s_vbus_force = v; }
int power_debug_vbus_all_get(void) { return s_vbus_force_all; }
static volatile unsigned s_vbus_edges; /* power_asleep_poll saw VBUS change (console `usbsim`) */
unsigned power_debug_vbus_edges(void) { return s_vbus_edges; }
#endif

/* VBUS as the light-sleep configuration sees it */
static bool vbus_now(void)
{
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (s_vbus_force >= 0 && s_vbus_lapses &&
        omni_hold_expired(s_vbus_since_ms, (uint32_t)(esp_timer_get_time() / 1000), OMNI_AWAKE_HOLD_MAX_MS)) {
        /* a lost console must not leave light sleep off on battery (or on over USB) until a reboot */
        s_vbus_lapses = false;
        s_vbus_force = -1;
        ESP_LOGW(TAG, "pm vbus override expired after %u min: VBUS read again", (unsigned)(OMNI_AWAKE_HOLD_MAX_MS / 60000));
    }
    if (s_vbus_force >= 0) return s_vbus_force != 0;
#endif
    return axp2101_vbus_present();
}

/* Time spent in automatic light sleep (the clock calibration's "asleep seconds": esp_timer is advanced over a
 * light sleep by its length on the RC slow clock, so the clock's error builds up there). The exit callback runs
 * in the idle task after each light sleep, with the slept time IDF measured. */
static uint64_t s_asleep_us;
static portMUX_TYPE s_asleep_lock = portMUX_INITIALIZER_UNLOCKED;

#if CONFIG_OMNI_DEBUG_CONSOLE
static volatile uint32_t s_sleeps, s_sleep_rejects; /* light sleeps taken / skipped (slept 0), console `usbsim` */
#endif

static esp_err_t IRAM_ATTR on_light_sleep_exit(int64_t slept_us, void *arg)
{
    (void)arg;
#if CONFIG_OMNI_DEBUG_CONSOLE
    if (slept_us > 0) s_sleeps++;
    else s_sleep_rejects++;
#endif
    if (slept_us > 0) {
        portENTER_CRITICAL_SAFE(&s_asleep_lock);
        s_asleep_us += (uint64_t)slept_us;
        portEXIT_CRITICAL_SAFE(&s_asleep_lock);
    }
    return ESP_OK;
}

#if CONFIG_OMNI_DEBUG_CONSOLE
void power_debug_sleeps(uint32_t *sleeps, uint32_t *rejects)
{
    *sleeps = s_sleeps;
    *rejects = s_sleep_rejects;
}
#endif

uint64_t power_asleep_us(void)
{
    portENTER_CRITICAL_SAFE(&s_asleep_lock);
    uint64_t v = s_asleep_us;
    portEXIT_CRITICAL_SAFE(&s_asleep_lock);
    return v;
}

void power_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    esp_pm_sleep_cbs_register_config_t cbs = {.exit_cb = on_light_sleep_exit};
    esp_err_t err = esp_pm_light_sleep_register_cbs(&cbs);
    if (err != ESP_OK) ESP_LOGE(TAG, "light sleep callback: %s (asleep time not counted)", esp_err_to_name(err));
}

static void lock(void)
{
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock(void)
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

/* QSPI command framing as the BSP and the CO5300 driver use it: opcode 0x02, command in bits 15:8. */
static esp_err_t panel_cmd(uint8_t cmd)
{
    if (s_io == NULL) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_io_tx_param(s_io, (0x02 << 24) | ((uint32_t)cmd << 8), NULL, 0);
}

/* Every panel command (brightness, display on/off, sleep in/out) is a polling SPI transaction on the bus LVGL
 * flushes frames over; sent mid-flush, two tasks can each wait for a result the other consumed, and LVGL or the
 * caller hangs (the brightness hang). So a command is only ever sent with LVGL paused (the worker finishes its pass
 * first). display_off leaves LVGL paused until display_on; otherwise lvgl_hold pauses it, trying twice. If the pause
 * times out twice the command is not sent: it is logged and left pending (s_*_pending), and power_service (the
 * event loop's tick while awake) or power_asleep_poll (the input task while asleep) sends it later.
 * The direct path (board.h, Stage 4a) draws without LVGL, so a pause alone does not free the bus: every pause made here
 * also holds the panel mutex (board_direct_hold), and display_off disables the path (board_direct_enable(false))
 * before DISPOFF, so it stays off until panel_on's DISPON. display_on's SLPOUT goes out without lvgl_hold: LVGL is
 * still paused from display_off and the path is disabled with its sender drained (D5). */
static bool s_lvgl_paused;         /* paused by display_off, until display_on */
static uint8_t s_brightness_sent;  /* 0 = never sent */
static bool s_panel_slept;         /* SLPIN went out: SLPOUT is due at the next wake */
static bool s_on_pending;          /* display_on could not send DISPON (+ brightness) yet */
static bool s_off_pending;         /* display_off could not send DISPOFF + SLPIN yet */
static unsigned s_deferred;        /* panel commands deferred by a pause timeout, since boot */

/* True when LVGL is paused (*resume: by this call, so the caller resumes it). */
static bool lvgl_hold(const char *what, bool *resume)
{
    *resume = false;
    if (s_lvgl_paused) return true;
    for (int i = 0; i < 2; i++) {
        if (esp_lv_adapter_pause(LVGL_PAUSE_TIMEOUT_MS) == ESP_OK) {
            *resume = true;
            board_direct_hold(true);   /* and no direct band on the wire */
            return true;
        }
    }
    s_deferred++;
    ESP_LOGW(TAG, "%s: LVGL pause timed out twice, not sent (deferred)", what);
    return false;
}

static void panel_off(void);

static void lvgl_release(bool resume)
{
    if (!resume) return;
    board_direct_hold(false);
    esp_lv_adapter_resume();
}

/* The glance keeps the configured brightness: its low luminance is in the layout's colours (28-40 %, as the
 * mockups' sleepFrame draws it); dimming the panel as well would make it unreadable. Caller holds the mutex. */
static void apply_brightness(void)
{
    if (s_brightness == s_brightness_sent) return;
    bool resume;
    if (!lvgl_hold("brightness", &resume)) return; /* s_brightness_sent unchanged: power_service retries */
    if (bsp_display_brightness_set((int)s_brightness) == ESP_OK) s_brightness_sent = s_brightness;
    lvgl_release(resume);
}

static void pm_set(int min_mhz, bool light_sleep)
{
    const esp_pm_config_t cfg = {
        .max_freq_mhz = CPU_MAX_MHZ, .min_freq_mhz = min_mhz, .light_sleep_enable = light_sleep,
    };
    esp_err_t err = esp_pm_configure(&cfg);
    if (err != ESP_OK) ESP_LOGE(TAG, "esp_pm_configure: %s", esp_err_to_name(err));
}

/* Light sleep only on battery. On USB power it saves nothing that matters, and a host that tries to enumerate the
 * board while it light-sleeps fails ("Device Descriptor Request Failed" on Windows) and never retries, so the console
 * would stay dead until a replug. The VBUS value light sleep was configured for is kept (s_vbus_cfg), so
 * power_asleep_poll compares against what was really used and a plug-in between this call and the input task's first
 * poll cannot be missed.
 *
 * Light sleep also waits while net holds Wi-Fi (a sync or provisioning still running as the screen goes off): enabling
 * it allocates 14 968 B of internal RAM (the CPU and cache-tag retention buffers, measured), and on battery that landed
 * on top of Wi-Fi's own peak (internal minimum 43 KB with a sync alone, 32 KB with light sleep switched on mid-sync,
 * reproduced with usbsim). power_asleep_poll switches it on within 100 ms of the hold's end; every sync and
 * provisioning outcome ends the hold, and power ignores a hold that outlasts OMNI_WIFI_HOLD_MAX_MS asleep (M1: a net
 * task blocked in a driver call never ends it). Disabling light sleep frees the retention buffers again
 * (sleep_cpu_configure(false) -> esp_sleep_cpu_retention_deinit in IDF 5.5); each change logs the internal free before
 * and after it (M2). Caller holds the power mutex. */
static bool wifi_held_locked(void)
{
    return omni_wifi_hold_effective(s_wifi_hold, s_asleep_since_ms, (uint32_t)(esp_timer_get_time() / 1000));
}

static void idle_apply_locked(void)
{
    const bool held = wifi_held_locked();
    const bool light = !s_vbus_cfg && !held;
    const bool log = light != s_light_cfg || held != s_idle_held;
    const bool change = light != s_light_cfg;
    const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_light_cfg = light;
    s_idle_held = held;
    pm_set(CPU_IDLE_MIN_MHZ, light);
    if (log)
        ESP_LOGI(TAG, "light sleep %s%s; internal free %u -> %u",
                 light ? "on" : s_vbus_cfg ? "off (USB)" : "deferred while Wi-Fi is up",
                 s_wifi_hold && !held ? " (Wi-Fi hold ignored: asleep 12 min)" : "", (unsigned)before,
                 (unsigned)(change ? heap_caps_get_free_size(MALLOC_CAP_INTERNAL) : before));
}

static void enter_idle_locked(void)
{
    if (!s_wifi_hold) {
        esp_err_t err = esp_wifi_stop();
        if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) ESP_LOGW(TAG, "esp_wifi_stop: %s", esp_err_to_name(err));
    }
    s_vbus_cfg = vbus_now();
    s_cfg_assumed = axp2101_vbus_failsafe();
    idle_apply_locked();
}

/* Drop the D+ pull-up briefly so the host sees an unplug/replug and enumerates the board again, now that the chip is
 * awake. The override writes conf0.dp_pullup, and that bit stays in force after the override is lifted (the D+
 * pull-up is on when either it or the pad's own pull-up is set): it must be written back to 1 before the override
 * goes, or D+ stays released and the host never sees the board again. esp_restart does not reset this register;
 * only a power cycle did (Defect A). Blocks the caller USB_RECONNECT_MS; without the mutex. */
static void usb_reconnect(void)
{
    const usb_serial_jtag_pull_override_vals_t detach = {0};
    const usb_serial_jtag_pull_override_vals_t attach = {.dp_pu = true};
    usb_serial_jtag_ll_phy_enable_pull_override(&detach);
    vTaskDelay(pdMS_TO_TICKS(USB_RECONNECT_MS));
    usb_serial_jtag_ll_phy_enable_pull_override(&attach);
    usb_serial_jtag_ll_phy_disable_pull_override();
}

/* Polled by the input task (10 Hz) while asleep: reconfigure when VBUS differs from the value the current
 * idle configuration was made for, and send a panel-off that a pause timeout deferred. */
void power_asleep_poll(void)
{
    lock();
    if (s_state == PWR_ASLEEP && s_off_pending) panel_off();
    bool present = vbus_now();
    bool changed = s_state == PWR_ASLEEP && present != s_vbus_cfg;
    if (changed) {
#if CONFIG_OMNI_DEBUG_CONSOLE
        s_vbus_edges++;
#endif
        s_vbus_cfg = present;
        s_cfg_assumed = axp2101_vbus_failsafe();
        idle_apply_locked();
    } else if (s_state == PWR_ASLEEP && s_idle_held != wifi_held_locked()) {
        idle_apply_locked();   /* net let go of Wi-Fi (or took it, or the backstop ran out): light sleep follows */
    }
    unlock();
    if (changed) { /* the event loop's tick does not run while asleep: tell it, for the battery record */
        omni_event_t e = {.kind = EV_VBUS, .t_ms = (uint32_t)(esp_timer_get_time() / 1000), .vbus = present};
        omni_events_post(&e);
    }
    if (!changed || !present) return;
    /* USB plugged in while light-sleeping: the host may already have failed to enumerate the board. */
    usb_reconnect();
    ESP_LOGI(TAG, "USB power: light sleep off, USB reconnected");
}

/* CONFIG_ESP_SLEEP_GPIO_RESET_WORKAROUND (S3 default) isolates every GPIO in light sleep. The panel's reset
 * and chip select and the touch controller's reset are outputs that must hold their level, or the sleeping
 * panel could be reset or selected by a floating line; they keep their normal configuration in sleep. */
static void keep_panel_pins_in_sleep(void)
{
    static bool done;
    if (done) return;
    gpio_sleep_sel_dis(BSP_LCD_RST);
    gpio_sleep_sel_dis(BSP_LCD_CS);
    gpio_sleep_sel_dis(BSP_LCD_TOUCH_RST);
    done = true;
}

/* DISPOFF + SLPIN with LVGL paused; LVGL stays paused while asleep (it only resumes in display_on). */
static void panel_off(void)
{
    bool resume;
    if (!lvgl_hold("panel off", &resume)) {
        s_off_pending = true;
        return;
    }
    s_lvgl_paused = true; /* kept paused: nothing is drawn while the panel is off */
    s_off_pending = false;
    if (s_panel == NULL || esp_lcd_panel_disp_on_off(s_panel, false) != ESP_OK) ESP_LOGE(TAG, "panel display off failed");
    if (panel_cmd(CO5300_SLPIN) == ESP_OK) s_panel_slept = true;
    else ESP_LOGE(TAG, "panel sleep in failed");
    /* LVGL stays paused, but the panel mutex goes back: the direct path is disabled (display_off), so its sender only
     * discards while the panel is off, and it must not block on the mutex the whole sleep */
    if (resume) board_direct_hold(false);
}

/* Brightness + DISPON with LVGL paused, after the fresh frame is in the panel's RAM. */
static void panel_on(void)
{
    bool resume;
    if (!lvgl_hold("panel on", &resume)) {
        s_on_pending = true;
        return;
    }
    s_on_pending = false;
    if (s_brightness != s_brightness_sent && bsp_display_brightness_set((int)s_brightness) == ESP_OK)
        s_brightness_sent = s_brightness;
    if (s_panel == NULL || esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK) ESP_LOGE(TAG, "panel on failed");
    lvgl_release(resume);
    board_direct_enable(true);   /* after DISPON (and outside the hold: enable takes the panel mutex itself) */
}

static void display_off(void)
{
    keep_panel_pins_in_sleep();
    input_set_awake(false);
    s_on_pending = false;
    board_direct_enable(false);   /* before DISPOFF: no sender draw from now on, drop mode force-ended (D4); panel_off's
                                   * hold waits for the wire with LVGL paused (D5) */
    panel_off();
}

/* Redraw the whole screen into the panel's RAM before it is shown, so nothing stale (or lost in sleep) appears:
 * SLPOUT (LVGL still paused from display_off), settle, resume, full refresh, then brightness + DISPON in a pause. */
static void display_on(void)
{
    pm_set(CPU_MAX_MHZ, false);
    s_light_cfg = s_idle_held = false;
    s_off_pending = false; /* if display_off never got through, the panel simply stayed on */
    if (s_lvgl_paused) {
        if (s_panel_slept) {
            if (panel_cmd(CO5300_SLPOUT) != ESP_OK) ESP_LOGE(TAG, "panel sleep out failed");
            s_panel_slept = false;
            vTaskDelay(pdMS_TO_TICKS(SLPOUT_SETTLE_MS));
        }
        esp_lv_adapter_resume();
        s_lvgl_paused = false;
    }
    s_brightness_sent = 0; /* the panel slept: send it again */
    if (bsp_display_lock(-1) == ESP_OK) {
        lv_obj_invalidate(lv_screen_active());
        bsp_display_unlock();
    }
    esp_lv_adapter_refresh_now(NULL);
    panel_on();
    input_set_awake(true);
}

void power_apply(omni_pwr_action_t a)
{
    lock();
    switch (a) {
    case PA_WAKE_FULL:
    case PA_WAKE_SKIP_REVEAL: { /* the reveal itself is Task 10's */
        omni_pwr_state_t was = s_state;
        s_state = PWR_AWAKE;
        if (was == PWR_ASLEEP) {
            display_on();
            board_tick_run(true); /* the 1 s tick only runs while the screen is on */
        } else {
            apply_brightness();
        }
        break;
    }
    case PA_TO_GLANCE:
        s_state = PWR_GLANCE;
        apply_brightness();
        break;
    case PA_TO_SLEEP:
        if (s_state == PWR_ASLEEP) break;
        s_state = PWR_ASLEEP;
        s_asleep_since_ms = (uint32_t)(esp_timer_get_time() / 1000);
        board_tick_run(false); /* no 1 s wake-ups while asleep */
        display_off();
        enter_idle_locked();
        break;
    case PA_NONE:
    default:
        break;
    }
    unlock();
}

void power_set_brightness(uint8_t pct)
{
    if (pct < BRIGHTNESS_MIN) pct = BRIGHTNESS_MIN;
    if (pct > BRIGHTNESS_MAX) pct = BRIGHTNESS_MAX;
    lock();
    s_brightness = pct;
    if (s_state != PWR_ASLEEP) apply_brightness();
    unlock();
}

void power_service(void)
{
    lock();
    bool reconnect = false;
    if (s_state != PWR_ASLEEP) {
        if (s_on_pending) panel_on();
        else apply_brightness();
        /* N2: the last light sleep was set up for the VBUS-read fallback's assumed battery, and USB is back while
         * awake: the asleep poll never sees that edge (and the next sleep would just set up USB), so pulse here. */
        reconnect = omni_vbus_reenumerate(s_vbus_cfg, s_cfg_assumed, vbus_now());
        if (reconnect) {
            s_vbus_cfg = true;
            s_cfg_assumed = false;
        }
    }
    unlock();
    if (reconnect) {
        usb_reconnect();   /* the event loop waits USB_RECONNECT_MS, once */
        ESP_LOGI(TAG, "USB power back after the VBUS-read fallback: USB reconnected");
    }
}

unsigned power_panel_deferrals(void) { return s_deferred; }

esp_lcd_panel_io_handle_t power_panel_io(void) { return s_io; }

/* board_lvgl_pause: the power mutex is held from begin to end, so power's own pauses (panel commands, display_off)
 * and this one never interleave, and a pause power already holds (asleep) is left as it is. */
static bool s_board_resume;

bool power_lvgl_pause_begin(void)
{
    lock();
    s_board_resume = false;
    if (s_lvgl_paused) return true;
    if (esp_lv_adapter_pause(LVGL_PAUSE_TIMEOUT_MS) != ESP_OK) {
        unlock();
        return false;
    }
    s_board_resume = true;
    board_direct_hold(true);
    return true;
}

void power_lvgl_pause_end(void)
{
    if (s_board_resume) {
        board_direct_hold(false);
        esp_lv_adapter_resume();
    }
    s_board_resume = false;
    unlock();
}

omni_pwr_state_t power_state(void) { return s_state; }
