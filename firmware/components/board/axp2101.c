/* Ported from Waveshare's examples/esp-idf/01_AXP2101 (Copyright 2026 Waveshare), under the Apache License, Version 2.0
 * (LICENSES/Apache-2.0.txt; see NOTICE and axp2101.h). Modified: ported to C, the read and IRQ parts only. */
#include "axp2101.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "omni/vbus.h"

static const char *TAG = "axp2101";

/* From axp2101_registers.h (01_AXP2101), the subset this driver uses. */
#define AXP2101_I2C_ADDRESS 0x34
#define AXP2101_CHIP_ID 0x4A
#define AXP2101_REG_STATUS1 0x00          /* bit3 battery present, bit5 VBUS good */
#define AXP2101_REG_STATUS2 0x01          /* bits7:5 = 001 charging, 010 discharging, 000 standby */
#define AXP2101_REG_IC_TYPE 0x03
#define AXP2101_REG_ADC_CHANNEL_CTRL 0x30 /* bit0 VBAT, bit1 TS, bit2 VBUS, bit3 VSYS, bit4 die temp */
#define AXP2101_REG_ADC_VBAT_H 0x34      /* VBAT mV = (H & 0x1F) << 8 | L */
#define AXP2101_REG_ADC_VBAT_L 0x35
#define AXP2101_REG_INTEN2 0x41
#define AXP2101_REG_INTSTS1 0x48
#define AXP2101_REG_INTSTS2 0x49
#define AXP2101_REG_INTSTS3 0x4A
#define AXP2101_REG_BAT_PERCENT_DATA 0xA4
/* IRQ bits 10/11 of the 24-bit mask = bits 2/3 of INTEN2/INTSTS2. */
#define AXP2101_IRQ2_PKEY_LONG (1u << 2)
#define AXP2101_IRQ2_PKEY_SHORT (1u << 3)

#define AXP2101_TIMEOUT_MS 100

static i2c_master_dev_handle_t s_dev;

static esp_err_t read_reg(uint8_t reg, uint8_t *value)
{
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, AXP2101_TIMEOUT_MS);
}

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof buf, AXP2101_TIMEOUT_MS);
}

static esp_err_t update_reg(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t cur = 0;
    esp_err_t ret = read_reg(reg, &cur);
    if (ret != ESP_OK) return ret;
    return write_reg(reg, (uint8_t)((cur & ~mask) | (value & mask)));
}

esp_err_t axp2101_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (ret != ESP_OK) return ret;

    uint8_t id = 0;
    ret = read_reg(AXP2101_REG_IC_TYPE, &id);
    if (ret != ESP_OK || id != AXP2101_CHIP_ID) {
        ESP_LOGE(TAG, "chip id 0x%02x (%s)", id, esp_err_to_name(ret));
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret != ESP_OK ? ret : ESP_ERR_NOT_FOUND;
    }

    /* ADC channels as in the example: VBAT, VBUS, VSYS, die temp on; TS off (no thermistor fitted). */
    ret |= update_reg(AXP2101_REG_ADC_CHANNEL_CTRL, 0x1F, 0x1D);
    /* Clear stale IRQ status, then let the PWR-key short/long events latch. */
    ret |= write_reg(AXP2101_REG_INTSTS1, 0xFF);
    ret |= write_reg(AXP2101_REG_INTSTS2, 0xFF);
    ret |= write_reg(AXP2101_REG_INTSTS3, 0xFF);
    ret |= update_reg(AXP2101_REG_INTEN2, AXP2101_IRQ2_PKEY_SHORT | AXP2101_IRQ2_PKEY_LONG,
                      AXP2101_IRQ2_PKEY_SHORT | AXP2101_IRQ2_PKEY_LONG);
    ESP_LOGI(TAG, "ready, battery %d%%, charging %d", axp2101_battery_percent(), axp2101_charging());
    return ret == ESP_OK ? ESP_OK : ESP_FAIL;
}

int axp2101_battery_percent(void)
{
    uint8_t st = 0, pct = 0;
    if (read_reg(AXP2101_REG_STATUS1, &st) != ESP_OK || !(st & (1u << 3))) return -1;
    if (read_reg(AXP2101_REG_BAT_PERCENT_DATA, &pct) != ESP_OK || pct > 100) return -1;
    return pct;
}

bool axp2101_charging(void)
{
    uint8_t st = 0;
    return read_reg(AXP2101_REG_STATUS2, &st) == ESP_OK && (st >> 5) == 0x01;
}

bool axp2101_pkey_short(void)
{
    uint8_t st = 0;
    if (read_reg(AXP2101_REG_INTSTS2, &st) != ESP_OK) return false;
    uint8_t pk = st & (AXP2101_IRQ2_PKEY_SHORT | AXP2101_IRQ2_PKEY_LONG);
    if (pk) write_reg(AXP2101_REG_INTSTS2, pk); /* write-1-to-clear */
    return (pk & AXP2101_IRQ2_PKEY_SHORT) != 0;
}

int axp2101_vbat_mv(void)
{
    uint8_t st = 0, h = 0, l = 0;
    if (read_reg(AXP2101_REG_STATUS1, &st) != ESP_OK || !(st & (1u << 3))) return -1;
    if (read_reg(AXP2101_REG_ADC_VBAT_H, &h) != ESP_OK || read_reg(AXP2101_REG_ADC_VBAT_L, &l) != ESP_OK) return -1;
    return ((h & 0x1F) << 8) | l;
}

/* VBUS through omni_vbus_feed: a failed read changes nothing (until OMNI_VBUS_FAIL_MAX in a row: then battery is
 * assumed for light sleep, logged; the records keep the confirmed value), and an edge needs two agreeing reads. The power
 * manager (input task, 10 Hz asleep), the event loop (1 s tick) and the console all read it: one filter, under
 * s_vbus_mux, with the failure count. */
static portMUX_TYPE s_vbus_mux = portMUX_INITIALIZER_UNLOCKED;
static omni_vbus_filter_t s_vbus;
static unsigned s_vbus_read_failures;

unsigned axp2101_vbus_read_failures(void)
{
    taskENTER_CRITICAL(&s_vbus_mux);
    unsigned n = s_vbus_read_failures;
    taskEXIT_CRITICAL(&s_vbus_mux);
    return n;
}

/* One read through the filter: *confirmed (the records' value) and *failsafe as it left them; returns state. */
static bool vbus_read(bool *confirmed, bool *failsafe)
{
    uint8_t st = 0;
    const esp_err_t err = read_reg(AXP2101_REG_STATUS1, &st);
    const omni_vbus_read_t r = err != ESP_OK ? OMNI_VBUS_READ_FAIL : (st & (1u << 5)) ? OMNI_VBUS_PRESENT
                                                                                       : OMNI_VBUS_ABSENT;
    taskENTER_CRITICAL(&s_vbus_mux);
    if (err != ESP_OK) s_vbus_read_failures++;
    const bool was_failsafe = s_vbus.failsafe;
    const bool v = omni_vbus_feed(&s_vbus, r);
    *failsafe = s_vbus.failsafe;
    *confirmed = s_vbus.confirmed;
    taskEXIT_CRITICAL(&s_vbus_mux);
    if (*failsafe != was_failsafe) { /* once per fallback, and once at its recovery (not every read) */
        if (*failsafe)
            ESP_LOGE(TAG, "VBUS read failed %u times in a row: battery assumed for light sleep (records unchanged)",
                     (unsigned)OMNI_VBUS_FAIL_MAX);
        else ESP_LOGW(TAG, "VBUS reads back after the fallback: %s", v ? "USB" : "battery");
    }
    return v;
}

bool axp2101_vbus_present(void)
{
    bool confirmed, failsafe;
    return vbus_read(&confirmed, &failsafe);
}

bool axp2101_vbus_confirmed(void)
{
    bool confirmed, failsafe;
    vbus_read(&confirmed, &failsafe);
    return confirmed;
}

bool axp2101_vbus_failsafe(void)
{
    taskENTER_CRITICAL(&s_vbus_mux);
    const bool f = s_vbus.failsafe;
    taskEXIT_CRITICAL(&s_vbus_mux);
    return f;
}
