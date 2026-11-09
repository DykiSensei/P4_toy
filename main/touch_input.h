#pragma once

#include <stdbool.h>

/* Call after the board's LDO4 / GPIO supply has been enabled.
 * A successful return means GPIO setup succeeded, not that the controller was
 * detected. XPT2046 has no ID-register probe in this driver.
 */
bool touch_input_init(void);

/* Uncalibrated, unrotated 12-bit channels: X command 0xD0, Y command 0x90.
 * Returns true only for a stable sample while IRQ is still asserted. Outputs
 * are unchanged on failure. Call from one task only; this is a polling driver.
 */
bool touch_input_read_raw(int *raw_x, int *raw_y);

/* Low-active PENIRQ. Unlike read_raw(), this does not reject noisy samples.
 * Use a debounced false here to detect release and prevent repeated clicks
 * while a finger remains on the panel. This is not a controller-presence test.
 */
bool touch_input_is_down(void);
