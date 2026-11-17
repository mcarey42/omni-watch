#pragma once
/* Power manager: carries out the omni_pwr_* policy's actions on the hardware.
 *
 *  AWAKE   panel on at the configured brightness, LVGL running, CPU fixed at 240 MHz, touch + BOOT read.
 *  GLANCE  as AWAKE, same brightness; the shell shows the face's low-luminance glance layout (Task 10).
 *  ASLEEP  panel off (CO5300 0x28 display off + 0x10 sleep in), LVGL worker and tick timer paused, touch
 *          and BOOT not read (PWR polled at 10 Hz), Wi-Fi off (unless net holds it: power_wifi_hold), and
 *          esp_pm_configure({240, 40, light_sleep}) so the CPU light-sleeps between the IMU task's 25 Hz polls. Waking: PWR short or a lift.
 *
 * Light sleep is used only on battery (no VBUS): USB-Serial-JTAG cannot run in light sleep on the ESP32-S3.
 * On USB power "asleep" is panel off + 40 MHz DFS with the console alive. A USB plug-in while asleep turns
 * light sleep off and forces a USB re-enumeration (power_asleep_poll). CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION
 * also holds light sleep off while a USB host is sending frames.
 */
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "omni/power.h"
#include "esp_lcd_types.h"

void power_apply(omni_pwr_action_t a);   /* display on/off, brightness, LVGL pause/resume, idle */
void power_set_brightness(uint8_t pct);  /* configured brightness, clamped to 5..100 -> bsp_display_brightness_set */
omni_pwr_state_t power_state(void);      /* the state the hardware is in (AWAKE after boot) */
/* The net component holds Wi-Fi on through a provisioning session or a sync attempt that may outlast the
 * screen; while held, going to sleep leaves Wi-Fi alone (net turns it off itself when the attempt ends). */
void power_wifi_hold(bool on);
/* Total time spent in automatic light sleep since boot (us; only on battery). Any task but LVGL's (R26). */
uint64_t power_asleep_us(void);
/* Panel commands go out only with LVGL paused; one whose pause timed out is deferred, and power_service (the event
 * loop's 1 s tick, awake) sends it. Any task but LVGL's (R26). */
void power_service(void);
unsigned power_panel_deferrals(void);    /* panel commands deferred by an LVGL pause timeout since boot */
/* Board-internal: power_init from board_init; power_asleep_poll from the input task's 10 Hz poll while asleep. */
void power_init(void);
bool power_panel_captured(void); /* the CO5300 panel/IO handles were captured (link wrap, power.c) */
void power_asleep_poll(void);    /* VBUS vs the light-sleep configuration; a deferred panel-off */
esp_lcd_panel_io_handle_t power_panel_io(void); /* the captured CO5300 panel IO (NULL before bsp_display_start) */
esp_lcd_panel_handle_t power_panel(void);       /* the captured CO5300 panel (board_direct_draw) */
/* board_lvgl_pause: begin pauses LVGL (unless power has it paused already) and holds the power mutex until end, from
 * the same task. false: the pause timed out (nothing held). Never from LVGL context. */
bool power_lvgl_pause_begin(void);
void power_lvgl_pause_end(void);
#if CONFIG_OMNI_DEBUG_CONSOLE
void power_debug_vbus(int v);    /* console `usbsim`: 0 = power sees no VBUS, 1 = VBUS, -1 = the real reading */
void power_debug_vbus_lapsing(int v); /* console `pm vbus`: the same, lapsing to -1 after OMNI_AWAKE_HOLD_MAX_MS */
unsigned power_debug_vbus_edges(void); /* VBUS changes power_asleep_poll acted on, since boot */
void power_debug_vbus_all(int v); /* as power_debug_vbus, and board_vbus_present follows too */
int power_debug_vbus_all_get(void);
void power_debug_sleeps(uint32_t *sleeps, uint32_t *rejects); /* light-sleep exits with time slept / with none */
#endif
