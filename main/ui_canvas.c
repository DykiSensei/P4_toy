#include "ui_canvas.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum { UI_COMMAND_LIMIT = 160, UI_TEXT_BYTES = 4096 };
typedef enum { CMD_RECT, CMD_ELLIPSE, CMD_LINE, CMD_TEXT } command_kind_t;
typedef struct {
    command_kind_t kind;
    int x, y, w, h, radius;
    uint32_t color;
    const char *text;
} draw_command_t;

/* Generated font data stays in flash; the renderer only reads glyph masks. */
extern const uint8_t *ui_font_glyph(int size, uint32_t codepoint, int *width,
                                  int *height, int *x_offset, int *y_offset,
                                  int *advance);
extern int ui_font_line_height(int size);

static draw_command_t commands[UI_COMMAND_LIMIT];
static char text_pool[UI_TEXT_BYTES];
static size_t command_count, text_used;
static uint32_t background;
static uint8_t strip[UI_WIDTH * UI_STRIP_HEIGHT * 2];
static int strip_y, strip_height;

static int minimum(int a, int b) { return a < b ? a : b; }
static int maximum(int a, int b) { return a > b ? a : b; }

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) |
                      ((rgb >> 3) & 0x001f));
}

static draw_command_t *append(command_kind_t kind, uint32_t color)
{
    assert(command_count < UI_COMMAND_LIMIT);
    draw_command_t *command = &commands[command_count++];
    memset(command, 0, sizeof(*command));
    command->kind = kind;
    command->color = color;
    return command;
}

void ui_canvas_reset(uint32_t color)
{
    background = color;
    command_count = 0;
    text_used = 0;
}

void ui_rect(int x, int y, int width, int height, int radius, uint32_t color)
{
    if (width <= 0 || height <= 0) return;
    draw_command_t *command = append(CMD_RECT, color);
    command->x = x; command->y = y; command->w = width; command->h = height;
    command->radius = maximum(0, minimum(radius, minimum(width, height) / 2));
}

void ui_ellipse(int x, int y, int width, int height, uint32_t color)
{
    if (width <= 0 || height <= 0) return;
    draw_command_t *command = append(CMD_ELLIPSE, color);
    command->x = x; command->y = y; command->w = width; command->h = height;
}

void ui_line(int x0, int y0, int x1, int y1, int width, uint32_t color)
{
    if (width <= 0) return;
    draw_command_t *command = append(CMD_LINE, color);
    command->x = x0; command->y = y0; command->w = x1; command->h = y1;
    command->radius = width;
}

void ui_text(int x, int y, const char *text, int size, uint32_t color)
{
    if (!text || !*text) return;
    const size_t length = strlen(text) + 1;
    assert(text_used + length <= sizeof(text_pool));
    draw_command_t *command = append(CMD_TEXT, color);
    command->x = x; command->y = y; command->radius = size;
    command->text = text_pool + text_used;
    memcpy(text_pool + text_used, text, length);
    text_used += length;
}

/* Invalid UTF-8 consumes one byte and draws '?', never reads beyond NUL. */
static uint32_t next_codepoint(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    if (*s < 0x80) { *text += *s != 0; return *s; }
    const int count = (*s >= 0xc2 && *s <= 0xdf) ? 2 :
                      (*s >= 0xe0 && *s <= 0xef) ? 3 :
                      (*s >= 0xf0 && *s <= 0xf4) ? 4 : 0;
    if (!count) { ++*text; return '?'; }
    uint32_t cp = *s & ((1u << (7 - count)) - 1u);
    for (int i = 1; i < count; ++i) {
        if ((s[i] & 0xc0) != 0x80) { ++*text; return '?'; }
        cp = (cp << 6) | (s[i] & 0x3f);
    }
    if ((count == 3 && cp < 0x800) || (count == 4 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        ++*text; return '?';
    }
    *text += count;
    return cp;
}

int ui_text_width(const char *text, int size)
{
    int current = 0, widest = 0;
    if (!text) return 0;
    while (*text) {
        uint32_t cp = next_codepoint(&text);
        if (cp == '\n') { widest = maximum(widest, current); current = 0; continue; }
        if (cp == '\r') continue;
        int w, h, xo, yo, advance;
        ui_font_glyph(size, cp, &w, &h, &xo, &yo, &advance);
        current += advance;
    }
    return maximum(widest, current);
}

static void blend_pixel(int x, int y, uint32_t color, unsigned alpha)
{
    if (!alpha || x < 0 || x >= UI_WIDTH || y < strip_y ||
        y >= strip_y + strip_height) return;
    const int index = ((y - strip_y) * UI_WIDTH + x) * 2;
    uint16_t value;
    if (alpha >= 15) {
        value = rgb565(color);
    } else {
        uint16_t old = ((uint16_t)strip[index] << 8) | strip[index + 1];
        unsigned r = ((old >> 11) & 31) * 255 / 31;
        unsigned g = ((old >> 5) & 63) * 255 / 63;
        unsigned b = (old & 31) * 255 / 31;
        r = (r * (15 - alpha) + ((color >> 16) & 255) * alpha + 7) / 15;
        g = (g * (15 - alpha) + ((color >> 8) & 255) * alpha + 7) / 15;
        b = (b * (15 - alpha) + (color & 255) * alpha + 7) / 15;
        value = rgb565((r << 16) | (g << 8) | b);
    }
    strip[index] = value >> 8;
    strip[index + 1] = value & 255;
}

/* Shape samples use quarter-pixel coordinates and four subpixel samples. */
static bool rounded_rect_contains(const draw_command_t *c, int sx, int sy)
{
    const int left = c->x * 4, top = c->y * 4;
    const int right = (c->x + c->w) * 4, bottom = (c->y + c->h) * 4;
    const int radius = c->radius * 4;
    if (sx < left || sx >= right || sy < top || sy >= bottom) return false;
    if (!radius || (sx >= left + radius && sx <= right - radius) ||
        (sy >= top + radius && sy <= bottom - radius)) return true;
    int dx = sx - (sx < left + radius ? left + radius : right - radius);
    int dy = sy - (sy < top + radius ? top + radius : bottom - radius);
    return (int64_t)dx * dx + (int64_t)dy * dy <= (int64_t)radius * radius;
}

static bool ellipse_contains(const draw_command_t *c, int sx, int sy)
{
    const int64_t rx = c->w * 2, ry = c->h * 2;
    const int64_t dx = sx - (c->x * 4 + rx), dy = sy - (c->y * 4 + ry);
    return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}

static bool line_contains(const draw_command_t *c, int sx, int sy)
{
    const int64_t ax = c->x * 4 + 2, ay = c->y * 4 + 2;
    const int64_t vx = (c->w - c->x) * 4, vy = (c->h - c->y) * 4;
    int64_t dx = sx - ax, dy = sy - ay;
    const int64_t length_squared = vx * vx + vy * vy;
    const int64_t dot = dx * vx + dy * vy;
    const int64_t radius_squared = (int64_t)c->radius * c->radius * 4;
    if (dot <= 0 || length_squared == 0) return dx * dx + dy * dy <= radius_squared;
    if (dot >= length_squared) {
        dx -= vx; dy -= vy;
        return dx * dx + dy * dy <= radius_squared;
    }
    const int64_t cross = dx * vy - dy * vx;
    return cross * cross <= radius_squared * length_squared;
}

static void render_shape(const draw_command_t *c)
{
    int x0 = c->x, y0 = c->y, x1 = c->x + c->w, y1 = c->y + c->h;
    if (c->kind == CMD_LINE) {
        const int extra = (c->radius + 1) / 2 + 1;
        x0 = minimum(c->x, c->w) - extra; x1 = maximum(c->x, c->w) + extra + 1;
        y0 = minimum(c->y, c->h) - extra; y1 = maximum(c->y, c->h) + extra + 1;
    }
    x0 = maximum(0, x0); x1 = minimum(UI_WIDTH, x1);
    y0 = maximum(strip_y, y0); y1 = minimum(strip_y + strip_height, y1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (c->kind == CMD_RECT && c->radius == 0) {
                blend_pixel(x, y, c->color, 15); continue;
            }
            unsigned coverage = 0;
            for (int dy = 1; dy <= 3; dy += 2) {
                for (int dx = 1; dx <= 3; dx += 2) {
                    const int sx = x * 4 + dx, sy = y * 4 + dy;
                    coverage += c->kind == CMD_RECT ? rounded_rect_contains(c, sx, sy) :
                                c->kind == CMD_ELLIPSE ? ellipse_contains(c, sx, sy) :
                                line_contains(c, sx, sy);
                }
            }
            blend_pixel(x, y, c->color, (coverage * 15 + 2) / 4);
        }
    }
}

static void render_text(const draw_command_t *c)
{
    const char *text = c->text;
    int pen_x = c->x, pen_y = c->y;
    while (*text) {
        const uint32_t cp = next_codepoint(&text);
        if (cp == '\n') { pen_x = c->x; pen_y += ui_font_line_height(c->radius); continue; }
        if (cp == '\r') continue;
        int width, height, xo, yo, advance;
        const uint8_t *mask = ui_font_glyph(c->radius, cp, &width, &height, &xo, &yo, &advance);
        const int gx = pen_x + xo, gy = pen_y + yo;
        const int top = maximum(0, strip_y - gy);
        const int bottom = minimum(height, strip_y + strip_height - gy);
        const int left = maximum(0, -gx), right = minimum(width, UI_WIDTH - gx);
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                const int pixel = y * width + x;
                const unsigned alpha = (pixel & 1) ? mask[pixel / 2] & 15 : mask[pixel / 2] >> 4;
                blend_pixel(gx + x, gy + y, c->color, alpha);
            }
        }
        pen_x += advance;
    }
}

void ui_canvas_render(ui_write_strip_fn write_strip)
{
    assert(write_strip);
    const uint16_t bg = rgb565(background);
    for (strip_y = 0; strip_y < UI_HEIGHT; strip_y += UI_STRIP_HEIGHT) {
        strip_height = minimum(UI_STRIP_HEIGHT, UI_HEIGHT - strip_y);
        for (int i = 0; i < UI_WIDTH * strip_height; ++i) {
            strip[i * 2] = bg >> 8; strip[i * 2 + 1] = bg & 255;
        }
        for (size_t i = 0; i < command_count; ++i) {
            if (commands[i].kind == CMD_TEXT) render_text(&commands[i]);
            else render_shape(&commands[i]);
        }
        write_strip(strip_y, strip_height, strip);
    }
}
