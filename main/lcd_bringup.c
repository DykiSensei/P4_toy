#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_display.h"

// LCD header pins 3..14, starting at CS, wired in order to P4 GPIO39..50.
enum {
    LCD_CS = 39, LCD_RESET = 40, LCD_DC = 41, LCD_MOSI = 42,
    LCD_SCK = 43, LCD_LED = 44, LCD_MISO = 45,
    TOUCH_CLK = 46, TOUCH_CS = 47, TOUCH_DIN = 48,
    TOUCH_DO = 49, TOUCH_IRQ = 50,
    LCD_WIDTH = 320, LCD_HEIGHT = 480, STRIP_LINES = 8,
    LCD_WRITE_HZ = 10000000, LCD_READ_HZ = 1000000,
};

static const char *TAG = "lcd_bringup";
static spi_device_handle_t lcd;
static uint8_t *pixels;
static esp_ldo_channel_handle_t gpio_supply;
// Set true only for a slow GPIO-only diagnostic that bypasses SPI hardware/DMA.
static const bool use_gpio_spi = false;

typedef struct {
    uint8_t command;
    uint8_t count;
    uint8_t data[14];
} lcd_init_command_t;

// Based on the supplied LCDWIKI ST7796S module example and IC datasheet.
// C2 takes A7 as data; F0/3C closes extension part I. Portrait, BGR, RGB565.
static const lcd_init_command_t init_commands[] = {
    {0xF0, 1, {0xC3}},
    {0xF0, 1, {0x96}},
    {0x36, 1, {0x48}},
    {0x3A, 1, {0x55}},
    {0xB0, 1, {0x00}},  // SPI_EN=0: separate MOSI (DIN) and MISO (DOUT).
    {0xB6, 2, {0x00, 0x02}},
    {0xB5, 4, {0x02, 0x03, 0x00, 0x04}},
    {0xB1, 2, {0x80, 0x10}},
    {0xB4, 1, {0x00}},
    {0xB7, 1, {0xC6}},
    {0xC5, 1, {0x24}},
    {0xE4, 1, {0x31}},
    {0xE8, 8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}},
    {0xC2, 1, {0xA7}},
    {0xE0, 14, {0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B, 0x3C, 0x44,
                 0x4B, 0x1B, 0x18, 0x17, 0x1D, 0x21}},
    {0xE1, 14, {0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27, 0x3B, 0x44,
                 0x4D, 0x0B, 0x17, 0x17, 0x1D, 0x21}},
    {0xF0, 1, {0x3C}},
    {0xF0, 1, {0x69}},
    {0x13, 0, {0}},
};

// Independent slow mode-0 GPIO probe, before the SPI peripheral owns the pins.
static uint8_t gpio_spi_byte(uint8_t value)
{
    uint8_t received = 0;
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(LCD_SCK, 0);
        gpio_set_level(LCD_MOSI, (value >> bit) & 1);
        esp_rom_delay_us(3);
        gpio_set_level(LCD_SCK, 1);
        esp_rom_delay_us(3);
        received = (received << 1) | gpio_get_level(LCD_MISO);
    }
    gpio_set_level(LCD_SCK, 0);
    return received;
}

static void gpio_spi_register(uint8_t command, uint8_t value)
{
    gpio_set_level(LCD_CS, 0);
    gpio_set_level(LCD_DC, 0);
    gpio_spi_byte(command);
    gpio_set_level(LCD_DC, 1);
    gpio_spi_byte(value);
    gpio_set_level(LCD_CS, 1);
}

static void gpio_spi_probe(void)
{
    const gpio_config_t out = {
        .pin_bit_mask = (1ULL << LCD_CS) | (1ULL << LCD_SCK) | (1ULL << LCD_MOSI),
        .mode = GPIO_MODE_INPUT_OUTPUT,
    };
    const gpio_config_t in = {
        .pin_bit_mask = 1ULL << LCD_MISO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    ESP_ERROR_CHECK(gpio_config(&in));
    gpio_set_level(LCD_CS, 1);
    gpio_set_level(LCD_SCK, 0);
    gpio_set_level(LCD_MOSI, 0);
    gpio_set_level(LCD_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(LCD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    gpio_spi_register(0xF0, 0xC3);
    gpio_spi_register(0xF0, 0x96);
    gpio_spi_register(0xB0, 0x00);

    gpio_set_level(LCD_CS, 0);
    const int selected_level = gpio_get_level(LCD_CS);
    gpio_set_level(LCD_DC, 0);
    gpio_spi_byte(0xD3);
    gpio_set_level(LCD_DC, 1);
    uint8_t id[4];
    for (int i = 0; i < 4; ++i) {
        id[i] = gpio_spi_byte(0);
    }
    gpio_set_level(LCD_CS, 1);
    ESP_LOGI(TAG, "Slow GPIO SPI D3 (MISO pull-up): %02X %02X %02X %02X; "
             "CS selected=%d idle=%d", id[0], id[1], id[2], id[3],
             selected_level, gpio_get_level(LCD_CS));
    ESP_ERROR_CHECK(gpio_set_pull_mode(LCD_MISO, GPIO_FLOATING));
}

static void lcd_write(const void *data, size_t size, int dc)
{
    if (size == 0) {
        return;
    }
    ESP_ERROR_CHECK(gpio_set_level(LCD_DC, dc));
    if (use_gpio_spi) {
        const uint8_t *bytes = data;
        gpio_set_level(LCD_CS, 0);
        for (size_t i = 0; i < size; ++i) {
            for (int bit = 7; bit >= 0; --bit) {
                gpio_set_level(LCD_SCK, 0);
                gpio_set_level(LCD_MOSI, (bytes[i] >> bit) & 1);
                esp_rom_delay_us(1);
                gpio_set_level(LCD_SCK, 1);
                esp_rom_delay_us(1);
            }
        }
        gpio_set_level(LCD_SCK, 0);
        gpio_set_level(LCD_CS, 1);
        if (size > 512) {
            vTaskDelay(1);
        }
        return;
    }
    spi_transaction_t transaction = {
        .length = size * 8,
        .override_freq_hz = LCD_WRITE_HZ,
    };
    if (size <= sizeof(transaction.tx_data)) {
        transaction.flags = SPI_TRANS_USE_TXDATA;
        memcpy(transaction.tx_data, data, size);
    } else {
        transaction.tx_buffer = data;
    }
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd, &transaction));
}

static void lcd_command(uint8_t command, const uint8_t *data, size_t size)
{
    lcd_write(&command, 1, 0);
    lcd_write(data, size, 1);
}

// Read at 1 MHz with CS held between command and response.
// The caller selects the response length (D3 includes a dummy byte).
static void lcd_read_register(uint8_t command, uint8_t *result, size_t size)
{
    assert(size <= 4);
    if (use_gpio_spi) {
        gpio_set_level(LCD_CS, 0);
        gpio_set_level(LCD_DC, 0);
        gpio_spi_byte(command);
        gpio_set_level(LCD_DC, 1);
        for (size_t i = 0; i < size; ++i) {
            result[i] = gpio_spi_byte(0);
        }
        gpio_set_level(LCD_CS, 1);
        return;
    }
    ESP_ERROR_CHECK(spi_device_acquire_bus(lcd, portMAX_DELAY));
    ESP_ERROR_CHECK(gpio_set_level(LCD_DC, 0));
    spi_transaction_t tx = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_CS_KEEP_ACTIVE,
        .length = 8,
        .tx_data = {command},
        .override_freq_hz = LCD_READ_HZ,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd, &tx));
    ESP_ERROR_CHECK(gpio_set_level(LCD_DC, 1));
    spi_transaction_t rx = {
        .flags = SPI_TRANS_USE_RXDATA,
        .length = size * 8,
        .rxlength = size * 8,
        .override_freq_hz = LCD_READ_HZ,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(lcd, &rx));
    memcpy(result, rx.rx_data, size);
    spi_device_release_bus(lcd);
}

static void lcd_window(int x, int y, int width, int height)
{
    const int right = x + width - 1;
    const int bottom = y + height - 1;
    const uint8_t columns[] = {x >> 8, x & 0xff, right >> 8, right & 0xff};
    const uint8_t rows[] = {y >> 8, y & 0xff, bottom >> 8, bottom & 0xff};
    lcd_command(0x2A, columns, sizeof(columns));
    lcd_command(0x2B, rows, sizeof(rows));
    lcd_command(0x2C, NULL, 0);
}

static void lcd_fill_rect(int x, int y, int width, int height, uint16_t color)
{
    assert(x >= 0 && y >= 0 && width > 0 && height > 0);
    assert(x + width <= LCD_WIDTH && y + height <= LCD_HEIGHT);
    const size_t capacity = LCD_WIDTH * STRIP_LINES;
    for (size_t i = 0; i < capacity; ++i) {
        pixels[i * 2] = color >> 8;
        pixels[i * 2 + 1] = color & 0xff;
    }
    lcd_window(x, y, width, height);
    size_t remaining = width * height;
    while (remaining > 0) {
        const size_t count = remaining < capacity ? remaining : capacity;
        lcd_write(pixels, count * 2, 1);
        remaining -= count;
    }
}

void lcd_display_init(void)
{
    ESP_LOGI(TAG, "ST7796S bring-up: 320x480 RGB565, SPI mode 0, %s",
             use_gpio_spi ? "software GPIO SPI" : "hardware SPI 10 MHz");
    ESP_LOGI(TAG, "CS=39 RST=40 DC=41 MOSI=42 SCK=43 BL=44 MISO=45");
    ESP_LOGI(TAG, "Touch initially parked: CLK=46 CS=47 DIN=48 DO=49 IRQ=50");

    // WTDKP4C5-S1 BSP uses LDO4 for the GPIO39..48 / SD I/O supply path.
    // Keep its handle for the whole application; screen VCC is supplied by 5V.
    const esp_ldo_channel_config_t supply_config = {
        .chan_id = 4,
        .voltage_mv = 3300,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&supply_config, &gpio_supply));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "LDO4 requested at 3300 mV before GPIO setup");

    const gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << LCD_DC) | (1ULL << LCD_RESET) |
                        (1ULL << LCD_LED) | (1ULL << TOUCH_CS) |
                        (1ULL << TOUCH_CLK) | (1ULL << TOUCH_DIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&outputs));
    // Enable pad readback for the two signals that must be high to light the panel.
    ESP_ERROR_CHECK(gpio_input_enable(LCD_RESET));
    ESP_ERROR_CHECK(gpio_input_enable(LCD_LED));
    ESP_ERROR_CHECK(gpio_input_enable(TOUCH_CS));
    ESP_ERROR_CHECK(gpio_set_level(LCD_LED, 0));
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_CS, 1));
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_CLK, 0));
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_DIN, 0));
    const gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << TOUCH_DO) | (1ULL << TOUCH_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&inputs));

    if (use_gpio_spi) {
        gpio_spi_probe();
    }

    const spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = LCD_MISO,
        .sclk_io_num = LCD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * STRIP_LINES * 2,
    };
    const spi_device_interface_config_t device = {
        .clock_speed_hz = LCD_WRITE_HZ,
        .mode = 0,
        .spics_io_num = LCD_CS,
        .queue_size = 1,
    };
    if (!use_gpio_spi) {
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
        ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &device, &lcd));
    }
    pixels = heap_caps_malloc(LCD_WIDTH * STRIP_LINES * 2,
                              MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_ERROR_CHECK(pixels == NULL ? ESP_ERR_NO_MEM : ESP_OK);

    ESP_ERROR_CHECK(gpio_set_level(LCD_RESET, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(LCD_RESET, 1));
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "RESET pad level=%d (expected 1)", gpio_get_level(LCD_RESET));

    uint8_t id[4];
    lcd_read_register(0xD3, id, sizeof(id));
    ESP_LOGI(TAG, "LCD ID (D3, dummy included): %02X %02X %02X %02X",
             id[0], id[1], id[2], id[3]);
    if (id[2] != 0x77 || id[3] != 0x96) {
        ESP_LOGW(TAG, "ST7796S ID not confirmed; continuing with supplied module init");
        ESP_ERROR_CHECK(gpio_set_pull_mode(LCD_MISO, GPIO_PULLUP_ONLY));
        vTaskDelay(pdMS_TO_TICKS(10));
        const int miso_pullup = gpio_get_level(LCD_MISO);
        uint8_t biased_id[4];
        lcd_read_register(0xD3, biased_id, sizeof(biased_id));
        ESP_ERROR_CHECK(gpio_set_pull_mode(LCD_MISO, GPIO_PULLDOWN_ONLY));
        vTaskDelay(pdMS_TO_TICKS(10));
        const int miso_pulldown = gpio_get_level(LCD_MISO);
        ESP_ERROR_CHECK(gpio_set_pull_mode(LCD_MISO, GPIO_FLOATING));
        ESP_LOGW(TAG, "MISO idle with weak pull-up=%d, pull-down=%d; "
                 "D3 with pull-up=%02X %02X %02X %02X",
                 miso_pullup, miso_pulldown,
                 biased_id[0], biased_id[1], biased_id[2], biased_id[3]);
    }
    for (size_t i = 0; i < sizeof(init_commands) / sizeof(init_commands[0]); ++i) {
        lcd_command(init_commands[i].command, init_commands[i].data,
                    init_commands[i].count);
    }
    lcd_command(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_command(0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t madctl[1], format[1];
    lcd_read_register(0x0B, madctl, sizeof(madctl));
    lcd_read_register(0x0C, format, sizeof(format));
    ESP_LOGI(TAG, "Readback MADCTL=%02X (expected 48), COLMOD=%02X (expected 55)",
             madctl[0], format[0]);

    // Keep the backlight off until the first complete UI frame is ready.
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, 0xF7BD);
    ESP_LOGI(TAG, "Display initialized; handing over to UI renderer");
}

void lcd_display_write_strip(int y,int height,const uint8_t *big_endian_pixels)
{
    assert(y>=0&&height>0&&height<=STRIP_LINES&&y+height<=LCD_HEIGHT);
    lcd_window(0,y,LCD_WIDTH,height);
    lcd_write(big_endian_pixels,LCD_WIDTH*height*2,1);
}

void lcd_display_backlight(bool on)
{
    ESP_ERROR_CHECK(gpio_set_level(LCD_LED,on));
    ESP_LOGI(TAG,"BACKLIGHT=%d RESET=%d",gpio_get_level(LCD_LED),gpio_get_level(LCD_RESET));
}
