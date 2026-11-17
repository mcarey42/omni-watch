#include "imu.h"
#include <stdio.h>
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qmi8658.h"
#include "omni_events.h"

static const char *TAG = "imu";

#define IMU_PERIOD_MS 40   /* 25 Hz */
#define IMU_TASK_PRIO 2    /* below input (4) and the event loop */
#define QMI8658_REG_RESET 0x60

static qmi8658_dev_t s_dev;
static omni_wake_sm_t s_wake;
static volatile omni_pose_t s_pose = POSE_UNKNOWN;
static volatile bool s_trace;
/* R21: a board that boots lying face up primes the classifier in LOOKING and "lifts" 250 ms later. Lifts
 * count only once the pose has been seen outside LOOKING since boot. */
static bool s_lift_armed;
/* The first read after the soft reset is not data (observed: saturated (4.0, -4.0, -3.2) g). Samples are
 * not fed to the classifier until one reads 0.5..1.5 g, so they cannot seed its low-pass. */
static bool s_seeded;
/* Failed reads: a dead bus would silently stop lift-to-wake, so they are counted (`state`) and logged at the
 * first one and then every IMU_FAIL_LOG_EVERY. */
#define IMU_FAIL_LOG_EVERY 1000
static volatile unsigned s_read_fail;

/* Debug RAM recorder: 25 Hz x BOARD_TRACE_REC_MAX_S, 8 bytes/sample (~14 KB, PSRAM, allocated on first use). */
#define REC_CAP (BOARD_TRACE_REC_MAX_S * 1000 / IMU_PERIOD_MS)
typedef struct { uint16_t t10; int16_t x, y, z; } rec_t; /* t in 10 ms from start; accel in mg */
enum { REC_IDLE, REC_ARMED, REC_ON };
static rec_t *s_rec;
static volatile int s_rec_state = REC_IDLE;
static volatile unsigned s_rec_n, s_rec_lifts;
static uint32_t s_rec_start, s_rec_len_ms;

static void rec_sample(uint32_t t, omni_accel_t a, bool lift)
{
    if (s_rec_state == REC_ARMED && (int32_t)(t - s_rec_start) >= 0) {
        s_rec_start = t;
        s_rec_n = 0;
        s_rec_lifts = 0;
        s_rec_state = REC_ON;
    }
    if (s_rec_state != REC_ON) return;
    uint32_t dt = t - s_rec_start;
    if (dt >= s_rec_len_ms || s_rec_n >= REC_CAP) {
        s_rec_state = REC_IDLE;
        return;
    }
    s_rec[s_rec_n] = (rec_t){(uint16_t)(dt / 10), (int16_t)(a.ax * 1000), (int16_t)(a.ay * 1000),
                             (int16_t)(a.az * 1000)};
    s_rec_n++;
    s_rec_lifts += lift;
}

/* Chip -> board frame (+x right, +y up the screen, +z out of the screen; specific force, so a watch lying
 * face up reads (0,0,+1)). The QMI8658 sits with +X toward the top of the screen, +Y toward its left and
 * +Z out of the screen: the vendor 05_LVGL_Widgets example shows the upright UI when accel X > 0.8, and
 * 04_Immersive_block moves shapes "downhill" with (x,y) = (+accelY, +accelX) in screen pixels (y down).
 * Checked so far: lying on the bench (presumably face up) the board reads (0.06, -0.03, +0.94).
 * x/y signs are still to be confirmed by the owner's worn traces. This is a specific-force frame: a watch
 * hanging upright reads (0,+1,0) (the earlier "(0,-1,0)" was the gravity direction). The classifier uses
 * only z (times z_sign) and |a|, so y's sign does not affect it. */
static omni_accel_t to_board(float cx, float cy, float cz)
{
    omni_accel_t a = {.ax = -cy, .ay = cx, .az = cz};
    return a;
}

static void imu_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(IMU_PERIOD_MS));
        float mx, my, mz; /* mg: the driver defaults to mg unless set to m/s^2 */
        esp_err_t err = qmi8658_read_accel(&s_dev, &mx, &my, &mz);
        if (err != ESP_OK) {
            if (s_read_fail++ % IMU_FAIL_LOG_EVERY == 0)
                ESP_LOGW(TAG, "accel read failed: %s (%u so far)", esp_err_to_name(err), s_read_fail);
            continue;
        }
        uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
        omni_accel_t a = to_board(mx / 1000.0f, my / 1000.0f, mz / 1000.0f);
        if (s_trace) printf("T,%lu,%.3f,%.3f,%.3f\n", (unsigned long)t, a.ax, a.ay, a.az);
        if (!s_seeded) {
            float m2 = a.ax * a.ax + a.ay * a.ay + a.az * a.az;
            if (m2 < 0.25f || m2 > 2.25f) continue;
            s_seeded = true;
        }
        bool lift = omni_wake_feed(&s_wake, &OMNI_WAKE_DEFAULTS, t, a);
        s_pose = omni_wake_pose(&s_wake);
        if (s_pose != POSE_LOOKING) s_lift_armed = true;
        else if (lift && !s_lift_armed) lift = false;
        rec_sample(t, a, lift);
        if (lift) {
            omni_event_t e = {.kind = EV_LIFT, .t_ms = t};
            omni_events_post(&e);
        }
    }
}

esp_err_t imu_init(i2c_master_bus_handle_t bus)
{
    esp_err_t ret = qmi8658_init(&s_dev, bus, QMI8658_ADDRESS_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "qmi8658_init: %s", esp_err_to_name(ret));
        return ret;
    }
    /* Reconfiguring straight after the driver's init (accel+gyro at 1 kHz) left the output frozen on this
     * board (timestamp stuck at 0, data never updated), even with sensors disabled around the change.
     * A soft reset (0xB0 -> reg 0x60) before configuring fixes it. The driver's qmi8658_reset() only writes CTRL1 = 0x80, which is not a reset. */
    ret = qmi8658_write_register(&s_dev, QMI8658_REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(30));
    if (ret == ESP_OK) ret = qmi8658_write_register(&s_dev, QMI8658_CTRL1, 0x60); /* addr auto-inc, as init */
    if (ret == ESP_OK) ret = qmi8658_set_accel_range(&s_dev, QMI8658_ACCEL_RANGE_4G);
    if (ret == ESP_OK) ret = qmi8658_set_accel_odr(&s_dev, QMI8658_ACCEL_ODR_62_5HZ);
    if (ret == ESP_OK) ret = qmi8658_enable_sensors(&s_dev, QMI8658_ENABLE_ACCEL); /* gyro off */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "config: %s", esp_err_to_name(ret));
        return ret;
    }
    omni_wake_reset(&s_wake);
    if (xTaskCreate(imu_task, "imu", 4096, NULL, IMU_TASK_PRIO, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

omni_pose_t imu_pose(void) { return s_pose; }

unsigned imu_read_failures(void) { return s_read_fail; }

void imu_trace(bool on) { s_trace = on; }

esp_err_t imu_trace_rec(unsigned seconds, unsigned delay_s)
{
    if (seconds == 0 || seconds > BOARD_TRACE_REC_MAX_S) return ESP_ERR_INVALID_ARG;
    if (s_rec_state != REC_IDLE) return ESP_ERR_INVALID_STATE;
    if (s_rec == NULL) s_rec = heap_caps_malloc(REC_CAP * sizeof(rec_t), MALLOC_CAP_SPIRAM);
    if (s_rec == NULL) return ESP_ERR_NO_MEM;
    s_rec_len_ms = seconds * 1000u;
    s_rec_start = (uint32_t)(esp_timer_get_time() / 1000) + delay_s * 1000u;
    s_rec_n = 0;
    s_rec_state = REC_ARMED;
    return ESP_OK;
}

void imu_trace_dump(void)
{
    if (s_rec_state != REC_IDLE) {
        printf("TRACE BUSY\n");
        return;
    }
    unsigned n = s_rec ? s_rec_n : 0;
    for (unsigned i = 0; i < n; i++) {
        const rec_t *r = &s_rec[i];
        printf("T,%u,%.3f,%.3f,%.3f\n", r->t10 * 10u, r->x / 1000.0f, r->y / 1000.0f, r->z / 1000.0f);
        if (i % 16 == 15) vTaskDelay(pdMS_TO_TICKS(10)); /* pace the USB-Serial-JTAG FIFO */
    }
    printf("TRACE END %u lifts=%u\n", n, s_rec_lifts);
}
