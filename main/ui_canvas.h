#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { UI_WIDTH = 320, UI_HEIGHT = 480, UI_STRIP_HEIGHT = 8 };
typedef void (*ui_write_strip_fn)(int y, int height, const uint8_t *big_endian_pixels);

/* All colors are 0xRRGGBB. Commands are replayed into one 8-row RGB565 strip. */
void ui_canvas_reset(uint32_t background);
void ui_rect(int x, int y, int width, int height, int radius, uint32_t color);
void ui_ellipse(int x, int y, int width, int height, uint32_t color);
void ui_line(int x0, int y0, int x1, int y1, int width, uint32_t color);
/* Text is UTF-8; y is the top of the line box, baseline is y + size. */
void ui_text(int x, int y, const char *text, int size, uint32_t color);
int ui_text_width(const char *text, int size);
void ui_canvas_render(ui_write_strip_fn write_strip);

#ifdef __cplusplus
}
#endif
