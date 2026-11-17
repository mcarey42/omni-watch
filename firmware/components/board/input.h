#pragma once
/* Input task: CST9217 touch at 60 Hz -> omni_gesture_feed, BOOT (GPIO0) -> omni_button_feed, AXP2101
 * PWR-key IRQ status at ~20 Hz. Posts EV_GESTURE / EV_BOOT / EV_PWR.
 *
 * This task is the only reader of the touch controller (on its IRQ). LVGL's indev (BSP + esp_lvgl_adapter)
 * is served the same samples through the adapter's custom_touch_read hook, so both see one consistent stream. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t input_init(void);   /* after bsp_display_start(); starts the task */
void input_set_touch_claim(bool (*claim)(int16_t, int16_t, void *), void *ctx, uint8_t want); /* see board_set_touch_claim_w */
void input_debug_touch(bool pressed, int16_t x, int16_t y);                       /* see board_debug_touch */
/* false while the screen is off: touch and BOOT are not read (touch IRQs are discarded, LVGL sees a
 * release), and only the PWR key and VBUS (-> power_asleep_poll) are polled, at 10 Hz. */
void input_set_awake(bool awake);
bool input_touch_pressed(void);   /* a finger is on the screen (latest sample) */
