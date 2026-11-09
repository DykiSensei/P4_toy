#pragma once

#include <stdbool.h>
#include <stdint.h>

void lcd_display_init(void);
void lcd_display_write_strip(int y, int height, const uint8_t *big_endian_pixels);
void lcd_display_backlight(bool on);
