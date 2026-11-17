#pragma once
/* Device event queue. The board layer creates it in board_init() and posts input, PMU and IMU events;
 * app_main's event loop consumes them. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "omni/input.h"

/* EV_NET: the net component's state changed (net.h). EV_UI: a screen asks the event loop to act (it may not
 * touch nav, power or settings itself, R26/R27); the codes are the UI's own (main/ui/shell.h). */
/* EV_SAVE: the settings' save debounce ran out (store.c); the event loop saves them. */
typedef enum { EV_GESTURE, EV_BOOT, EV_PWR, EV_LIFT, EV_TICK_1S, EV_NET, EV_UI, EV_VBUS, EV_SETTIME, EV_SAVE } omni_event_kind_t;

typedef struct {
    omni_event_kind_t kind;
    uint32_t t_ms;
    union {
        omni_gesture_t g;     /* EV_GESTURE */
        omni_press_t press;   /* EV_BOOT */
        int net;              /* EV_NET */
        int ui;               /* EV_UI: code | argument << 8 */
        int vbus;             /* EV_VBUS: VBUS present now (power.c saw it change while asleep) */
        int64_t unix_s;       /* EV_SETTIME: set the clock (console settime; the event loop owns the clock) */
    };
} omni_event_t;

QueueHandle_t omni_events(void);

/* Non-blocking send (dropped with a warning if the queue is full: returns false); also kept in a 5-deep history. */
bool omni_events_post(const omni_event_t *e);

/* Copies up to max of the most recent events, oldest first; returns the count. */
int omni_events_recent(omni_event_t *out, int max);

/* Formats e as e.g. "EV_GESTURE G_TAP 233,240" or "EV_BOOT B_SHORT"; returns buf. */
const char *omni_event_format(const omni_event_t *e, char *buf, size_t n);
