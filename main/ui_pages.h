#pragma once

#include <stdbool.h>

typedef enum {
    UI_HOME, UI_CAMERA, UI_MAP, UI_COLLECTION, UI_BATTLE,
    UI_SETTINGS, UI_ABOUT, UI_CALIBRATE,
} ui_page_t;

void ui_pages_draw(ui_page_t page, bool calibrated);
ui_page_t ui_pages_tap(ui_page_t page, int x, int y);
const char *ui_page_name(ui_page_t page);
void ui_calibration_draw(int step, bool failed);
