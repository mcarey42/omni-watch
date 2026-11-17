#pragma once
/* Board layer for the Waveshare ESP32-S3-Touch-AMOLED-1.75C: display + CST9217 touch (BSP), BOOT button
 * (GPIO0), AXP2101 PMU (PWR key, battery) and QMI8658 IMU (pose, lift-to-wake), all on the BSP's I2C bus
 * (SDA 15 / SCL 14).
 *
 * Interrupt routing (schematic, vendor/waveshare-examples/hardware/schematic, page 1):
 *  - QMI_INT1 reaches GPIO21 (net label "QMI_INT1 GPIO21" beside U3; the pin table on the same page prints
 *    QMI_INT1 one row low, next to GPIO38, which is QSPI_SCL). Not yet verified on hardware.
 *    Not used: while asleep the CPU is in automatic light sleep and the IMU task keeps polling at 25 Hz
 *    (power.h). INT1 wake-on-motion would only matter for a deeper sleep.
 *  - QMI_INT2 reaches no GPIO.
 *  - AXP_IRQ reaches no GPIO (only a 10k pull-up to VCC-RTC), so the PWR key is polled over I2C.
 *  - Touch INT is GPIO11 (owned by the BSP / LVGL adapter).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "omni/power.h"
#include "lvgl.h"

/* Starts the display (bsp_display_start), touch, BOOT gpio, AXP2101 and QMI8658, the input and IMU
 * tasks, and a 1 s esp_timer that posts EV_TICK_1S. Returns ESP_FAIL if the display does not start
 * (nothing else is started then); otherwise the first PMU/IMU error, with everything that could start
 * running. */
esp_err_t board_init(void);

/* Start/stop the 1 s EV_TICK_1S timer (power.c stops it while asleep so the chip is not woken every second). */
void board_tick_run(bool on);

int board_battery_percent(void);   /* 0..100, or -1 if unknown (no battery / PMU error) */
bool board_charging(void);
int board_vbat_mv(void);           /* battery voltage, -1 if unknown */
bool board_vbus_present(void);     /* USB power present, as the records follow it (unchanged by the VBUS-read fallback) */
bool board_vbus_assumed(void);     /* the VBUS reads keep failing: battery assumed for light sleep (axp2101 fallback) */
unsigned board_vbus_read_failures(void); /* VBUS reads that failed since boot (each left the VBUS state as it was) */
bool board_touch_pressed(void);    /* a finger is on the screen now */
omni_pose_t board_pose(void);
unsigned board_imu_read_failures(void); /* QMI8658 reads that failed since boot (lift-to-wake is blind while they do) */

/* Internal-RAM telemetry (`state`, net state-change logs). The display's flushes and the Wi-Fi/BLE stacks need
 * contiguous internal DMA-capable RAM, so what matters is the largest free block, not the total. board_heap_sample
 * takes a reading and keeps the low-water mark; it runs on the event loop's tick, on each net state change, at
 * `state`, and at most every 200 ms from the start of an LVGL flush (the moment a flush needs its memory). Failed
 * heap allocations of any kind are counted (heap_caps failed-alloc callback), with the last one's size and caps. */
typedef struct {
    size_t dma_largest;       /* largest free internal DMA-capable block now, bytes */
    size_t dma_largest_min;   /* the smallest of those seen since boot */
    unsigned alloc_failures;  /* failed heap allocations since boot */
    size_t last_fail_size;
    uint32_t last_fail_caps;
} board_heap_t;
void board_heap_sample(void);
board_heap_t board_heap(void);
/* Display flushes sent without the internal staging buffer (board.c), each needing a DMA bounce allocation: 0. */
unsigned board_unstaged_flushes(void);

/* The display's flush hook (board.c): each LVGL flush is copied into the internal DMA staging buffer and sent. */
typedef struct {
    uint32_t flushes;              /* per window: staged flushes */
    uint32_t copy_us, send_us;     /* summed: the copies into the stage (with the wait for it); draw_bitmap calls */
    uint32_t hook_us_max;          /* the longest hook call */
    bool early;                    /* flush_ready given at the end of the hook, not by the transfer-done ISR */
} board_flush_stats_t;
void board_flush_stats(board_flush_stats_t *s, bool reset);
/* LVGL paused (true: paused, or already paused by power while asleep; false: the pause timed out) and resumed
 * (false). Pair the calls from one task; never from LVGL context. Panel and flush configuration change only so. */
bool board_lvgl_pause(bool pause);
/* Early release (Task 1 step 4, on by default): flush_ready as soon as a band is staged. false: from the ISR, as
 * Stage 2 did (console comparisons). */
bool board_display_flush_early(bool on);

/* Panel ownership for the direct path (Stage 4a lever B, main/ui/disp_direct.c). While direct mode is on, a sender task
 * draws internal-RAM bands straight to the panel and LVGL's flushes are dropped: each gets flush_ready at once and its
 * area is kept, so the hand-back can invalidate it. One FreeRTOS mutex (the panel mutex) guards every direct draw, the
 * waits, power's hold and the enable switch, so no direct band is on the wire while power sends a panel command (D5).
 * Never DMA from PSRAM to the panel: it underflows and wedges the SPI driver (inv. section 4e). */
#define BOARD_DIRECT_DROP_MAX 4
bool board_direct_begin(void);        /* LVGL ctx: waits for panel IO idle; from now on LVGL's flushes are dropped
                                       * (flush_ready at once, area kept); false while disabled (display off) */
esp_err_t board_direct_draw(int x0, int y0, int x1, int y1, const void *px);   /* sender only; tests the enabled
                                       * flag and draws under the panel mutex; ESP_ERR_INVALID_STATE while disabled
                                       * or force-ended (the caller discards and counts the band lost) */
void board_direct_wait(void);         /* sender: every queued transfer is on the wire (under the mutex) */
/* LVGL ctx (or the event loop under bsp_display_lock), after the sender drained: the stage is returned and flushes
 * reach the panel again; the dropped flushes' areas, kept as up to BOARD_DIRECT_DROP_MAX rects (omni_band_drop_keep,
 * D14), go to out; returns their count. After a force-end by board_direct_enable(false) it still returns the stage and
 * the kept areas (no sender band went out since). */
int board_direct_end(lv_area_t out[BOARD_DIRECT_DROP_MAX]);
bool board_direct_ended(void);        /* force-ended since begin (D4): the client treats the panel as unknown */
bool board_direct_enabled(void);      /* board_direct_enable's state: the panel is on (admission's display_on) */
void board_direct_hold(bool on);      /* power.c: takes / gives the panel mutex (no direct band on the wire) */
/* power.c: takes the panel mutex (no sender draw under way); it sends nothing on the bus, since LVGL is live then (Task
 * 5: a wait here raced LVGL's flushes); D5's wait for the wire is panel_off's board_direct_hold. false (display_off)
 * also force-ends the mode: no sender band reaches the panel again, and board_direct_ended() turns true. LVGL's flushes
 * stay dropped until board_direct_end returns the stage (a render task may still write a band of it, M1); the event
 * loop releases the path right after display_off, so display_on's full invalidate and refresh reach the panel (D4).
 * true after panel_on's DISPON. */
void board_direct_enable(bool on);
uint8_t *board_stage_lend(size_t *bytes);   /* LVGL ctx, between begin and end: the 14 912 B staging buffer (its
                                             * size may be read before begin; the buffer is the path's only until end
                                             * or a force-end) */
/* `disp`: LVGL flushes dropped while direct mode was on, since boot. */
uint32_t board_direct_dropped(void);

/* claim(x0, y0, ctx) is asked once per touch, when it first moves > 12 px; returning true turns the touch
 * into G_DRAG_* gestures. NULL = never claim. The pair is replaced as one (under a spinlock), so the input
 * task never sees one screen's function with another's ctx.
 *
 * THREADING: claim runs in the INPUT task, without the LVGL lock, and may run once more just after it was
 * replaced. It must not touch LVGL objects or anything a screen frees: decide from plain, immutable data
 * only (e.g. the screen's fixed geometry), and return quickly (the input task samples touch at 60 Hz). */
void board_set_touch_claim(bool (*claim)(int16_t, int16_t, void *), void *ctx);
/* The same, asking for the appended events (OMNI_GW_DOWN / HOLD / LIFT_STILL, omni/input.h; I9: HOLD or LIFT_STILL
 * suppresses G_LONG_PRESS). board_set_touch_claim(c, x) == board_set_touch_claim_w(c, x, 0). */
void board_set_touch_claim_w(bool (*claim)(int16_t, int16_t, void *), void *ctx, uint8_t want);

/* Debug (console `inject drag/arc`): a synthetic finger fed to the gesture classifier (and claim) in place of
 * the touch controller, from the next input period on. pressed=false lifts it (fed once, then the real
 * controller is read again). LVGL's own indev does not see it. Works only while the screen is awake. */
void board_debug_touch(bool pressed, int16_t x, int16_t y);

/* Stream "T,<ms>,<ax>,<ay>,<az>" accelerometer lines (board frame, g) on stdout at 25 Hz. */
void board_trace(bool on);

/* Debug: record the same samples into a RAM buffer (max BOARD_TRACE_REC_MAX_S seconds) starting delay_s
 * from now, for untethered recordings; board_trace_dump() prints them as "T,<ms>,<ax>,<ay>,<az>" lines
 * (ms from the recording start) followed by "TRACE END <n> lifts=<EV_LIFTs during the recording>". */
#define BOARD_TRACE_REC_MAX_S 72
esp_err_t board_trace_rec(unsigned seconds, unsigned delay_s);
void board_trace_dump(void);
