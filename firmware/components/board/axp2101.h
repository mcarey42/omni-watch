#pragma once
/* AXP2101 PMU: battery gauge, charge state and PWR-key IRQ status, over the shared BSP I2C bus.
 * C port of the read/IRQ parts of Waveshare's examples/esp-idf/01_AXP2101 (port_axp2101.cpp, axp2101_registers.h;
 * https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C), Copyright 2026 Waveshare, under the Apache License,
 * Version 2.0 (LICENSES/Apache-2.0.txt; see NOTICE). Modified: ported to C, the read and IRQ parts only.
 * The example's rail configuration (it switches off DCDC2-5 and every LDO but ALDO1) and charger
 * reprogramming are NOT ported: this board's rails/charger stay as the PMU's factory OTP sets them. */
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t axp2101_init(i2c_master_bus_handle_t bus);
int axp2101_battery_percent(void);  /* 0..100, -1 if no battery or read error */
bool axp2101_charging(void);
int axp2101_vbat_mv(void);          /* battery voltage (ADC), -1 if no battery or read error */
/* USB power present (STATUS1 VBUS good), filtered (omni/vbus.h): a failed read keeps the last state, a change needs
 * two agreeing reads; after OMNI_VBUS_FAIL_MAX failures in a row, absent (the fallback). Any task; each call reads.
 * present: what light sleep follows (the fallback included). confirmed: what the records follow (the last value
 * good reads gave; the fallback opens no battery session). */
bool axp2101_vbus_present(void);
bool axp2101_vbus_confirmed(void);
bool axp2101_vbus_failsafe(void);   /* in the fallback now (no read) */
unsigned axp2101_vbus_read_failures(void); /* failed VBUS reads since boot */
/* The AXP2101 has no battery-current ADC or coulomb counter: drain can only be estimated from the VBAT / %
 * slope over time while running on battery. */
/* Reads and clears the PWR-key IRQ status; true if a short press was latched since the last call.
 * (A long press is left to the AXP's own hardware power-off.) */
bool axp2101_pkey_short(void);
