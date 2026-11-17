#pragma once
/* QMI8658 accelerometer: 25 Hz poll in a low-priority task, fed to omni_wake_feed; posts EV_LIFT. */
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "omni/power.h"

esp_err_t imu_init(i2c_master_bus_handle_t bus);   /* also starts the task */
omni_pose_t imu_pose(void);
unsigned imu_read_failures(void);                 /* failed accelerometer reads since boot */
void imu_trace(bool on);
esp_err_t imu_trace_rec(unsigned seconds, unsigned delay_s);
void imu_trace_dump(void);
