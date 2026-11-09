#include "touch_input.h"

#include <stdint.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

/* The supplied MSP4021 schematic identifies U2 as XPT2046. Its supplied
 * LCDWIKI_TOUCH reference uses 0xD0 / 0x90, a BUSY clock, then 16 read clocks
 * with the 12-bit result in bits 15..4. These pins are an independent bus;
 * this driver never changes the LCD SPI2 pins or its transaction ownership.
 */
enum {
    TP_CLK = 46,
    TP_CS = 47,
    TP_DIN = 48,
    TP_DOUT = 49,
    TP_IRQ = 50,
    CMD_X = 0xD0,
    CMD_Y = 0x90,
    SAMPLE_COUNT = 5,
    HALF_CLOCK_US = 2,
    MAX_INNER_SPREAD = 100,
    MAX_PAIR_DELTA = 80,
    ADC_RAIL_MARGIN = 16,
};

static const char *TAG = "touch_input";
static bool ready;

static void clock_delay(void)
{
    esp_rom_delay_us(HALF_CLOCK_US);
}

/* All commands end in PD1:PD0=00: conversion powers down afterwards and
 * PENIRQ is available again. DIN stays low during data readout so no extra
 * start bit can accidentally leave the controller powered up.
 */
static uint16_t read_adc(uint8_t command)
{
    gpio_set_level(TP_CLK, 0);
    gpio_set_level(TP_CS, 0);
    clock_delay();

    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(TP_DIN, (command >> bit) & 1);
        clock_delay();
        gpio_set_level(TP_CLK, 1);
        clock_delay();
        gpio_set_level(TP_CLK, 0);
    }
    gpio_set_level(TP_DIN, 0);
    esp_rom_delay_us(8);

    /* Consume the BUSY/null cycle before the twelve significant bits. */
    gpio_set_level(TP_CLK, 1);
    clock_delay();
    gpio_set_level(TP_CLK, 0);
    clock_delay();

    uint16_t value = 0;
    for (int bit = 0; bit < 16; ++bit) {
        gpio_set_level(TP_CLK, 1);
        clock_delay();
        value = (uint16_t)((value << 1) | gpio_get_level(TP_DOUT));
        gpio_set_level(TP_CLK, 0);
        clock_delay();
    }
    gpio_set_level(TP_CS, 1);
    esp_rom_delay_us(10);
    return value >> 4;
}

static bool read_filtered(uint8_t command, int *result)
{
    uint16_t samples[SAMPLE_COUNT];

    /* The first sample after changing panel drive/axis may still be settling. */
    (void)read_adc(command);
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        if (!touch_input_is_down()) {
            return false;
        }
        const uint16_t value = read_adc(command);
        int j = i;
        while (j > 0 && samples[j - 1] > value) {
            samples[j] = samples[j - 1];
            --j;
        }
        samples[j] = value;
    }

    /* Ignore one outlier at each end, then reject an unstable middle group.
     * Rail values reject a floating DOUT pulled high and a line stuck low.
     * These are validity checks, not a panel-specific coordinate calibration.
     */
    if (samples[1] < ADC_RAIL_MARGIN ||
        samples[3] > 4095 - ADC_RAIL_MARGIN ||
        samples[3] - samples[1] > MAX_INNER_SPREAD) {
        return false;
    }
    *result = (samples[1] + samples[2] + samples[3]) / 3;
    return true;
}

bool touch_input_init(void)
{
    ready = false;
    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << TP_CLK) | (1ULL << TP_CS) | (1ULL << TP_DIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << TP_DOUT) | (1ULL << TP_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&outputs);
    if (err == ESP_OK) {
        err = gpio_config(&inputs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch GPIO setup failed: %s", esp_err_to_name(err));
        return false;
    }
    gpio_set_level(TP_CS, 1);
    gpio_set_level(TP_CLK, 0);
    gpio_set_level(TP_DIN, 0);
    esp_rom_delay_us(100);

    /* Recover PENIRQ even if a preceding firmware left PD0 set. */
    (void)read_adc(CMD_Y);
    ready = true;
    ESP_LOGI(TAG, "XPT2046 GPIO polling ready: CLK=46 CS=47 DIN=48 DO=49 IRQ=50; IRQ=%d",
             gpio_get_level(TP_IRQ));
    return true;
}

bool touch_input_is_down(void)
{
    return ready && gpio_get_level(TP_IRQ) == 0;
}

bool touch_input_read_raw(int *raw_x, int *raw_y)
{
    if (raw_x == NULL || raw_y == NULL || !touch_input_is_down()) {
        return false;
    }

    int x0, y0, x1, y1;
    if (!read_filtered(CMD_X, &x0) || !read_filtered(CMD_Y, &y0) ||
        !read_filtered(CMD_X, &x1) || !read_filtered(CMD_Y, &y1) ||
        !touch_input_is_down() || abs(x0 - x1) > MAX_PAIR_DELTA ||
        abs(y0 - y1) > MAX_PAIR_DELTA) {
        return false;
    }
    *raw_x = (x0 + x1) / 2;
    *raw_y = (y0 + y1) / 2;
    return true;
}
